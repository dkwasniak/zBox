"""Definitions for system sound slots used by the firmware."""

ACTIVE_SYSTEM_SOUND_NAMES = (
    "startup",
    "power_off",
    "nfc_mode",
    "music_mode",
)

ACTIVE_SYSTEM_SOUND_SET = set(ACTIVE_SYSTEM_SOUND_NAMES)

DEFAULT_SYSTEM_SOUND_FILES = {
    "startup": "zbox_startup_premium.mp3",
    "power_off": "power_off.mp3",
    "nfc_mode": "nfc_mode.mp3",
    "music_mode": "music_mode.mp3",
}
