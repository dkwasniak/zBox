"""API endpoints dla panelu administracyjnego."""

import os
import uuid
import subprocess
import tempfile
import threading
import re
import json
import unicodedata
from datetime import datetime, UTC
from pathlib import Path
from typing import List, Dict

from fastapi import APIRouter, Depends, HTTPException, UploadFile, File, Form, BackgroundTasks
from fastapi.responses import StreamingResponse
from requests import RequestException
from sqlalchemy.orm import Session, joinedload

from database import get_db, SessionLocal, Track, Figurine, SystemSound
from device_sync import (
    build_sync_plan,
    fetch_device_files,
    fetch_device_log_content,
    fetch_device_logs,
    fetch_device_status,
    fetch_led_config,
    get_configured_device,
    get_device_settings,
    push_led_config,
    restart_device,
    save_device_settings,
    sync_device,
    download_device_log,
)
from models import (
    DeviceFilesResponse,
    DeviceLogContentResponse,
    DeviceLogsResponse,
    DeviceSettings,
    DeviceSyncCheckResponse,
    DeviceStatus,
    DeviceSyncStartResponse,
    DeviceSyncTaskStatus,
    TrackResponse,
    FigurineCreate,
    FigurineUpdate,
    FigurineResponse,
    SystemSoundResponse,
)


router = APIRouter(prefix="/admin", tags=["Panel administracyjny"])

MUSIC_DIR = Path("./music")
SYSTEM_SOUNDS_DIR = Path("./music/system")

VALID_SYSTEM_SOUNDS = {
    "vol_up",
    "vol_down",
    "power_on",
    "power_off",
    "sync",
    "ready",
    "nfc_mode",
    "music_mode",
}

# Globalny dict do trzymania progressu zadań YouTube
youtube_tasks: Dict[str, dict] = {}
device_sync_tasks: Dict[str, dict] = {}


def _device_error_detail(exc: RequestException) -> str:
    if exc.response is None:
        return f"Błąd połączenia z urządzeniem: {exc}"

    text = (exc.response.text or "").strip()
    if not text:
        return f"Błąd urządzenia HTTP {exc.response.status_code}"

    try:
        payload = json.loads(text)
    except json.JSONDecodeError:
        return text

    if isinstance(payload, dict):
        if isinstance(payload.get("error"), str) and payload["error"].strip():
            return payload["error"].strip()
        if isinstance(payload.get("detail"), str) and payload["detail"].strip():
            return payload["detail"].strip()
    return text


def _slugify_filename_component(value: str, fallback: str = "track") -> str:
    normalized = unicodedata.normalize("NFKD", value)
    ascii_only = normalized.encode("ascii", "ignore").decode("ascii")
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "_", ascii_only).strip("._-")
    return cleaned or fallback


def _safe_track_filename(name: str, unique_id: str | None = None) -> str:
    raw_name = Path(name).name
    stem = Path(raw_name).stem
    safe_stem = _slugify_filename_component(stem)
    prefix = f"{unique_id}_" if unique_id else ""
    return f"{prefix}{safe_stem}.mp3"


def _normalize_track_filenames(db: Session) -> bool:
    changed = False
    tracks = db.query(Track).order_by(Track.id.asc()).all()

    for track in tracks:
        current_name = Path(track.filename).name
        if current_name.lower().endswith(".mp3"):
            stem_source = current_name[:-4]
        else:
            stem_source = current_name

        if "_" in stem_source:
            prefix, remainder = stem_source.split("_", 1)
            if re.fullmatch(r"[0-9a-fA-F]{8}", prefix):
                safe_name = _safe_track_filename(remainder, unique_id=prefix.lower())
            else:
                safe_name = _safe_track_filename(current_name)
        else:
            safe_name = _safe_track_filename(current_name)

        if safe_name == current_name:
            continue

        old_path = MUSIC_DIR / current_name
        new_name = safe_name
        new_path = MUSIC_DIR / new_name

        if new_path.exists() and new_path != old_path:
            new_name = _safe_track_filename(current_name, unique_id=uuid.uuid4().hex[:8])
            new_path = MUSIC_DIR / new_name

        if old_path.exists() and old_path != new_path:
            os.replace(old_path, new_path)
        elif not old_path.exists() and not new_path.exists():
            continue

        track.filename = new_name
        changed = True

    if changed:
        db.commit()

    return changed


def _summarize_command_output(output: str, limit: int = 300) -> str:
    lines = [line.strip() for line in (output or "").splitlines() if line.strip()]
    if not lines:
        return "Brak szczegolow bledu."
    for line in reversed(lines):
        if "ERROR:" in line or "Requested format is not available" in line:
            return line[:limit]
    return lines[-1][:limit]


def _create_sync_task_state(task_id: str, device_id: str) -> dict:
    return {
        "task_id": task_id,
        "device_id": device_id,
        "status": "running",
        "started_at": datetime.now(UTC).isoformat(),
        "finished_at": None,
        "progress": 0,
        "message": "Przygotowanie synchronizacji...",
        "stage": "queued",
        "current_file": None,
        "current_file_index": None,
        "current_file_total": None,
        "current_file_progress": None,
        "uploaded": [],
        "deleted": [],
        "uploaded_count": 0,
        "deleted_count": 0,
        "mappings_written": False,
        "mappings_count": 0,
        "mappings_path": "/data/mappings.json",
        "system_sounds_written": False,
        "system_sounds_count": 0,
        "system_sounds_path": "/data/system_sounds.json",
        "led_config_written": False,
        "led_config_path": None,
        "restart_required": False,
        "error": None,
    }


def _get_current_sync_task(device_id: str) -> dict | None:
    running_tasks = [
        task for task in device_sync_tasks.values()
        if task.get("device_id") == device_id and task.get("status") == "running"
    ]
    if not running_tasks:
        return None
    return max(running_tasks, key=lambda task: task.get("started_at") or "")


def _run_device_sync_task(task_id: str, device: dict, requested_device_id: str):
    db = SessionLocal()
    try:
        _normalize_track_filenames(db)
        result = sync_device(
            device,
            db,
            progress_callback=lambda update: device_sync_tasks[task_id].update(update),
        )
        device_sync_tasks[task_id].update(result)
        device_sync_tasks[task_id]["finished_at"] = datetime.now(UTC).isoformat()
    except RequestException as exc:
        current = device_sync_tasks.get(task_id, _create_sync_task_state(task_id, requested_device_id))
        current.update({
            "status": "error",
            "finished_at": datetime.now(UTC).isoformat(),
            "progress": current.get("progress", 0),
            "message": f"Blad polaczenia z urzadzeniem: {exc}",
            "error": str(exc),
        })
        device_sync_tasks[task_id] = current
    except Exception as exc:
        current = device_sync_tasks.get(task_id, _create_sync_task_state(task_id, requested_device_id))
        current.update({
            "status": "error",
            "finished_at": datetime.now(UTC).isoformat(),
            "progress": current.get("progress", 0),
            "message": f"Synchronizacja nie powiodla sie: {exc}",
            "error": str(exc),
        })
        device_sync_tasks[task_id] = current
    finally:
        db.close()


# === Tracks (Utwory) ===


@router.get("/tracks", response_model=List[TrackResponse])
def list_tracks(db: Session = Depends(get_db)):
    """Zwraca listę wszystkich utworów."""
    tracks = db.query(Track).order_by(Track.created_at.desc()).all()
    return tracks


@router.post("/tracks", response_model=TrackResponse)
async def upload_track(
    title: str = Form(...),
    file: UploadFile = File(...),
    db: Session = Depends(get_db),
):
    """Upload nowego utworu MP3 z automatyczną konwersją do 64kbps."""
    if not file.filename.lower().endswith(".mp3"):
        raise HTTPException(status_code=400, detail="Dozwolone tylko pliki MP3")

    # Generuj unikalną nazwę pliku
    unique_id = uuid.uuid4().hex[:8]
    safe_filename = _safe_track_filename(file.filename, unique_id=unique_id)
    final_path = MUSIC_DIR / safe_filename

    MUSIC_DIR.mkdir(parents=True, exist_ok=True)

    # Zapisz plik tymczasowy
    content = await file.read()
    with tempfile.NamedTemporaryFile(delete=False, suffix=".mp3") as temp_input:
        temp_input.write(content)
        temp_input_path = temp_input.name

    try:
        # Konwertuj do 64kbps używając ffmpeg
        result = subprocess.run(
            [
                "ffmpeg",
                "-i", temp_input_path,
                "-b:a", "64k",
                "-ar", "44100",
                "-ac", "2",
                "-y",
                str(final_path),
            ],
            capture_output=True,
            text=True,
            timeout=60,
        )

        if result.returncode != 0:
            raise HTTPException(
                status_code=500,
                detail=f"Błąd konwersji MP3: {result.stderr}"
            )

    finally:
        # Usuń plik tymczasowy
        os.unlink(temp_input_path)

    # Zapisz w bazie
    track = Track(title=title, filename=safe_filename)
    db.add(track)
    db.commit()
    db.refresh(track)

    return track


def download_youtube_task(task_id: str, title: str, youtube_url: str):
    """Background task dla pobierania z YouTube."""
    import logging
    logger = logging.getLogger(__name__)

    try:
        youtube_tasks[task_id] = {
            'status': 'downloading',
            'progress': 0,
            'message': 'Łączenie z YouTube...',
            'error': None,
            'track_id': None
        }

        # Generuj unikalną nazwę pliku
        unique_id = uuid.uuid4().hex[:8]
        safe_filename = _safe_track_filename(title, unique_id=unique_id)
        final_path = MUSIC_DIR / safe_filename

        MUSIC_DIR.mkdir(parents=True, exist_ok=True)

        # Pobierz z YouTube do pliku tymczasowego
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_audio = os.path.join(temp_dir, "audio")

            youtube_tasks[task_id]['progress'] = 10
            youtube_tasks[task_id]['message'] = 'Pobieranie z YouTube...'

            # Pobierz audio używając yt-dlp
            process = subprocess.Popen(
                [
                    "yt-dlp",
                    "-x",
                    "--audio-format", "mp3",
                    "--audio-quality", "0",
                    "-o", temp_audio,
                    "--no-playlist",
                    "--max-filesize", "50M",
                    "--newline",  # Każdy progress w nowej linii
                    youtube_url,
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True
            )

            yt_dlp_output: list[str] = []
            # Parsuj output w czasie rzeczywistym
            for line in process.stdout:
                yt_dlp_output.append(line.rstrip())
                # Szukaj linii z procentami, np: "[download]  45.2% of 5.23MiB"
                match = re.search(r'\[download\]\s+(\d+\.?\d*)%', line)
                if match:
                    percent = float(match.group(1))
                    # Mapuj 0-100% downloadu na 10-60% całości
                    youtube_tasks[task_id]['progress'] = int(10 + (percent * 0.5))
                    youtube_tasks[task_id]['message'] = f'Pobieranie: {percent:.1f}%'

            process.wait()

            if process.returncode != 0:
                details = _summarize_command_output("\n".join(yt_dlp_output))
                raise Exception(f"Blad yt-dlp: {details}")

            youtube_tasks[task_id]['progress'] = 60
            youtube_tasks[task_id]['message'] = 'Szukanie pobranego pliku...'

            # Znajdź pobrany plik
            downloaded_file = temp_audio + ".mp3"
            if not os.path.exists(downloaded_file):
                downloaded_file = temp_audio
                if not os.path.exists(downloaded_file):
                    files = os.listdir(temp_dir)
                    if files:
                        downloaded_file = os.path.join(temp_dir, files[0])
                    else:
                        raise Exception("Nie znaleziono pobranego pliku")

            youtube_tasks[task_id]['progress'] = 70
            youtube_tasks[task_id]['message'] = 'Konwersja do MP3 64kbps...'

            # Konwertuj do 64kbps
            result = subprocess.run(
                [
                    "ffmpeg",
                    "-i", downloaded_file,
                    "-b:a", "64k",
                    "-ar", "44100",
                    "-ac", "2",
                    "-y",
                    str(final_path),
                ],
                capture_output=True,
                text=True,
                timeout=60,
            )

            if result.returncode != 0:
                details = _summarize_command_output(result.stderr or result.stdout)
                raise Exception(f"Blad ffmpeg: {details}")

            youtube_tasks[task_id]['progress'] = 90
            youtube_tasks[task_id]['message'] = 'Zapisywanie do bazy...'

            # Zapisz w bazie - używamy nowej sesji dla background task
            from database import SessionLocal
            db = SessionLocal()
            try:
                track = Track(title=title, filename=safe_filename)
                db.add(track)
                db.commit()
                db.refresh(track)

                youtube_tasks[task_id]['progress'] = 100
                youtube_tasks[task_id]['status'] = 'completed'
                youtube_tasks[task_id]['message'] = 'Gotowe!'
                youtube_tasks[task_id]['track_id'] = track.id
            finally:
                db.close()

    except Exception as e:
        logger.exception("YouTube download error for %s", youtube_url)
        youtube_tasks[task_id]['status'] = 'error'
        youtube_tasks[task_id]['error'] = str(e)
        youtube_tasks[task_id]['message'] = f'Błąd: {str(e)}'


@router.post("/tracks/youtube")
async def start_youtube_download(
    title: str = Form(...),
    youtube_url: str = Form(...),
    background_tasks: BackgroundTasks = None,
):
    """Rozpoczyna pobieranie z YouTube w tle."""
    task_id = uuid.uuid4().hex

    # Uruchom w tle
    thread = threading.Thread(
        target=download_youtube_task,
        args=(task_id, title, youtube_url)
    )
    thread.daemon = True
    thread.start()

    return {"task_id": task_id}


@router.get("/tracks/youtube/{task_id}/status")
async def get_youtube_task_status(task_id: str):
    """Zwraca status zadania YouTube."""
    if task_id not in youtube_tasks:
        raise HTTPException(status_code=404, detail="Zadanie nie znalezione")

    return youtube_tasks[task_id]


@router.post("/tracks/{track_id}/trim", response_model=TrackResponse)
async def trim_track(
    track_id: int,
    start_time: float = Form(...),
    end_time: float = Form(...),
    db: Session = Depends(get_db),
):
    """Przycina utwór do określonych czasów."""
    track = db.query(Track).filter(Track.id == track_id).first()
    if not track:
        raise HTTPException(status_code=404, detail="Nie znaleziono utworu")

    original_path = MUSIC_DIR / track.filename
    if not original_path.exists():
        raise HTTPException(status_code=404, detail="Plik nie istnieje")

    # Walidacja czasów
    if start_time < 0 or end_time <= start_time:
        raise HTTPException(
            status_code=400,
            detail="Nieprawidłowe czasy (start >= 0 i end > start)"
        )

    # Nowa nazwa z sufiksem _trimmed + timestamp
    unique_id = uuid.uuid4().hex[:8]
    # Wyciągnij tytuł bez poprzednich ID
    parts = track.filename.split("_", 1)
    if len(parts) > 1:
        clean_name = parts[1].rsplit(".", 1)[0]  # Usuń też .mp3
    else:
        clean_name = track.filename.rsplit(".", 1)[0]

    # Usuń poprzedni sufiks _trimmed jeśli istnieje
    clean_name = clean_name.replace("_trimmed", "")

    new_filename = _safe_track_filename(f"{clean_name}_trimmed.mp3", unique_id=unique_id)
    trimmed_path = MUSIC_DIR / new_filename

    try:
        # Przytnij używając ffmpeg
        duration = end_time - start_time
        result = subprocess.run(
            [
                "ffmpeg",
                "-i", str(original_path),
                "-ss", str(start_time),
                "-t", str(duration),
                "-b:a", "64k",
                "-ar", "44100",
                "-ac", "2",
                "-y",
                str(trimmed_path),
            ],
            capture_output=True,
            text=True,
            timeout=60,
        )

        if result.returncode != 0:
            raise HTTPException(
                status_code=500,
                detail=f"Błąd przycinania: {result.stderr}"
            )

        # Usuń stary plik
        os.remove(original_path)

        # Zaktualizuj w bazie
        track.filename = new_filename
        db.commit()
        db.refresh(track)

        return track

    except subprocess.TimeoutExpired:
        raise HTTPException(status_code=500, detail="Timeout podczas przycinania")


@router.delete("/tracks/{track_id}")
def delete_track(track_id: int, db: Session = Depends(get_db)):
    """Usuwa utwór i plik MP3."""
    track = db.query(Track).filter(Track.id == track_id).first()

    if not track:
        raise HTTPException(status_code=404, detail="Nie znaleziono utworu")

    # Usuń plik
    file_path = MUSIC_DIR / track.filename
    if file_path.exists():
        os.remove(file_path)

    # Usuń z bazy
    db.delete(track)
    db.commit()

    return {"message": "Utwór usunięty", "id": track_id}


# === Figurines (Figurki) ===


@router.get("/figurines", response_model=List[FigurineResponse])
def list_figurines(db: Session = Depends(get_db)):
    """Zwraca listę wszystkich figurek z przypisanymi utworami."""
    figurines = (
        db.query(Figurine)
        .options(joinedload(Figurine.track))
        .order_by(Figurine.created_at.desc())
        .all()
    )
    return figurines


@router.post("/figurines", response_model=FigurineResponse)
def create_figurine(data: FigurineCreate, db: Session = Depends(get_db)):
    """Tworzy nową figurkę (opcjonalnie z przypisanym utworem)."""
    # Sprawdź czy NFC UID już istnieje
    existing = db.query(Figurine).filter(Figurine.nfc_uid == data.nfc_uid).first()
    if existing:
        raise HTTPException(
            status_code=400, detail="Figurka z tym kodem NFC już istnieje"
        )

    # Sprawdź czy utwór istnieje (jeśli podano)
    if data.track_id:
        track = db.query(Track).filter(Track.id == data.track_id).first()
        if not track:
            raise HTTPException(status_code=404, detail="Nie znaleziono utworu")

    figurine = Figurine(
        name=data.name,
        nfc_uid=data.nfc_uid,
        track_id=data.track_id,
    )
    db.add(figurine)
    db.commit()
    db.refresh(figurine)

    # Załaduj relację track
    db.refresh(figurine, ["track"])

    return figurine


@router.put("/figurines/{figurine_id}", response_model=FigurineResponse)
def update_figurine(
    figurine_id: int, data: FigurineUpdate, db: Session = Depends(get_db)
):
    """Aktualizuje figurkę (nazwę lub przypisany utwór)."""
    figurine = db.query(Figurine).filter(Figurine.id == figurine_id).first()

    if not figurine:
        raise HTTPException(status_code=404, detail="Nie znaleziono figurki")

    # Sprawdź czy utwór istnieje (jeśli podano)
    if data.track_id is not None and data.track_id != 0:
        track = db.query(Track).filter(Track.id == data.track_id).first()
        if not track:
            raise HTTPException(status_code=404, detail="Nie znaleziono utworu")

    if data.name is not None:
        figurine.name = data.name

    if data.track_id is not None:
        # track_id = 0 oznacza usunięcie przypisania
        figurine.track_id = data.track_id if data.track_id != 0 else None

    db.commit()
    db.refresh(figurine, ["track"])

    return figurine


@router.delete("/figurines/{figurine_id}")
def delete_figurine(figurine_id: int, db: Session = Depends(get_db)):
    """Usuwa figurkę."""
    figurine = db.query(Figurine).filter(Figurine.id == figurine_id).first()

    if not figurine:
        raise HTTPException(status_code=404, detail="Nie znaleziono figurki")

    db.delete(figurine)
    db.commit()

    return {"message": "Figurka usunięta", "id": figurine_id}


# === System Sounds (Dźwięki systemowe) ===


@router.get("/system_sounds", response_model=List[SystemSoundResponse])
def list_system_sounds(db: Session = Depends(get_db)):
    """Zwraca listę wszystkich slotów dźwięków systemowych."""
    sounds = db.query(SystemSound).order_by(SystemSound.name).all()
    return sounds


@router.post("/system_sounds/{name}", response_model=SystemSoundResponse)
async def upload_system_sound(
    name: str,
    file: UploadFile = File(...),
    db: Session = Depends(get_db),
):
    """Upload pliku MP3 dla dźwięku systemowego."""
    if name not in VALID_SYSTEM_SOUNDS:
        raise HTTPException(
            status_code=400,
            detail=f"Nieprawidłowa nazwa. Dozwolone: {', '.join(sorted(VALID_SYSTEM_SOUNDS))}"
        )

    if not file.filename.lower().endswith(".mp3"):
        raise HTTPException(status_code=400, detail="Dozwolone tylko pliki MP3")

    sound = db.query(SystemSound).filter(SystemSound.name == name).first()
    if not sound:
        raise HTTPException(status_code=404, detail="Slot dźwięku nie istnieje")

    SYSTEM_SOUNDS_DIR.mkdir(parents=True, exist_ok=True)

    filename = f"{name}.mp3"
    final_path = SYSTEM_SOUNDS_DIR / filename

    # Zapisz plik tymczasowy i konwertuj do 64kbps
    content = await file.read()
    with tempfile.NamedTemporaryFile(delete=False, suffix=".mp3") as temp_input:
        temp_input.write(content)
        temp_input_path = temp_input.name

    try:
        result = subprocess.run(
            [
                "ffmpeg",
                "-i", temp_input_path,
                "-b:a", "64k",
                "-ar", "44100",
                "-ac", "2",
                "-y",
                str(final_path),
            ],
            capture_output=True,
            text=True,
            timeout=60,
        )

        if result.returncode != 0:
            raise HTTPException(
                status_code=500,
                detail=f"Błąd konwersji MP3: {result.stderr}"
            )
    finally:
        os.unlink(temp_input_path)

    sound.filename = filename
    db.commit()
    db.refresh(sound)

    return sound


@router.delete("/system_sounds/{name}")
def delete_system_sound(name: str, db: Session = Depends(get_db)):
    """Usuwa plik dźwięku systemowego (slot pozostaje)."""
    if name not in VALID_SYSTEM_SOUNDS:
        raise HTTPException(status_code=400, detail="Nieprawidłowa nazwa")

    sound = db.query(SystemSound).filter(SystemSound.name == name).first()
    if not sound:
        raise HTTPException(status_code=404, detail="Slot dźwięku nie istnieje")

    if sound.filename:
        file_path = SYSTEM_SOUNDS_DIR / sound.filename
        if file_path.exists():
            os.remove(file_path)

    sound.filename = None
    db.commit()

    return {"message": "Dźwięk usunięty", "name": name}


def _get_device_or_404() -> dict:
    try:
        return get_configured_device()
    except RuntimeError as exc:
        raise HTTPException(status_code=500, detail=str(exc)) from exc


@router.get("/device-settings", response_model=DeviceSettings)
def read_device_settings():
    return get_device_settings()


@router.post("/device-settings", response_model=DeviceSettings)
def update_device_settings(payload: DeviceSettings):
    return save_device_settings(payload.ip)


@router.get("/devices", response_model=List[DeviceStatus])
def list_devices():
    """Zwraca skonfigurowany zBox po stałym IP."""
    try:
        return [fetch_device_status(_get_device_or_404())]
    except RequestException:
        return []


@router.get("/devices/{device_id}/files", response_model=DeviceFilesResponse)
def get_device_files(device_id: str):
    """Zwraca listę plików dostępnych na SD urządzenia."""
    try:
        return fetch_device_files(_get_device_or_404())
    except RequestException as exc:
        raise HTTPException(status_code=502, detail=f"Błąd połączenia z urządzeniem: {exc}") from exc


@router.get("/devices/{device_id}/logs", response_model=DeviceLogsResponse)
def get_device_logs(device_id: str):
    try:
        return fetch_device_logs(_get_device_or_404())
    except RequestException as exc:
        detail = _device_error_detail(exc)
        status = exc.response.status_code if exc.response is not None else 502
        raise HTTPException(status_code=status, detail=detail) from exc


@router.get("/devices/{device_id}/logs/content", response_model=DeviceLogContentResponse)
def get_device_log_content(device_id: str, name: str, tail: int = 200):
    try:
        return fetch_device_log_content(_get_device_or_404(), name=name, tail=tail)
    except RequestException as exc:
        detail = _device_error_detail(exc)
        status = exc.response.status_code if exc.response is not None else 502
        raise HTTPException(status_code=status, detail=detail) from exc


@router.get("/devices/{device_id}/logs/download")
def get_device_log_download(device_id: str, name: str):
    try:
        response = download_device_log(_get_device_or_404(), name=name)
    except RequestException as exc:
        detail = _device_error_detail(exc)
        status = exc.response.status_code if exc.response is not None else 502
        raise HTTPException(status_code=status, detail=detail) from exc

    media_type = response.headers.get("Content-Type", "text/plain; charset=utf-8")
    disposition = response.headers.get("Content-Disposition", f'attachment; filename="{name}"')

    def iter_chunks():
        try:
            yield from response.iter_content(chunk_size=8192)
        finally:
            response.close()

    return StreamingResponse(
        iter_chunks(),
        media_type=media_type,
        headers={"Content-Disposition": disposition},
    )


@router.get("/devices/{device_id}/sync/check", response_model=DeviceSyncCheckResponse)
def check_device_sync(device_id: str, db: Session = Depends(get_db)):
    """Sprawdza, czy na urządzeniu są zmiany do synchronizacji."""
    try:
        _normalize_track_filenames(db)
        plan = build_sync_plan(_get_device_or_404(), db)
        return {
            "device_id": plan["device_id"],
            "needs_sync": plan["needs_sync"],
            "message": plan["message"],
            "files_to_upload": plan["files_to_upload"],
            "files_to_delete": plan["files_to_delete"],
            "upload_count": plan["upload_count"],
            "delete_count": plan["delete_count"],
            "music_needs_update": plan["music_needs_update"],
            "music_files_to_upload": plan["music_files_to_upload"],
            "music_files_to_delete": plan["music_files_to_delete"],
            "music_upload_count": plan["music_upload_count"],
            "music_delete_count": plan["music_delete_count"],
            "mappings_count": plan["mappings_count"],
            "mappings_present": plan["mappings_present"],
            "mappings_needs_update": plan["mappings_needs_update"],
            "mappings_path": plan["mappings_path"],
            "system_sounds_count": plan["system_sounds_count"],
            "system_sounds_present": plan["system_sounds_present"],
            "system_sounds_needs_update": plan["system_sounds_needs_update"],
            "system_sounds_path": plan["system_sounds_path"],
            "system_sound_files_to_upload": plan["system_sound_files_to_upload"],
            "system_sound_files_to_delete": plan["system_sound_files_to_delete"],
            "system_sound_upload_count": plan["system_sound_upload_count"],
            "system_sound_delete_count": plan["system_sound_delete_count"],
            "system_sounds_manifest_needs_update": plan["system_sounds_manifest_needs_update"],
        }
    except RequestException as exc:
        raise HTTPException(status_code=502, detail=f"Błąd połączenia z urządzeniem: {exc}") from exc


@router.get("/devices/{device_id}/config")
def get_device_config(device_id: str):
    """Zwraca aktualną konfigurację animacji LED z urządzenia."""
    try:
        return fetch_led_config(_get_device_or_404())
    except RequestException as exc:
        raise HTTPException(status_code=502, detail=f"Błąd połączenia z urządzeniem: {exc}") from exc


@router.post("/devices/{device_id}/config")
def save_device_config(device_id: str, payload: dict):
    """Zapisuje konfigurację animacji LED na urządzeniu."""
    try:
        device = _get_device_or_404()
        push_led_config(device, payload)
        return {"saved": True, "device_id": device_id}
    except RequestException as exc:
        raise HTTPException(status_code=502, detail=f"Błąd połączenia z urządzeniem: {exc}") from exc


@router.post("/devices/{device_id}/sync", response_model=DeviceSyncStartResponse)
def sync_device_now(device_id: str):
    """Uruchamia synchronizację w tle i zwraca task_id do pollingu."""
    try:
        current_task = _get_current_sync_task(device_id)
        if current_task:
            return {"task_id": current_task["task_id"]}
        device = _get_device_or_404()
        task_id = uuid.uuid4().hex
        device_sync_tasks[task_id] = _create_sync_task_state(task_id, device_id)
        thread = threading.Thread(
            target=_run_device_sync_task,
            args=(task_id, device, device_id),
            daemon=True,
        )
        thread.start()
        return {"task_id": task_id}
    except RequestException as exc:
        raise HTTPException(status_code=502, detail=f"Błąd połączenia z urządzeniem: {exc}") from exc


@router.get("/devices/{device_id}/sync/current", response_model=DeviceSyncTaskStatus)
def get_current_device_sync(device_id: str):
    task = _get_current_sync_task(device_id)
    if not task:
        raise HTTPException(status_code=404, detail="Brak aktywnej synchronizacji")
    return task


@router.get("/devices/{device_id}/sync/{task_id}/status", response_model=DeviceSyncTaskStatus)
def get_device_sync_status(device_id: str, task_id: str):
    task = device_sync_tasks.get(task_id)
    if not task or task.get("device_id") != device_id:
        raise HTTPException(status_code=404, detail="Nie znaleziono zadania synchronizacji")
    return task


@router.post("/devices/{device_id}/restart")
def restart_device_now(device_id: str):
    """Restartuje urządzenie po operacjach serwisowych."""
    try:
        restart_device(_get_device_or_404())
        return {"restart": True, "device_id": device_id}
    except RequestException as exc:
        raise HTTPException(status_code=502, detail=f"Błąd połączenia z urządzeniem: {exc}") from exc
