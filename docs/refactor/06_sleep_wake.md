# Part 7/11: Sleep and Wake Protocol

← Prev: [05_bt_nfc_adapters.md](05_bt_nfc_adapters.md) | → Next: [07_led_model.md](07_led_model.md)

---

## Domain Sleep Sequence (Reducer-Owned)

The reducer owns the logical state machine for sleep. The physical sequence is in executors.

1. Event `SleepRequested(kind)` arrives.
2. Reducer sets `sleep_state = PreparingDeepSleep`, `requested_sleep_kind = kind`.
3. Reducer freezes all non-sleep playback policy (see Ignored Events below).
4. Reducer emits `StopAudio`.
5. On `AudioStopped` (in `PreparingDeepSleep`, Normal sleep only):
   reducer sets `sleep_state = WaitingPowerOffSound`, emits `PlaySystemSound(power_off)`.
5a. On `AudioStopped` (in `PreparingDeepSleep`, Emergency/NightLightTimeout):
   reducer skips sound, sets `sleep_state = WaitingBtShutdown`, emits `ShutdownBt`.
6. On `SystemSoundCompleted(power_off)` (in `WaitingPowerOffSound`):
   reducer sets `sleep_state = WaitingBtShutdown`, emits `ShutdownBt`.
6a. On `SystemSoundFailed` (in `WaitingPowerOffSound`):
   reducer treats it as completed — sound failure must not block sleep.
   Sets `sleep_state = WaitingBtShutdown`, emits `ShutdownBt`.
7. On `BtShutdownCompleted` (or `BtShutdownFailed`):
   reducer sets `sleep_state = ReadyToSleep`, emits `EnterDeepSleep(kind)`.
8. `EnterDeepSleep` executor runs the hardware protocol (see below) and never returns.

### Events Ignored During PreparingDeepSleep / WaitingBtShutdown

While `sleep_state != Awake`, the reducer drops (returns unchanged state, no effects):
- `TrackEnded`
- `NfcTagDetected` / `NfcTagRemoved`
- `PlayPausePressed`, `NextTrackPressed`, `PrevTrackPressed`
- `ModeToggleRequested`
- `VolumeUpPressed`, `VolumeDownPressed`
- `BatteryCheckRequested`, `DiagnosticEntryRequested`
- `IdleTimeoutFired`, `NightLightTimeoutFired`
- `BrightnessSaveDeadlineFired`
- New BT connection events (already in shutdown path)

Exceptions — these ARE processed during sleep preparation:
- `AudioStopped`, `SystemSoundCompleted`, `BtShutdownCompleted`, `BtShutdownFailed`
  (needed to advance the sleep state machine)
- `SleepRequested(Emergency)` while preparing Normal sleep: upgrades to Emergency path,
  skips system sound.

---

## Normal Sleep Executor Contract

[Verified in current code: `esp32/src/sleep.cpp` `enterDeepSleep()`]

The sleep executor runs when the dispatcher dispatches `EnterDeepSleep(Normal)`.
Each step is marked MUST (required for correctness) or best-effort.

| Step | Action | Tag | On failure |
|------|--------|-----|-----------|
| 1 | Flush pending night-light brightness to NVS synchronously if `BrightnessSaveDeadlineFired` deadline is active; cancel the deadline timer | best-effort | Log WARN, continue |
| 2 | Play `power_off` system sound (if BT was active) | best-effort | Log WARN, continue |
| 3 | Delete audio task (MP3 decoder/reader, not BT) | MUST | Log CRIT; gInSleepExecutorPath=true |
| 4 | `nfcAdapterStopTaskForSleep()` → `nfcPowerDown()` | best-effort | Log WARN, continue |
| 5 | `esp_bt_controller_disable()` | **MUST — before JBL** | If fails: log CRIT, continue |
| 6 | Suspend LED task → shutdown animation → `ledPowerOff()` | best-effort | Skip anim, power off |
| 7 | `jblPowerOff()` (GPIO pulse) | MUST | Log CRIT, continue |
| 8 | `esp_sleep_enable_ext0_wakeup(BTN_D, LOW)` | MUST | Cannot continue: hang+WDT |
| 9 | `esp_deep_sleep_start()` | MUST | Does not return |

Step 5 ordering is crash-critical: `esp_bt_controller_disable()` MUST precede `jblPowerOff()`.
Calling A2DP disconnect after JBL power-off causes BT stack state machine crash.
[Verified in current code]

`gInSleepExecutorPath = true` is set before Step 3. This changes MUSICBOX_ASSERT behavior
(log + continue instead of restart). See [03_reducer.md §Assert Safety](03_reducer.md).

---

## Emergency Sleep Executor Contract

[Verified in current code: `esp32/src/sleep.cpp` `enterEmergencyDeepSleep()`]

Triggered by `EnterDeepSleep(Emergency)`. Skips non-essential steps. Crash-safe ordering
is still preserved.

| Step | Action | Tag | On failure |
|------|--------|-----|-----------|
| 1 | Delete audio task | MUST | Log CRIT, continue |
| 2 | `esp_bt_controller_disable()` | **MUST — before JBL** | Log CRIT, continue |
| 3 | `nfcPowerDown()` if `nfcReady` | conditional best-effort | Skip |
| 4 | `ledPowerOff()` (no animation) | best-effort | Skip |
| 5 | `jblPowerOff()` via direct GPIO pulse | MUST | Log CRIT, continue |
| 6 | `esp_sleep_enable_ext0_wakeup(BTN_D, LOW)` | MUST | Hang + WDT |
| 7 | `esp_deep_sleep_start()` | MUST | Does not return |

Differences from normal sleep:
- No system sound (power_off is skipped).
- No LED animation (direct power-off).
- No NVS flush.
- Same crash-sensitive BT/JBL ordering as normal sleep (Step 2 before Step 5).

Emergency sleep is NOT "normal sleep without the sound." It has its own validated sequence.
Any change to emergency sleep ordering must be hardware-validated independently.
[Must validate on hardware]

---

## Night-Light Timeout Sleep Executor Contract

Triggered by `EnterDeepSleep(NightLightTimeout)`.

Same as normal sleep executor EXCEPT:
- Step 2 (system sound) is skipped (no BT active in night-light mode).
- BT was never initialized in night-light mode, so Step 5 (`esp_bt_controller_disable()`)
  is a no-op or skipped based on `bt_state == Disabled`.

| Step | Action | Tag |
|------|--------|-----|
| 1 | Flush pending night-light brightness to NVS | best-effort |
| 2 | (skipped — no BT, no sound) | — |
| 3 | Delete audio task if running | conditional |
| 4 | NFC task stop + `nfcPowerDown()` | best-effort |
| 5 | `esp_bt_controller_disable()` only if `bt_state != Disabled` | conditional |
| 6 | LED shutdown animation → `ledPowerOff()` | best-effort |
| 7 | `jblPowerOff()` if JBL was active | conditional |
| 8 | `esp_sleep_enable_ext0_wakeup(BTN_D, LOW)` | MUST |
| 9 | `esp_deep_sleep_start()` | MUST |

---

## Wake Handling

`handleWakeFromDeepSleep()` runs before `setup()` completes, before FreeRTOS scheduler
starts. It is a hardware gate, not reducer policy.

[Verified in current code: `esp32/src/sleep.cpp` `handleWakeFromDeepSleep()`]

**Wake decision logic:**
- Check `esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0`.
- If false (power-on boot): return immediately, continue normal `setup()`.
- If true (wake from sleep): implement hold-to-confirm:
  - Sample BTN_D every 20ms.
  - `< WAKE_ABORT_MS` (800ms) hold then release → go back to deep sleep.
  - `800ms–1600ms` hold → `WakeDecision::NORMAL_BOOT`.
  - `≥ WAKE_NIGHT_LIGHT_MS` (1600ms) hold → `WakeDecision::NIGHT_LIGHT`.

- During hold: animate LED progress bar (pre-FreeRTOS, uses `ledPreInitHardware()`).

> Note: these values match the current implementation (`WAKE_ABORT_MS = 800`,
> `WAKE_NIGHT_LIGHT_MS = 1600` in `esp32/src/sleep.cpp`). Do NOT change them without
> hardware validation and explicit update to Behavior Preservation Matrix.

**Wake-derived events posted to dispatcher:**

| Wake decision | Event posted |
|--------------|-------------|
| NORMAL_BOOT | `WakeCauseResolvedNormal` |
| NIGHT_LIGHT | `WakeCauseResolvedNightLight` |

Early-release abort returns to deep sleep without posting any event.

Wake-hold LED behavior remains a pre-boot executor concern. Reducer does not see it.

---

## Hardware State After Deep Sleep

When device wakes:
- NFC PN532 may be in `PowerDown` state (if `rtcNfcPowerDownSent` RTC flag is set).
  `nfcInitSequence()` checks this and issues a firmware wake via raw SPI before normal init.
- JBL may be OFF. Boot sequence checks ADC and pulses GPIO if needed.
- RTC memory preserves: crash flag, NFC powerdown flag, wake decision.

Boot sequence must check RTC crash flag and log a warning if set. This is the mechanism
for detecting that a MUSICBOX_ASSERT fired during the previous sleep sequence.
