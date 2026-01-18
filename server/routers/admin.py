"""API endpoints dla panelu administracyjnego."""

import os
import uuid
from pathlib import Path
from typing import List

from fastapi import APIRouter, Depends, HTTPException, UploadFile, File, Form
from sqlalchemy.orm import Session, joinedload

from database import get_db, Track, Figurine
from models import (
    TrackResponse,
    FigurineCreate,
    FigurineUpdate,
    FigurineResponse,
)


router = APIRouter(prefix="/admin", tags=["Panel administracyjny"])

MUSIC_DIR = Path("./music")


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
    """Upload nowego utworu MP3."""
    if not file.filename.lower().endswith(".mp3"):
        raise HTTPException(status_code=400, detail="Dozwolone tylko pliki MP3")

    # Generuj unikalną nazwę pliku
    unique_id = uuid.uuid4().hex[:8]
    safe_filename = f"{unique_id}_{file.filename}"
    file_path = MUSIC_DIR / safe_filename

    # Zapisz plik
    MUSIC_DIR.mkdir(parents=True, exist_ok=True)
    content = await file.read()
    with open(file_path, "wb") as f:
        f.write(content)

    # Zapisz w bazie
    track = Track(title=title, filename=safe_filename)
    db.add(track)
    db.commit()
    db.refresh(track)

    return track


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
