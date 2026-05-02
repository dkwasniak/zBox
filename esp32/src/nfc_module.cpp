#include "nfc_module.h"
#include <Adafruit_PN532.h>
#include "musicbox_config.h"
#include "logging.h"
#include "state.h"
#include "leds.h"
#include "helpers.h"
#include "persistent_log.h"

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
    ledSuspendTask(); // zawiesza LED task — Software SPI nie zostanie przerwany
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
    PLOGF("[NFC] raw SPI wake ack=%d response=%d", (int)ackOk, (int)responseOk);
    return ackOk && responseOk;
}

// =============================================================================
// Private: init sequence
// =============================================================================

// Inicjalizuje PN532: opcjonalny raw SPI wake + begin + getFirmwareVersion z retry.
// Bezpieczny dla wszystkich ścieżek: cold boot, deep sleep wake, software reset,
// reinit podczas pracy. Jeśli RTC mówi że ostatnio wysłano PowerDown,
// najpierw budzimy PN532 surową ramką SPI z NSS trzymanym low.
// Zakłada że wywołujący trzyma nfcMutex i zawiesił LED task.
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
        PLOGF("[NFC] init retry %d/3", attempt);
        if (!rawWakeAttempted)
        {
            rawWakeAttempted = true;
            nfcRawSpiWakeWithFirmwareCommand();
        }
        delay(50 * (attempt + 1)); // 100ms, 150ms
        ver = nfc.getFirmwareVersion();
    }

    if (ver)
    {
        rtcNfcPowerDownSent = false;
        PLOGF("[NFC] fw=0x%08lX rawWake=%d", ver, (int)rawWakeAttempted);
        nfc.SAMConfig();
        nfc.setPassiveActivationRetries(0x10);
        return true;
    }
    return false;
}

static void reinitNfc()
{
    PLOGF("[NFC] reinit attempt");
    if (!nfcCriticalBegin(pdMS_TO_TICKS(1000)))
    {
        PLOGF("[NFC] reinit skipped - bus busy");
        return;
    }
    bool ok = nfcInitSequence();
    nfcCriticalEnd();
    if (ok)
    {
        nfcReady = true;
        nfcErrorCount = 0;
        PLOGF("[NFC] reinit OK");
    }
    else
    {
        nfcReady = false;
        PLOGF("[NFC] reinit FAIL");
    }
}

static String readNfcTag()
{
    if (!nfcReady)
    {
        if (++nfcErrorCount > NFC_ERROR_THRESHOLD)
        {
            reinitNfc();
        }
        return "";
    }

    uint8_t uid[7];
    uint8_t uidLength;
    if (!nfcCriticalBegin(pdMS_TO_TICKS(100)))
        return "";

    bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 20);
    nfcCriticalEnd();
    if (found)
    {
        String uidStr = uidToString(uid, uidLength);
        LOG("\nNFC Tag: %s\n", uidStr.c_str());
        return uidStr;
    }
    return "";
}

// =============================================================================
// Private: NFC task
// =============================================================================

static void nfcTaskFunc(void *param)
{
    int localNoTagCount = 0;
    char localLastUid[30] = {};

    for (;;)
    {
        if (nfcTaskStopRequested)
            break;

        static unsigned long lastNfcHb = 0;
        if (millis() - lastNfcHb > 5000) {
            lastNfcHb = millis();
            PLOGF("[NFC] alive hwm=%u err=%d", uxTaskGetStackHighWaterMark(NULL), nfcErrorCount);
        }

        String uid = readNfcTag();

        if (!uid.isEmpty())
        {
            localNoTagCount = 0;
            if (strcmp(uid.c_str(), localLastUid) != 0)
            {
                strlcpy(localLastUid, uid.c_str(), sizeof(localLastUid));
                NfcEvent evt = {true, {}};
                strlcpy(evt.uid, uid.c_str(), sizeof(evt.uid));
                xQueueSend(nfcQueue, &evt, 0);
            }
        }
        else
        {
            if (++localNoTagCount >= NO_TAG_THRESHOLD && localLastUid[0] != '\0')
            {
                localLastUid[0] = '\0';
                localNoTagCount = 0;
                NfcEvent evt = {false, {}};
                xQueueSend(nfcQueue, &evt, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(NFC_READ_INTERVAL));
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
        nfcReady = nfcInitSequence();
        nfcCriticalEnd();
    }
    else
    {
        nfcReady = false;
    }
    return nfcReady;
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
    LOG("[T+%4lu] NFC task started (core 1)\n", millis() - bootStart);
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
            LOGLN("[NFC] task stop timeout - PowerDown will wait for bus");
    }
}

void nfcPowerDown()
{
    if (!nfcReady)
    {
        LOGLN("[NFC] PowerDown skipped - not ready");
        rtcNfcPowerDownSent = false;
        return;
    }
    if (!nfcCriticalBegin(pdMS_TO_TICKS(1000)))
    {
        LOGLN("[NFC] PowerDown skipped - bus busy");
        rtcNfcPowerDownSent = false;
        return;
    }
    // PN532 PowerDown — pobiera ~1mA zamiast ~100mA podczas deep sleep.
    // ESP32 po wybudzeniu robi pełny boot i wywołuje nfc.begin() od nowa.
    uint8_t cmd[] = {PN532_COMMAND_POWERDOWN, PN532_WAKEUP_SPI};
    bool ack = nfc.sendCommandCheckAck(cmd, sizeof(cmd), 100);
    rtcNfcPowerDownSent = ack;
    nfcReady = false;
    nfcCriticalEnd();
    LOG("[NFC] PowerDown wake=SPI(0x%02X) ack=%d\n", PN532_WAKEUP_SPI, (int)ack);
    if (ack)
        delay(2); // PN532 potrzebuje ok. 1ms żeby faktycznie wejść w PowerDown.
}

uint32_t nfcGetTaskHWM()
{
    return nfcTaskHandle ? uxTaskGetStackHighWaterMark(nfcTaskHandle) : 0;
}
