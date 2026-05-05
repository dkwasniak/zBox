import os
import json
from pathlib import Path
from typing import Callable, Dict, List

import requests
from requests_toolbelt.multipart.encoder import MultipartEncoder, MultipartEncoderMonitor
from sqlalchemy.orm import Session, joinedload

from database import Figurine, SystemSound, Track


HTTP_TIMEOUT = 20
UPLOAD_TIMEOUT = 120
MUSIC_DIR = Path("./music")
SYSTEM_SOUNDS_DIR = Path("./music/system")
DEVICE_SETTINGS_PATH = Path("./data/device_settings.json")


def _load_device_settings() -> dict:
    if not DEVICE_SETTINGS_PATH.exists():
        return {}
    try:
        return json.loads(DEVICE_SETTINGS_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


def get_device_settings() -> dict:
    settings = _load_device_settings()
    return {"ip": str(settings.get("ip") or "").strip()}


def save_device_settings(ip: str) -> dict:
    ip = ip.strip()
    DEVICE_SETTINGS_PATH.parent.mkdir(parents=True, exist_ok=True)
    DEVICE_SETTINGS_PATH.write_text(
        json.dumps({"ip": ip}, ensure_ascii=True, indent=2) + "\n",
        encoding="utf-8",
    )
    return {"ip": ip}


def get_configured_device() -> dict:
    ip = get_device_settings()["ip"]
    if not ip:
        ip = (os.getenv("ZBOX_IP") or "").strip()
    if not ip:
        raise RuntimeError("Brak konfiguracji ZBOX_IP")
    return {"ip": ip}


def _base_url(device: dict) -> str:
    return f"http://{device['ip']}"


def _http_client(session: requests.Session | None = None):
    return session or requests


def fetch_device_status(device: dict | None = None, session: requests.Session | None = None) -> dict:
    device = device or get_configured_device()
    response = _http_client(session).get(f"{_base_url(device)}/diag/status", timeout=HTTP_TIMEOUT)
    response.raise_for_status()
    payload = response.json()
    payload["ip"] = device["ip"]
    return payload


def fetch_device_files(device: dict | None = None, session: requests.Session | None = None) -> dict:
    device = device or get_configured_device()
    response = _http_client(session).get(f"{_base_url(device)}/diag/files", timeout=HTTP_TIMEOUT)
    response.raise_for_status()
    payload = response.json()
    payload["ip"] = device["ip"]
    return payload


def fetch_led_config(device: dict | None = None, session: requests.Session | None = None) -> dict:
    device = device or get_configured_device()
    response = _http_client(session).get(f"{_base_url(device)}/diag/config", timeout=HTTP_TIMEOUT)
    response.raise_for_status()
    return response.json()


def push_led_config(device: dict | None, config: dict, session: requests.Session | None = None) -> None:
    device = device or get_configured_device()
    response = _http_client(session).post(
        f"{_base_url(device)}/diag/config",
        json=config,
        timeout=HTTP_TIMEOUT,
    )
    response.raise_for_status()


def restart_device(device: dict | None = None, session: requests.Session | None = None) -> None:
    device = device or get_configured_device()
    response = _http_client(session).post(f"{_base_url(device)}/diag/restart", timeout=HTTP_TIMEOUT)
    response.raise_for_status()


def _build_mappings_payload(db: Session) -> dict:
    figurines_db = (
        db.query(Figurine)
        .options(joinedload(Figurine.track))
        .filter(Figurine.track_id.isnot(None))
        .order_by(Figurine.nfc_uid.asc())
        .all()
    )

    figurines = {}
    for figurine in figurines_db:
        if figurine.track:
            figurines[figurine.nfc_uid] = {"file": figurine.track.filename}
    return {"figurines": figurines}


def _build_system_sounds_payload(db: Session) -> tuple[dict, Dict[str, Path]]:
    sounds_db = (
        db.query(SystemSound)
        .filter(SystemSound.filename.isnot(None))
        .order_by(SystemSound.name.asc())
        .all()
    )
    mapping = {}
    expected_files: Dict[str, Path] = {}
    for sound in sounds_db:
        if not sound.filename:
            continue
        path = SYSTEM_SOUNDS_DIR / sound.filename
        if path.exists():
            mapping[sound.name] = f"/data/system/{sound.filename}"
            expected_files[f"/data/system/{sound.filename}"] = path
    return mapping, expected_files


def _build_expected_music_files(db: Session) -> Dict[str, Path]:
    expected_music: Dict[str, Path] = {}
    for track in db.query(Track).order_by(Track.filename.asc()).all():
        path = MUSIC_DIR / track.filename
        if path.exists():
            expected_music[f"/music/{track.filename}"] = path
    return expected_music


def _json_storage_size(payload: dict) -> int:
    return len(json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8"))


def build_sync_plan(device: dict | None, db: Session, session: requests.Session | None = None) -> dict:
    device = device or get_configured_device()
    files_state = fetch_device_files(device, session=session)
    remote_files = {
        entry["path"]: {
            "size": entry["size"],
            "mtime": entry.get("mtime"),
        }
        for entry in files_state.get("files", [])
    }
    resolved_device_id = files_state.get("device_id") or device.get("ip") or "unknown"

    mappings_payload = _build_mappings_payload(db)
    system_sounds_payload, expected_sound_files = _build_system_sounds_payload(db)
    expected_music = _build_expected_music_files(db)
    expected_files = {**expected_music, **expected_sound_files}

    pending_uploads: List[str] = []
    for remote_path, local_path in expected_files.items():
        local_stat = local_path.stat()
        local_size = local_stat.st_size
        local_mtime = int(local_stat.st_mtime)
        remote_entry = remote_files.get(remote_path)
        remote_size = remote_entry["size"] if remote_entry else None
        remote_mtime = remote_entry.get("mtime") if remote_entry else None
        if remote_size != local_size or remote_mtime != local_mtime:
            pending_uploads.append(remote_path)

    pending_deletions = sorted(
        path for path in remote_files
        if (path.startswith("/music/") or path.startswith("/data/system/")) and path not in expected_files
    )

    mappings_path = "/data/mappings.json"
    system_sounds_path = "/data/system_sounds.json"
    mappings_present = mappings_path in remote_files
    system_sounds_present = system_sounds_path in remote_files
    expected_mappings_size = _json_storage_size(mappings_payload)
    expected_system_sounds_size = _json_storage_size(system_sounds_payload)
    mappings_needs_update = (
        not mappings_present
        or remote_files.get(mappings_path, {}).get("size") != expected_mappings_size
    )
    system_sounds_manifest_needs_update = (
        not system_sounds_present
        or remote_files.get(system_sounds_path, {}).get("size") != expected_system_sounds_size
    )
    music_upload_candidates = [path for path in pending_uploads if path.startswith("/music/")]
    music_delete_candidates = [path for path in pending_deletions if path.startswith("/music/")]
    system_sound_upload_candidates = [path for path in pending_uploads if path.startswith("/data/system/")]
    system_sound_delete_candidates = [path for path in pending_deletions if path.startswith("/data/system/")]
    music_needs_update = bool(music_upload_candidates or music_delete_candidates)
    system_sounds_needs_update = bool(
        system_sound_upload_candidates
        or system_sound_delete_candidates
        or system_sounds_manifest_needs_update
    )
    needs_sync = bool(
        music_needs_update
        or mappings_needs_update
        or system_sounds_needs_update
    )

    changed_parts: List[str] = []
    if music_needs_update:
        changed_parts.append("pliki audio")
    if mappings_needs_update:
        changed_parts.append("mapowania")
    if system_sounds_needs_update:
        changed_parts.append("system sounds")

    if changed_parts:
        message = f"Update wymagany: {', '.join(changed_parts)}."
    else:
        message = "Brak zmian. Muzyka, mapowania i system sounds wygladaja na aktualne."

    return {
        "device_id": resolved_device_id,
        "needs_sync": needs_sync,
        "message": message,
        "files_to_upload": pending_uploads,
        "files_to_delete": pending_deletions,
        "upload_count": len(pending_uploads),
        "delete_count": len(pending_deletions),
        "music_files_to_upload": music_upload_candidates,
        "music_files_to_delete": music_delete_candidates,
        "music_upload_count": len(music_upload_candidates),
        "music_delete_count": len(music_delete_candidates),
        "mappings_count": len(mappings_payload["figurines"]),
        "mappings_present": mappings_present,
        "mappings_needs_update": mappings_needs_update,
        "mappings_path": mappings_path,
        "mappings_expected_size": expected_mappings_size,
        "system_sounds_count": len(system_sounds_payload),
        "system_sounds_present": system_sounds_present,
        "system_sounds_needs_update": system_sounds_needs_update,
        "system_sounds_path": system_sounds_path,
        "system_sounds_expected_size": expected_system_sounds_size,
        "system_sound_files_to_upload": system_sound_upload_candidates,
        "system_sound_files_to_delete": system_sound_delete_candidates,
        "system_sound_upload_count": len(system_sound_upload_candidates),
        "system_sound_delete_count": len(system_sound_delete_candidates),
        "system_sounds_manifest_needs_update": system_sounds_manifest_needs_update,
        "music_needs_update": music_needs_update,
        "remote_files": remote_files,
        "expected_files": expected_files,
    }


def _emit_progress(callback: Callable[[dict], None] | None, **payload) -> None:
    if callback is not None:
        callback(payload)


def _stage_progress(start: int, end: int, index: int, total: int) -> int:
    if total <= 0:
        return end
    if total == 1:
        return end
    return start + int(((end - start) * index) / (total - 1))


def _upload_file_with_progress(
    device: dict,
    remote_path: str,
    local_path: Path,
    local_mtime: int,
    file_index: int,
    file_total: int,
    session: requests.Session | None = None,
    progress_callback: Callable[[dict], None] | None = None,
) -> requests.Response:
    with local_path.open("rb") as fh:
        encoder = MultipartEncoder(
            fields={"file": (local_path.name, fh, "audio/mpeg")}
        )

        def _on_upload(monitor: MultipartEncoderMonitor) -> None:
            current_percent = int((monitor.bytes_read / monitor.len) * 100) if monitor.len else 0
            file_fraction = (file_index - 1 + (current_percent / 100)) / max(1, file_total)
            overall_progress = 20 + int(file_fraction * 50)
            _emit_progress(
                progress_callback,
                progress=max(20, min(70, overall_progress)),
                stage="uploading_audio",
                message=f"Przesylanie plikow audio: {file_index}/{file_total}",
                current_file=remote_path,
                current_file_index=file_index,
                current_file_total=file_total,
                current_file_progress=max(0, min(100, current_percent)),
            )

        monitor = MultipartEncoderMonitor(encoder, _on_upload)
        response = _http_client(session).post(
            f"{_base_url(device)}/diag/upload",
            params={"path": remote_path, "mtime": str(local_mtime)},
            data=monitor,
            headers={"Content-Type": monitor.content_type},
            timeout=(HTTP_TIMEOUT, UPLOAD_TIMEOUT),
        )
    return response


def sync_device(
    device: dict | None,
    db: Session,
    led_config: dict | None = None,
    progress_callback: Callable[[dict], None] | None = None,
) -> dict:
    device = device or get_configured_device()
    with requests.Session() as session:
        _emit_progress(
            progress_callback,
            status="running",
            progress=5,
            stage="fetching_device_state",
            message="Pobieranie stanu urzadzenia...",
        )
        sync_plan = build_sync_plan(device, db, session=session)
        remote_files = sync_plan["remote_files"]
        resolved_device_id = sync_plan["device_id"]

        uploaded: List[str] = []
        deleted: List[str] = []
        led_written = False
        mappings_written = False
        system_sounds_written = False
        mappings_payload = _build_mappings_payload(db)
        mappings_count = len(mappings_payload["figurines"])

        _emit_progress(
            progress_callback,
            device_id=resolved_device_id,
            progress=15,
            stage="comparing_files",
            message="Porownywanie plikow na urzadzeniu z biblioteka...",
            uploaded=list(uploaded),
            deleted=list(deleted),
            mappings_count=mappings_count,
        )
        system_sounds_payload, _expected_sound_files = _build_system_sounds_payload(db)
        expected_files = sync_plan["expected_files"]
        system_sounds_count = len(system_sounds_payload)
        pending_upload_paths = sorted(sync_plan["files_to_upload"])
        pending_uploads = [(remote_path, expected_files[remote_path]) for remote_path in pending_upload_paths]
        managed_remote = sync_plan["files_to_delete"]

        _emit_progress(
            progress_callback,
            progress=20,
            stage="uploading_audio",
            message=(
                "Brak plikow do przeslania."
                if not pending_uploads
                else f"Przesylanie plikow audio: 0/{len(pending_uploads)}"
            ),
            uploaded=list(uploaded),
            deleted=list(deleted),
            uploaded_count=len(uploaded),
            deleted_count=len(deleted),
            system_sounds_count=system_sounds_count,
            current_file=None,
            current_file_index=None,
            current_file_total=len(pending_uploads),
            current_file_progress=0 if pending_uploads else None,
        )
        for index, (remote_path, local_path) in enumerate(pending_uploads, start=1):
            local_mtime = int(local_path.stat().st_mtime)
            response = _upload_file_with_progress(
                device=device,
                remote_path=remote_path,
                local_path=local_path,
                local_mtime=local_mtime,
                file_index=index,
                file_total=len(pending_uploads),
                session=session,
                progress_callback=progress_callback,
            )
            response.raise_for_status()
            uploaded.append(remote_path)
            _emit_progress(
                progress_callback,
                progress=20 + int((index / max(1, len(pending_uploads))) * 50) if pending_uploads else 70,
                stage="uploading_audio",
                message=f"Przesylanie plikow audio: {index}/{len(pending_uploads)}",
                current_file=remote_path,
                current_file_index=index,
                current_file_total=len(pending_uploads),
                current_file_progress=100,
                uploaded=list(uploaded),
                deleted=list(deleted),
                uploaded_count=len(uploaded),
            )

        _emit_progress(
            progress_callback,
            progress=55,
            stage="deleting_remote",
            message=(
                "Brak zbednych plikow do usuniecia."
                if not managed_remote
                else f"Usuwanie zbednych plikow: 0/{len(managed_remote)}"
            ),
            current_file=None,
            current_file_index=None,
            current_file_total=None,
            current_file_progress=None,
            uploaded=list(uploaded),
            deleted=list(deleted),
            uploaded_count=len(uploaded),
            deleted_count=len(deleted),
        )
        for index, remote_path in enumerate(managed_remote, start=1):
            response = session.delete(
                f"{_base_url(device)}/diag/file",
                params={"path": remote_path},
                timeout=HTTP_TIMEOUT,
            )
            response.raise_for_status()
            deleted.append(remote_path)
            _emit_progress(
                progress_callback,
                progress=_stage_progress(55, 70, index, len(managed_remote)),
                stage="deleting_remote",
                message=f"Usuwanie zbednych plikow: {index}/{len(managed_remote)}",
                uploaded=list(uploaded),
                deleted=list(deleted),
                deleted_count=len(deleted),
            )

        if sync_plan["mappings_needs_update"]:
            _emit_progress(
                progress_callback,
                progress=75,
                stage="writing_mappings",
                message="Zapisywanie mapowan figurek...",
                uploaded=list(uploaded),
                deleted=list(deleted),
            )
            response = session.post(
                f"{_base_url(device)}/diag/write-mappings",
                json=mappings_payload,
                timeout=HTTP_TIMEOUT,
            )
            response.raise_for_status()
            mappings_written = True

        _emit_progress(
            progress_callback,
            progress=85,
            stage="writing_system_sounds",
            message=(
                "Zapisywanie dzwiekow systemowych..."
                if sync_plan["system_sounds_needs_update"]
                else "Dzwieki systemowe nie wymagaja zapisu."
            ),
            mappings_written=mappings_written,
            mappings_count=mappings_count,
            mappings_path="/data/mappings.json",
        )
        if sync_plan["system_sounds_needs_update"]:
            response = session.post(
                f"{_base_url(device)}/diag/write-system-sounds",
                json=system_sounds_payload,
                timeout=HTTP_TIMEOUT,
            )
            response.raise_for_status()
            system_sounds_written = True

        if led_config is not None:
            _emit_progress(
                progress_callback,
                progress=92,
                stage="writing_led_config",
                message="Zapisywanie konfiguracji LED...",
                system_sounds_written=system_sounds_written,
                system_sounds_count=system_sounds_count,
                system_sounds_path="/data/system_sounds.json",
            )
            push_led_config(device, led_config, session=session)
            led_written = True

    return {
        "device_id": resolved_device_id,
        "status": "completed",
        "progress": 100,
        "message": "Synchronizacja zakonczona. Restart pozostaje osobna akcja.",
        "stage": "completed",
        "uploaded": uploaded,
        "deleted": deleted,
        "uploaded_count": len(uploaded),
        "deleted_count": len(deleted),
        "mappings_written": mappings_written,
        "mappings_count": mappings_count,
        "mappings_path": "/data/mappings.json",
        "system_sounds_written": system_sounds_written,
        "system_sounds_count": system_sounds_count,
        "system_sounds_path": "/data/system_sounds.json",
        "led_config_written": led_written,
        "led_config_path": "/data/led_config.json" if led_written else None,
        "restart_required": True,
        "error": None,
    }
