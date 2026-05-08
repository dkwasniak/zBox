"""Shared filesystem paths for the zBox repository and runtime."""

from pathlib import Path


SERVER_DIR = Path(__file__).resolve().parent
REPO_ROOT = SERVER_DIR.parent
DATA_DIR = REPO_ROOT / "data"
MUSIC_DIR = REPO_ROOT / "music"
SYSTEM_SOUNDS_DIR = MUSIC_DIR / "system"
WEB_DIR = REPO_ROOT / "web"
