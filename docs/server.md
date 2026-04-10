# Server MusicBox

FastAPI + SQLite + Alpine.js, uruchamiany w Dockerze na Raspberry Pi.

## Stack

- Python 3.11
- FastAPI
- SQLAlchemy 2.x (DeclarativeBase)
- SQLite (`./data/musicbox.db`)
- Alpine.js (panel webowy, `web/index.html`)
- Docker + docker-compose

**Lokalizacja na RPi:** `~/musicbox/`
**Docker:** `docker compose up -d`
**Port:** 8000
**Panel webowy:** http://<musicbox-server>:8000

## Struktura

```
server/
├── main.py                  # FastAPI app, mount routerów, static files
├── database.py              # SQLAlchemy models + engine (SQLite)
├── models.py                # Pydantic schemas
├── requirements.txt
├── routers/
│   ├── api.py               # Endpointy dla ESP32 (/api/...)
│   └── admin.py             # Endpointy dla panelu (/admin/...)
└── web/                     # (statyki obsługiwane z /web/)
```

## Model danych (`server/database.py`)

### Track
| Kolumna | Typ | Uwagi |
|---------|-----|-------|
| id | int PK | |
| title | str(255) | |
| filename | str(255) unique | Nazwa pliku w `music/` |
| created_at | datetime | |

### Figurine
| Kolumna | Typ | Uwagi |
|---------|-----|-------|
| id | int PK | |
| name | str(255) | |
| nfc_uid | str(64) unique | Format `XX:XX:XX:XX:...` hex uppercase |
| track_id | int FK tracks.id nullable | `ON DELETE SET NULL` |
| created_at | datetime | |

### SystemSound
| Kolumna | Typ | Uwagi |
|---------|-----|-------|
| id | int PK | |
| name | str(64) unique | np. `ready`, `start` |
| filename | str(255) nullable | W `music/system/`, może być null (nieprzypisany) |
| created_at | datetime | |

## API dla ESP32 (`server/routers/api.py`)

| Metoda | Ścieżka | Opis |
|--------|---------|------|
| GET | `/api/play/{nfc_uid}` | (legacy, online) Zwraca `{stream_url, track_title, figurine_name}` dla danego UID. Nieużywane przez obecny firmware - firmware działa offline z SD. |
| GET | `/api/stream/{track_id}` | Stream MP3 po ID z bazy (legacy online playback). |
| GET | `/api/stream/file/{filename}` | Stream MP3 po nazwie pliku - **używane przez sync ESP32** do pobierania brakujących plików. |
| GET | `/api/sync` | **Manifest sync dla ESP32.** Zwraca listę figurek, utworów i dźwięków systemowych. |
| GET | `/api/health` | Health check, `{"status":"ok","service":"musicbox"}` |
| GET | `/api/system_sounds/{sound_name}` | Stream dźwięku systemowego po `name` z bazy `SystemSound`. Zwraca 404 jeśli nieprzypisany. |

### Format `/api/sync` (używany przez ESP32 sync)

```json
{
  "figurines": [
    {
      "nfc_uid": "04:A3:B2:C1:DE:FF:80",
      "track_filename": "9383471d_babajaga.mp3",
      "track_title": "Baba Jaga"
    }
  ],
  "tracks": [
    {"filename": "9383471d_babajaga.mp3", "title": "Baba Jaga"}
  ],
  "system_sounds": [
    {"name": "ready", "filename": "ready.mp3"}
  ]
}
```

Tylko figurki z `track_id IS NOT NULL` są w `figurines`. Wszystkie tracki są w `tracks` (żeby ESP32 mógł pobrać nawet te nieprzypisane - do wygody).

**Uwaga:** `system_sounds` w manifeście są dostępne, ale obecny firmware ich nie pobiera ani nie odtwarza. To zostało z poprzedniej wersji online-only.

## API panelu admin (`server/routers/admin.py`)

### Tracks
| Metoda | Ścieżka | Opis |
|--------|---------|------|
| GET | `/admin/tracks` | Lista utworów |
| POST | `/admin/tracks` | Upload MP3 (multipart: `title` + `file`) |
| POST | `/admin/tracks/youtube` | Pobranie ścieżki z YouTube (async task) |
| GET | `/admin/tracks/youtube/{task_id}/status` | Status pobierania YT |
| POST | `/admin/tracks/{track_id}/trim` | Przycięcie utworu (start/end w sekundach) |
| DELETE | `/admin/tracks/{track_id}` | Usuń utwór (figurki z tym track_id → SET NULL) |

### Figurines
| Metoda | Ścieżka | Opis |
|--------|---------|------|
| GET | `/admin/figurines` | Lista figurek |
| POST | `/admin/figurines` | Dodaj (JSON: `name`, `nfc_uid`, opcjonalnie `track_id`) |
| PUT | `/admin/figurines/{id}` | Edytuj figurkę |
| DELETE | `/admin/figurines/{id}` | Usuń figurkę |

### System sounds
| Metoda | Ścieżka | Opis |
|--------|---------|------|
| GET | `/admin/system_sounds` | Lista dźwięków systemowych z bazy |
| POST | `/admin/system_sounds/{name}` | Upload pliku dla danego `name` (np. `ready`, `start`) |
| DELETE | `/admin/system_sounds/{name}` | Usuń plik (wpis w bazie zostaje z `filename=null`) |

## Panel webowy (`web/index.html`)

Jedna strona Alpine.js. Funkcje:
- Upload plików MP3
- Import z YouTube (async z paskiem postępu)
- Trim utworów (start/end w sekundach)
- Podgląd i usuwanie utworów
- Dodawanie figurek (z lub bez przypisanego utworu)
- Edycja przypisania utwór ↔ figurka
- Upload / usuwanie dźwięków systemowych
- Odtwarzanie podglądu w przeglądarce (HTML5 `<audio>`)

## Volumes (Docker)

- `./music:/app/music` - pliki MP3 (w tym `music/system/` dla dźwięków systemowych)
- `./data:/app/data` - SQLite database

## Deployment

Z lokalnej maszyny:

```bash
rsync -avz --exclude '__pycache__' --exclude '*.pyc' --exclude '.git' \
  <repo>/ rpi@<musicbox-server>:~/musicbox/

ssh rpi@<musicbox-server> "cd ~/musicbox && docker compose up -d --build"
```

Logi:
```bash
ssh rpi@<musicbox-server> "docker logs -f musicbox"
```

Restart bez rebuildu:
```bash
ssh rpi@<musicbox-server> "cd ~/musicbox && docker compose restart"
```

Skrypt `deploy.sh` w roocie automatyzuje powyższe.
