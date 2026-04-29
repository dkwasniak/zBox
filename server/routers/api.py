"""API endpoints dla ESP32."""

import time
from pathlib import Path
from typing import List

from fastapi import APIRouter, Depends, HTTPException, Request
from fastapi.responses import FileResponse
from sqlalchemy.orm import Session, joinedload

from database import get_db, Figurine, Track, SystemSound
from models import PlayResponse, SyncResponse, SyncFigurine, SyncTrack, SyncSystemSound


router = APIRouter(prefix="/api", tags=["ESP32 API"])

MUSIC_DIR = Path("./music")
SYSTEM_SOUNDS_DIR = Path("./music/system")


@router.get("/play/{nfc_uid}", response_model=PlayResponse)
def play_by_nfc(nfc_uid: str, request: Request, db: Session = Depends(get_db)):
    """
    Zwraca URL do streamu muzyki dla danego kodu NFC.
    Używane przez ESP32 do odtwarzania muzyki.
    """
    figurine = db.query(Figurine).filter(Figurine.nfc_uid == nfc_uid).first()

    if not figurine:
        raise HTTPException(status_code=404, detail="Nie znaleziono figurki")

    if not figurine.track:
        raise HTTPException(
            status_code=404, detail="Figurka nie ma przypisanego utworu"
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
    """
    Streamuje plik MP3 dla danego utworu.
    """
    track = db.query(Track).filter(Track.id == track_id).first()

    if not track:
        raise HTTPException(status_code=404, detail="Nie znaleziono utworu")

    file_path = MUSIC_DIR / track.filename

    if not file_path.exists():
        raise HTTPException(status_code=404, detail="Plik nie istnieje")

    return FileResponse(
        path=file_path,
        media_type="audio/mpeg",
        filename=track.filename,
    )


@router.get("/stream/file/{filename}")
def stream_file(filename: str):
    """
    Streamuje plik MP3 po nazwie pliku.
    Używane przez ESP32 do synchronizacji.
    """
    # Blokuj path traversal, resztę przepuszczaj (polskie znaki, spacje)
    if ".." in filename or "/" in filename or "\\" in filename:
        raise HTTPException(status_code=400, detail="Nieprawidłowa nazwa pliku")

    # Szukaj w music/ i music/system/
    file_path = MUSIC_DIR / filename
    if not file_path.exists():
        file_path = SYSTEM_SOUNDS_DIR / filename
    if not file_path.exists():
        raise HTTPException(status_code=404, detail="Plik nie istnieje")

    return FileResponse(
        path=file_path,
        media_type="audio/mpeg",
        filename=filename,
    )


@router.get("/sync", response_model=SyncResponse)
def sync_manifest(db: Session = Depends(get_db), force: bool = False):
    """
    Zwraca manifest synchronizacji dla ESP32.
    Zawiera listę figurek z przypisanymi utworami, listę utworów i dźwięki systemowe.
    ?force=true — wymusza pobranie przez ESP32 (mtime przesunięty o +1)
    """
    # Figurki z przypisanymi utworami
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

    # Wszystkie utwory
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

    # Dźwięki systemowe z przypisanymi plikami
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
    """Endpoint do sprawdzania czy serwer działa."""
    return {"status": "ok", "service": "musicbox"}


@router.get("/system_sounds/{sound_name}")
def stream_system_sound(sound_name: str, db: Session = Depends(get_db)):
    """
    Streamuje dźwięki systemowe z bazy SystemSound.
    """
    if not sound_name.replace("_", "").isalnum():
        raise HTTPException(status_code=400, detail="Nieprawidłowa nazwa dźwięku")

    sound = db.query(SystemSound).filter(SystemSound.name == sound_name).first()
    if not sound or not sound.filename:
        raise HTTPException(
            status_code=404,
            detail=f"Dźwięk systemowy '{sound_name}' nie jest przypisany"
        )

    file_path = SYSTEM_SOUNDS_DIR / sound.filename

    if not file_path.exists():
        raise HTTPException(
            status_code=404,
            detail=f"Plik dźwięku systemowego nie istnieje"
        )

    return FileResponse(
        path=file_path,
        media_type="audio/mpeg",
        filename=sound.filename,
    )
