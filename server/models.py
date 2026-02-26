from datetime import datetime
from typing import Optional

from pydantic import BaseModel, ConfigDict


# === Pydantic Schemas ===

# Track schemas
class TrackBase(BaseModel):
    title: str


class TrackCreate(TrackBase):
    filename: str


class TrackResponse(TrackBase):
    id: int
    filename: str
    created_at: datetime

    model_config = ConfigDict(from_attributes=True)


# Figurine schemas
class FigurineBase(BaseModel):
    name: str
    nfc_uid: str


class FigurineCreate(FigurineBase):
    track_id: Optional[int] = None


class FigurineUpdate(BaseModel):
    name: Optional[str] = None
    track_id: Optional[int] = None


class FigurineResponse(FigurineBase):
    id: int
    track_id: Optional[int]
    track: Optional[TrackResponse] = None
    created_at: datetime

    model_config = ConfigDict(from_attributes=True)


# System sound schemas
class SystemSoundResponse(BaseModel):
    id: int
    name: str
    filename: Optional[str]
    created_at: datetime

    model_config = ConfigDict(from_attributes=True)


# Sync schemas
class SyncFigurine(BaseModel):
    nfc_uid: str
    track_filename: str
    track_title: str


class SyncTrack(BaseModel):
    filename: str
    title: str


class SyncSystemSound(BaseModel):
    name: str
    filename: str


class SyncResponse(BaseModel):
    figurines: list[SyncFigurine]
    tracks: list[SyncTrack]
    system_sounds: list[SyncSystemSound]


# ESP32 API response
class PlayResponse(BaseModel):
    stream_url: str
    track_title: str
    figurine_name: str
