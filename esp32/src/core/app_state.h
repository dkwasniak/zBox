#pragma once
#include <stdbool.h>
#include <stdint.h>

using CmdId = uint16_t;

enum class SessionMode : uint8_t { Normal, NightLight };
enum class PlaybackMode : uint8_t { Nfc, Music };
enum class AudioState : uint8_t {
    Idle,
    StartingFile,
    StartingSystemSound,
    PlayingFile,
    PlayingSystemSound,
    Paused,
    Stopping,
    StoppingForOutputChange,
    StoppingForModeChange
};
enum class AudioOutputMode : uint8_t { LocalSpeaker, BtHeadphones };
enum class BtHeadphonesState : uint8_t {
    Inactive,
    WaitingForHeadphones,
    Connected,
    Stopping
};
enum class SleepState : uint8_t {
    Awake,
    PreparingDeepSleep,
    WaitingPowerOffSound,
    WaitingBtHeadphonesStop,
    ReadyToSleep
};
enum class BootState : uint8_t {
    ColdBootInit,
    WakeHoldCheck,
    NormalBootInit,
    NightLightBootInit,
    Ready
};
enum class RequestedSleepKind : uint8_t { None, Normal, NightLightTimeout };

enum class PlaybackFailReason : uint8_t {
    MappingNotFound,
    FileNotFound,
    AudioCommandRejected,
    AudioStartTimeout,
    DriverFault
};
enum class SoundFailReason : uint8_t {
    FileNotFound,
    AudioCommandRejected,
    PlaybackTimeout,
    DriverFault
};
enum class BtFailReason : uint8_t {
    StartFailed,
    StopFailed,
    StackError,
    DriverFault
};
enum class PersistenceFailReason : uint8_t { NvsError };

enum class PendingPlaybackKind : uint8_t {
    None,
    NfcUid,
    MusicCurrentTrack,
    MusicSpecificTrack
};

struct PendingPlayback {
    PendingPlaybackKind kind;
    char uid[24];
    uint16_t track_index;
};

static_assert(sizeof(PendingPlayback::uid) >= 22, "NFC UID buffer too small");

struct CurrentTrack {
    bool valid;
    uint16_t index;
};

struct AppState {
    SessionMode session_mode;
    PlaybackMode playback_mode;
    AudioState audio_state;
    AudioOutputMode output_mode;
    BtHeadphonesState bt_headphones_state;
    SleepState sleep_state;
    BootState boot_state;
    RequestedSleepKind requested_sleep_kind;

    PendingPlayback pending_playback;
    CurrentTrack current_track;
    char last_nfc_uid[24];
    uint8_t night_light_brightness_percent;
    uint8_t output_volume_level;
    bool bt_headphones_mode_active;
    bool sync_mode;
    uint8_t battery_bars;
    uint8_t volume_overlay_level;
    bool battery_preview_active;
    bool nfc_card_played;
    uint16_t total_track_count;

    uint32_t idle_deadline_ms;
    uint32_t night_light_deadline_ms;
    uint32_t volume_overlay_deadline_ms;
    uint32_t battery_preview_deadline_ms;
    uint32_t brightness_save_deadline_ms;
    uint32_t sleep_transition_deadline_ms;
};

static_assert(sizeof(AppState) <= 128, "AppState too large for stack copy");
