#pragma once
#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// CmdId — command correlation token (dispatcher fills; reducer emits 0)
// ---------------------------------------------------------------------------
using CmdId = uint16_t;

// ---------------------------------------------------------------------------
// Substate enums (each 1 byte)
// ---------------------------------------------------------------------------
enum class SessionMode       : uint8_t { Normal, NightLight };
enum class PlaybackMode      : uint8_t { Nfc, Music };
enum class AudioState        : uint8_t {
    Idle, StartingFile, StartingSystemSound,
    PlayingFile, PlayingSystemSound, Paused, Stopping
};
enum class BtState           : uint8_t {
    Unknown,
    WaitingForSpeaker,
    Connected,
    RecoveryPulsePending,
    DiscoveryFallbackPending,
    Disabled
};
enum class SleepState        : uint8_t {
    Awake, PreparingDeepSleep, WaitingPowerOffSound,
    WaitingBtShutdown, ReadyToSleep
};
enum class BootState         : uint8_t {
    ColdBootInit, WakeHoldCheck, NormalBootInit,
    NightLightBootInit, Ready
};
enum class RequestedSleepKind : uint8_t { None, Normal, Emergency, NightLightTimeout };

// ---------------------------------------------------------------------------
// Failure reason enums
// ---------------------------------------------------------------------------
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
    DisableTimeout,
    RestartTimeout,
    StackError,
    DriverFault
};
enum class PersistenceFailReason : uint8_t { NvsError };

// ---------------------------------------------------------------------------
// PendingPlayback and CurrentTrack
// ---------------------------------------------------------------------------
enum class PendingPlaybackKind : uint8_t {
    None,
    NfcUid,
    MusicCurrentTrack,
    MusicSpecificTrack
};

struct PendingPlayback {
    PendingPlaybackKind kind;   // 1 byte
    char uid[24];               // 24 bytes
    uint16_t track_index;       // 2 bytes
};                              // total: ~27 bytes (may have 1-byte alignment pad)

static_assert(sizeof(PendingPlayback::uid) >= 22, "NFC UID buffer too small");

struct CurrentTrack {
    bool     valid;             // 1 byte
    uint16_t index;             // 2 bytes
};                              // total: 3 bytes

// ---------------------------------------------------------------------------
// AppState — single authoritative state snapshot for the reducer
// ---------------------------------------------------------------------------
struct AppState {
    // Mode substates
    SessionMode            session_mode;            // 1 byte
    PlaybackMode           playback_mode;           // 1 byte
    AudioState             audio_state;             // 1 byte
    BtState                bt_state;                // 1 byte
    SleepState             sleep_state;             // 1 byte
    BootState              boot_state;              // 1 byte
    RequestedSleepKind     requested_sleep_kind;    // 1 byte

    // Context
    PendingPlayback        pending_playback;        // ~28 bytes
    CurrentTrack           current_track;           // 3 bytes
    char                   last_nfc_uid[24];        // 24 bytes
    uint8_t                night_light_brightness_percent; // 1 byte
    uint8_t                music_volume_percent;    // 1 byte
    bool                   bt_volume_applied;       // 1 byte
    bool                   sync_active;             // 1 byte
    bool                   sync_mode;               // 1 byte
    uint8_t                battery_bars;            // 1 byte
    uint8_t                volume_overlay_level_percent; // 1 byte
    bool                   battery_preview_active;  // 1 byte
    bool                   sleep_warn_active;        // 1 byte
    uint16_t               total_track_count;       // 2 bytes
    uint16_t               sync_progress_current;   // 2 bytes
    uint16_t               sync_progress_total;     // 2 bytes

    // Deadlines (absolute millis() timestamps; 0 = no active deadline)
    uint32_t               idle_deadline_ms;             // 4 bytes
    uint32_t               night_light_deadline_ms;      // 4 bytes
    uint32_t               volume_overlay_deadline_ms;   // 4 bytes
    uint32_t               battery_preview_deadline_ms;  // 4 bytes
    uint32_t               jbl_recovery_deadline_ms;     // 4 bytes
    uint32_t               bt_reconnect_deadline_ms;     // 4 bytes
    uint32_t               brightness_save_deadline_ms;  // 4 bytes
};

static_assert(sizeof(AppState) <= 132, "AppState too large for stack copy");
