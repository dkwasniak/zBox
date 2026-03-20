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

### ESP32 - ZAIMPLEMENTOWANY

**Posiadany sprzęt:**
- ESP32 Lolin D32 Pro (WROOM32 + PSRAM + wbudowany slot SD)
- Czytnik NFC PN532
- DAC PCM5102A
- JBL Go (głośnik przez AUX, sterowany przez tranzystory)
- 2x przycisk tact switch (VOL+/VOL-)
- 3x tranzystor NPN BC547 + rezystory 2.2kΩ (sterowanie przyciskami JBL)
- Karta SD (muzyka i mappingi offline)

**Tryb pracy:** Offline - muzyka i mappingi na karcie SD, synchronizacja z serwerem na żądanie (oba przyciski 2s).

**Schemat połączeń:**
```
┌─────────────┐      I2S       ┌───────────┐    mini jack    ┌─────────┐
│ ESP32 Lolin │ ──────────────→│ PCM5102A  │ ──────────────→ │ JBL Go  │
│  D32 Pro    │                │   DAC     │                 │  (AUX)  │
└─────────────┘                └───────────┘                 └─────────┘
       │                                                          ▲
       │ Software SPI                              Tranzystory NPN│
       ▼                                           (POWER/VOL+/-) │
┌─────────────┐                                    GPIO13/14/15 ──┘
│   PN532     │
│   (NFC)     │
└─────────────┘
```

**Okablowanie PN532 → ESP32 (Software SPI):**
| PN532 | ESP32 | Uwagi |
|-------|-------|-------|
| VCC   | 3.3V  | |
| GND   | GND   | |
| SCK   | GPIO22 | Software SPI (nie koliduje z SD) |
| MISO  | GPIO21 | |
| MOSI  | GPIO12 | Strapping pin - PN532 nie ciągnie HIGH przy boot |
| SS    | GPIO5  | |

**Okablowanie PCM5102A → ESP32 (I2S):**

Moduł ma 6 pinów: VIN, GND, BCK, DIN, SCK, LRCK.
SCK podciągnięty do GND na module (zmierzone 0.01V) - tryb slave z wewnętrznym zegarem.

| PCM5102A | ESP32/Zasilanie | Uwagi |
|----------|-----------------|-------|
| VIN      | 5V (VBUS)       | Zasilanie z zewnętrznego PSU |
| GND      | GND             | Star ground z resztą systemu |
| BCK      | GPIO26          | Bit Clock |
| LRCK     | GPIO25          | Left/Right Clock |
| DIN      | GPIO27          | Data In |
| SCK      | (na module→GND) | Nie podłączać - wewnętrznie na GND |

Głośność DAC ustawiona na stałe (max 21) - regulacja głośności fizycznie przez JBL Go.

**Znany problem:** Moduł wrażliwy na zakłócenia pojemnościowe (zbliżenie palca powoduje trzeszczenie podczas odtwarzania). Sygnały I2S pływają gdy audio nie gra. Rozwiązanie: obudowa izolująca moduł od dotyku.

**SD Card (wbudowany slot Lolin D32 Pro):**
CS=GPIO4. W kodzie: `SPI.begin(18, 19, 23, 4)` + `SD.begin(4)`.
GPIO18/19/23 są wewnętrznie połączone ze slotem SD (routing na PCB). Choć wyprowadzone na pin headers, **NIE WOLNO ich używać** do innych celów gdy SD jest aktywna (konflikty na magistrali SPI). Jedyny wyjątek: dodatkowy slave SPI z osobnym CS.

**Okablowanie przycisków ESP32:**
| Przycisk | ESP32 | Drugi pin |
|----------|-------|-----------|
| VOL+     | GPIO32 | GND |
| VOL-     | GPIO33 | GND |

(Wewnętrzne pull-up, GPIO32/33 obsługują RTC wake-up z deep sleep)

**Sterowanie JBL Go (przez tranzystory NPN BC547):**

Przyciski JBL są normalnie otwarte (NO) - wciśnięcie zwiera kontakty.
Każdy przycisk JBL ma dwie nóżki: lewa (~4V), prawa (0V/GND).
Tranzystor NPN zwiera nóżki przycisku gdy GPIO jest HIGH.

| Funkcja | GPIO ESP32 | Tranzystor | Rezystor |
|---------|-----------|------------|----------|
| JBL POWER | GPIO13 | Collector→lewa nóżka POWER, Emitter→prawa nóżka | 2.2kΩ Base→GPIO13 |
| JBL VOL+  | GPIO14 | Collector→lewa nóżka VOL+, Emitter→prawa nóżka  | 2.2kΩ Base→GPIO14 |
| JBL VOL-  | GPIO15 | Collector→lewa nóżka VOL-, Emitter→prawa nóżka  | 2.2kΩ Base→GPIO15 |

**WAŻNE:** GPIO15 wymaga pull-down 10kΩ do GND (strapping pin - bez tego może przypadkowo wyzwolić tranzystor przy bootowaniu).

**GND ESP32 musi być połączony z GND JBL** (minus baterii na płytce JBL).

**Odczyt statusu JBL (czy włączony):**
| Z | Do | Uwagi |
|---|-----|-------|
| Linia statusowa JBL (~4V gdy włączony) | rezystor 10kΩ → GPIO34 | Dzielnik napięcia |
| GPIO34 | rezystor 22kΩ → GND | Daje ~2.75V na ADC (bezpieczne dla ESP32) |

Próg ADC: >2000 = JBL włączony. Używane do:
- `jblPowerOn()` - nie wciska power jeśli JBL już działa
- `jblPowerOff()` - nie wciska power jeśli JBL już wyłączony

**Zasilanie:**
Zewnętrzny zasilacz 5V ze star ground. Zasilanie wspólne dla ESP32, PCM5102A i JBL Go. Nie z USB ESP32.

**PCM5102A → JBL Go:**
Kabel mini jack 3.5mm z wyjścia audio PCM5102A do wejścia AUX JBL Go.

**Biblioteki Arduino:**
- ESP32-audioI2S (odtwarzanie MP3 z SD)
- Adafruit_PN532 (NFC przez Software SPI)
- WiFiManager (konfiguracja WiFi dla sync)
- ArduinoJson (parsowanie mappings)
- Preferences (flaga sync)

**Logika przycisków:**
- Krótkie VOL+ → puls na GPIO14 (JBL VOL+, 80ms)
- Krótkie VOL- → puls na GPIO15 (JBL VOL-, 80ms)
- Akcja na naciśnięcie (nie na puszczenie) - szybka reakcja
- Przytrzymanie VOL+ (2s) → wybudź z deep sleep
- Przytrzymanie VOL- (2s) → wyłącz JBL + deep sleep
- Oba przyciski (2s) → synchronizacja z serwerem (restart w trybie sync)

**Logika NFC:**
- Figurka postawiona → odtwarzaj muzykę z SD
- Figurka zdjęta → zatrzymaj muzykę

**Logika JBL Power:**
- Boot / wake z deep sleep → `jblPowerOn()` (sprawdza status ADC, włącza tylko jeśli wyłączony, czeka max 3s na uruchomienie)
- Przed deep sleep → `jblPowerOff()` (sprawdza status ADC, wyłącza tylko jeśli włączony)

**Kompletna mapa GPIO ESP32 Lolin D32 Pro:**
| GPIO | Funkcja | Typ | Uwagi |
|------|---------|-----|-------|
| 4 | SD CS | Output (HW SPI) | Wbudowany slot, nie zmieniać |
| 5 | PN532 SS | Output (SW SPI) | Strapping pin - pull-up OK |
| 12 | PN532 MOSI | Output (SW SPI) | Strapping pin - musi być LOW przy boot |
| 13 | JBL POWER | Output | Tranzystor NPN, 2.2kΩ na bazie |
| 14 | JBL VOL+ | Output | Tranzystor NPN, 2.2kΩ na bazie |
| 15 | JBL VOL- | Output | Strapping pin - wymaga pull-down 10kΩ |
| 18 | SD SCK | HW SPI (wewnętrzny) | ZAJĘTY - nie używać! |
| 19 | SD MISO | HW SPI (wewnętrzny) | ZAJĘTY - nie używać! |
| 21 | PN532 MISO | Input (SW SPI) | Domyślny I2C SDA - I2C niedostępny |
| 22 | PN532 SCK | Output (SW SPI) | Domyślny I2C SCL - I2C niedostępny |
| 23 | SD MOSI | HW SPI (wewnętrzny) | ZAJĘTY - nie używać! |
| 25 | I2S LRCK | Output (I2S) | PCM5102A Left/Right Clock |
| 26 | I2S BCK | Output (I2S) | PCM5102A Bit Clock |
| 27 | I2S DOUT | Output (I2S) | PCM5102A Data In |
| 32 | BTN_A (VOL+) | Input (pull-up) | RTC wake-up z deep sleep |
| 33 | BTN_B (VOL-) | Input (pull-up) | RTC wake-up z deep sleep |
| 34 | JBL STATUS | Input (ADC) | Tylko input, dzielnik 10kΩ/22kΩ |

Wolne GPIO (dostępne do rozbudowy): 0*, 2*, 16, 17, 35*, 36*, 39*
(*) z ograniczeniami: 0/2 = strapping pins, 35/36/39 = tylko input (brak pull-up)

**Synchronizacja z serwerem:**
- Oba przyciski 2s → zapisuje flagę sync_pending → restart ESP32
- Po restarcie: WiFi ON → pobiera manifest z /api/sync → pobiera brakujące pliki MP3 → usuwa nieaktualne → zapisuje mappings.json na SD → restart
- WiFi + Audio nie mieszczą się w RAM jednocześnie, stąd restart

## API Endpoints

### Dla ESP32
| Endpoint | Opis |
|----------|------|
| `GET /api/play/{nfc_uid}` | Zwraca `{stream_url, track_title, figurine_name}` |
| `GET /api/stream/{track_id}` | Stream MP3 |
| `GET /api/system_sounds/{sound_name}` | Stream dźwięków systemowych (ready, start) z folderu `music/system/` |
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
│       ├── api.py           # Endpointy dla ESP32 (w tym /api/sync)
│       └── admin.py         # Endpointy dla panelu
├── music/                   # Pliki MP3 (Docker volume)
│   └── system/              # Dźwięki systemowe
├── data/                    # Baza SQLite (Docker volume)
├── web/
│   └── index.html           # Panel administracyjny
└── esp32/
    └── src/
        └── main.cpp         # Firmware ESP32 (PlatformIO)
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

**Custom PCB (zrealizowane):**
- Wejście zasilania USB-C (J1)
- Złącza pin header: PN532 (J3, 6-pin), PCM5102A (J4, 5-pin), przyciski (J7, 4-pin), JBL (J10, 7-pin)
- Złącze JST-PH 2-pin (J2) na dodatkowe GND
- Gniazda ESP32 Lolin D32 Pro (J8 lewy, J9 prawy - 16 pinów każdy)
- Tranzystory BC547 (Q1-Q3) + rezystory 2.2kΩ (R3-R5) na płytce
- Rezystory pull-up 5.1kΩ (R1, R2) na CC1/CC2 USB-C
- Rezystor 10kΩ (R6) na linii JBL_STATUS

**TODO:** Złącze J4 ma 5 pinów - brakuje SCK. Nie jest potrzebny (na module podciągnięty do GND), ale dla kompletności można dodać 6-pin w następnej rewizji.

## Przyszłe rozszerzenia (planowane)
- Skanowanie NFC telefonem → automatyczne tworzenie figurki
- Playlisty zamiast pojedynczych utworów
