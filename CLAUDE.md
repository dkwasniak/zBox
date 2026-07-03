# MusicBox - Muzyczne Pudełko dla Dzieci

## Zdalny dostęp do ESP32 (przez sieć)

**ESP32 hostname**: `musicbox.local` (mDNS) lub lokalny adres IP urządzenia.

### OTA - wgrywanie firmware przez WiFi
```bash
cd esp32
pio run -t upload --upload-port musicbox.local
# lub po lokalnym IP:
pio run -t upload --upload-port <esp32-ip>
```

### WebSerial - logi debug przez przeglądarkę
Otwórz w przeglądarce:
```
http://musicbox.local/webserial
```
lub `http://<IP_ESP32>/webserial`

Pokazuje logi na żywo (NFC, DLNA, błędy) bez podłączania USB.
Uwaga: pokazuje tylko nowe logi od momentu otwarcia strony — logi startowe już przeszły.

## Opis projektu
Muzyczne pudełko które odtwarza muzykę po postawieniu figurki z naklejką NFC. Projekt dla córki.

## Architektura

```
┌─────────────────┐         HTTP/REST          ┌─────────────────────┐
│     ESP32       │ ◄─────────────────────────► │   Raspberry Pi      │
│  + Czytnik NFC  │                             │ (musicbox-server)   │
└────────┬────────┘                             │   Docker: musicbox  │
         │                                      └─────────────────────┘
         │ DLNA/UPnP (SOAP)
         ▼
┌─────────────────┐
│  Yamaha YAS-209 │
│  (soundbar)     │
└─────────────────┘
```

ESP32 pobiera URL streamu z serwera MusicBox, a następnie wysyła go bezpośrednio
do Yamahy przez DLNA (UPnP AVTransport). Yamaha sama pobiera i odtwarza stream MP3.

## Komponenty

### Serwer (Raspberry Pi) - ZAIMPLEMENTOWANY
- **Lokalizacja na RPi**: `~/musicbox/`
- **Stack**: Python 3.11 + FastAPI + SQLite + Alpine.js
- **Docker**: `docker compose up -d`
- **Port**: 8000
- **Panel webowy**: `http://<musicbox-server>:8000`

### ESP32 - ZAIMPLEMENTOWANY

**Sprzęt:**
- ESP32 WROOM32
- Czytnik NFC PN532

**Schemat połączeń:**
```
┌─────────────┐
│   ESP32     │
│  WROOM32    │
└─────────────┘
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

**Biblioteki Arduino (PlatformIO):**
- Adafruit_PN532 (NFC przez SPI)
- ArduinoJson (parsowanie odpowiedzi z serwera)
- ArduinoOTA (wbudowana - firmware update przez WiFi)
- WebSerial (debug logi przez HTTP)
- ESPAsyncWebServer + AsyncTCP (zależności WebSerial)

**Partycje:** `min_spiffs.csv` — 2x partycja aplikacji ~1.9MB (wymagane dla OTA).

**Odtwarzanie audio:**
ESP32 nie odtwarza dźwięku lokalnie. Steruje soundbarem Yamaha YAS-209 przez DLNA/UPnP.
Yamaha sama pobiera stream MP3 z serwera MusicBox i odtwarza go.

**DLNA - Yamaha YAS-209:**
| Parametr | Wartość |
|----------|---------|
| IP | dynamicznie przez SSDP |
| Port | `49152` |
| AVTransport | `/upnp/control/rendertransport1` |
| RenderingControl | `/upnp/control/rendercontrol1` |
| UDN | `uuid:FFB8F002-F478-76B8-9813-5248FFB8F002` |

**SSDP Discovery:** ESP32 wyszukuje Yamahę automatycznie przez multicast
(`239.255.255.250:1900`, ST: `MediaRenderer:1`), identyfikując ją po UUID.
Jeśli discovery nie znajdzie przy starcie (np. WiFi multicast jeszcze niegotowe),
rediscovery triggeruje się przy pierwszej komendzie odtwarzania.

**Komunikacja SOAP:** ESP32 używa `HTTPClient` i czeka na odpowiedź HTTP 200
(wcześniej było fire-and-forget przez raw TCP — nie działało niezawodnie, bo
Yamaha nie zdążała przetworzyć żądania przed zamknięciem socketu).

Komendy DLNA (SOAP over HTTP):
- `SetAVTransportURI` → ustaw URL streamu
- `Play` → odtwarzaj
- `Stop` → zatrzymaj
- `SetVolume` / `GetVolume` → sterowanie głośnością

**Logika NFC:**
- Figurka postawiona → pobierz stream URL z serwera → wyślij do Yamahy przez DLNA → Play
- Figurka zdjęta (~1s) → Stop
- Inna figurka → Stop + nowy stream

**Dźwięki systemowe (planowane):**
- Dźwięk "ready" (cichy, ambientowy) - odtwarzany po uruchomieniu urządzenia
- Dźwięk "start" (cichy, ambientowy) - odtwarzany przy wykryciu tagu NFC
- Pliki w `music/system/ready.mp3` i `music/system/start.mp3` na serwerze

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
│       ├── api.py           # Endpointy dla ESP32
│       └── admin.py         # Endpointy dla panelu
├── music/                   # Pliki MP3 (Docker volume)
│   └── system/              # Dźwięki systemowe (ready.mp3, start.mp3)
├── data/                    # Baza SQLite (Docker volume)
└── web/
    └── index.html           # Panel administracyjny
```

## Deployment na RPi

```bash
# Kopiowanie zmian z lokalnej maszyny
rsync -avz --exclude '__pycache__' --exclude '*.pyc' --exclude '.git' \
  ./ rpi@<musicbox-server>:~/musicbox/

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
- Przyciski VOL+/VOL- (GPIO32/33) ze sterowaniem głośnością DLNA
- Deep sleep z wybudzaniem przyciskiem
- Dźwięki systemowe (ready/start) przez DLNA announcement
- Skanowanie NFC telefonem → automatyczne tworzenie figurki
- Playlisty zamiast pojedynczych utworów
- Głośność per figurka

## Testowe dane
- Używaj fikcyjnych UID NFC i nazw utworów w dokumentacji publicznej.
