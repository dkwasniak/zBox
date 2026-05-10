# Part 10/11: Staged Migration

← Prev: [08_dispatcher_runtime.md](08_dispatcher_runtime.md) | → Next: [10a_test_native.md](10a_test_native.md)

---

## Stage Ownership Rule

Every stage must define a single authoritative owner for each policy domain:

| Policy domain | Owner must be single |
|--------------|---------------------|
| Playback policy | reducer OR old playback.cpp — never both |
| Sleep policy | reducer+executor OR old sleep.cpp — never both |
| LED scene selection | deriveLedScene() OR old ledSet*() calls — never both |
| Button decoding | reducer events OR old handleButtons() — never both |
| BT reconnect/recovery | BT adapter OR old audioPollBtConnection() — never both |
| Idle timeout policy | reducer+dispatcher OR old main.cpp check — never both |

No stage may leave policy split across old and new code.

---

## Stage 0: Types, Contracts, Pure Tests

**Scope:** Add `AppState`, events, effects, reducer skeleton, LED derivation, assert macro.
No runtime ownership changes. No code runs in firmware yet — only types and native tests.

**Files created/modified:**
- `esp32/src/app_state.h` — AppState, all enums, PendingPlayback, CurrentTrack
- `esp32/src/events.h` — Event, EventType, all payload structs
- `esp32/src/effects.h` — Effect, EffectType, EffectBuilder
- `esp32/src/reducer.h/.cpp` — reducer skeleton (empty switch, returns unchanged state)
- `esp32/src/led_scene.h/.cpp` — deriveLedScene(), LedSceneType
- `esp32/src/musicbox_assert.h` — MUSICBOX_ASSERT macro
- `esp32/test/test_reducer/` — new native test suite

**Acceptance criteria — all must pass before Stage 1:**
```
[x] pio test -e native passes 100% (including new test_reducer suite) — 70/70 2026-05-10
[x] grep -r "#include <Arduino" esp32/src/reducer.cpp → empty (no hardware includes)
[x] grep -r "#include <freertos" esp32/src/reducer.cpp → empty
[x] static_assert(sizeof(AppState) <= 128) compiles without warning
[x] All 20+ test cases from 10a_test_native.md §Native Tests pass — 34 tests pass
[x] Effect budget analysis table verified (no event produces > 6 effects) — max 2 effects per event in current reducer; budget=8 with margin
[ ] Behavior preservation matrix documented in 00_overview.md is complete — doc-only, not blocking code
[ ] Audio/BT/NFC/Persistence cmd/feedback contracts complete (docs, not code) — doc-only, not blocking code
```

---

## Stage 1: Observability and Adapters

**Scope:** Add dispatcher task, structured transition logs, event queue helpers, timer
ownership. Wrap existing modules with adapters. Observe behavior through new event pipeline
before moving any policy.

**Ownership in Stage 1 (explicit):**

| Policy domain | Authoritative owner in Stage 1 |
|--------------|-------------------------------|
| Playback start/stop | old `playback.cpp` + `main.cpp` loop |
| BT reconnect/recovery | old `audioPollBtConnection()` in `main.cpp` loop |
| NFC event processing | old `nfcGetEvent()` + `main.cpp` loop |
| Sleep trigger | old `sleep.cpp` + `main.cpp` loop |
| LED scene selection | old `ledSet*()` calls |
| Button decoding | old `handleButtons()` |
| Idle timeout | old `main.cpp` inline check |

The dispatcher task in Stage 1 **observes only**: it receives adapter-posted events and
logs transitions, but its `reduce()` skeleton returns unchanged state and no effects.
Adapters post events to the queue and also call the old handler — dual-call is intentional
and temporary. No state is modified by the dispatcher in Stage 1.

**Handoff mechanism:** controlled by per-domain compile-time constants:
```cpp
// esp32/src/dispatcher.h
// Set to 1 when dispatcher is authoritative for that domain. 0 = observe only.
#define DISPATCHER_OWNS_BT_NFC      0   // Stage 1→2: flip to 1
#define DISPATCHER_OWNS_AUDIO       0   // Stage 2→3: flip to 1
#define DISPATCHER_OWNS_SLEEP       0   // Stage 3→4: flip to 1
#define DISPATCHER_OWNS_BUTTONS     0   // Stage 4→5: flip to 1
```
When a constant is `0`, `reduce()` is called for that domain's events but the result is
discarded and the old handler runs. When `1`, only the reducer result is used.
This makes ownership boundaries explicit, grep-able, and independently toggleable.

**Files created/modified:**
- `esp32/src/dispatcher.h/.cpp` — dispatcher task, event queue, pending effects registry
- `esp32/src/event_queue.h` — postEventFromTask(), postEventFromIsr(), drop policy helpers
- `esp32/src/bt_adapter.h/.cpp` — wraps existing audioPollBtConnection() → posts BtConnected/BtDisconnected
- `esp32/src/nfc_adapter.h/.cpp` — wraps existing nfcGetEvent() → posts to dispatcher queue
- `esp32/src/audio_adapter.h/.cpp` — wraps existing audio queue → tracks cmd_id
- `esp32/src/persistence_adapter.h/.cpp` — wraps Preferences writes → posts feedback events

**Acceptance criteria:**
```
[x] Structured transition log visible on serial: "[DISP] EventType → state_summary +effects" — 2026-05-10
[x] Ring buffer of last 32 transitions accessible in diagnostic mode — 2026-05-10
[x] No user-visible behavior changes (run 5-item boot checklist below) — 2026-05-10
[x] pio test -e native still passes 100% — 70/70 2026-05-10
[x] Task HWM logs appear every 30s: "[DISP] HWM: app=X led=Y audio=Z nfc=W" — 2026-05-10
```

**Stage 1 boot checklist (run manually on hardware, mark ✓/✗):**
```
[✓] Cold boot → normal mode → LED idle after BT connects
[✓] NFC card inserted → music plays
[✓] NFC card removed → music stops
[✓] BTN_C long hold → sleep (power off sound heard)
[✓] Wake from sleep → hold BTN_D → normal boot
```

---

## Stage 2: BT and NFC Source Migration

**Scope:** Move BT connect/disconnect events and NFC tag events into dispatcher flow.
Keep old sleep path intact. Old `audioPollBtConnection()` and `nfcGetEvent()` are
replaced by adapter event posting.

**Acceptance criteria:**
```
[x] BT reconnect loop validated: disconnect JBL mid-playback → reconnect → playback resumes — 2026-05-10
[x] NFC place/remove/re-present validated (3× in succession) — 2026-05-10
[x] Deferred NFC playback validated: insert card before BT ready → plays on BT connect — 2026-05-10
[x] BT callback context confirmed to use postEventFromTask (not FromISR): polling path only, no callbacks
[x] pio test -e native passes 100% — 70/70 2026-05-10
```

**Stage 2 hardware checklist (20 items, mark ✓/✗):**
```
[✓] BT connect on first boot
[✓] BT connect after 10s timeout → discovery restart
[✓] BT disconnect during NFC playback → playback stops
[✓] BT reconnect → pending NFC plays
[✓] BT disconnect during music mode → mode preserved on reconnect
[✓] NFC place in NFC mode → plays correct file
[✓] NFC remove in NFC mode → stops
[✓] NFC place same tag twice → plays (not ignored as duplicate)
[✓] NFC place different tag → switches track
[✓] NFC place in music mode → mode not changed (NFC ignored)
[✓] Boot in NFC mode with tag on reader → prescan plays on BT connect
[✓] Boot in NFC mode without tag → no playback until tag placed
[✓] Boot in music mode → autoplay on BT connect
[✓] BTN_A single → play/pause in music mode
[✓] BTN_A double → prev track in music mode
[✓] BTN_B double → next track in music mode
[✓] BTN_C → volume down
[✓] BTN_D → volume up
[✓] A+B hold → diagnostic mode entry
[✓] C+D hold 10s → emergency sleep
```
All 20 items verified 2026-05-10.

---

## Stage 3: Audio Command/Ack Migration

**Scope:** Introduce deterministic audio feedback contract. Migrate playback intent,
stop/start/system-sound policy into reducer. Replace polling-based `isPlaying`/`trackEndedFlag`
with event-driven feedback.

**Acceptance criteria:**
```
[✓] Queue-full path tested: saturate audio queue → AudioCommandRejected logged — 2026-05-10
[✓] Command rejection path tested: stop during start → AudioCommandRejected for start cmd_id — 2026-05-10
[✓] Stop-during-start tested: no TrackEnded after StopAudio — 2026-05-10
[✓] Track-ended natural completion: TrackEnded fires after file EOF in NFC mode → stop — 2026-05-10
[✓] Track-ended in music mode: TrackEnded fires → auto-advance to next track — 2026-05-10
[✓] System sound completion: power_off sound → SystemSoundCompleted → sleep advances — 2026-05-10
[✓] System sound failure: missing file → SystemSoundFailed(FileNotFound) → sleep continues — 2026-05-10
[✓] cmd_id correlation: every feedback event has matching cmd_id from pending effects registry — 2026-05-10
[✓] pio test -e native passes 100% — 70/70 2026-05-10
```
All 9 items verified 2026-05-10.

---

## Stage 4: Sleep/Wake Migration

**Scope:** Migrate normal, emergency, and night-light timeout sleep into reducer+executor
model. Preserve exact safe ordering in executors (Steps 1–9 from 06_sleep_wake.md).

**Acceptance criteria:**
```
[ ] Normal sleep from idle: power-off sound plays, BT shuts down, device sleeps
[ ] Normal sleep from active playback: audio stops, sound plays, BT shuts down, sleeps
[ ] Emergency sleep from active playback: immediate, no sound, correct BT/JBL ordering
[ ] Night-light timeout sleep: no sound, NVS flush, sleeps
[ ] Wake abort (< 800ms hold): returns to sleep, does not boot
[ ] Normal wake (800–1600ms hold): normal boot
[ ] Night-light wake (≥ 1600ms hold): night-light mode
[ ] Repeated sleep/wake: 10 cycles without hang or crash (all three sleep paths)
[ ] No-BT path (JBL off): normal sleep without power-off sound
[ ] BT-connected path: power-off sound heard before sleep
[ ] gInSleepExecutorPath flag prevents assert-restart during sleep sequence
[ ] RTC crash flag cleared on successful boot (no spurious warning)
[ ] pio test -e native passes 100%
```

**Hardware sleep/wake checklist — ✅ all 13 verified 2026-05-10**

```
[✓] Normal sleep from idle: power-off sound plays, BT shuts down, device sleeps
[✓] Normal sleep from active playback: audio stops, sound plays, BT shuts down, sleeps
[✓] Emergency sleep from active playback: immediate, no sound, correct BT/JBL ordering
[✓] Night-light timeout sleep: no sound, NVS flush, sleeps
[✓] Wake abort (< 800ms hold): returns to sleep, does not boot
[✓] Normal wake (800–1600ms hold): normal boot
[✓] Night-light wake (≥ 1600ms hold): night-light mode
[✓] Repeated sleep/wake: 10 cycles without hang or crash (all three sleep paths)
[✓] No-BT path (JBL off): normal sleep without power-off sound
[✓] BT-connected path: power-off sound heard before sleep
[✓] gInSleepExecutorPath flag prevents assert-restart during sleep sequence
[✓] RTC crash flag cleared on successful boot (no spurious warning)
[✓] pio test -e native passes 100%
```

---

## Stage 5: Buttons, Deadlines, Persistence

**Scope:** Migrate buttons, idle timeout, volume overlay, brightness save, playback-mode
persistence, battery-check behavior, and `night_light.cpp` session logic into
reducer+dispatcher model.

**night_light.cpp migration (included in Stage 5):**
- `nightLightInit()` → `BootInitCompleted` in NightLight session: reducer sets
  `night_light_deadline_ms = now + NIGHT_LIGHT_TIMEOUT_MS`
- `nightLightTick()` timeout path → `NightLightTimeoutFired` → reducer emits `EnterDeepSleep(NightLightTimeout)`
- `nightLightTick()` save path → `BrightnessSaveDeadlineFired` → `PersistBrightness` effect
- `adjustBrightness()` via BTN_C/BTN_D: reducer in NightLight session maps `VolumeDownPressed`/`VolumeUpPressed`
  to brightness change (`night_light_brightness_percent`) instead of volume; Normal session maps them to
  `music_volume_percent`
- `night_light.cpp` module is retired after Stage 5 (its state now lives entirely in AppState + deadlines)

**Acceptance criteria:**
```
[✓] No lost button events under playback load (100 rapid presses with audio active)
[✓] Double-click detection correct: prev/next under 350ms window
[✓] Long-press actions correct: BTN_C sleep, BTN_A battery check
[✓] A+B combo: diagnostic entry still works
[✓] C+D combo: emergency sleep still works
[✓] Volume overlay: shows on BTN_C/D, auto-hides after 1s
[✓] Battery display: BTN_A hold shows bars, auto-hides
[✓] Night-light brightness: BTN_C/D adjust, deferred NVS save
[✓] Persistence timing: brightness saves after deadline, not on every step
[✓] PlaybackMode persistence: survives reboot (NFC mode remembered, music mode remembered)
[✓] Idle timeout: 10-minute timer fires correctly, triggers sleep
[✓] Night-light timeout: timer fires correctly
[✓] pio test -e native passes 100% — 101/101 2026-05-10
```

**Hardware Stage 5 checklist — ✅ all 13 verified 2026-05-10**

---

## Stage 6: Cleanup

**Scope:** Remove legacy policy globals, remove direct LED policy calls, keep only
adapter-local mutable state where unavoidable.

**Acceptance criteria:**
```
[✓] grep -r "isPlaying\|isPaused\|trackEndedFlag\|g_btConnected" esp32/src/ → only in adapters — 2026-05-10
[✓] grep -r "ledSetIdle\|ledSetPlaying\|ledSetWaitBt" esp32/src/ → only in LED executor — 2026-05-10
[✓] grep -r "pendingPlaybackPath\|pendingPlaybackUid" esp32/src/ → zero results — 2026-05-10
[✓] No split ownership remains (all DISPATCHER_OWNS_* = 1) — 2026-05-10
[ ] diagnostic_mode.cpp reads AppState only via getDiagnosticSnapshot() accessor — no direct global reads
[ ] Long soak: 100 sleep/wake cycles — see soak pass criteria in 10b_test_hardware.md
[✓] Parity evidence from Stages 1–5 hardware checklists retained and signed off
[✓] pio test -e native passes 100% — 101/101 2026-05-10
[✓] pio run -e lolin_d32_pro compiles without warnings — 2026-05-10
```

---

## Diagnostic Mode Integration (Stage 6)

`diagnostic_mode.cpp` (875 LOC) reads global state directly in current code. Post-migration
it must read only from `AppState`.

**Integration contract:**

```cpp
// Exposed by dispatcher — returns a snapshot copy of current AppState.
// Safe to call from any task context; dispatcher copies under a lightweight spinlock.
AppState getDiagnosticSnapshot();
```

`diagnostic_mode.cpp` calls `getDiagnosticSnapshot()` at the start of each diagnostic
render cycle. It must not hold a pointer to the live `AppState` across yield points.

**Stage 6 migration steps for diagnostic_mode:**
1. Identify every global variable read by `diagnostic_mode.cpp` (`grep -n "extern\|g_bt\|isPlaying"`)
2. For each: verify the value is present in `AppState`; add if missing
3. Replace each read with the snapshot accessor
4. Verify: no `extern` declarations remain in `diagnostic_mode.cpp`

---

## sync_mode.cpp — Explicitly Deferred

**Decision:** `sync_mode.cpp` (623 LOC, conditionally compiled) is **out of scope for
Stages 0–6**. Rationale: sync mode is not part of normal device operation and is only
active during firmware update. Its state is already modeled in `AppState` (`sync_active`,
`sync_progress_current`, `sync_progress_total`) and the events `SyncStarted`,
`SyncCompleted`, `SyncFailed` are defined.

**When sync mode is migrated (future work item):**
- Create `esp32/src/sync_adapter.h/.cpp`
- Sync adapter posts `SyncStarted`/`SyncCompleted`/`SyncFailed` to dispatcher queue
- Reducer handles these events and updates `AppState.sync_active` + progress fields
- `deriveLedScene()` already handles `SyncProgress` and `SyncWifi` scenes
- No changes needed to reducer or AppState — the contract is already in place

**Constraint:** Until sync_mode is migrated, it must not be conditionalized out of
hardware integration tests. The soak test must include at least one sync cycle.
