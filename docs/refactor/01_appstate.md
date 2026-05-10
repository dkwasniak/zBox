# Part 2/11: AppState Design

← Prev: [00_overview.md](00_overview.md) | → Next: [02_events_effects.md](02_events_effects.md)

---

## AppState

`AppState` contains only domain state and UI-selection state needed for deterministic policy.
No heap allocation, no `String`, no `std::optional`, no hardware handles, no SD paths.

```cpp
struct AppState {
    // --- Mode substates ---
    SessionMode   session_mode;           // 1 byte
    PlaybackMode  playback_mode;          // 1 byte
    AudioState    audio_state;            // 1 byte
    BtState       bt_state;              // 1 byte
    SleepState    sleep_state;           // 1 byte
    BootState     boot_state;            // 1 byte
    RequestedSleepKind requested_sleep_kind; // 1 byte

    // --- Context ---
    PendingPlayback pending_playback;    // 27 bytes (see below)
    CurrentTrack    current_track;       // 3 bytes
    char last_nfc_uid[24];              // 24 bytes
    uint8_t  night_light_brightness_percent; // 1 byte
    uint8_t  music_volume_percent;       // 1 byte — current BT volume (0–100).
                                         // Loaded from NVS at boot via VolumeLoaded event.
                                         // Used by reducer to fill SetVolume effect payload.
    bool     bt_volume_applied;          // 1 byte
    bool     sync_active;               // 1 byte
    bool     diagnostic_mode;           // 1 byte
    uint8_t  battery_bars;              // 1 byte
    uint8_t  volume_overlay_level_percent; // 1 byte
    bool     battery_preview_active;    // 1 byte
    uint16_t total_track_count;         // 2 bytes — loaded from music library at boot,
                                        // set via MappingsLoaded event payload.
                                        // 0 = music library not yet loaded / unavailable.
                                        // Used by reducer for NextTrack/PrevTrack wraparound.
    uint16_t sync_progress_current;     // 2 bytes — 0 if no sync in progress
    uint16_t sync_progress_total;       // 2 bytes — 0 if no sync in progress

    // --- Deadlines (see Deadline Convention below) ---
    uint32_t idle_deadline_ms;          // 4 bytes
    uint32_t night_light_deadline_ms;   // 4 bytes
    uint32_t volume_overlay_deadline_ms; // 4 bytes
    uint32_t battery_preview_deadline_ms; // 4 bytes
    uint32_t jbl_recovery_deadline_ms;  // 4 bytes
    uint32_t bt_reconnect_deadline_ms;  // 4 bytes
    uint32_t brightness_save_deadline_ms; // 4 bytes
};

static_assert(sizeof(AppState) <= 128, "AppState too large for stack copy");
// Updated estimated sizeof: ~109–113 bytes (with alignment padding, +1 byte for music_volume_percent) — safe for 8192B dispatcher stack.
// Confirm with: Serial.printf("sizeof(AppState)=%u\n", sizeof(AppState)); in Stage 0 boot.
// Note: PendingPlayback layout (1+24+2) has 1-byte alignment pad before track_index → 28 bytes,
// not 27 as the struct comment suggests.
```

### Substate Enums

```cpp
enum class SessionMode       : uint8_t { Normal, NightLight };
enum class PlaybackMode      : uint8_t { Nfc, Music };
enum class AudioState        : uint8_t {
    Idle, StartingFile, StartingSystemSound,
    PlayingFile, PlayingSystemSound, Paused, Stopping
};
enum class BtState           : uint8_t {
    Unknown,                    // valid only during boot (see Enum Invariants)
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
```

---

## PendingPlayback and CurrentTrack

```cpp
enum class PendingPlaybackKind : uint8_t {
    None,
    NfcUid,
    MusicCurrentTrack,
    MusicSpecificTrack
};

struct PendingPlayback {
    PendingPlaybackKind kind;   // 1 byte
    char uid[24];               // 24 bytes (see NFC UID sizing below)
    uint16_t track_index;       // 2 bytes
};                              // total: 27 bytes

struct CurrentTrack {
    bool     valid;             // 1 byte
    uint16_t index;             // 2 bytes
};                              // total: 3 bytes
```

Rules:
- `CurrentTrack` is the only domain carrier of current music-track identity.
- `PendingPlayback` is the only domain carrier of deferred playback intent.
- Reducer never stores resolved file paths.

---

## NFC UID Sizing

`last_nfc_uid[24]` and `PendingPlayback::uid[24]`:

PN532 maximum UID length = 7 bytes (ISO14443A Type B / NFC-B). [Verified in current code:
`nfc_module.cpp` uses `char uid[30]` with a 3-char-per-byte hex+colon format.]

Format: `"AA:BB:CC:DD:EE:FF:GG"` = 7 × 3 chars − 1 trailing colon + null = 21 chars.
Buffer of 24 provides 3 bytes margin. Rule: never write more than 21 chars + null.

```cpp
static_assert(sizeof(PendingPlayback::uid) >= 22, "NFC UID buffer too small");
```

---

## Deadline Convention

All `*_deadline_ms` fields store **absolute `millis()` timestamps** from device boot.

- `0` is the sentinel meaning "no active deadline". Never assign `0` as a real timestamp.
  (Probability of `millis()` returning exactly 0 after boot is negligible; if needed, use 1.)
- `millis()` wraps after ~49.7 days. Device auto-sleeps after 10 minutes of idle. Wraparound
  cannot occur during normal operation.
- Exception: `diagnostic_mode` can prevent auto-sleep. In that case, the watchdog (15s)
  remains active independently of deadline checks.
- Dispatcher checks: `deadline != 0 && millis() >= deadline`.
- Reducer sets deadlines to `now + interval`. Dispatcher passes `now` as a parameter so
  reducer remains pure (no direct `millis()` calls inside reducer).

---

## Enum Invariants

### RequestedSleepKind::None
`None` is a valid steady-state value meaning "no pending sleep request." The reducer does not
need a separate representation. Invariant:

> `requested_sleep_kind == None` if and only if `sleep_state == Awake`.

After `SleepRequested` event, `requested_sleep_kind` becomes `Normal|Emergency|NightLightTimeout`
and stays set until the device enters deep sleep.

### BtState::Unknown
Valid only during early boot, before BT adapter is initialized.

> `BtState::Unknown` is allowed only when `boot_state ∈ {ColdBootInit, WakeHoldCheck}`.

After `BootInitCompleted` event, BT must transition to `WaitingForSpeaker` (normal mode)
or `Disabled` (night-light mode, which skips BT). Reducer must assert this invariant.

### bt_volume_applied
`bt_volume_applied == true` iff:
  - `bt_state == Connected`, AND
  - a `SetVolume` effect has been executed since the last BT connection event.

This flag resets to `false` on every `BtConnected` event (new connection → volume not yet set).
Kept as a `bool` rather than a BtState substate to avoid complicating the enum.

---

## Failure Reason Enums

All `*Failed` feedback events carry a reason enum. Defined here, used in adapter contracts.

```cpp
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

enum class PersistenceFailReason : uint8_t {
    NvsError
};
```

---

## Rules Summary

- No heap allocation in AppState or its members.
- No `String`, `std::string`, `std::optional`, `std::vector`.
- No hardware handles (QueueHandle_t, TaskHandle_t) in AppState.
- No SD file paths in AppState.
- Short-lived UI state that affects LED scene selection (volume overlay, battery bars) belongs
  in AppState.
- Beat-energy modulation does NOT belong in AppState (see [07_led_model.md](07_led_model.md)).
