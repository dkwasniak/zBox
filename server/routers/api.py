"""Public API endpoints used by the ESP32 device."""

import time
from typing import List

from fastapi import APIRouter, Depends, HTTPException, Request
from fastapi.responses import FileResponse
from sqlalchemy.orm import Session, joinedload

from database import get_db, Figurine, Track, SystemSound
from models import PlayResponse, SyncResponse, SyncFigurine, SyncTrack, SyncSystemSound
from paths import MUSIC_DIR, SYSTEM_SOUNDS_DIR


router = APIRouter(prefix="/api", tags=["Device API"])


@router.get("/play/{nfc_uid}", response_model=PlayResponse)
def play_by_nfc(nfc_uid: str, request: Request, db: Session = Depends(get_db)):
    """Return a playback URL for the given NFC UID."""
    figurine = db.query(Figurine).filter(Figurine.nfc_uid == nfc_uid).first()

    if not figurine:
        raise HTTPException(status_code=404, detail="Figurine not found")

    if not figurine.track:
        raise HTTPException(
            status_code=404, detail="Figurine has no assigned track"
        )

    base_url = str(request.base_url).rstrip("/")
    stream_url = f"{base_url}/api/stream/{figurine.track.id}"

    return PlayResponse(
        stream_url=stream_url,
        track_title=figurine.track.title,
        figurine_name=figurine.name,
    )


@router.get("/stream/{track_id}")
def stream_track(track_id: int, db: Session = Depends(get_db)):
    """Stream an MP3 file for the requested track."""
    track = db.query(Track).filter(Track.id == track_id).first()

    if not track:
        raise HTTPException(status_code=404, detail="Track not found")

    file_path = MUSIC_DIR / track.filename

    if not file_path.exists():
        raise HTTPException(status_code=404, detail="File does not exist")

    return FileResponse(
        path=file_path,
        media_type="audio/mpeg",
        filename=track.filename,
    )


@router.get("/stream/file/{filename}")
def stream_file(filename: str):
    """Stream an MP3 file by filename for device sync downloads."""
    # Block path traversal, allow the rest.
    if ".." in filename or "/" in filename or "\\" in filename:
        raise HTTPException(status_code=400, detail="Invalid filename")

    # Search both music/ and music/system/.
    file_path = MUSIC_DIR / filename
    if not file_path.exists():
        file_path = SYSTEM_SOUNDS_DIR / filename
    if not file_path.exists():
        raise HTTPException(status_code=404, detail="File does not exist")

    return FileResponse(
        path=file_path,
        media_type="audio/mpeg",
        filename=filename,
    )


@router.get("/sync", response_model=SyncResponse)
def sync_manifest(db: Session = Depends(get_db), force: bool = False):
    """Return the sync manifest used by the ESP32."""
    # Figurines with assigned tracks.
    figurines_db = (
        db.query(Figurine)
        .options(joinedload(Figurine.track))
        .filter(Figurine.track_id.isnot(None))
        .all()
    )

    figurines = []
    for f in figurines_db:
        if f.track:
            figurines.append(SyncFigurine(
                nfc_uid=f.nfc_uid,
                track_filename=f.track.filename,
                track_title=f.track.title,
            ))

    # All tracks.
    tracks_db = db.query(Track).all()
    tracks = []
    for t in tracks_db:
        file_path = MUSIC_DIR / t.filename
        try:
            stat = file_path.stat()
            mtime = int(time.time()) if force else int(stat.st_mtime)
            size = stat.st_size
        except OSError:
            mtime = 0
            size = 0
        tracks.append(SyncTrack(filename=t.filename, title=t.title, mtime=mtime, size=size))

    # Assigned system sounds.
    sounds_db = db.query(SystemSound).filter(SystemSound.filename.isnot(None)).all()
    system_sounds = [
        SyncSystemSound(name=s.name, filename=s.filename) for s in sounds_db
    ]

    return SyncResponse(
        figurines=figurines,
        tracks=tracks,
        system_sounds=system_sounds,
    )


@router.get("/health")
def health_check():
    """Health endpoint."""
    return {"status": "ok", "service": "zbox"}


@router.get("/system_sounds/{sound_name}")
def stream_system_sound(sound_name: str, db: Session = Depends(get_db)):
    """Stream an assigned system sound."""
    if not sound_name.replace("_", "").isalnum():
        raise HTTPException(status_code=400, detail="Invalid sound name")

    sound = db.query(SystemSound).filter(SystemSound.name == sound_name).first()
    if not sound or not sound.filename:
        raise HTTPException(
            status_code=404,
            detail=f"System sound '{sound_name}' is not assigned"
        )

    file_path = SYSTEM_SOUNDS_DIR / sound.filename

    if not file_path.exists():
        raise HTTPException(
            status_code=404,
            detail="System sound file does not exist"
        )

    return FileResponse(
        path=file_path,
        media_type="audio/mpeg",
        filename=sound.filename,
    )
