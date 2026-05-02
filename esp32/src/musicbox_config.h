#pragma once

// =============================================================================
// PINY
// =============================================================================

#define SD_CS 4 // SD Card CS (wbudowany slot Lolin D32 Pro)

// PN532 NFC (Software SPI)
#define PN532_SCK 22
#define PN532_MISO 21
#define PN532_MOSI 0
#define PN532_SS 5

#define LED_PIN 14  // WS2812B DIN
#define LED_EN  27  // P-MOSFET gate (AO3415A): LOW = LEDy ON, HIGH = OFF
#define LED_COUNT 12
#define LED_BRIGHTNESS 40

#define BTN_A 32 // wolny  (RTC)
#define BTN_B 33 // wolny  (RTC)
#define BTN_C 25 // VOL-   (RTC)
#define BTN_D 26 // VOL+   (RTC, wake-up z deep sleep przez ext0)
#define BTN_COUNT 4

#define JBL_POWER 13  // Tranzystor NPN -> przycisk POWER na JBL
#define JBL_STATUS 34 // ADC - linia statusowa JBL (dzielnik 10k/22k)
#define BAT_ADC_PIN 36 // GPIO36 (VP) - zewnętrzny dzielnik 100k/100k VBAT->VP->GND

// =============================================================================
// KONFIGURACJA
// =============================================================================

#define BT_SPEAKER_NAME "JBL GO 2"

#define TEST_AUDIO_MODE false
#define TEST_SD_FILE "/music/9383471d_babajaga.mp3"

#define LONG_PRESS_MS 2000
#define DEBOUNCE_MS 50
#define NFC_READ_INTERVAL 1000 // było 300 - szybsza detekcja tagu
#define NFC_ERROR_THRESHOLD 10
#define NO_TAG_THRESHOLD 2
#define PN532_WAKEUP_SPI 0x20
#define PN532_WAKE_SETTLE_MS 5
#define AUDIO_BUF_SIZE 2048

// JBL
#define JBL_POWER_PRESS_MS 500
#define JBL_STATUS_THRESHOLD 500  // ~0.4V — powyżej residual/noise, poniżej ON (~2V+)
#define JBL_BOOT_WAIT_MS 5000 // timeout na cold boot JBL + A2DP reconnect

// Głośność Bluetooth (AVRCP)
#define BT_VOL_STEP 5
#define BT_VOL_MIN 0
#define BT_VOL_MAX 100
#define BT_VOL_DEFAULT 50

// Idle timeout → deep sleep
#define IDLE_TIMEOUT_MS (10UL * 60 * 1000) // 15 minut bez odtwarzania

// Sync
#define SERVER_HOST "<musicbox-server-ip>"
#define SERVER_PORT 8000
#define HTTP_TIMEOUT 15000
#define DOWNLOAD_BUF_SIZE 16384
