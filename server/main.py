"""zBox server for the ESP32-based NFC audio box."""

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from database import create_tables, SessionLocal, SystemSound
from paths import DATA_DIR, MUSIC_DIR, SYSTEM_SOUNDS_DIR, WEB_DIR
from routers import api, admin
from system_sounds import ACTIVE_SYSTEM_SOUND_NAMES


# Create runtime directories if they do not exist yet.
MUSIC_DIR.mkdir(parents=True, exist_ok=True)
SYSTEM_SOUNDS_DIR.mkdir(parents=True, exist_ok=True)
DATA_DIR.mkdir(parents=True, exist_ok=True)

# Initialize the database.
create_tables()

def seed_system_sounds():
    """Reconcile system sound slots with the firmware-supported list."""
    db = SessionLocal()
    try:
        stale_sounds = (
            db.query(SystemSound)
            .filter(SystemSound.name.notin_(ACTIVE_SYSTEM_SOUND_NAMES))
            .all()
        )
        for sound in stale_sounds:
            if sound.filename:
                stale_file = SYSTEM_SOUNDS_DIR / sound.filename
                if stale_file.exists():
                    stale_file.unlink()
            db.delete(sound)

        for name in ACTIVE_SYSTEM_SOUND_NAMES:
            existing = db.query(SystemSound).filter(SystemSound.name == name).first()
            if not existing:
                db.add(SystemSound(name=name))
        db.commit()
    finally:
        db.close()


seed_system_sounds()

app = FastAPI(
    title="zBox",
    description="Admin and sync server for the zBox NFC audio device",
    version="1.0.0",
)

# CORS for the built-in web admin portal.
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Routers
app.include_router(api.router)
app.include_router(admin.router)

# Static admin portal
app.mount("/", StaticFiles(directory=str(WEB_DIR), html=True), name="web")


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(app, host="0.0.0.0", port=8000)
