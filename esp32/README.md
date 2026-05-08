# zBox ESP32 Firmware

Firmware for the ESP32 Lolin D32 Pro using PlatformIO and the mixed `arduino, espidf` framework setup.

## Quick start

```bash
pio run
pio run -t upload
pio device monitor
```

## Before flashing

- review `TEST_AUDIO_MODE`
- set `SERVER_HOST` for your own deployment
- verify the board and partition settings match your hardware

The main project constants live in [`src/zbox_config.h`](src/zbox_config.h).

## Documentation

- [Firmware architecture](../docs/esp32-firmware.md)
- [Hardware notes](../docs/hardware.md)
- [Server API and deployment](../docs/server.md)
