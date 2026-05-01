#include "persistent_log.h"
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

RTC_NOINIT_ATTR static char rtcRing[PLOG_LINES][PLOG_LINE_LEN];
RTC_NOINIT_ATTR static uint16_t rtcWriteIdx;
RTC_NOINIT_ATTR static uint32_t rtcMagic;

#define MAGIC_VALID 0xCAFEBABEu
static const char* SD_LOG = "/data/debug.log";
static const char* SD_OLD = "/data/debug.log.old";
static unsigned long lastFlushMs = 0;
static SemaphoreHandle_t plogMutex = NULL;

void plogInit()
{
    if (!plogMutex) plogMutex = xSemaphoreCreateMutex();
    bool valid = (rtcMagic == MAGIC_VALID);
    if (valid) {
        Serial.println("\n===== RECOVERED FROM RTC =====");
        for (int i = 0; i < PLOG_LINES; i++) {
            int idx = (rtcWriteIdx + i) % PLOG_LINES;
            if (rtcRing[idx][0]) Serial.println(rtcRing[idx]);
        }
        Serial.println("==============================\n");
        if (SD.exists("/data")) {
            File f = SD.open(SD_LOG, FILE_APPEND);
            if (f) {
                f.printf("\n===== BOOT %lu (recovery) =====\n", millis());
                for (int i = 0; i < PLOG_LINES; i++) {
                    int idx = (rtcWriteIdx + i) % PLOG_LINES;
                    if (rtcRing[idx][0]) f.println(rtcRing[idx]);
                }
                f.close();
            }
        }
    } else {
        memset(rtcRing, 0, sizeof(rtcRing));
        rtcWriteIdx = 0;
        if (SD.exists("/data")) {
            File f = SD.open(SD_LOG, FILE_APPEND);
            if (f) { f.printf("\n===== BOOT %lu (clean) =====\n", millis()); f.close(); }
        }
    }
    rtcMagic = MAGIC_VALID;

    // Rotacja jeśli > 1MB
    File f = SD.open(SD_LOG, FILE_READ);
    if (f && f.size() > 1024 * 1024) {
        f.close();
        if (SD.exists(SD_OLD)) SD.remove(SD_OLD);
        SD.rename(SD_LOG, SD_OLD);
    } else if (f) f.close();
}

void plogWrite(const char* line)
{
    if (plogMutex && xSemaphoreTake(plogMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        Serial.println(line);
        strlcpy(rtcRing[rtcWriteIdx], line, PLOG_LINE_LEN);
        rtcWriteIdx = (rtcWriteIdx + 1) % PLOG_LINES;
        xSemaphoreGive(plogMutex);
    } else {
        Serial.println(line);  // fallback - tylko Serial
    }
}

void plogMark(const char* tag)
{
    char b[PLOG_LINE_LEN];
    snprintf(b, sizeof(b), "<%lu> ===== %s =====", millis(), tag);
    plogWrite(b);
}

void plogFlushToSd()
{
    unsigned long now = millis();
    if (now - lastFlushMs < 10000) return;
    lastFlushMs = now;
    File f = SD.open(SD_LOG, FILE_APPEND);
    if (!f) return;
    f.printf("\n--- snapshot %lu ---\n", now);
    if (plogMutex && xSemaphoreTake(plogMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < PLOG_LINES; i++) {
            int idx = (rtcWriteIdx + i) % PLOG_LINES;
            if (rtcRing[idx][0]) f.println(rtcRing[idx]);
        }
        xSemaphoreGive(plogMutex);
    }
    f.close();
}
