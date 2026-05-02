#include "diagnostic_mode.h"
#include "musicbox_config.h"
#include "logging.h"
#include "state.h"
#include "leds.h"
#include "battery.h"
#include "diagnostics.h"
#include "audio.h"
#include "nfc_module.h"
#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <WiFiManager.h>
#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <stdarg.h>
#include <string.h>

static WiFiServer diagServer(DIAG_TELNET_PORT);
static WiFiClient diagClient;

static void diagLogf(const char *fmt, ...)
{
    char line[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    Serial.println(line);
    if (diagClient && diagClient.connected())
        diagClient.println(line);
}

static void clearDiagnosticFlag()
{
    if (SD.exists(DIAG_PENDING_PATH))
        SD.remove(DIAG_PENDING_PATH);
}

static void disableBtForDiagnostic()
{
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    esp_bt_mem_release(ESP_BT_MODE_BTDM);
}

static void sendBanner()
{
    diagClient.println();
    diagClient.println("=== MusicBox Diagnostic Mode ===");
    diagClient.printf("IP: %s\r\n", WiFi.localIP().toString().c_str());
    diagClient.println("Commands: exit, quit");
    diagClient.println();
}

static void sendDiagnosticSnapshot()
{
    BatteryReading bat = readBatteryReading();
    esp_reset_reason_t reason = esp_reset_reason();
    diagLogf("[DIAG] uptime_ms=%lu reset=%d(%s) ip=%s rssi=%d",
             millis(), (int)reason, resetReasonName(reason),
             WiFi.localIP().toString().c_str(), WiFi.RSSI());
    diagLogf("[DIAG] battery adc_pin_v=%.3f battery_v=%.3f bars=%d color=%s charging_known=false",
             bat.adcPinVoltage, bat.batteryVoltage, bat.bars, bat.color);
    diagLogf("[DIAG] heap free=%u min=%u largest=%u",
             ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    diagLogf("[DIAG] hwm loop=%u led=%u audio=%u nfc=%u",
             uxTaskGetStackHighWaterMark(NULL), ledGetTaskHWM(), audioGetTaskHWM(), nfcGetTaskHWM());
}

static bool handleClientInput()
{
    static char cmd[32] = {};
    static size_t len = 0;

    while (diagClient && diagClient.connected() && diagClient.available())
    {
        char c = (char)diagClient.read();
        if (c == '\r')
            continue;
        if (c == '\n')
        {
            cmd[len] = '\0';
            len = 0;
            if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0)
                return true;
            if (cmd[0])
                diagClient.printf("Unknown command: %s\r\n", cmd);
            cmd[0] = '\0';
            continue;
        }
        if (len < sizeof(cmd) - 1)
            cmd[len++] = c;
    }
    return false;
}

static bool handleButtonExit()
{
    static bool armed = false;
    static unsigned long holdStart = 0;

    bool bothDown = digitalRead(BTN_A) == LOW && digitalRead(BTN_B) == LOW;
    if (!armed)
    {
        if (!bothDown)
            armed = true;
        return false;
    }

    if (!bothDown)
    {
        holdStart = 0;
        return false;
    }

    if (holdStart == 0)
        holdStart = millis();
    return millis() - holdStart >= LONG_PRESS_MS;
}

static void exitDiagnosticMode(const char *reason)
{
    diagLogf("[DIAG] exit requested: %s", reason);
    ledFlashResult(true);
    clearDiagnosticFlag();
    delay(500);
    ESP.restart();
}

void runDiagnosticMode()
{
    LOGLN("\n=== MusicBox DIAGNOSTIC MODE ===\n");
    ledSetDiagnostic();
    disableBtForDiagnostic();

    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = 15000,
        .idle_core_mask = 0,
        .trigger_panic = false
    };
    esp_task_wdt_reconfigure(&wdt_cfg);
    esp_task_wdt_add(NULL);

    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    wm.setConnectTimeout(10);

    if (!wm.autoConnect(DIAG_AP_NAME))
    {
        LOGLN("[DIAG] WiFi not connected");
        ledFlashResult(false);
        clearDiagnosticFlag();
        delay(1000);
        ESP.restart();
    }

    diagServer.begin();
    diagServer.setNoDelay(true);
    LOG("[DIAG] Telnet server listening on %s:%d\n",
        WiFi.localIP().toString().c_str(), DIAG_TELNET_PORT);
    LOGLN("[DIAG] Connect with: nc <ip> 23");

    unsigned long lastSnapshot = 0;
    for (;;)
    {
        esp_task_wdt_reset();

        if (!diagClient || !diagClient.connected())
        {
            WiFiClient incoming = diagServer.accept();
            if (incoming)
            {
                diagClient.stop();
                diagClient = incoming;
                diagClient.setNoDelay(true);
                sendBanner();
                sendDiagnosticSnapshot();
                lastSnapshot = millis();
            }
        }

        if (handleClientInput())
            exitDiagnosticMode("telnet");

        if (handleButtonExit())
            exitDiagnosticMode("buttons A+B long");

        unsigned long now = millis();
        if (now - lastSnapshot >= DIAG_LOG_INTERVAL_MS)
        {
            sendDiagnosticSnapshot();
            lastSnapshot = now;
        }

        delay(20);
    }
}
