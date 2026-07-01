#pragma once

#define ENABLE_LEDS true
#define ENABLE_NFC true

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

#define BTN_A 36 // VP / input-only, external pull-up required
#define BTN_B 39 // VN / input-only, external pull-up required
#define BTN_C 25 // VOL- (RTC)
#define BTN_D 26 // VOL+ (RTC, deep-sleep wake through ext0)
#define BTN_COUNT 4

#define AUDIO_I2S_BCLK 32
#define AUDIO_I2S_LRCK 33
#define AUDIO_I2S_DOUT 13
#define BAT_ADC_PIN 35 // GPIO35 (I35) reads the onboard D32 Pro battery divider
#define BAT_ADC_DIVIDER_RATIO 2.0f // D32 Pro: VBAT --100k-- GPIO35 --100k-- GND
#define BAT_ADC_CALIBRATION 0.983f // Per-device ADC calibration factor

// Active low-voltage cutoff: while the device is running, a sustained critical
// voltage forces a normal deep sleep (power-off sound + shutdown) instead of
// discharging the cell down to the pack's PCM cutoff (~2.5 V). No hardware latch
// exists, so deep sleep is the lowest state firmware can reach.
#define BAT_CRITICAL_VOLTAGE 3.30f    // force deep sleep below this
#define BAT_CRITICAL_RELEASE_V 3.40f  // hysteresis: reset the low counter above this
#define BAT_CRITICAL_POLL_MS 15000UL  // sampling cadence while awake
#define BAT_CRITICAL_CONSECUTIVE 3    // consecutive lows before sleeping (~45 s debounce)

// =============================================================================
// Configuration
// =============================================================================

#define LONG_PRESS_MS 2000
#define DOUBLE_CLICK_WINDOW_MS 350
#define WAKE_ABORT_MS 400
#define NIGHT_LIGHT_SLEEP_HOLD_MS 1000
#define DEBOUNCE_MS 20
#define NFC_READ_INTERVAL 1000
#define NFC_PRESENT_POLL_INTERVAL_MS NFC_READ_INTERVAL
#define NFC_BOOT_INIT_DELAY_MS 250
#define NFC_BOOT_MAPPING_DELAY_MS 350
#define NFC_ERROR_THRESHOLD 10
#define NFC_TAG_LOST_MS 1500  // Require a sustained missing read before treating the tag as removed
#define NFC_LOST_POLL_INTERVAL_MS 150
#define PN532_WAKEUP_SPI 0x20
#define PN532_WAKE_SETTLE_MS 5
#define AUDIO_BUF_SIZE 2048

// Output volume
#define SYSTEM_SOUND_VOL_PERCENT 20
#define NIGHT_LIGHT_BRIGHTNESS_STEP 10
#define NIGHT_LIGHT_BRIGHTNESS_MIN 10
#define NIGHT_LIGHT_BRIGHTNESS_MAX 100
#define NIGHT_LIGHT_BRIGHTNESS_DEFAULT 50

// Idle timeout -> deep sleep
#define IDLE_TIMEOUT_MS (10UL * 60 * 1000) // 10 minutes without playback
#define NIGHT_LIGHT_TIMEOUT_MS (15UL * 60 * 1000)

// Sync / service mode
#define WIFI_HOSTNAME "zbox"
#define SYNC_PENDING_PATH "/data/sync_pending"
#define SYNC_AP_NAME "zBox-Sync"
#define SYNC_HTTP_PORT 80
#define SYNC_OTA_PORT 3232
#define SYNC_LOG_INTERVAL_MS 5000

// Bluetooth target speaker (NVS-backed, changeable from web portal)
#define BT_TARGET_NVS_KEY   "bt_target"
#define BT_TARGET_MAC_NVS_KEY "bt_mac"
#define BT_DEFAULT_NAME     "zBox Headphones"
#define BT_SCAN_MAX_RESULTS 12
#define BT_SCAN_NAME_MAX    64
