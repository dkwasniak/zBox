# Part 4/11: Reducer Contract

← Prev: [02_events_effects.md](02_events_effects.md) | → Next: [04_audio_adapter.md](04_audio_adapter.md)

---

## ReduceResult

```cpp
struct ReduceResult {
    AppState next_state;
    std::array<Effect, MAX_EFFECTS> effects;
    uint8_t effect_count;
};

ReduceResult reduce(const AppState& state, const Event& event, uint32_t now_ms);
```

`now_ms` is passed in by the dispatcher so the reducer can compute deadline timestamps
(`deadline = now_ms + interval`) without calling `millis()` directly. This keeps the
reducer pure and testable without mocking time.

---

## Reducer Rules

The reducer function must obey all of the following:

**No hardware includes:**
```cpp
// FORBIDDEN in reducer.cpp / reducer.h:
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <esp_bt.h>
#include <SD.h>
#include <Preferences.h>
```

**No side effects:**
- No blocking calls
- No LED writes
- No direct driver calls
- No global variable mutations outside `ReduceResult`
- No dynamic memory allocation (`new`, `malloc`, `String`, `std::vector`)

**No optimism:**
- Reducer does not assume async effects succeed
- Every async effect must have a corresponding failure path in the reducer

**Determinism:**
- Same `(state, event, now_ms)` inputs always produce the same `ReduceResult`
- No dependency on hidden mutable state

---

## EffectBuilder

Used inside the reducer to accumulate effects with budget enforcement:

```cpp
class EffectBuilder {
public:
    void add(const Effect& e) {
        MUSICBOX_ASSERT(count < MAX_EFFECTS, "effect budget overflow");
        effects[count++] = e;
    }
    uint8_t count = 0;
    std::array<Effect, MAX_EFFECTS> effects{};
};
```

Usage pattern in reducer:

```cpp
ReduceResult reduce(const AppState& s, const Event& ev, uint32_t now_ms) {
    AppState next = s;
    EffectBuilder fx;

    switch (ev.type) {
        case EventType::BtConnected:
            next.bt_state = BtState::Connected;
            next.bt_volume_applied = false;
            fx.add(makeSetVolumeEffect(last_volume));
            if (s.pending_playback.kind == PendingPlaybackKind::NfcUid) {
                CmdId id = /* passed from dispatcher context */;
                fx.add(makeStartNfcPlaybackEffect(s.pending_playback.uid, id));
                next.audio_state = AudioState::StartingFile;
            }
            break;
        // ...
    }

    return { next, fx.effects, fx.count };
}
```

Note: `CmdId` generation happens in the dispatcher, not the reducer. The committed approach:

1. Reducer emits effects with `cmd_id = 0` (placeholder — reducer has no counter).
2. Dispatcher iterates `result.effects[]`. For each effect that expects feedback,
   it calls `cmd_id = nextCmdId()` and writes the value into `effect.payload.*cmd_id`
   before calling the adapter function.
3. The adapter receives the filled `cmd_id` and passes it through to the hardware driver.

This keeps the reducer signature clean (`reduce(state, event, now_ms)` — no cmd_id context)
and is consistent with what `10a_test_native.md` assumes: cmd_id in expected effects is
verified as non-zero, but exact value is not asserted (dispatcher fills it post-reduce).

---

## MUSICBOX_ASSERT

Dual-path assertion macro:

```cpp
#ifdef NATIVE_BUILD
    #define MUSICBOX_ASSERT(cond, msg) assert(cond)
#else
    #define MUSICBOX_ASSERT(cond, msg) \
        do { \
            if (!(cond)) { \
                musicboxAssertFail(__FILE__, __LINE__, msg); \
            } \
        } while(0)
#endif

void musicboxAssertFail(const char* file, int line, const char* msg);
```

`musicboxAssertFail` implementation on firmware:

```cpp
void musicboxAssertFail(const char* file, int line, const char* msg) {
    LOGC("[ASSERT] %s:%d — %s\n", file, line, msg);
    // Persist to RTC memory for post-boot crash report
    rtcSaveAssertContext(file, line, msg);
    // DO NOT restart here if called from sleep executor path (see Assert Safety below)
    if (!gInSleepExecutorPath) {
        esp_restart();
    }
    // In sleep executor path: log and continue — hardware safety takes priority
}
```

The assert must log:
- Current state summary (session_mode, boot_state, audio_state, sleep_state as integers)
- Triggering event type
- Source module (file + line)
- Failed assertion message

---

## Assert Safety in Sleep Executor Path

`MUSICBOX_ASSERT` behavior depends on context:

**Outside sleep executor path (normal operation):**
- Log CRIT entry
- Persist crash context to RTC memory
- Call `esp_restart()`

**Inside sleep executor path (Steps 1–9 of sleep sequence):**
- Log CRIT entry
- Persist crash context to RTC memory
- **Do NOT restart** — continue the sleep sequence
- Rationale: if BT controller is half-disabled or JBL is in an indeterminate state,
  a restart is more dangerous than completing the sleep sequence.
- At next boot, `setup()` checks RTC crash flag and logs a warning.

The sleep executor sets `gInSleepExecutorPath = true` before Step 1 and `false` after
`esp_deep_sleep_start()` is called (effectively never, since deep sleep is entered).

```cpp
extern bool gInSleepExecutorPath;  // initialized to false at boot
```

---

## State Transition Invariants (Reducer Enforcement)

The reducer must assert these invariants after every transition:

1. `requested_sleep_kind == None` iff `sleep_state == Awake`
2. `BtState::Unknown` only if `boot_state ∈ {ColdBootInit, WakeHoldCheck}`
3. `audio_state ∈ {PlayingFile, PlayingSystemSound, Paused}` → `bt_state == Connected`
4. `sleep_state != Awake` → ignore `TrackEnded`, `NfcTagDetected`, play/pause inputs
5. `session_mode == NightLight` → `bt_state == Disabled` (BT is not used in night-light)

Violation of any invariant: MUSICBOX_ASSERT fires with the invariant description as message.

---

## What the Reducer Does NOT Own

These remain in executors or adapters:

- Hardware protocol ordering (sleep sequences, BT shutdown steps)
- FreeRTOS queue management
- LED rendering
- SD path resolution
- Bluetooth AVRCP commands
- NVS/Preferences reads and writes
- Timer creation and deletion
- Watchdog reset
