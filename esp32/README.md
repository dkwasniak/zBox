# MusicBox ESP32 Firmware

Firmware dla ESP32 Lolin D32 Pro (PlatformIO, framework `arduino, espidf`).

Cały kod firmware siedzi w `src/main.cpp`.

## Szybki start

```bash
pio run                     # kompilacja
pio run -t upload           # upload przez USB
pio device monitor          # serial 115200
```

## Dokumentacja

Szczegóły projektu są w katalogu `../docs/`:

| Temat | Plik |
|-------|------|
| Architektura firmware, stack audio, boot flow, tryby pracy (normal / test / sync / deep sleep), logika NFC / BT / przycisków, LED, build, znane problemy | [`../docs/esp32-firmware.md`](../docs/esp32-firmware.md) |
| Pinout ESP32, okablowanie PN532 / WS2812B / JBL / przycisków, PCB, zasilanie | [`../docs/hardware.md`](../docs/hardware.md) |
| API serwera używane przez sync mode (`/api/sync`, `/api/stream/file/{filename}`) | [`../docs/server.md`](../docs/server.md) |

High-level overview projektu: [`../CLAUDE.md`](../CLAUDE.md).

## ⚠️ Uwaga przed flashowaniem

W `src/main.cpp` obecnie ustawione jest `#define TEST_AUDIO_MODE true` - w tym trybie firmware pomija odczyt NFC i zapętla jeden plik testowy. Przed flashowaniem docelowego urządzenia ustaw `TEST_AUDIO_MODE` na `false` i upewnij się że `SERVER_HOST` wskazuje na właściwe IP serwera (obecnie hardcoded na `<musicbox-server-ip>`).
