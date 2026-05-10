# Part 11b/12: Hardware Test Plan

← Prev: [10a_test_native.md](10a_test_native.md) | → Next: [11_button_adapter.md](11_button_adapter.md)

---

## Adapter Mock Requirements

Native tests mock audio, BT, NFC, and persistence adapters. Mocks must faithfully
reproduce the following behaviors — otherwise tests pass and hardware fails:

| Adapter | Must reproduce | May simplify |
|---------|---------------|-------------|
| Audio | `AudioCommandRejected` when queue full; `TrackEnded` on natural EOF only (not on stop) | MP3 decode latency |
| Audio | `AudioStopped` fires after `StopAudio`, even if nothing was playing (idempotent) | Buffering behavior |
| BT | `BtDisconnected` fires while audio is in `StartingFile` state | A2DP reconnect timing |
| BT | `BtShutdownFailed` fires after timeout (3000ms); sleep MUST proceed | BT stack internals |
| NFC | `NfcTagDetected` is `EdgeTriggered` — rapid identical UID does not double-fire | SPI timing |
| Persistence | `BrightnessPersistFailed` fires correctly; session continues | NVS write latency |

Any mock that silently succeeds on all commands is insufficient.

---

## Hardware Integration Tests

Required scenarios (each must be run and signed off on hardware).
Use Python serial harness (`test/integration/`) where pattern-matchable.

| # | Scenario | Pass criteria |
|---|----------|--------------|
| 1 | Boot to NFC playback | Figurine placed before boot → plays on BT connect |
| 2 | Boot to music mode | Music mode restored → autoplays on BT connect |
| 3 | NFC place/remove/re-present (3×) | Each cycle plays/stops correctly |
| 4 | BT disconnect during pending playback | Pending preserved → plays on reconnect |
| 5 | Normal sleep from idle | Power-off sound plays, device sleeps |
| 6 | Normal sleep from active playback | Stops, sound plays, sleeps |
| 7 | Emergency sleep from active playback | Immediate sleep, no sound |
| 8 | Night-light timeout sleep | No sound, sleeps after timeout |
| 9 | Wake abort (< 800ms hold) | Returns to sleep without booting |
| 10 | Normal wake (800–1600ms hold) | Normal boot |
| 11 | Night-light wake (≥ 1600ms hold) | Night-light mode |
| 12 | Missing SD card | Boots with degraded mode, no crash |
| 13 | Missing mapped NFC file | NfcPlaybackStartFailed logged, idle |
| 14 | Missing system sound | SleepRequested still completes sleep |
| 15 | BT unavailable at boot | WaitBt scene, no crash, no hang |
| 16 | Repeated BT flap (10× disconnect/reconnect) | No hang, playback resumes each time |
| 17 | NFC busy/reinit path | Recovers from NFC read error |
| 18 | Diagnostic entry via A+B hold | Enters diagnostic mode |
| 19 | Battery-check via BTN_A hold | Battery bars displayed |
| 20 | Mode-toggle: NFC→Music→NFC | Mode-change sound, persistence, correct behavior |
| 21 | BT/JBL shutdown ordering | Serial log shows `esp_bt_controller_disable()` timestamp before `jblPowerOff()` on every sleep path (normal, emergency, night-light) |
| 22 | Sync cycle completes | At least one sync cycle during soak; `SyncCompleted` logged, no crash |

Scenarios 9–11 verify wake timing against committed values (800ms/1600ms from `06_sleep_wake.md`).

---

## Fault Injection Tests

```
[ ] Audio queue saturation: send 10 rapid play commands → AudioCommandRejected logged for excess
[ ] Stop during start: StopAudio while StartNfc in flight → no TrackEnded, AudioStopped received
[ ] Audio timeout: block audio task 6s → AudioStartTimeout event fires, audio_state → Idle
[ ] BT shutdown timeout: simulate 4s BT disable → BtShutdownFailed, sleep continues
[ ] Persistence failure: corrupt NVS → BrightnessPersistFailed logged, session continues
[ ] ISR queue saturation: press all 4 buttons in < 1ms bursts → no event lost, no crash
```

---

## Soak Test

### Setup

100 sleep/wake cycles (mixed: ~40 normal, ~30 emergency, ~30 night-light) with full BT,
NFC, and button activity. Cycles must be interspersed with continuous BT playback segments
(≥ 10 minutes each) — not sleep/wake only.

Rationale: the known random-freeze bug (documented in `docs/random_hang.md`) reproduces
after 30–60 minutes of continuous BT playback. 8 hours of soak provides 8–16 reproduction
windows — the minimum meaningful signal. Overnight (12h) preferred before final release.

At least one sync cycle must be included in the soak (see integration test #22).

Monitor with serial harness (`test/integration/test_heartbeat.py`) extended to log metrics
per cycle.

### Pass Criteria

All of the following must be true for pass:

| Metric | Threshold |
|--------|-----------|
| Watchdog resets | 0 |
| CRIT log entries (excluding diagnostic mode) | 0 |
| `uxTaskGetFreeHeapSize()` minimum | ≥ 20480 bytes |
| All task HWMs | ≥ 512 bytes |
| PLAY commands dropped (non-rejection) | 0 |
| Sleep/wake completion rate | 100% (no hang) |
| Sync cycles completed (if triggered) | 100% |

---

## Release Gates

No stage may ship without:

- All native tests passing (`pio test -e native` 100%)
- All hardware checklist items for that stage signed off (✓)
- No unclassified failure mode
- No silent drop on critical event path
- No known crash-sensitive ordering left undocumented

Final release requires Stage 6 completion + soak test pass + all prior stage checklists
retained and accessible (stored alongside PROGRESS.md).

### Compile-time Release Checks

```bash
# No hardware includes in reducer:
grep -r "#include <Arduino" esp32/src/reducer.cpp  # → empty
grep -r "#include <freertos" esp32/src/reducer.cpp  # → empty

# No split ownership remaining:
grep -r "isPlaying\|isPaused\|trackEndedFlag\|g_btConnected" esp32/src/ | grep -v "adapter"

# No direct LED policy calls outside LED executor:
grep -r "ledSetIdle\|ledSetPlaying\|ledSetWaitBt" esp32/src/ | grep -v "led_executor"

# No direct global reads in diagnostic mode:
grep -n "extern\|g_bt\|isPlaying\|isPaused" esp32/src/diagnostic_mode.cpp  # → 0 results

# Static size constraints:
# static_assert(sizeof(AppState) <= 128) — compile error if violated
# static_assert(MAX_EFFECTS == 8) — documents budget
```
