# Part 3/11: Events and Effects

← Prev: [01_appstate.md](01_appstate.md) | → Next: [03_reducer.md](03_reducer.md)

---

## Event Hierarchy

Events are inputs to the reducer. Every state transition is triggered by an event.

```cpp
enum class EventType : uint8_t {
    // Boot
    BootStarted,
    WakeCauseResolvedNormal,
    WakeCauseResolvedNightLight,
    BootInitCompleted,
    MappingsLoaded,              // carries: uint16_t total_track_count
    VolumeLoaded,                // carries: uint8_t percent — posted after loadBtVolume() during boot
    BtInitStarted,
    NfcPreScanCompleted,        // carries: bool uid_found, char uid[24]

    // BT
    BtConnected,
    BtDisconnected,
    BtRecoveryPulseCompleted,
    BtRecoveryPulseFailed,      // carries: BtFailReason
    BtDiscoveryRestarted,
    BtDiscoveryRestartFailed,   // carries: BtFailReason
    BtShutdownCompleted,
    BtShutdownFailed,           // carries: BtFailReason

    // Audio
    AudioCommandRejected,       // carries: CmdId, PlaybackFailReason
    NfcPlaybackStarted,         // carries: char uid[24], CmdId
    NfcPlaybackStartFailed,     // carries: char uid[24], PlaybackFailReason, CmdId
    MusicTrackStarted,          // carries: uint16_t index, CmdId
    MusicTrackStartFailed,      // carries: uint16_t index, PlaybackFailReason, CmdId
    AudioStopped,               // carries: CmdId
    SystemSoundCompleted,       // carries: uint8_t sound_id, CmdId
    SystemSoundFailed,          // carries: uint8_t sound_id, SoundFailReason, CmdId
    TrackEnded,                 // natural completion only (never on stop/pause)

    // NFC
    NfcTagDetected,             // carries: char uid[24]
    NfcTagRemoved,

    // Buttons / user input
    SleepRequested,             // carries: RequestedSleepKind
    PlayPausePressed,
    NextTrackPressed,
    PrevTrackPressed,
    VolumeUpPressed,
    VolumeDownPressed,
    ModeToggleRequested,
    BatteryCheckRequested,
    DiagnosticEntryRequested,

    // Timers (posted by dispatcher when deadline expires)
    IdleTimeoutFired,
    NightLightTimeoutFired,
    VolumeOverlayExpired,
    BatteryPreviewExpired,
    JblRecoveryTimeoutFired,
    BtReconnectTimeoutFired,
    BrightnessSaveDeadlineFired,

    // Persistence feedback
    BrightnessPersisted,
    BrightnessPersistFailed,    // carries: PersistenceFailReason
    PlaybackModePersisted,
    PlaybackModePersistFailed,  // carries: PersistenceFailReason

    // Sync / diagnostic
    SyncStarted,
    SyncCompleted,
    SyncFailed,
    DiagnosticModeEntered,
};
```

### Event Drop Policy

Events are classified by behavior when the queue is full:

| Class | Drop behavior | Examples |
|-------|--------------|---------|
| `Critical` | Must never drop — queue full is a contract failure | SleepRequested, BtShutdownCompleted |
| `Coalescible` | Replace equivalent pending event in queue | IdleTimeoutFired, VolumeOverlayExpired |
| `EdgeTriggered` | Deduplicate: drop if same event already pending | NfcTagDetected, BtConnected |
| `Telemetry` | May drop silently | DiagnosticModeEntered |

Rules:
- Queue overflow on `Critical` must trigger a CRIT log and watchdog-induced restart.
- Queue overflow on noncritical classes follows coalescing/dedup — never silent on Critical path.
- Event posting helper captures event type and source for diagnostics.

---

## Event Payload

Events that carry data use a tagged union:

```cpp
struct Event {
    EventType type;
    union {
        struct { bool uid_found; char uid[24]; } nfc_prescan;
        struct { char uid[24]; CmdId cmd_id; } playback_started;
        struct { char uid[24]; PlaybackFailReason reason; CmdId cmd_id; } nfc_fail;
        struct { uint16_t index; CmdId cmd_id; } track_started;
        struct { uint16_t index; PlaybackFailReason reason; CmdId cmd_id; } track_fail;
        struct { CmdId cmd_id; } audio_stopped;
        struct { uint8_t sound_id; CmdId cmd_id; } sound_completed;
        struct { uint8_t sound_id; SoundFailReason reason; CmdId cmd_id; } sound_failed;
        struct { char uid[24]; } nfc_detected;
        struct { RequestedSleepKind kind; } sleep_requested;
        struct { CmdId cmd_id; PlaybackFailReason reason; } cmd_rejected;
        struct { BtFailReason reason; } bt_fail;
        struct { PersistenceFailReason reason; } persist_fail;
    } payload;
};
```

`sizeof(Event)` is dominated by the `uid[24]` field in the union. Estimate: ~32 bytes.

---

## Effect Types

Effects are intents emitted by the reducer. Executors map intents to hardware protocols.
Reducer must not call hardware APIs directly.

```cpp
enum class EffectType : uint8_t {
    // Audio
    StartNfcPlaybackByUid,       // carries: char uid[24], CmdId
    StartMusicTrackByIndex,      // carries: uint16_t index, CmdId
    StopAudio,                   // carries: CmdId
    PauseAudio,                  // carries: CmdId
    ResumeAudio,                 // carries: CmdId
    PlaySystemSound,             // carries: uint8_t sound_id, CmdId

    // Volume
    SetVolume,                   // carries: uint8_t level_percent

    // Persistence
    PersistBrightness,           // carries: uint8_t level_percent
    PersistPlaybackMode,         // carries: PlaybackMode

    // BT
    TriggerBtRecoveryPulse,
    TriggerBtDiscoveryRestart,
    ShutdownBt,

    // Sleep
    // PrepareDeepSleep — REMOVED. Not used in state machine. Sleep preparation is driven
    // by the reducer via StopAudio + PlaySystemSound + ShutdownBt effects sequenced through
    // the WaitingPowerOffSound / WaitingBtShutdown states. No separate "prepare" effect needed.
    EnterDeepSleep,              // carries: RequestedSleepKind (for sequencing)

    // Diagnostic
    LogDiagnostic,               // carries: uint8_t code
};

struct Effect {
    EffectType type;
    union {
        struct { char uid[24]; CmdId cmd_id; } nfc_playback;
        struct { uint16_t index; CmdId cmd_id; } music_track;
        struct { CmdId cmd_id; } audio_control;
        struct { uint8_t sound_id; CmdId cmd_id; } system_sound;
        struct { uint8_t level_percent; } volume;
        struct { uint8_t level_percent; } brightness;
        struct { PlaybackMode mode; } playback_mode;
        struct { RequestedSleepKind kind; } deep_sleep;
        struct { uint8_t code; } diagnostic;
    } payload;
};
```

---

## Effect Budget Analysis

The reducer returns at most `MAX_EFFECTS = 8` effects per call. Justification:

| Triggering event | Effects emitted | Count |
|-----------------|----------------|-------|
| `SleepRequested(Normal)` | StopAudio + LogDiagnostic | 2 |
| `BtConnected` (with pending NFC) | StartNfcPlaybackByUid + SetVolume + PersistPlaybackMode + LogDiagnostic | 4 |
| `BtConnected` (with pending music) | StartMusicTrackByIndex + SetVolume + PersistPlaybackMode + LogDiagnostic | 4 |
| `SystemSoundCompleted` → normal sleep ready | ShutdownBt + LogDiagnostic | 2 |
| `BtShutdownCompleted` → ready to sleep | EnterDeepSleep + LogDiagnostic | 2 |
| `ModeToggleRequested` | PlaySystemSound(mode_change) + PersistPlaybackMode + SetVolume + LogDiagnostic | 4 |
| Theoretical worst-case | Not found to exceed 6 in exhaustive analysis | **6** |

Budget of 8 provides a margin of 2. If a new event path produces > 6 effects, it must be
documented here before implementation and the budget increased if needed.

```cpp
// EffectBuilder enforces the budget with assertion:
void EffectBuilder::add(const Effect& e) {
    MUSICBOX_ASSERT(count < MAX_EFFECTS, "effect budget overflow");
    effects[count++] = e;
}
static_assert(MAX_EFFECTS == 8, "update budget analysis in 02_events_effects.md if changed");
```

---

## CmdId — Command Correlation

Every async effect that expects a feedback event carries a `CmdId` to correlate the
feedback to the issued command.

```cpp
using CmdId = uint16_t;

// Generated by dispatcher, passed to executor, returned in feedback event.
// 0 is reserved as "no correlation / not tracked".
CmdId nextCmdId() {
    static uint16_t counter = 0;
    if (++counter == 0) counter = 1;
    return counter;
}
```

Lifecycle:
1. `reduce()` returns effects with `cmd_id = 0` (placeholder — reducer has no counter).
2. Dispatcher iterates `result.effects[]`. For each effect that expects feedback,
   it calls `cmd_id = nextCmdId()` and writes it into `effect.payload.*cmd_id`.
3. Dispatcher registers the effect in `pendingEffects[]` with the generated `cmd_id`.
4. Dispatcher calls the adapter function, passing the now-filled `cmd_id`.
5. Executor passes `cmd_id` through to the hardware driver / audio task.
6. On completion or failure, executor posts an Event carrying the same `cmd_id`.
7. Dispatcher matches incoming `cmd_id` to `pendingEffects[]` (see §Stale Feedback Policy
   in [08_dispatcher_runtime.md](08_dispatcher_runtime.md)).
8. Timeout: if no feedback arrives within the window, dispatcher posts a synthetic failure event.

The reducer never calls `nextCmdId()`. It never reads or depends on `cmd_id` values.
In native tests, `cmd_id` in expected effects is verified as non-zero but exact value
is not asserted (dispatcher fills it post-reduce).

`CmdId` overflow wraps from 65535 → 1 (never 0). Outstanding commands at overflow are
unlikely given command latency of < 5s and 16-bit range.
