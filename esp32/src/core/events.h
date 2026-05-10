#pragma once
#include "app_state.h"

// ---------------------------------------------------------------------------
// EventType — all events that drive the reducer
// ---------------------------------------------------------------------------
enum class EventType : uint8_t {
    // Boot
    BootStarted,
    WakeCauseResolvedNormal,
    WakeCauseResolvedNightLight,
    BootInitCompleted,
    MappingsLoaded,              // carries: uint16_t total_track_count
    VolumeLoaded,                // carries: uint8_t percent
    BtInitStarted,
    NfcPreScanCompleted,         // carries: bool uid_found, char uid[24]

    // BT
    BtConnected,
    BtDisconnected,
    BtRecoveryPulseCompleted,
    BtRecoveryPulseFailed,       // carries: BtFailReason
    BtDiscoveryRestarted,
    BtDiscoveryRestartFailed,    // carries: BtFailReason
    BtShutdownCompleted,
    BtShutdownFailed,            // carries: BtFailReason

    // Audio
    AudioCommandRejected,        // carries: CmdId, PlaybackFailReason
    NfcPlaybackStarted,          // carries: char uid[24], CmdId
    NfcPlaybackStartFailed,      // carries: char uid[24], PlaybackFailReason, CmdId
    MusicTrackStarted,           // carries: uint16_t index, CmdId
    MusicTrackStartFailed,       // carries: uint16_t index, PlaybackFailReason, CmdId
    AudioStopped,                // carries: CmdId
    SystemSoundCompleted,        // carries: uint8_t sound_id, CmdId
    SystemSoundFailed,           // carries: uint8_t sound_id, SoundFailReason, CmdId
    TrackEnded,                  // natural completion only

    // NFC
    NfcTagDetected,              // carries: char uid[24]
    NfcTagRemoved,

    // Buttons / user input
    SleepHoldWarning,            // BTN_C held past sleep threshold — warn user before release
    SleepRequested,              // carries: RequestedSleepKind
    PlayPausePressed,
    NextTrackPressed,
    PrevTrackPressed,
    VolumeUpPressed,
    VolumeDownPressed,
    ModeToggleRequested,
    BatteryCheckRequested,       // carries: uint8_t bars
    SyncModeRequested,

    // Timers
    IdleTimeoutFired,
    NightLightTimeoutFired,
    VolumeOverlayExpired,
    BatteryPreviewExpired,
    JblRecoveryTimeoutFired,
    BtReconnectTimeoutFired,
    BrightnessSaveDeadlineFired,

    // Persistence feedback
    BrightnessLoaded,            // carries: uint8_t percent
    BrightnessPersisted,
    BrightnessPersistFailed,     // carries: PersistenceFailReason
    PlaybackModePersisted,
    PlaybackModePersistFailed,   // carries: PersistenceFailReason

    // Sync
    SyncStarted,
    SyncCompleted,
    SyncFailed,
    SyncModeEntered,
};

// ---------------------------------------------------------------------------
// Event — tagged union carrying optional payload
// ---------------------------------------------------------------------------
struct Event {
    EventType type;
    union {
        struct { bool uid_found; char uid[24]; }                          nfc_prescan;
        struct { char uid[24]; CmdId cmd_id; }                           playback_started;
        struct { char uid[24]; PlaybackFailReason reason; CmdId cmd_id; } nfc_fail;
        struct { uint16_t index; CmdId cmd_id; }                         track_started;
        struct { uint16_t index; PlaybackFailReason reason; CmdId cmd_id; } track_fail;
        struct { CmdId cmd_id; }                                          audio_stopped;
        struct { uint8_t sound_id; CmdId cmd_id; }                       sound_completed;
        struct { uint8_t sound_id; SoundFailReason reason; CmdId cmd_id; } sound_failed;
        struct { char uid[24]; }                                          nfc_detected;
        struct { RequestedSleepKind kind; }                               sleep_requested;
        struct { CmdId cmd_id; PlaybackFailReason reason; }              cmd_rejected;
        struct { BtFailReason reason; }                                   bt_fail;
        struct { PersistenceFailReason reason; }                          persist_fail;
        struct { uint16_t total_track_count; }                            mappings_loaded;
        struct { uint8_t percent; }                                       volume_loaded;
        struct { uint8_t bars; }                                          battery_check;
        struct { uint8_t percent; }                                       brightness_loaded;
    } payload;
};

// ---------------------------------------------------------------------------
// Convenience constructors
// ---------------------------------------------------------------------------
inline Event makeEvent(EventType type) {
    Event e{};
    e.type = type;
    return e;
}

inline Event makeNfcPrescanEvent(bool uid_found, const char* uid) {
    Event e{};
    e.type = EventType::NfcPreScanCompleted;
    e.payload.nfc_prescan.uid_found = uid_found;
    if (uid_found && uid) {
        int i = 0;
        while (uid[i] && i < 23) { e.payload.nfc_prescan.uid[i] = uid[i]; i++; }
        e.payload.nfc_prescan.uid[i] = '\0';
    }
    return e;
}

inline Event makeNfcDetectedEvent(const char* uid) {
    Event e{};
    e.type = EventType::NfcTagDetected;
    if (uid) {
        int i = 0;
        while (uid[i] && i < 23) { e.payload.nfc_detected.uid[i] = uid[i]; i++; }
        e.payload.nfc_detected.uid[i] = '\0';
    }
    return e;
}

inline Event makeSleepRequestedEvent(RequestedSleepKind kind) {
    Event e{};
    e.type = EventType::SleepRequested;
    e.payload.sleep_requested.kind = kind;
    return e;
}

inline Event makeNfcPlaybackStartFailedEvent(const char* uid, PlaybackFailReason reason, CmdId cmd_id) {
    Event e{};
    e.type = EventType::NfcPlaybackStartFailed;
    e.payload.nfc_fail.reason = reason;
    e.payload.nfc_fail.cmd_id = cmd_id;
    if (uid) {
        int i = 0;
        while (uid[i] && i < 23) { e.payload.nfc_fail.uid[i] = uid[i]; i++; }
        e.payload.nfc_fail.uid[i] = '\0';
    }
    return e;
}

inline Event makeSystemSoundFailedEvent(uint8_t sound_id, SoundFailReason reason, CmdId cmd_id) {
    Event e{};
    e.type = EventType::SystemSoundFailed;
    e.payload.sound_failed.sound_id = sound_id;
    e.payload.sound_failed.reason = reason;
    e.payload.sound_failed.cmd_id = cmd_id;
    return e;
}

inline Event makeSystemSoundCompletedEvent(uint8_t sound_id, CmdId cmd_id) {
    Event e{};
    e.type = EventType::SystemSoundCompleted;
    e.payload.sound_completed.sound_id = sound_id;
    e.payload.sound_completed.cmd_id = cmd_id;
    return e;
}

inline Event makeAudioStoppedEvent(CmdId cmd_id) {
    Event e{};
    e.type = EventType::AudioStopped;
    e.payload.audio_stopped.cmd_id = cmd_id;
    return e;
}

inline Event makeNfcPlaybackStartedEvent(const char* uid, CmdId cmd_id) {
    Event e{};
    e.type = EventType::NfcPlaybackStarted;
    e.payload.playback_started.cmd_id = cmd_id;
    if (uid) {
        int i = 0;
        while (uid[i] && i < 23) { e.payload.playback_started.uid[i] = uid[i]; i++; }
        e.payload.playback_started.uid[i] = '\0';
    }
    return e;
}

inline Event makeMusicTrackStartedEvent(uint16_t index, CmdId cmd_id) {
    Event e{};
    e.type = EventType::MusicTrackStarted;
    e.payload.track_started.index = index;
    e.payload.track_started.cmd_id = cmd_id;
    return e;
}

inline Event makeMusicTrackStartFailedEvent(uint16_t index, PlaybackFailReason reason, CmdId cmd_id) {
    Event e{};
    e.type = EventType::MusicTrackStartFailed;
    e.payload.track_fail.index = index;
    e.payload.track_fail.reason = reason;
    e.payload.track_fail.cmd_id = cmd_id;
    return e;
}

inline Event makeBatteryCheckEvent(uint8_t bars) {
    Event e{};
    e.type = EventType::BatteryCheckRequested;
    e.payload.battery_check.bars = bars;
    return e;
}

inline Event makeBrightnessLoadedEvent(uint8_t percent) {
    Event e{};
    e.type = EventType::BrightnessLoaded;
    e.payload.brightness_loaded.percent = percent;
    return e;
}

inline Event makeAudioCommandRejectedEvent(CmdId cmd_id, PlaybackFailReason reason) {
    Event e{};
    e.type = EventType::AudioCommandRejected;
    e.payload.cmd_rejected.cmd_id = cmd_id;
    e.payload.cmd_rejected.reason = reason;
    return e;
}

inline Event makeMappingsLoadedEvent(uint16_t total_track_count) {
    Event e{};
    e.type = EventType::MappingsLoaded;
    e.payload.mappings_loaded.total_track_count = total_track_count;
    return e;
}

inline Event makeVolumeLoadedEvent(uint8_t percent) {
    Event e{};
    e.type = EventType::VolumeLoaded;
    e.payload.volume_loaded.percent = percent;
    return e;
}
