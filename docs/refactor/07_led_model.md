# Part 8/11: LED Model

← Prev: [06_sleep_wake.md](06_sleep_wake.md) | → Next: [08_dispatcher_runtime.md](08_dispatcher_runtime.md)

---

## LED Scene Ownership

Reducer and dispatcher own scene selection. LED module owns rendering.

The dispatcher calls `deriveLedScene(AppState)` after every committed state transition
and passes the result to the LED executor. No other code path may change the LED scene
except via this function.

```cpp
enum class LedSceneType : uint8_t {
    Off,
    BootProgress,
    WakeProgress,       // pre-boot, managed directly by handleWakeFromDeepSleep()
    WaitBt,
    Idle,
    Playing,
    NightLight,
    VolumeOverlay,
    SleepReady,
    BatteryPreview,
    ModeChange,
    WarningFlash,
    SyncWifi,
    SyncProgress,
    Diagnostic
};

struct LedSceneParams {
    LedSceneType type;
    union {
        struct { uint8_t step; uint8_t total; } boot_progress;
        struct { uint8_t percent; } night_light;
        struct { uint8_t percent; } volume;
        struct { uint8_t bars; } battery;
        struct { uint16_t current; uint16_t total; } sync_progress;
    } params;
};
```

---

## deriveLedScene

```cpp
LedSceneParams deriveLedScene(const AppState& s);
```

Scene derivation priority (highest wins):

| Priority | Condition | Scene |
|----------|-----------|-------|
| 1 | `sleep_state ∈ {PreparingDeepSleep, WaitingPowerOffSound, WaitingBtShutdown, ReadyToSleep}` | SleepReady |
| 2 | `diagnostic_mode` | Diagnostic |
| 3 | `sync_active && sync_progress.total > 0` | SyncProgress |
| 3b | `sync_active && sync_progress.total == 0` | SyncWifi (waiting, no progress yet) |
| 4 | `session_mode == NightLight` | NightLight |
| 5 | `boot_state != Ready` | BootProgress |
| 6 | `bt_state == WaitingForSpeaker \|\| bt_state == RecoveryPulsePending` | WaitBt |
| 7 | `battery_preview_active` | BatteryPreview |
| 8 | `volume_overlay_deadline_ms != 0 && !expired` | VolumeOverlay |
| 9 | `audio_state ∈ {PlayingFile, StartingFile}` | Playing |
| 10 | `audio_state == Paused` | Idle |
| 11 | default | Idle |

Note: `WakeProgress` is not in this table — it is managed directly by `handleWakeFromDeepSleep()`
before FreeRTOS starts and is never returned by `deriveLedScene()`.

### Short-lived overlay scenes

`VolumeOverlay` and `BatteryPreview` are time-limited overlays. They are modeled as
`AppState` fields with deadlines:

- `volume_overlay_deadline_ms`: set by reducer when volume changes, cleared on expiry.
- `battery_bars` + a `BatteryPreviewActive` flag in AppState: set when BTN_A hold triggers
  battery check.

The dispatcher posts `VolumeOverlayExpired` when `volume_overlay_deadline_ms` is reached,
and the reducer clears the overlay.

### One-shot executor animations

`ModeChange`, `WarningFlash`, `BootProgress` steps are one-shot animations:
- Triggered by specific events (mode toggle, warning condition, boot step).
- LED executor runs the animation once, then returns to the durable scene.
- These are NOT persisted in `AppState` — they are effect-triggered behaviors.

---

## LED Coalescing

The dispatcher calls `deriveLedScene()` after every state transition. Under burst events,
this may produce rapid scene changes.

Rule: if the derived `LedSceneType` AND all params are identical to the current scene,
the LED executor does NOT restart the animation. If type is the same but params changed
(e.g., `VolumeOverlay` level 40% → 60%, `NightLight` brightness 50% → 60%), the executor
updates params without restarting the animation from the beginning.

```cpp
// In LED executor (called by dispatcher after every transition):
if (derived.type != currentLedScene.type) {
    // Scene type changed: start new animation from scratch
    ledExecutorSetScene(derived);
    currentLedScene = derived;
} else if (memcmp(&derived.params, &currentLedScene.params, sizeof(derived.params)) != 0) {
    // Same type, different params: update params in-place, no animation restart
    ledExecutorUpdateParams(derived);
    currentLedScene = derived;
}
// If type AND params identical: no-op (coalesced)
```

Coalescing is in the LED executor, not the dispatcher. The dispatcher always calls
`deriveLedScene()` — the executor decides whether to re-render.

---

## Beat Globals Contract

The LED task reads two globals updated by the audio task for beat-reactive animations.
This is the only explicitly permitted cross-module shared mutable state.

```cpp
volatile uint8_t g_audioEnergy;   // 0–255, instantaneous audio level
volatile bool    g_beatDetected;   // true for one LED frame when a beat is detected
```

**Write context:** `audioTaskFunc` (core 1, priority 2). Written continuously during playback.

**Read context:** `ledTaskFunc` (core 1, priority 1). Read once per animation frame (5ms).

**Synchronization analysis:**
- Both tasks run on core 1. Audio task has higher priority — it preempts LED task.
- `uint8_t` read on ESP32 (Xtensa LX6, 32-bit bus) is naturally atomic for aligned access.
- `bool` read is likewise naturally atomic.
- No mutex is needed. The LED task reads a snapshot at frame start.

**Invariant:** LED task reads `g_audioEnergy` and `g_beatDetected` exactly once at the
beginning of each animation frame render, not mid-frame. This prevents partial-update
artifacts even without a mutex.

**If tasks move to different cores in future:** this contract must be revisited and a
proper memory barrier or mutex added. [Must validate on hardware if core affinity changes]

---

## LED Task Architecture

[Verified in current code: `esp32/src/leds.cpp`]

- Separate FreeRTOS task, core 1, priority 1, stack 4096 bytes.
- 5ms loop (200Hz max frame rate).
- Can be suspended (`ledSuspendTask()`) during NFC SPI critical sections to prevent
  WS2812B timing jitter.
- Pre-init path: `ledPreInitHardware()` runs before FreeRTOS during wake-hold sequence.
  Must be safe to call before `ledInit()`.

---

## LED Behaviors Classification

Every LED behavior is classified as one of three types:

**State-derived durable scenes** (from `deriveLedScene(AppState)`):
- WaitBt, Idle, Playing, NightLight, Diagnostic, SleepReady, SyncWifi

**Short-lived state-derived overlays** (from AppState fields with deadlines):
- VolumeOverlay (`volume_overlay_deadline_ms`)
- BatteryPreview (`battery_bars` + preview flag + deadline)

**One-shot executor animations** (triggered by effects, not persisted in AppState):
- BootProgress (updated per boot step via `ledSetBootProgress(step)` calls)
- WakeProgress (pre-boot, in `handleWakeFromDeepSleep()`)
- ModeChange flash (triggered by `PlaySystemSound` effect during mode toggle)
- WarningFlash (triggered by specific failure events)

After migration, no LED behavior may exist outside these three categories.

---

## Prohibited LED Access Patterns (Post-Migration)

After Stage 6 migration is complete, the following are forbidden:

```cpp
// FORBIDDEN — direct LED policy calls from non-LED modules:
ledSetPlaying();     // must come from deriveLedScene() → Playing
ledSetIdle();        // must come from deriveLedScene() → Idle
ledSetWaitBt();      // must come from deriveLedScene() → WaitBt

// FORBIDDEN — reading LED state from outside LED module:
extern LedMode ledMode;

// ALLOWED — effects that trigger one-shot animations:
fx.add(Effect{EffectType::PlaySystemSound, ...});  // executor may trigger ModeChange flash
```
