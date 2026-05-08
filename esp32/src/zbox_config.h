#pragma once

#define ENABLE_LEDS true

// =============================================================================
// Pins
// =============================================================================

#define SD_CS 4 // SD card CS on the built-in Lolin D32 Pro slot

// PN532 NFC (software SPI)
#define PN532_SCK 22
#define PN532_MISO 21
#define PN532_MOSI 0
#define PN532_SS 5

#define LED_PIN 14  // WS2812B DIN
#define LED_EN  27  // P-MOSFET gate (AO3415A): LOW = LEDs on, HIGH = off
#define LED_COUNT 12
#define LED_BRIGHTNESS 40

#define BTN_A 32 // free (RTC)
#define BTN_B 33 // free (RTC)
#define BTN_C 25 // VOL- (RTC)
#define BTN_D 26 // VOL+ (RTC, deep-sleep wake through ext0)
#define BTN_COUNT 4

#define JBL_POWER 13  // NPN transistor driving the speaker power button
#define JBL_STATUS 34 // ADC input for the speaker status line (10k/22k divider)
#define BAT_ADC_PIN 35 // GPIO35 (I35) reads the onboard D32 Pro battery divider
#define BAT_ADC_DIVIDER_RATIO 2.0f // D32 Pro: VBAT --100k-- GPIO35 --100k-- GND
#define BAT_ADC_CALIBRATION 0.983f // Per-device ADC calibration factor

// =============================================================================
// Configuration
// =============================================================================

#define BT_SPEAKER_NAME "JBL GO 2"

#define LONG_PRESS_MS 2000
#define DOUBLE_CLICK_WINDOW_MS 350
#define EMERGENCY_SLEEP_MS 10000
#define WAKE_ABORT_MS 800
#define WAKE_NIGHT_LIGHT_MS 1600
#define NIGHT_LIGHT_SLEEP_HOLD_MS 1000
#define DEBOUNCE_MS 50
#define NFC_READ_INTERVAL 1000
#define NFC_ERROR_THRESHOLD 10
#define NO_TAG_THRESHOLD 2
#define NFC_TAG_LOST_MS 3000  // Require a sustained missing read before treating the tag as removed
#define PN532_WAKEUP_SPI 0x20
#define PN532_WAKE_SETTLE_MS 5
#define AUDIO_BUF_SIZE 2048

// JBL
#define JBL_POWER_PRESS_MS 500
#define JBL_STATUS_THRESHOLD 500  // ~0.4V; residual/noise stays below, ON is usually ~2V+
#define JBL_BOOT_WAIT_MS 5000 // timeout for cold boot + A2DP reconnect

// Bluetooth volume (AVRCP)
#define BT_VOL_STEP 5
#define BT_VOL_MIN 0
#define BT_VOL_MAX 100
#define BT_VOL_DEFAULT 50
#define NIGHT_LIGHT_BRIGHTNESS_STEP 10
#define NIGHT_LIGHT_BRIGHTNESS_MIN 10
#define NIGHT_LIGHT_BRIGHTNESS_MAX 100
#define NIGHT_LIGHT_BRIGHTNESS_DEFAULT 50

// Idle timeout -> deep sleep
#define IDLE_TIMEOUT_MS (10UL * 60 * 1000) // 10 minutes without playback
#define NIGHT_LIGHT_TIMEOUT_MS (15UL * 60 * 1000)

// Sync
#define SERVER_HOST "zbox.local"
#define SERVER_PORT 8000
#define HTTP_TIMEOUT 15000
#define DOWNLOAD_BUF_SIZE 16384

// Diagnostic mode
#define DIAG_PENDING_PATH "/data/diag_pending"
#define DIAG_AP_NAME "zBox-Diag"
#define DIAG_HTTP_PORT 80
#define DIAG_OTA_PORT 3232
#define DIAG_LOG_INTERVAL_MS 5000
