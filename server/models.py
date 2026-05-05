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
    mtime: int
    size: int


class SyncSystemSound(BaseModel):
    name: str
    filename: str


class SyncResponse(BaseModel):
    figurines: list[SyncFigurine]
    tracks: list[SyncTrack]
    system_sounds: list[SyncSystemSound]


class DeviceFileEntry(BaseModel):
    path: str
    size: int
    mtime: int | None = None


class DeviceStatus(BaseModel):
    device_id: str
    hostname: str
    mode: str
    ip: str
    rssi: int
    sd_ok: bool
    battery_v: float
    battery_bars: int
    sd_total: int | None = None
    sd_used: int | None = None


class DeviceFilesResponse(DeviceStatus):
    files: list[DeviceFileEntry]


class DeviceSyncStartResponse(BaseModel):
    task_id: str


class DeviceSyncTaskStatus(BaseModel):
    task_id: str
    device_id: str
    status: str
    started_at: str | None = None
    finished_at: str | None = None
    progress: int
    message: str
    stage: str
    current_file: str | None = None
    current_file_index: int | None = None
    current_file_total: int | None = None
    current_file_progress: int | None = None
    uploaded: list[str]
    deleted: list[str]
    uploaded_count: int
    deleted_count: int
    mappings_written: bool
    mappings_count: int
    mappings_path: str
    system_sounds_written: bool
    system_sounds_count: int
    system_sounds_path: str
    led_config_written: bool = False
    led_config_path: str | None = None
    restart_required: bool = False
    error: str | None = None


class DeviceSyncCheckResponse(BaseModel):
    device_id: str
    needs_sync: bool
    message: str
    files_to_upload: list[str]
    files_to_delete: list[str]
    upload_count: int
    delete_count: int
    music_needs_update: bool
    music_files_to_upload: list[str]
    music_files_to_delete: list[str]
    music_upload_count: int
    music_delete_count: int
    mappings_count: int
    mappings_present: bool
    mappings_needs_update: bool
    mappings_path: str
    system_sounds_count: int
    system_sounds_present: bool
    system_sounds_needs_update: bool
    system_sounds_path: str
    system_sound_files_to_upload: list[str]
    system_sound_files_to_delete: list[str]
    system_sound_upload_count: int
    system_sound_delete_count: int
    system_sounds_manifest_needs_update: bool


class DeviceSettings(BaseModel):
    ip: str = ""


# ESP32 API response
class PlayResponse(BaseModel):
    stream_url: str
    track_title: str
    figurine_name: str
