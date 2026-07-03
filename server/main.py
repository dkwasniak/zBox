"""MusicBox - Serwer muzyczny dla ESP32 z NFC."""

from pathlib import Path

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from database import create_tables
from routers import api, admin


# Utwórz katalogi jeśli nie istnieją
Path("./music").mkdir(parents=True, exist_ok=True)
Path("./data").mkdir(parents=True, exist_ok=True)

# Inicjalizacja bazy danych
create_tables()

# Aplikacja FastAPI
app = FastAPI(
    title="MusicBox",
    description="Serwer muzyczny dla pudełka z figurkami NFC",
    version="1.0.0",
)

# CORS - dla panelu webowego
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Routery
app.include_router(api.router)
app.include_router(admin.router)

# Statyczne pliki - panel webowy
app.mount("/", StaticFiles(directory="web", html=True), name="web")


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(app, host="0.0.0.0", port=8000)
