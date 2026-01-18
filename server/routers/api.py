"""API endpoints dla ESP32."""

from pathlib import Path

from fastapi import APIRouter, Depends, HTTPException, Request
from fastapi.responses import FileResponse
from sqlalchemy.orm import Session

from database import get_db, Figurine, Track
from models import PlayResponse


router = APIRouter(prefix="/api", tags=["ESP32 API"])

MUSIC_DIR = Path("./music")


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


@router.get("/health")
def health_check():
    """Endpoint do sprawdzania czy serwer działa."""
    return {"status": "ok", "service": "musicbox"}
