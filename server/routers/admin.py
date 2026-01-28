"""API endpoints dla panelu administracyjnego."""

import os
import uuid
import subprocess
import tempfile
import threading
import re
from pathlib import Path
from typing import List, Dict

from fastapi import APIRouter, Depends, HTTPException, UploadFile, File, Form, BackgroundTasks
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

# Globalny dict do trzymania progressu zadań YouTube
youtube_tasks: Dict[str, dict] = {}


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
    safe_filename = f"{unique_id}_{file.filename}"
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
        safe_filename = f"{unique_id}_{title}.mp3"
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

            # Parsuj output w czasie rzeczywistym
            for line in process.stdout:
                # Szukaj linii z procentami, np: "[download]  45.2% of 5.23MiB"
                match = re.search(r'\[download\]\s+(\d+\.?\d*)%', line)
                if match:
                    percent = float(match.group(1))
                    # Mapuj 0-100% downloadu na 10-60% całości
                    youtube_tasks[task_id]['progress'] = int(10 + (percent * 0.5))
                    youtube_tasks[task_id]['message'] = f'Pobieranie: {percent:.1f}%'

            process.wait()

            if process.returncode != 0:
                raise Exception("Błąd pobierania z YouTube")

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
                raise Exception(f"Błąd konwersji: {result.stderr[:200]}")

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
        logger.error(f"YouTube download error: {str(e)}")
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

    new_filename = f"{unique_id}_{clean_name}_trimmed.mp3"
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
