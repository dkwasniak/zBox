#include "nfc_module.h"
#include <Adafruit_PN532.h>
#include "zbox_config.h"
#include "logging.h"
#include "state.h"
#include "leds.h"
#include "helpers.h"
#include "persistent_log.h"
#include "nfc_presence.h"

// =============================================================================
// Private objects (static — owned by this module)
// =============================================================================

static Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
static QueueHandle_t nfcQueue = NULL;
static TaskHandle_t nfcTaskHandle = NULL;
static SemaphoreHandle_t nfcMutex = NULL;
static volatile bool nfcTaskStopRequested = false;
static RTC_DATA_ATTR bool rtcNfcPowerDownSent = false;
static int nfcErrorCount = 0;
static bool s_nfcReady = false;
static constexpr unsigned long NFC_CRITICAL_WARN_MS = 90;
static constexpr unsigned long NFC_REINIT_BACKOFF_MS = 30000;
static constexpr uint16_t NFC_READ_TIMEOUT_MS = 20;
static unsigned long s_lastNfcReinitAttemptMs = 0;

// =============================================================================
// Private: mutex / bus helpers
// =============================================================================

static void ensureNfcMutex()
{
    if (!nfcMutex)
        nfcMutex = xSemaphoreCreateMutex();
}

static bool nfcBusTake(TickType_t waitTicks)
{
    ensureNfcMutex();
    if (!nfcMutex)
        return false;
    return xSemaphoreTake(nfcMutex, waitTicks) == pdTRUE;
}

static void nfcBusGive()
{
    if (nfcMutex)
        xSemaphoreGive(nfcMutex);
}

static bool nfcCriticalBegin(TickType_t waitTicks)
{
    if (!nfcBusTake(waitTicks))
        return false;
    ledSuspendTask(); // suspends the LED task — Software SPI will not be interrupted
    return true;
}

static void nfcCriticalEnd()
{
    ledResumeTask();
    nfcBusGive();
}

// =============================================================================
// Private: raw SPI wake (PN532 cold-wake after PowerDown)
// =============================================================================

static uint8_t nfcRawSpiTransferByte(uint8_t value)
{
    uint8_t received = 0;
    for (uint8_t mask = 0x01; mask; mask <<= 1)
    {
        digitalWrite(PN532_MOSI, (value & mask) ? HIGH : LOW);
        delayMicroseconds(1);
        digitalWrite(PN532_SCK, HIGH);
        if (digitalRead(PN532_MISO))
            received |= mask;
        delayMicroseconds(1);
        digitalWrite(PN532_SCK, LOW);
        delayMicroseconds(1);
    }
    return received;
}

static uint8_t nfcRawSpiCommandReadByte(uint8_t command)
{
    digitalWrite(PN532_SS, LOW);
    delayMicroseconds(2);
    nfcRawSpiTransferByte(command);
    uint8_t value = nfcRawSpiTransferByte(0x00);
    digitalWrite(PN532_SS, HIGH);
    delayMicroseconds(2);
    return value;
}

static bool nfcRawSpiWaitReady(uint16_t timeoutMs)
{
    unsigned long start = millis();
    do
    {
        if (nfcRawSpiCommandReadByte(PN532_SPI_STATREAD) == PN532_SPI_READY)
            return true;
        delay(5);
    } while (millis() - start < timeoutMs);
    return false;
}

static void nfcRawSpiReadData(uint8_t *buffer, size_t len)
{
    digitalWrite(PN532_SS, LOW);
    delayMicroseconds(2);
    nfcRawSpiTransferByte(PN532_SPI_DATAREAD);
    for (size_t i = 0; i < len; i++)
        buffer[i] = nfcRawSpiTransferByte(0x00);
    digitalWrite(PN532_SS, HIGH);
    delayMicroseconds(2);
}

static bool nfcRawSpiWakeWithFirmwareCommand()
{
    static const uint8_t getFirmwareFrame[] = {
        PN532_SPI_DATAWRITE,
        PN532_PREAMBLE, PN532_STARTCODE1, PN532_STARTCODE2,
        0x02, 0xFE,
        PN532_HOSTTOPN532, PN532_COMMAND_GETFIRMWAREVERSION,
        0x2A,
        PN532_POSTAMBLE
    };
    static const uint8_t expectedAck[] = {
        PN532_PREAMBLE, PN532_STARTCODE1, PN532_STARTCODE2,
        0x00, 0xFF, PN532_POSTAMBLE
    };

    pinMode(PN532_SCK, OUTPUT);
    pinMode(PN532_MOSI, OUTPUT);
    pinMode(PN532_MISO, INPUT);
    pinMode(PN532_SS, OUTPUT);
    digitalWrite(PN532_SCK, LOW);
    digitalWrite(PN532_MOSI, LOW);
    digitalWrite(PN532_SS, HIGH);
    delay(1);

    digitalWrite(PN532_SS, LOW);
    delay(PN532_WAKE_SETTLE_MS);
    for (size_t i = 0; i < sizeof(getFirmwareFrame); i++)
        nfcRawSpiTransferByte(getFirmwareFrame[i]);
    delay(1);
    digitalWrite(PN532_SS, HIGH);

    uint8_t ack[6] = {};
    uint8_t response[12] = {};
    bool ackReady = nfcRawSpiWaitReady(100);
    if (ackReady)
        nfcRawSpiReadData(ack, sizeof(ack));
    bool responseReady = nfcRawSpiWaitReady(100);
    if (responseReady)
        nfcRawSpiReadData(response, sizeof(response));

    bool ackOk = ackReady && memcmp(ack, expectedAck, sizeof(ack)) == 0;
    bool responseOk = responseReady &&
        response[0] == PN532_PREAMBLE &&
        response[1] == PN532_STARTCODE1 &&
        response[2] == PN532_STARTCODE2 &&
        response[5] == PN532_PN532TOHOST &&
        response[6] == (PN532_COMMAND_GETFIRMWAREVERSION + 1);
    LOGW("[NFC] raw SPI wake ack=%d response=%d\n", (int)ackOk, (int)responseOk);
    return ackOk && responseOk;
}

// =============================================================================
// Private: init sequence
// =============================================================================

// Initialises the PN532: optional raw SPI wake + begin + getFirmwareVersion with retry.
// Safe for all paths: cold boot, deep sleep wake, software reset,
// reinit during operation. If RTC indicates a PowerDown was last sent,
// the PN532 is woken first with a raw SPI frame while NSS is held low.
// Assumes the caller holds nfcMutex and has suspended the LED task.
static bool nfcInitSequence()
{
    bool rawWakeAttempted = false;
    if (rtcNfcPowerDownSent)
    {
        rawWakeAttempted = true;
        nfcRawSpiWakeWithFirmwareCommand();
    }

    nfc.begin();

    uint32_t ver = nfc.getFirmwareVersion();
    for (int attempt = 1; attempt < 3 && !ver; attempt++)
    {
        LOGW("[NFC] init retry %d/3\n", attempt);
        if (!rawWakeAttempted)
        {
            rawWakeAttempted = true;
            nfcRawSpiWakeWithFirmwareCommand();
        }
        delay(50 * (attempt + 1)); // 100 ms, 150 ms
        ver = nfc.getFirmwareVersion();
    }

    if (ver)
    {
        rtcNfcPowerDownSent = false;
        LOGC("[BOOT] nfc_fw=0x%08lX raw_wake=%d\n", (unsigned long)ver, (int)rawWakeAttempted);
        nfc.SAMConfig();
        nfc.setPassiveActivationRetries(0x10);
        return true;
    }
    return false;
}

static void reinitNfc()
{
    s_lastNfcReinitAttemptMs = millis();
    LOGW("[NFC] reinit attempt\n");
    if (!nfcCriticalBegin(pdMS_TO_TICKS(1000)))
    {
        LOGW("[NFC] reinit skipped - bus busy\n");
        return;
    }
    bool ok = nfcInitSequence();
    nfcCriticalEnd();
    if (ok)
    {
        s_nfcReady = true;
        nfcErrorCount = 0;
        LOGC("[RECOVERY] NFC reinit OK\n");
    }
    else
    {
        s_nfcReady = false;
        LOGE("[NFC] reinit FAIL\n");
    }
}

static bool nfcReinitDue(unsigned long now)
{
    if (nfcErrorCount <= NFC_ERROR_THRESHOLD)
        return false;
    if (s_lastNfcReinitAttemptMs == 0)
        return true;
    return now - s_lastNfcReinitAttemptMs >= NFC_REINIT_BACKOFF_MS;
}

static String readNfcTagWithTimeout(uint16_t timeoutMs, bool logFound)
{
    if (!s_nfcReady)
    {
        unsigned long now = millis();
        nfcErrorCount++;
        if (nfcReinitDue(now))
        {
            reinitNfc();
        }
        return "";
    }

    uint8_t uid[7];
    uint8_t uidLength;
    unsigned long criticalStart = millis();
    if (!nfcCriticalBegin(pdMS_TO_TICKS(100)))
        return "";

    bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, timeoutMs);
    nfcCriticalEnd();
    unsigned long criticalElapsed = millis() - criticalStart;
    if (criticalElapsed > NFC_CRITICAL_WARN_MS)
    {
        LOGW("[NFC] read critical dt=%lu ms found=%d\n", criticalElapsed, (int)found);
    }
    if (found)
    {
        String uidStr = uidToString(uid, uidLength);
        if (logFound)
            LOGI("\nNFC Tag: %s\n", uidStr.c_str());
        return uidStr;
    }
    return "";
}

static String readNfcTag()
{
    return readNfcTagWithTimeout(NFC_READ_TIMEOUT_MS, true);
}

// =============================================================================
// Private: NFC task
// =============================================================================

static void nfcTaskFunc(void *param)
{
    NfcPresenceState presence{};
    nfcPresenceReset(presence);

    for (;;)
    {
        if (nfcTaskStopRequested)
            break;

        static unsigned long lastNfcHb = 0;
        if (millis() - lastNfcHb > 5000) {
            lastNfcHb = millis();
            LOGI("[NFC] alive hwm=%u err=%d\n", uxTaskGetStackHighWaterMark(NULL), nfcErrorCount);
        }

        const bool hadStableTag = presence.current_uid[0] != '\0' && !presence.lost_pending;
        String uid = readNfcTagWithTimeout(NFC_READ_TIMEOUT_MS, !hadStableTag);

        NfcPresenceResult presenceResult = nfcPresenceUpdate(
            presence,
            uid.c_str(),
            millis(),
            NFC_TAG_LOST_MS);

        if (presenceResult.action == NfcPresenceAction::Detected) {
            NfcEvent evt = {true, {}};
            strlcpy(evt.uid, presenceResult.uid, sizeof(evt.uid));
            xQueueSend(nfcQueue, &evt, 0);
        } else if (presenceResult.action == NfcPresenceAction::Removed) {
            NfcEvent evt = {false, {}};
            xQueueSend(nfcQueue, &evt, 0);
        }

        uint32_t delayMs = NFC_READ_INTERVAL;
        if (presence.lost_pending) {
            delayMs = NFC_LOST_POLL_INTERVAL_MS;
        } else if (presence.current_uid[0] != '\0') {
            delayMs = NFC_PRESENT_POLL_INTERVAL_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(delayMs));
    }

    nfcTaskHandle = NULL;
    vTaskDelete(NULL);
}

// =============================================================================
// Public API
// =============================================================================

bool nfcInit()
{
    if (nfcCriticalBegin(portMAX_DELAY))
    {
        s_nfcReady = nfcInitSequence();
        nfcCriticalEnd();
    }
    else
    {
        s_nfcReady = false;
    }
    return s_nfcReady;
}

bool nfcIsReady()
{
    return s_nfcReady;
}

bool nfcPrescan(char *uidBuf, size_t len)
{
    uint8_t uid[7];
    uint8_t uidLength;
    bool found = false;
    if (nfcCriticalBegin(pdMS_TO_TICKS(200)))
    {
        found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 50);
        nfcCriticalEnd();
    }
    if (found)
    {
        String uidStr = uidToString(uid, uidLength);
        strlcpy(uidBuf, uidStr.c_str(), len);
        return true;
    }
    return false;
}

void nfcStartTask()
{
    nfcTaskStopRequested = false;
    nfcQueue = xQueueCreate(5, sizeof(NfcEvent));
    xTaskCreatePinnedToCore(nfcTaskFunc, "nfc", 4096, NULL, 1, &nfcTaskHandle, 1);
    LOGI("NFC task started (core 1)\n");
}

bool nfcGetEvent(NfcEvent *e, TickType_t timeout)
{
    if (!nfcQueue) return false;
    return xQueueReceive(nfcQueue, e, timeout) == pdTRUE;
}

void nfcStopTaskForSleep()
{
    if (nfcTaskHandle) {
        nfcTaskStopRequested = true;
        unsigned long stopStart = millis();
        while (nfcTaskHandle && millis() - stopStart < (NFC_READ_INTERVAL + 250))
            delay(10);
        if (nfcTaskHandle)
            LOGW("[NFC] task stop timeout - PowerDown will wait for bus\n");
    }
}

void nfcPowerDown()
{
    if (!s_nfcReady)
    {
        LOGW("[NFC] PowerDown skipped - not ready\n");
        rtcNfcPowerDownSent = false;
        return;
    }
    if (!nfcCriticalBegin(pdMS_TO_TICKS(1000)))
    {
        LOGW("[NFC] PowerDown skipped - bus busy\n");
        rtcNfcPowerDownSent = false;
        return;
    }
    // PN532 PowerDown — draws ~1 mA instead of ~100 mA during deep sleep.
    // ESP32 performs a full boot on wake-up and calls nfc.begin() again.
    uint8_t cmd[] = {PN532_COMMAND_POWERDOWN, PN532_WAKEUP_SPI};
    bool ack = nfc.sendCommandCheckAck(cmd, sizeof(cmd), 100);
    rtcNfcPowerDownSent = ack;
    s_nfcReady = false;
    nfcCriticalEnd();
    LOGC("[SLEEP] NFC PowerDown wake=SPI(0x%02X) ack=%d\n", PN532_WAKEUP_SPI, (int)ack);
    if (ack)
        delay(2); // PN532 needs ~1 ms to actually enter PowerDown.
}

uint32_t nfcGetTaskHWM()
{
    return nfcTaskHandle ? uxTaskGetStackHighWaterMark(nfcTaskHandle) : 0;
}
