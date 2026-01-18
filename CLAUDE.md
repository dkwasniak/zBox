# MusicBox - Muzyczne Pudełko dla Dzieci

## Opis projektu
Muzyczne pudełko które odtwarza muzykę po postawieniu figurki z naklejką NFC. Projekt dla córki.

## Architektura

```
┌─────────────────┐         HTTP/REST          ┌─────────────────────┐
│     ESP32       │ ◄─────────────────────────► │   Raspberry Pi      │
│  + Czytnik NFC  │                             │   (<musicbox-server>)    │
│  + Głośnik/DAC  │      Stream MP3             │   Docker: musicbox  │
└─────────────────┘                             └─────────────────────┘
```

## Komponenty

### Serwer (Raspberry Pi) - ZAIMPLEMENTOWANY
- **Lokalizacja na RPi**: `~/musicbox/`
- **Stack**: Python 3.11 + FastAPI + SQLite + Alpine.js
- **Docker**: `docker compose up -d`
- **Port**: 8000
- **Panel webowy**: http://<musicbox-server>:8000

### ESP32 - DO ZAIMPLEMENTOWANIA

**Posiadany sprzęt (kompletny):**
- ESP32 WROOM32
- Czytnik NFC PN532
- DAC PCM5102A
- JBL Go (głośnik przez AUX)
- 2x przycisk tact switch (VOL+/VOL-)

**Schemat połączeń:**
```
┌─────────────┐      I2S       ┌───────────┐    mini jack    ┌─────────┐
│   ESP32     │ ──────────────→│ PCM5102A  │ ──────────────→ │ JBL Go  │
│  WROOM32    │                │   DAC     │                 │  (AUX)  │
└─────────────┘                └───────────┘                 └─────────┘
       │
       │ SPI
       ▼
┌─────────────┐
│   PN532     │
│   (NFC)     │
└─────────────┘
```

**Okablowanie PN532 → ESP32 (SPI):**
| PN532 | ESP32 |
|-------|-------|
| VCC   | 3.3V  |
| GND   | GND   |
| SCK   | GPIO18 |
| MISO  | GPIO19 |
| MOSI  | GPIO23 |
| SS    | GPIO5  |

**Okablowanie PCM5102A → ESP32 (I2S):**
| PCM5102A | ESP32 |
|----------|-------|
| VIN      | 5V    |
| GND      | GND   |
| BCK      | GPIO26 |
| LCK      | GPIO25 |
| DIN      | GPIO22 |
| SCK      | GND    |

**Okablowanie przycisków:**
| Przycisk | ESP32 | Drugi pin |
|----------|-------|-----------|
| VOL+     | GPIO32 | GND |
| VOL-     | GPIO33 | GND |

(Wewnętrzne pull-up, GPIO32/33 obsługują RTC wake-up z deep sleep)

**PCM5102A → JBL Go:**
Kabel mini jack 3.5mm z wyjścia audio PCM5102A do wejścia AUX JBL Go.

**Biblioteki Arduino:**
- ESP32-audioI2S (streamowanie MP3)
- Adafruit_PN532 (NFC przez SPI)
- Preferences (zapis głośności do flash)

**Logika przycisków:**
- Krótkie VOL+ → głośność +1 (max 21)
- Krótkie VOL- → głośność -1 (min 0)
- Przytrzymanie VOL+ (2s) → wybudź z deep sleep
- Przytrzymanie VOL- (2s) → uśpij (deep sleep ~10µA)
- Głośność zapisywana w Preferences (pamięta po restarcie)

**Logika NFC:**
- Figurka postawiona → odtwarzaj muzykę
- Figurka zdjęta → zatrzymaj muzykę

## API Endpoints

### Dla ESP32
| Endpoint | Opis |
|----------|------|
| `GET /api/play/{nfc_uid}` | Zwraca `{stream_url, track_title, figurine_name}` |
| `GET /api/stream/{track_id}` | Stream MP3 |
| `GET /api/health` | Health check |

### Dla Panelu Admin
| Endpoint | Opis |
|----------|------|
| `GET /admin/tracks` | Lista utworów |
| `POST /admin/tracks` | Upload MP3 (multipart: title + file) |
| `DELETE /admin/tracks/{id}` | Usuń utwór |
| `GET /admin/figurines` | Lista figurek |
| `POST /admin/figurines` | Dodaj figurkę (JSON: name, nfc_uid, track_id?) |
| `PUT /admin/figurines/{id}` | Edytuj figurkę |
| `DELETE /admin/figurines/{id}` | Usuń figurkę |

## Struktura projektu

```
musicbox/
├── docker-compose.yml
├── Dockerfile
├── server/
│   ├── main.py              # FastAPI app
│   ├── database.py          # SQLAlchemy models + SQLite
│   ├── models.py            # Pydantic schemas
│   └── routers/
│       ├── api.py           # Endpointy dla ESP32
│       └── admin.py         # Endpointy dla panelu
├── music/                   # Pliki MP3 (Docker volume)
├── data/                    # Baza SQLite (Docker volume)
└── web/
    └── index.html           # Panel administracyjny
```

## Deployment na RPi

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

## Model danych

### Track (utwór)
- id, title, filename, created_at

### Figurine (figurka)
- id, name, nfc_uid (unique), track_id (nullable - placeholder), created_at

## Funkcjonalności panelu webowego
- Upload plików MP3
- Podgląd i usuwanie utworów
- Dodawanie figurek (z lub bez przypisanego utworu)
- Edycja przypisania utwór ↔ figurka
- Odtwarzanie podglądu w przeglądarce

## Przyszłe rozszerzenia (planowane)
- Skanowanie NFC telefonem → automatyczne tworzenie figurki
- Playlisty zamiast pojedynczych utworów
- Głośność per figurka

## Testowe dane
- Figurka "Elsa" z NFC UID: `<example-nfc-uid>`
- Utwór: "Pieski małe dwa"
