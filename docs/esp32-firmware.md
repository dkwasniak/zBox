# ESP32 Firmware

The zBox firmware targets the ESP32 Lolin D32 Pro and is built with PlatformIO using the mixed `arduino, espidf` framework setup.

## What it does

- reads NFC tags through a PN532 module
- plays MP3 files stored on the SD card
- sends audio to a paired Bluetooth speaker through A2DP
- exposes sync and diagnostic modes over Wi-Fi
- drives LED feedback and button input

## Key constraints

- playback is offline-first
- Wi-Fi is used only for sync and maintenance
- Bluetooth and Wi-Fi are intentionally separated in the runtime flow due to memory and radio limits
- the firmware still needs a compile-time `SERVER_HOST` value before production flashing

## Important files

- [`esp32/src/main.cpp`](../esp32/src/main.cpp)
- [`esp32/src/zbox_config.h`](../esp32/src/zbox_config.h)
- [`esp32/platformio.ini`](../esp32/platformio.ini)
- [`esp32/sdkconfig.defaults`](../esp32/sdkconfig.defaults)

## Main dependencies

- `arduino-audio-tools`
- `ESP32-A2DP`
- `arduino-libhelix`
- `Adafruit PN532`
- `ArduinoJson`
- `WiFiManager`
- `FastLED`

## Runtime modes

### Normal mode

- initializes storage, LEDs, NFC, buttons, and Bluetooth
- loads figurine mappings from the SD card
- starts playback when a known NFC tag is detected

### Sync mode

- connects to Wi-Fi
- downloads missing audio files from the server
- removes stale files no longer present in the manifest
- writes updated mappings to the SD card

### Diagnostic mode

- exposes service endpoints used by the admin portal
- is intended for maintenance and troubleshooting rather than daily use

### Deep sleep

- powers down playback-related peripherals
- preserves low-power wake behavior through the configured button path

## Before flashing

Review the following first:

- `TEST_AUDIO_MODE`
- `SERVER_HOST`
- board and partition settings
- any local Wi-Fi or deployment assumptions

## Related docs

- [Hardware documentation](hardware.md)
- [Server documentation](server.md)
- [ESP32 quick start](../esp32/README.md)
