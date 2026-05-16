# zBox Server

The server is a FastAPI application with a SQLite database and a browser-based admin portal. It manages the track library, figurine mappings, system sound assignments, and ESP32 sync operations.

## Stack

- Python 3.11
- FastAPI
- SQLAlchemy
- SQLite in `./data/zbox.db`
- React-based static admin portal served from `web/`
- Docker / Docker Compose for deployment

## Directory layout

```text
server/
├── main.py
├── paths.py
├── database.py
├── device_sync.py
├── models.py
├── system_sounds.py
├── requirements.txt
├── routers/
│   ├── admin.py
│   └── api.py
```

```text
web/
├── index.html
├── app.jsx
├── data.jsx
├── styles.css
└── section-*.jsx
```

## Data model

- `Track`: library entry with title, stored filename, and creation timestamp.
- `Figurine`: NFC UID mapped to an optional track. Referred to as "NFC Tag" in the admin UI and user-facing documentation.
- `SystemSound`: named event slot such as `power_on` or `ready`, optionally assigned to an MP3 file.

## Public API used by the ESP32

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/api/play/{nfc_uid}` | Legacy online playback response. Kept for compatibility. |
| `GET` | `/api/stream/{track_id}` | Legacy track streaming by database ID. |
| `GET` | `/api/stream/file/{filename}` | File download endpoint used by ESP32 sync. |
| `GET` | `/api/sync` | Sync manifest containing figurines, tracks, and assigned system sounds. |
| `GET` | `/api/health` | Health endpoint. |
| `GET` | `/api/system_sounds/{sound_name}` | Preview endpoint for assigned system sounds. |

## Admin API

The `/admin/...` routes provide:

- track upload, trimming, delete, and background YouTube import
- figurine CRUD and track assignment
- system sound upload and reset
- device status lookup
- device sync planning and execution
- diagnostic log browsing and download

## Sync diagnostics

The backend now emits structured application logs for device connectivity and sync execution. The most useful log streams are:

- `zbox.main`: server startup and base runtime initialization
- `zbox.admin`: admin-triggered sync task lifecycle, per-stage progress, offline detection, log download failures, and restart requests
- `zbox.device_sync`: direct HTTP interactions with the ESP32, sync plan generation, per-file upload start/finish, delete operations, metadata writes, and retry-after-connection-reset events

When investigating a failed or interrupted sync, look for:

- the sync `task_id`
- configured device IP and resolved `device_id`
- current sync `stage`
- `current_file` and per-file upload progress
- connection errors raised while polling `/diag/status`, `/diag/files`, `/diag/logs`, or `/diag/upload`

## Sync task status payload

The admin sync status response includes both aggregate progress and per-file progress for uploads.

Important fields on `GET /admin/devices/{device_id}/sync/{task_id}/status`:

- `progress`: whole-task progress in percent
- `stage`: current sync stage such as `fetching_device_state`, `uploading_audio`, or `writing_system_sounds`
- `current_file`: file currently being uploaded
- `current_file_progress`: percent for the current file
- `sync_files`: ordered list of files scheduled for upload, each with:
  - `path`
  - `status`: `pending`, `uploading`, or `uploaded`
  - `progress`

The web admin uses `sync_files` to render the live upload queue so short connectivity glitches are easier to correlate with the exact file being transferred.

## Runtime model

```bash
docker compose up -d --build
```

The server is intended to run in Docker, not as a host-level Python process.

The compose setup mounts:

- `./music` to `/app/music`
- `./data` to `/app/data`
- `./web` to `/app/web`

## Deployment

The default deployment shape is a Linux host running Docker:

```bash
docker compose up -d --build
```

Avoid hardcoding personal hostnames or private IP addresses in documentation or scripts.
