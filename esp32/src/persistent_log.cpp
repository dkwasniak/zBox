#include "persistent_log.h"
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>
#include <string.h>

RTC_NOINIT_ATTR static char rtcRing[PLOG_LINES][PLOG_LINE_LEN];
RTC_NOINIT_ATTR static uint16_t rtcWriteIdx;
RTC_NOINIT_ATTR static uint32_t rtcWriteCount;
RTC_NOINIT_ATTR static uint32_t rtcMagic;

#define MAGIC_VALID 0xCAFEBABEu
static const char* SD_LOG = "/data/debug.log";
static const char* SD_OLD = "/data/debug.log.old";
static unsigned long lastFlushMs = 0;
static uint32_t lastFlushedCount = 0;
static SemaphoreHandle_t plogMutex = NULL;
static bool plogRtcInitialized = false;
static bool plogSdAttached = false;
static bool plogRecoveredSession = false;

void formatUptime(char* out, size_t outSize, unsigned long ms)
{
    unsigned long totalSeconds = ms / 1000UL;
    unsigned long hours = totalSeconds / 3600UL;
    unsigned long minutes = (totalSeconds % 3600UL) / 60UL;
    unsigned long seconds = totalSeconds % 60UL;
    unsigned long millisPart = ms % 1000UL;
    snprintf(out, outSize, "%02lu:%02lu:%02lu.%03lu", hours, minutes, seconds, millisPart);
}

static void normalizeLogLine(char *line)
{
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
}

static void plogAppendRingToSd(File &f)
{
    for (int i = 0; i < PLOG_LINES; i++) {
        int idx = (rtcWriteIdx + i) % PLOG_LINES;
        if (rtcRing[idx][0]) f.println(rtcRing[idx]);
    }
}

static void plogAppendPendingToSd(File &f)
{
    uint32_t startCount = lastFlushedCount;
    if (rtcWriteCount - startCount > PLOG_LINES) {
        startCount = rtcWriteCount - PLOG_LINES;
    }

    for (uint32_t count = startCount; count < rtcWriteCount; count++) {
        const char *line = rtcRing[count % PLOG_LINES];
        if (line[0]) f.println(line);
    }
}

static void plogRotateIfNeeded()
{
    File f = SD.open(SD_LOG, FILE_READ);
    if (f && f.size() > 1024 * 1024) {
        f.close();
        if (SD.exists(SD_OLD)) SD.remove(SD_OLD);
        SD.rename(SD_LOG, SD_OLD);
    } else if (f) {
        f.close();
    }
}

void plogInit(bool sdAvailable)
{
    if (!plogMutex) plogMutex = xSemaphoreCreateMutex();

    if (!plogRtcInitialized) {
        plogRecoveredSession = (rtcMagic == MAGIC_VALID);
        if (plogRecoveredSession) {
            Serial.println("\n===== RECOVERED FROM RTC =====");
            for (int i = 0; i < PLOG_LINES; i++) {
                int idx = (rtcWriteIdx + i) % PLOG_LINES;
                if (rtcRing[idx][0]) Serial.println(rtcRing[idx]);
            }
            Serial.println("==============================\n");
        } else {
            memset(rtcRing, 0, sizeof(rtcRing));
            rtcWriteIdx = 0;
            rtcWriteCount = 0;
        }
        rtcMagic = MAGIC_VALID;
        plogRtcInitialized = true;
    }

    if (!sdAvailable || plogSdAttached) {
        return;
    }

    plogRotateIfNeeded();
    if (plogRecoveredSession && SD.exists("/data")) {
        File f = SD.open(SD_LOG, FILE_APPEND);
        if (f) {
            char uptime[16];
            formatUptime(uptime, sizeof(uptime), millis());
            f.printf("[%s] [CRIT] ===== RTC RECOVERY =====\n", uptime);
            plogAppendRingToSd(f);
            f.close();
        }
    }
    lastFlushedCount = rtcWriteCount;
    plogSdAttached = true;
}

void plogWrite(const char* line)
{
    char normalized[PLOG_LINE_LEN];
    strlcpy(normalized, line, sizeof(normalized));
    normalizeLogLine(normalized);

    if (plogMutex && xSemaphoreTake(plogMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        Serial.println(normalized);
        strlcpy(rtcRing[rtcWriteIdx], normalized, PLOG_LINE_LEN);
        rtcWriteIdx = (rtcWriteIdx + 1) % PLOG_LINES;
        rtcWriteCount++;
        xSemaphoreGive(plogMutex);
    } else {
        Serial.println(normalized);  // fallback - tylko Serial
    }
}

void plogMark(const char* level, const char* tag)
{
    char uptime[16];
    char b[PLOG_LINE_LEN];
    formatUptime(uptime, sizeof(uptime), millis());
    snprintf(b, sizeof(b), "[%s] [%s] ===== %s =====", uptime, level, tag);
    plogWrite(b);
}

void logWritef(const char* level, bool persistent, const char* fmt, ...)
{
    char msg[PLOG_LINE_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    char uptime[16];
    char line[PLOG_LINE_LEN];
    formatUptime(uptime, sizeof(uptime), millis());
    snprintf(line, sizeof(line), "[%s] [%s] %s", uptime, level, msg);
    normalizeLogLine(line);
    Serial.println(line);

    if (persistent) {
        if (plogMutex && xSemaphoreTake(plogMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            strlcpy(rtcRing[rtcWriteIdx], line, PLOG_LINE_LEN);
            rtcWriteIdx = (rtcWriteIdx + 1) % PLOG_LINES;
            rtcWriteCount++;
            xSemaphoreGive(plogMutex);
        }
    }
}

void plogFlushToSd()
{
    unsigned long now = millis();
    if (now - lastFlushMs < 10000) return;
    lastFlushMs = now;
    plogRotateIfNeeded();
    File f = SD.open(SD_LOG, FILE_APPEND);
    if (!f) return;
    if (plogMutex && xSemaphoreTake(plogMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        plogAppendPendingToSd(f);
        lastFlushedCount = rtcWriteCount;
        xSemaphoreGive(plogMutex);
    }
    f.close();
}
