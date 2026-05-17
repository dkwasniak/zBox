"""Admin API endpoints for the zBox portal."""

import logging
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
    fetch_bt_devices,
    fetch_device_files,
    fetch_device_log_content,
    fetch_device_logs,
    fetch_device_status,
    fetch_led_config,
    fetch_recent_log,
    get_configured_device,
    get_device_settings,
    push_led_config,
    restart_device,
    save_device_settings,
    select_bt_device,
    start_bt_scan,
    stop_bt_scan,
    sync_device,
    download_device_log,
)
from models import (
    BtDevicesResponse,
    BtSelectRequest,
    BtSelectResponse,
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
from paths import MUSIC_DIR, SYSTEM_SOUNDS_DIR
from system_sounds import ACTIVE_SYSTEM_SOUND_NAMES, ACTIVE_SYSTEM_SOUND_SET


router = APIRouter(prefix="/admin", tags=["Admin"])
logger = logging.getLogger("zbox.admin")

VALID_SYSTEM_SOUNDS = ACTIVE_SYSTEM_SOUND_SET

# Global dictionaries tracking background task progress.
youtube_tasks: Dict[str, dict] = {}
device_sync_tasks: Dict[str, dict] = {}
last_known_device_status: dict | None = None


def _device_error_detail(exc: RequestException) -> str:
    if exc.response is None:
        msg = str(exc)
        if "No route to host" in msg or "Failed to establish" in msg or "Connection refused" in msg:
            return "Nie można połączyć się z urządzeniem. Sprawdź czy jest włączone i w trybie sync."
        if "timed out" in msg.lower():
            return "Przekroczono czas oczekiwania na odpowiedź urządzenia."
        return f"Device connection error: {exc}"

    text = (exc.response.text or "").strip()
    if not text:
        return f"Device HTTP error {exc.response.status_code}"

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
        return "No additional error details."
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
        "message": "Preparing sync...",
        "stage": "queued",
        "current_file": None,
        "current_file_index": None,
        "current_file_total": None,
        "current_file_progress": None,
        "sync_files": [],
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


def _remember_device_status(payload: dict) -> dict:
    global last_known_device_status
    last_known_device_status = dict(payload)
    return last_known_device_status


def _log_sync_progress(task_id: str, update: dict, requested_device_id: str, device: dict) -> None:
    stage = update.get("stage")
    current_file = update.get("current_file")
    error = update.get("error")
    if not stage and not current_file and not error:
        return

    logger.info(
        "Sync progress task_id=%s requested_device_id=%s device_ip=%s stage=%s progress=%s current_file=%s file_progress=%s message=%s error=%s",
        task_id,
        requested_device_id,
        device.get("ip", "unknown"),
        stage,
        update.get("progress"),
        current_file,
        update.get("current_file_progress"),
        update.get("message"),
        error,
    )


def _run_device_sync_task(task_id: str, device: dict, requested_device_id: str):
    db = SessionLocal()
    try:
        logger.info(
            "Background sync task started task_id=%s requested_device_id=%s device_ip=%s",
            task_id,
            requested_device_id,
            device.get("ip", "unknown"),
        )
        _normalize_track_filenames(db)
        result = sync_device(
            device,
            db,
            progress_callback=lambda update: (
                device_sync_tasks[task_id].update(update),
                _log_sync_progress(task_id, update, requested_device_id, device),
            ),
        )
        device_sync_tasks[task_id].update(result)
        device_sync_tasks[task_id]["finished_at"] = datetime.now(UTC).isoformat()
        logger.info(
            "Background sync task completed task_id=%s requested_device_id=%s resolved_device_id=%s uploaded_count=%s deleted_count=%s",
            task_id,
            requested_device_id,
            result.get("device_id"),
            result.get("uploaded_count"),
            result.get("deleted_count"),
        )
    except RequestException as exc:
        current = device_sync_tasks.get(task_id, _create_sync_task_state(task_id, requested_device_id))
        current.update({
            "status": "error",
            "finished_at": datetime.now(UTC).isoformat(),
            "progress": current.get("progress", 0),
            "message": f"Device connection error: {exc}",
            "error": str(exc),
        })
        device_sync_tasks[task_id] = current
        logger.warning(
            "Background sync task failed with device connection error task_id=%s requested_device_id=%s device_ip=%s stage=%s progress=%s error=%s",
            task_id,
            requested_device_id,
            device.get("ip", "unknown"),
            current.get("stage"),
            current.get("progress"),
            exc,
        )
    except Exception as exc:
        current = device_sync_tasks.get(task_id, _create_sync_task_state(task_id, requested_device_id))
        current.update({
            "status": "error",
            "finished_at": datetime.now(UTC).isoformat(),
            "progress": current.get("progress", 0),
            "message": f"Sync failed: {exc}",
            "error": str(exc),
        })
        device_sync_tasks[task_id] = current
        logger.exception(
            "Background sync task crashed task_id=%s requested_device_id=%s device_ip=%s stage=%s progress=%s",
            task_id,
            requested_device_id,
            device.get("ip", "unknown"),
            current.get("stage"),
            current.get("progress"),
        )
    finally:
        db.close()


# === Tracks ===


@router.get("/tracks", response_model=List[TrackResponse])
def list_tracks(db: Session = Depends(get_db)):
    """Return all tracks."""
    tracks = db.query(Track).order_by(Track.created_at.desc()).all()
    return tracks


@router.post("/tracks", response_model=TrackResponse)
async def upload_track(
    title: str = Form(...),
    file: UploadFile = File(...),
    db: Session = Depends(get_db),
):
    """Upload a new MP3 track and normalize it to 64 kbps."""
    if not file.filename.lower().endswith(".mp3"):
        raise HTTPException(status_code=400, detail="Only MP3 files are allowed")

    # Generate a unique filename.
    unique_id = uuid.uuid4().hex[:8]
    safe_filename = _safe_track_filename(file.filename, unique_id=unique_id)
    final_path = MUSIC_DIR / safe_filename

    MUSIC_DIR.mkdir(parents=True, exist_ok=True)

    # Save the temporary upload.
    content = await file.read()
    with tempfile.NamedTemporaryFile(delete=False, suffix=".mp3") as temp_input:
        temp_input.write(content)
        temp_input_path = temp_input.name

    try:
        # Convert to 64 kbps with ffmpeg.
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
                detail=f"MP3 conversion failed: {result.stderr}"
            )

    finally:
        # Remove the temporary file.
        os.unlink(temp_input_path)

    # Store in the database.
    track = Track(title=title, filename=safe_filename)
    db.add(track)
    db.commit()
    db.refresh(track)

    return track


def download_youtube_task(task_id: str, title: str, youtube_url: str):
    """Background task for YouTube audio import."""
    try:
        youtube_tasks[task_id] = {
            'status': 'downloading',
            'progress': 0,
            'message': 'Connecting to YouTube...',
            'error': None,
            'track_id': None
        }

        # Generate a unique filename.
        unique_id = uuid.uuid4().hex[:8]
        safe_filename = _safe_track_filename(title, unique_id=unique_id)
        final_path = MUSIC_DIR / safe_filename

        MUSIC_DIR.mkdir(parents=True, exist_ok=True)

        # Download to a temporary file.
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_audio = os.path.join(temp_dir, "audio")

            youtube_tasks[task_id]['progress'] = 10
            youtube_tasks[task_id]['message'] = 'Downloading from YouTube...'

            # Download audio with yt-dlp.
            process = subprocess.Popen(
                [
                    "yt-dlp",
                    "-x",
                    "--audio-format", "mp3",
                    "--audio-quality", "0",
                    "-o", temp_audio,
                    "--no-playlist",
                    "--max-filesize", "50M",
                    "--newline",  # Emit each progress update on its own line.
                    youtube_url,
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True
            )

            yt_dlp_output: list[str] = []
            # Parse output in real time.
            for line in process.stdout:
                yt_dlp_output.append(line.rstrip())
                # Look for percentage progress lines.
                match = re.search(r'\[download\]\s+(\d+\.?\d*)%', line)
                if match:
                    percent = float(match.group(1))
                    # Map 0-100% download progress to the overall task range.
                    youtube_tasks[task_id]['progress'] = int(10 + (percent * 0.5))
                    youtube_tasks[task_id]['message'] = f'Downloading: {percent:.1f}%'

            process.wait()

            if process.returncode != 0:
                details = _summarize_command_output("\n".join(yt_dlp_output))
                raise Exception(f"yt-dlp failed: {details}")

            youtube_tasks[task_id]['progress'] = 60
            youtube_tasks[task_id]['message'] = 'Locating the downloaded file...'

            # Locate the downloaded file.
            downloaded_file = temp_audio + ".mp3"
            if not os.path.exists(downloaded_file):
                downloaded_file = temp_audio
                if not os.path.exists(downloaded_file):
                    files = os.listdir(temp_dir)
                    if files:
                        downloaded_file = os.path.join(temp_dir, files[0])
                    else:
                        raise Exception("Downloaded file not found")

            youtube_tasks[task_id]['progress'] = 70
            youtube_tasks[task_id]['message'] = 'Converting to 64 kbps MP3...'

            # Convert to 64 kbps.
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
                raise Exception(f"ffmpeg failed: {details}")

            youtube_tasks[task_id]['progress'] = 90
            youtube_tasks[task_id]['message'] = 'Saving to the database...'

            # Save using a new session dedicated to the background task.
            from database import SessionLocal
            db = SessionLocal()
            try:
                track = Track(title=title, filename=safe_filename)
                db.add(track)
                db.commit()
                db.refresh(track)

                youtube_tasks[task_id]['progress'] = 100
                youtube_tasks[task_id]['status'] = 'completed'
                youtube_tasks[task_id]['message'] = 'Done'
                youtube_tasks[task_id]['track_id'] = track.id
            finally:
                db.close()

    except Exception as e:
        logger.exception("YouTube download error for %s", youtube_url)
        youtube_tasks[task_id]['status'] = 'error'
        youtube_tasks[task_id]['error'] = str(e)
        youtube_tasks[task_id]['message'] = f'Error: {str(e)}'


@router.post("/tracks/youtube")
async def start_youtube_download(
    title: str = Form(...),
    youtube_url: str = Form(...),
    background_tasks: BackgroundTasks = None,
):
    """Start a background YouTube import."""
    task_id = uuid.uuid4().hex

    # Run in the background.
    thread = threading.Thread(
        target=download_youtube_task,
        args=(task_id, title, youtube_url)
    )
    thread.daemon = True
    thread.start()

    return {"task_id": task_id}


@router.get("/tracks/youtube/{task_id}/status")
async def get_youtube_task_status(task_id: str):
    """Return YouTube task status."""
    if task_id not in youtube_tasks:
        raise HTTPException(status_code=404, detail="Task not found")

    return youtube_tasks[task_id]


@router.post("/tracks/{track_id}/trim", response_model=TrackResponse)
async def trim_track(
    track_id: int,
    start_time: float = Form(...),
    end_time: float = Form(...),
    db: Session = Depends(get_db),
):
    """Trim a track to the requested time range."""
    track = db.query(Track).filter(Track.id == track_id).first()
    if not track:
        raise HTTPException(status_code=404, detail="Track not found")

    original_path = MUSIC_DIR / track.filename
    if not original_path.exists():
        raise HTTPException(status_code=404, detail="File does not exist")

    # Validate time range.
    if start_time < 0 or end_time <= start_time:
        raise HTTPException(
            status_code=400,
            detail="Invalid time range (start >= 0 and end > start)"
        )

    # New filename with a trimmed suffix and fresh ID.
    unique_id = uuid.uuid4().hex[:8]
    # Strip old IDs from the basename.
    parts = track.filename.split("_", 1)
    if len(parts) > 1:
        clean_name = parts[1].rsplit(".", 1)[0]  # Strip the .mp3 suffix too.
    else:
        clean_name = track.filename.rsplit(".", 1)[0]

    # Remove any previous _trimmed suffix.
    clean_name = clean_name.replace("_trimmed", "")

    new_filename = _safe_track_filename(f"{clean_name}_trimmed.mp3", unique_id=unique_id)
    trimmed_path = MUSIC_DIR / new_filename

    try:
        # Trim with ffmpeg.
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
                detail=f"Trim failed: {result.stderr}"
            )

        # Remove the previous file.
        os.remove(original_path)

        # Update the database record.
        track.filename = new_filename
        db.commit()
        db.refresh(track)

        return track

    except subprocess.TimeoutExpired:
        raise HTTPException(status_code=500, detail="Trim timed out")


@router.delete("/tracks/{track_id}")
def delete_track(track_id: int, db: Session = Depends(get_db)):
    """Delete a track and its MP3 file."""
    track = db.query(Track).filter(Track.id == track_id).first()

    if not track:
        raise HTTPException(status_code=404, detail="Track not found")

    # Remove the file.
    file_path = MUSIC_DIR / track.filename
    if file_path.exists():
        os.remove(file_path)

    # Remove the database record.
    db.delete(track)
    db.commit()

    return {"message": "Track deleted", "id": track_id}


# === Figurines ===


@router.get("/figurines", response_model=List[FigurineResponse])
def list_figurines(db: Session = Depends(get_db)):
    """Return all figurines with their assigned tracks."""
    figurines = (
        db.query(Figurine)
        .options(joinedload(Figurine.track))
        .order_by(Figurine.created_at.desc())
        .all()
    )
    return figurines


@router.post("/figurines", response_model=FigurineResponse)
def create_figurine(data: FigurineCreate, db: Session = Depends(get_db)):
    """Create a new figurine, optionally with an assigned track."""
    # Check whether the NFC UID already exists.
    existing = db.query(Figurine).filter(Figurine.nfc_uid == data.nfc_uid).first()
    if existing:
        raise HTTPException(
            status_code=400, detail="A figurine with this NFC UID already exists"
        )

    # Check whether the requested track exists.
    if data.track_id:
        track = db.query(Track).filter(Track.id == data.track_id).first()
        if not track:
            raise HTTPException(status_code=404, detail="Track not found")

    figurine = Figurine(
        name=data.name,
        nfc_uid=data.nfc_uid,
        track_id=data.track_id,
    )
    db.add(figurine)
    db.commit()
    db.refresh(figurine)

    # Load the track relationship.
    db.refresh(figurine, ["track"])

    return figurine


@router.put("/figurines/{figurine_id}", response_model=FigurineResponse)
def update_figurine(
    figurine_id: int, data: FigurineUpdate, db: Session = Depends(get_db)
):
    """Update a figurine name or track assignment."""
    figurine = db.query(Figurine).filter(Figurine.id == figurine_id).first()

    if not figurine:
        raise HTTPException(status_code=404, detail="Figurine not found")

    # Check whether the requested track exists.
    if data.track_id is not None and data.track_id != 0:
        track = db.query(Track).filter(Track.id == data.track_id).first()
        if not track:
            raise HTTPException(status_code=404, detail="Track not found")

    if data.name is not None:
        figurine.name = data.name

    if data.track_id is not None:
        # track_id = 0 removes the assignment.
        figurine.track_id = data.track_id if data.track_id != 0 else None

    db.commit()
    db.refresh(figurine, ["track"])

    return figurine


@router.delete("/figurines/{figurine_id}")
def delete_figurine(figurine_id: int, db: Session = Depends(get_db)):
    """Delete a figurine."""
    figurine = db.query(Figurine).filter(Figurine.id == figurine_id).first()

    if not figurine:
        raise HTTPException(status_code=404, detail="Figurine not found")

    db.delete(figurine)
    db.commit()

    return {"message": "Figurine deleted", "id": figurine_id}


# === System Sounds ===


@router.get("/system_sounds", response_model=List[SystemSoundResponse])
def list_system_sounds(db: Session = Depends(get_db)):
    """Return all system sound slots."""
    order = {name: index for index, name in enumerate(ACTIVE_SYSTEM_SOUND_NAMES)}
    sounds = db.query(SystemSound).all()
    sounds.sort(key=lambda sound: order.get(sound.name, len(order)))
    return sounds


@router.post("/system_sounds/{name}", response_model=SystemSoundResponse)
async def upload_system_sound(
    name: str,
    file: UploadFile = File(...),
    db: Session = Depends(get_db),
):
    """Upload an MP3 file for a system sound slot."""
    if name not in VALID_SYSTEM_SOUNDS:
        raise HTTPException(
            status_code=400,
            detail=f"Invalid name. Allowed values: {', '.join(sorted(VALID_SYSTEM_SOUNDS))}"
        )

    if not file.filename.lower().endswith(".mp3"):
        raise HTTPException(status_code=400, detail="Only MP3 files are allowed")

    sound = db.query(SystemSound).filter(SystemSound.name == name).first()
    if not sound:
        raise HTTPException(status_code=404, detail="Sound slot does not exist")

    SYSTEM_SOUNDS_DIR.mkdir(parents=True, exist_ok=True)

    filename = f"{name}.mp3"
    final_path = SYSTEM_SOUNDS_DIR / filename

    # Save a temporary upload and convert it to 64 kbps.
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
                detail=f"MP3 conversion failed: {result.stderr}"
            )
    finally:
        os.unlink(temp_input_path)

    sound.filename = filename
    db.commit()
    db.refresh(sound)

    return sound


@router.delete("/system_sounds/{name}")
def delete_system_sound(name: str, db: Session = Depends(get_db)):
    """Delete a system sound file while keeping the slot."""
    if name not in VALID_SYSTEM_SOUNDS:
        raise HTTPException(status_code=400, detail="Invalid name")

    sound = db.query(SystemSound).filter(SystemSound.name == name).first()
    if not sound:
        raise HTTPException(status_code=404, detail="Sound slot does not exist")

    if sound.filename:
        file_path = SYSTEM_SOUNDS_DIR / sound.filename
        if file_path.exists():
            os.remove(file_path)

    sound.filename = None
    db.commit()

    return {"message": "System sound deleted", "name": name}


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
    """Return the configured device, falling back to the last known status during active sync."""
    try:
        device = _get_device_or_404()
        payload = _remember_device_status(fetch_device_status(device))
        return [payload]
    except RequestException as exc:
        configured = get_device_settings()
        running_syncs = [
            task_id for task_id, task in device_sync_tasks.items()
            if task.get("status") == "running"
        ]
        if running_syncs and last_known_device_status:
            logger.warning(
                "Configured device status fetch failed during active sync; returning cached device status configured_ip=%s running_sync_task_ids=%s error=%s",
                configured.get("ip") or "unknown",
                running_syncs,
                exc,
            )
            return [dict(last_known_device_status)]
        logger.warning(
            "Configured device is unreachable configured_ip=%s running_sync_task_ids=%s error=%s",
            configured.get("ip") or "unknown",
            running_syncs,
            exc,
        )
        return []


@router.get("/devices/{device_id}/files", response_model=DeviceFilesResponse)
def get_device_files(device_id: str):
    """Return the list of files currently available on the device SD card."""
    try:
        return fetch_device_files(_get_device_or_404())
    except RequestException as exc:
        logger.warning("Fetching device files failed device_id=%s error=%s", device_id, exc)
        raise HTTPException(status_code=502, detail=f"Device connection error: {exc}") from exc


@router.get("/devices/{device_id}/logs", response_model=DeviceLogsResponse)
def get_device_logs(device_id: str):
    try:
        return fetch_device_logs(_get_device_or_404())
    except RequestException as exc:
        logger.warning("Fetching device logs failed device_id=%s error=%s", device_id, exc)
        detail = _device_error_detail(exc)
        status = exc.response.status_code if exc.response is not None else 502
        raise HTTPException(status_code=status, detail=detail) from exc


@router.get("/devices/{device_id}/logs/content", response_model=DeviceLogContentResponse)
def get_device_log_content(device_id: str, name: str, tail: int = 200):
    try:
        return fetch_device_log_content(_get_device_or_404(), name=name, tail=tail)
    except RequestException as exc:
        logger.warning(
            "Fetching device log content failed device_id=%s log_name=%s tail=%s error=%s",
            device_id,
            name,
            tail,
            exc,
        )
        detail = _device_error_detail(exc)
        status = exc.response.status_code if exc.response is not None else 502
        raise HTTPException(status_code=status, detail=detail) from exc


@router.get("/devices/{device_id}/logs/download")
def get_device_log_download(device_id: str, name: str):
    try:
        response = download_device_log(_get_device_or_404(), name=name)
    except RequestException as exc:
        logger.warning("Downloading device log failed device_id=%s log_name=%s error=%s", device_id, name, exc)
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
    """Check whether the device needs sync."""
    try:
        _normalize_track_filenames(db)
        plan = build_sync_plan(_get_device_or_404(), db)
        logger.info(
            "Sync check completed device_id=%s resolved_device_id=%s needs_sync=%s upload_count=%s delete_count=%s",
            device_id,
            plan["device_id"],
            plan["needs_sync"],
            plan["upload_count"],
            plan["delete_count"],
        )
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
        logger.warning("Sync check failed device_id=%s error=%s", device_id, exc)
        raise HTTPException(status_code=502, detail=f"Device connection error: {exc}") from exc


@router.get("/devices/{device_id}/config")
def get_device_config(device_id: str):
    """Return the current LED animation configuration from the device."""
    try:
        return fetch_led_config(_get_device_or_404())
    except RequestException as exc:
        logger.warning("Fetching device config failed device_id=%s error=%s", device_id, exc)
        raise HTTPException(status_code=502, detail=f"Device connection error: {exc}") from exc


@router.post("/devices/{device_id}/config")
def save_device_config(device_id: str, payload: dict):
    """Write LED animation configuration to the device."""
    try:
        device = _get_device_or_404()
        push_led_config(device, payload)
        logger.info(
            "Device config saved device_id=%s device_ip=%s keys=%s",
            device_id,
            device.get("ip", "unknown"),
            sorted(payload.keys()),
        )
        return {"saved": True, "device_id": device_id}
    except RequestException as exc:
        logger.warning("Saving device config failed device_id=%s error=%s", device_id, exc)
        raise HTTPException(status_code=502, detail=f"Device connection error: {exc}") from exc


@router.post("/devices/{device_id}/sync", response_model=DeviceSyncStartResponse)
def sync_device_now(device_id: str):
    """Start a background sync and return a pollable task ID."""
    try:
        current_task = _get_current_sync_task(device_id)
        if current_task:
            logger.info(
                "Reusing running sync task device_id=%s task_id=%s progress=%s stage=%s",
                device_id,
                current_task["task_id"],
                current_task.get("progress"),
                current_task.get("stage"),
            )
            return {"task_id": current_task["task_id"]}
        device = _get_device_or_404()
        task_id = uuid.uuid4().hex
        device_sync_tasks[task_id] = _create_sync_task_state(task_id, device_id)
        logger.info(
            "Starting new sync task device_id=%s task_id=%s device_ip=%s",
            device_id,
            task_id,
            device.get("ip", "unknown"),
        )
        thread = threading.Thread(
            target=_run_device_sync_task,
            args=(task_id, device, device_id),
            daemon=True,
        )
        thread.start()
        return {"task_id": task_id}
    except RequestException as exc:
        logger.warning("Starting sync failed device_id=%s error=%s", device_id, exc)
        raise HTTPException(status_code=502, detail=f"Device connection error: {exc}") from exc


@router.get("/devices/{device_id}/sync/current", response_model=DeviceSyncTaskStatus)
def get_current_device_sync(device_id: str):
    task = _get_current_sync_task(device_id)
    if not task:
        raise HTTPException(status_code=404, detail="No active sync task")
    return task


@router.get("/devices/{device_id}/sync/{task_id}/status", response_model=DeviceSyncTaskStatus)
def get_device_sync_status(device_id: str, task_id: str):
    task = device_sync_tasks.get(task_id)
    if not task or task.get("device_id") != device_id:
        logger.warning("Requested unknown sync task device_id=%s task_id=%s", device_id, task_id)
        raise HTTPException(status_code=404, detail="Sync task not found")
    return task


@router.post("/devices/{device_id}/restart")
def restart_device_now(device_id: str):
    """Restart the device after service operations."""
    try:
        restart_device(_get_device_or_404())
        logger.warning("Restart requested from admin device_id=%s", device_id)
        return {"restart": True, "device_id": device_id}
    except RequestException as exc:
        logger.warning("Restarting device failed device_id=%s error=%s", device_id, exc)
        raise HTTPException(status_code=502, detail=f"Device connection error: {exc}") from exc


@router.get("/devices/{device_id}/bt/devices", response_model=BtDevicesResponse)
def get_bt_devices(device_id: str):
    """Return live BT scan results from the device (device must be in sync mode)."""
    try:
        return fetch_bt_devices(_get_device_or_404())
    except RequestException as exc:
        logger.warning("Fetching BT devices failed device_id=%s error=%s", device_id, exc)
        detail = _device_error_detail(exc)
        status = exc.response.status_code if exc.response is not None else 502
        raise HTTPException(status_code=status, detail=detail) from exc


@router.post("/devices/{device_id}/bt/start-scan")
def bt_start_scan(device_id: str):
    """Start BT inquiry scan on device (device must be in sync mode)."""
    try:
        return start_bt_scan(_get_device_or_404())
    except RequestException as exc:
        logger.warning("Starting BT scan failed device_id=%s error=%s", device_id, exc)
        detail = _device_error_detail(exc)
        status = exc.response.status_code if exc.response is not None else 502
        raise HTTPException(status_code=status, detail=detail) from exc


@router.post("/devices/{device_id}/bt/stop-scan")
def bt_stop_scan(device_id: str):
    """Stop ongoing BT scan on device."""
    try:
        return stop_bt_scan(_get_device_or_404())
    except RequestException as exc:
        logger.warning("Stopping BT scan failed device_id=%s error=%s", device_id, exc)
        detail = _device_error_detail(exc)
        status = exc.response.status_code if exc.response is not None else 502
        raise HTTPException(status_code=status, detail=detail) from exc


@router.post("/devices/{device_id}/bt/select", response_model=BtSelectResponse)
def bt_select_device(device_id: str, body: BtSelectRequest):
    """Save chosen BT speaker name to device NVS (device must be in sync mode)."""
    try:
        return select_bt_device(_get_device_or_404(), name=body.name)
    except RequestException as exc:
        logger.warning(
            "Selecting BT device failed device_id=%s name=%s error=%s",
            device_id, body.name, exc
        )
        detail = _device_error_detail(exc)
        status = exc.response.status_code if exc.response is not None else 502
        raise HTTPException(status_code=status, detail=detail) from exc


@router.get("/devices/{device_id}/diag/recent-log")
def get_recent_log(device_id: str, since: int = 0):
    """Poll in-memory ESP32 log ring buffer. Returns lines since the given index."""
    try:
        return fetch_recent_log(_get_device_or_404(), since=since)
    except RequestException as exc:
        logger.warning("Fetching recent log failed device_id=%s error=%s", device_id, exc)
        detail = _device_error_detail(exc)
        status = exc.response.status_code if exc.response is not None else 502
        raise HTTPException(status_code=status, detail=detail) from exc
