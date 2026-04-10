# MusicBox - Muzyczne Pudełko dla Dzieci

## Opis projektu
Muzyczne pudełko które odtwarza muzykę po postawieniu figurki z naklejką NFC. Projekt dla córki.

## Architektura

```
┌─────────────────┐    Bluetooth A2DP     ┌─────────────┐
│     ESP32       │ ─────────────────────► │   JBL GO 2  │
│  + PN532 (NFC)  │                        └─────────────┘
│  + karta SD     │
│  + LED WS2812B  │         HTTP (tylko w trybie sync)
│  + przyciski    │ ◄───────────────────────┐
└─────────────────┘                         │
                                 ┌───────────────────────┐
                                 │  Raspberry Pi         │
                                 │  <musicbox-server>:8000    │
                                 │  Docker: musicbox     │
                                 └───────────────────────┘
```

**Tryb pracy ESP32:** offline. Muzyka + mappingi leżą na SD. WiFi uruchamiane tylko w trybie sync (oba przyciski 2s), nigdy równocześnie z Bluetooth.

## Struktura repo

```
musicbox/
├── CLAUDE.md                # ten plik (high-level, auto-loaded)
├── docs/
│   ├── hardware.md          # piny, okablowanie, PCB, zasilanie
│   ├── esp32-firmware.md    # stack audio, boot flow, tryby, logika
│   └── server.md            # API, model danych, deployment
├── docker-compose.yml
├── Dockerfile
├── deploy.sh
├── server/                  # FastAPI + SQLite (szczegóły: docs/server.md)
├── web/index.html           # Panel admin (Alpine.js)
├── music/                   # Pliki MP3 (Docker volume)
├── data/                    # SQLite (Docker volume)
├── esp32/                   # Firmware PlatformIO (szczegóły: docs/esp32-firmware.md)
└── gerbers/                 # Pliki PCB (szczegóły: docs/hardware.md)
```

## Gdzie szukać szczegółów

| Potrzebujesz informacji o... | Plik |
|-------------------------------|------|
| Pinoucie ESP32, okablowaniu, PCB, zasilaniu, JBL, LED | `docs/hardware.md` |
| Architekturze firmware, bibliotekach, boot flow, trybach pracy, logice NFC/BT/sync, buildzie | `docs/esp32-firmware.md` |
| API serwera, modelu danych, panelu admin, deploymencie | `docs/server.md` |

**WAŻNE:** zawsze przeczytaj odpowiedni plik z `docs/` przed modyfikacją kodu w danej warstwie. Nie zgaduj pinów ani endpointów z pamięci.

## Deployment serwera na RPi

```bash
# Kopiowanie zmian z lokalnej maszyny
rsync -avz --exclude '__pycache__' --exclude '*.pyc' --exclude '.git' \
  <repo>/ rpi@<musicbox-server>:~/musicbox/

# Na RPi - rebuild i restart
ssh rpi@<musicbox-server> "cd ~/musicbox && docker compose up -d --build"

# Logi
ssh rpi@<musicbox-server> "docker logs musicbox"

# Restart
ssh rpi@<musicbox-server> "cd ~/musicbox && docker compose restart"
```

Panel webowy: http://<musicbox-server>:8000

## Build firmware ESP32

```bash
cd esp32
pio run                 # kompilacja
pio run -t upload       # upload
pio device monitor      # serial 115200
```

Szczegóły buildu, zależności i flagi: `docs/esp32-firmware.md`.

## Przyszłe rozszerzenia (planowane)
- Skanowanie NFC telefonem → automatyczne tworzenie figurki
- Playlisty zamiast pojedynczych utworów
