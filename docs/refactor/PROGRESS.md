# Refactor Progress

Last updated: 2026-05-10
Current stage: Stage 6 — Cleanup
Status: in-progress (code cleanup done; hardware soak pending)

## Stages

| Stage | Status | Notes |
|-------|--------|-------|
| Stage 0: Types, contracts, pure tests | ✅ done | All 34 test cases pass; 70/70 native suite |
| Stage 1: Observability and adapters | ✅ done | All criteria + HW checklist verified 2026-05-10 |
| Stage 2: BT and NFC source migration | ✅ done | HW checklist 20/20 verified 2026-05-10 |
| Stage 3: Audio command/ack migration | ✅ done | HW checklist verified 2026-05-10 |
| Stage 4: Sleep/wake migration | ✅ done | HW checklist 13/13 verified 2026-05-10 |
| Stage 5: Buttons, deadlines, persist | ✅ done | HW checklist 13/13 verified 2026-05-10 |
| Stage 6: Cleanup | 🔄 in-progress | Code cleanup done; hardware soak pending |

## Current Stage Detail

### What is done (Stage 1)
- `esp32/src/event_queue.h/.cpp` — postEventFromTask(), postEventFromIsr(), drop policy classification.
- `esp32/src/dispatcher.h/.cpp` — dispatcher FreeRTOS task ("app", core 1, 8192 stack), ring buffer
  (32 transitions), deadline polling, HWM logging every 30s, getDiagnosticSnapshot().
  All DISPATCHER_OWNS_* constants = 0 (observe-only).
- `esp32/src/bt_adapter.h/.cpp` — wraps audioPollBtConnection(); dual-call posts BtConnected/BtDisconnected.
- `esp32/src/nfc_adapter.h/.cpp` — wraps nfcGetEvent(); dual-call posts NfcTagDetected/NfcTagRemoved.
- `esp32/src/audio_adapter.h/.cpp` — Stage 1 stub (Stage 3+ implementation).
- `esp32/src/persistence_adapter.h/.cpp` — Stage 1 stub (Stage 5+ implementation).
- `esp32/src/musicbox_assert.cpp` — defines gInSleepExecutorPath + musicboxAssertFail() (was missing).
- `esp32/src/main.cpp` — dispatcherInit() in shared setup path; btAdapterInit()+dispatcherStartTask()
  in both night-light and normal paths; btAdapterPoll() replaces audioPollBtConnection();
  nfcAdapterDrain() replaces inline NFC drain loop.
- `esp32/src/playback.h/.cpp` — PlaybackMode moved from playback.h to app_state.h (authoritative);
  NFC→Nfc and MUSIC→Music renamed in playback.cpp.
- `pio run -e lolin_d32_pro`: SUCCESS (86s) 2026-05-10
- `pio test -e native`: 70/70 pass 2026-05-10

### What is done (Stage 2)
- `esp32/src/dispatcher.h` — DISPATCHER_OWNS_BT_NFC flipped to 1; added dispatcherSetInitialPlaybackMode().
- `esp32/src/dispatcher.cpp` — effect execution (SetVolume, StartNfcPlaybackByUid, StartMusicTrackByIndex,
  StopAudio, TriggerBtRecoveryPulse, TriggerBtDiscoveryRestart); LED scene applied from
  deriveLedScene() after each state update when boot_state==Ready.
- `esp32/src/bt_adapter.cpp` — removed audioPollBtConnection() dual-call when OWNS=1; g_btConnected
  still updated for legacy code compatibility.
- `esp32/src/nfc_adapter.cpp` — removed playbackHandleNfcTagPresent/Removed dual-calls when OWNS=1.
- `esp32/src/playback.h/.cpp` — added playbackStartMusicTrackAt(int index) for dispatcher use.
- `esp32/src/main.cpp` — dispatcherSetInitialPlaybackMode() called after playbackInit(); NfcPreScanCompleted
  event posted after prescan; BootInitCompleted posted before dispatcherStartTask();
  loop() steps 2+3 (old BT connect/disconnect handlers) guarded with #if !DISPATCHER_OWNS_BT_NFC;
  step 4 JBL recovery kept as-is (deadlines not yet wired); btDiscoveryFallbackDone reset on BT connect in step 4.
- `pio run -e lolin_d32_pro`: SUCCESS (14s) 2026-05-10
- `pio test -e native`: 70/70 pass 2026-05-10

### What is done (Stage 3)
- `esp32/src/events.h` — added makeNfcPlaybackStartedEvent, makeMusicTrackStartedEvent,
  makeMusicTrackStartFailedEvent, makeAudioCommandRejectedEvent constructors.
- `esp32/src/audio.h` — added Stage 3+ correlation API:
  audioStartNfcTrack(), audioStartMusicTrack(), audioStopWithId(), audioPlaySystemSound().
- `esp32/src/audio.cpp` — extended AudioCmd struct with cmd_id, uid, track_index, sound_id,
  is_nfc_track, is_system_sound fields. Audio task posts feedback events (NfcPlaybackStarted,
  MusicTrackStarted, NfcPlaybackStartFailed, MusicTrackStartFailed, AudioStopped,
  SystemSoundCompleted, SystemSoundFailed, TrackEnded) when cmd_id != 0.
  Old-style plays (cmd_id=0) keep legacy behavior (trackEndedFlag, LED calls) for
  button-triggered paths not yet migrated (Stage 5).
- `esp32/src/playback.h/.cpp` — added playbackGetMusicPath(index, buf, size) for adapter.
- `esp32/src/audio_adapter.h` — full Stage 3 API: Start/Stop/Pause/Resume/SystemSound.
- `esp32/src/audio_adapter.cpp` — full implementation: uid→path, index→path resolution,
  cmd_id forwarding, synchronous failure events, queue-full rejection.
- `esp32/src/dispatcher.h` — DISPATCHER_OWNS_AUDIO flipped to 1.
- `esp32/src/dispatcher.cpp` — PendingAudioEffect registry (8 slots, deadlines),
  nextCmdId() counter, registerPending/completePending/checkPendingTimeouts helpers,
  onAudioFeedbackEvent() cleans registry before reduce(), executeEffect() uses
  audio adapter functions for all audio effects under DISPATCHER_OWNS_AUDIO=1.
- `esp32/src/main.cpp` — trackEndedFlag polling kept (handles button-triggered old-style
  plays); comment explains Stage 3 hybrid approach.
- `pio run -e lolin_d32_pro`: SUCCESS (16s) 2026-05-10
- `pio test -e native`: 70/70 pass 2026-05-10

### What is done (Stage 4)
- `esp32/src/reducer.cpp` — added `advanceSleepAfterAudioStopped()` helper; fixed `SleepRequested`,
  `IdleTimeoutFired`, `NightLightTimeoutFired` to advance sleep state machine immediately when
  audio is not active (previously stuck in PreparingDeepSleep with no StopAudio effect emitted).
  Simplified `AudioStopped`-in-PreparingDeepSleep to use the same helper.
- `esp32/src/sleep.h` — added `#include "app_state.h"` and `sleepExecuteDeepSleep(RequestedSleepKind)` declaration.
- `esp32/src/sleep.cpp` — added `sleepExecuteDeepSleep()`: three-path executor (Normal, Emergency,
  NightLightTimeout). BT already disabled by `btAdapterShutdown()` before this is called. Sets
  `gInSleepExecutorPath=true`, then follows Steps 1–9 per 06_sleep_wake.md.
- `esp32/src/bt_adapter.cpp` — implemented `btAdapterShutdown()`: calls `esp_bt_controller_disable()`,
  delay(50), posts `BtShutdownCompleted` (success always — sleep must not be blocked by BT state).
- `esp32/src/dispatcher.h` — `DISPATCHER_OWNS_SLEEP` flipped to 1.
- `esp32/src/dispatcher.cpp` — added `#include "sleep.h"` under `DISPATCHER_OWNS_SLEEP`; added
  `ShutdownBt` and `EnterDeepSleep` handlers in `executeEffect()`.
- `esp32/src/buttons.cpp` — added includes (dispatcher.h, event_queue.h, events.h); guarded all
  three sleep call sites with `#if DISPATCHER_OWNS_SLEEP` / `#else` / `#endif`.
- `esp32/src/night_light.cpp` — added includes (dispatcher.h, event_queue.h, events.h); guarded
  `enterDeepSleep()` in `nightLightTick()`.
- `esp32/src/main.cpp` — guarded idle timeout sleep call with `#if !DISPATCHER_OWNS_SLEEP` / `#else`.
- `esp32/test/test_reducer/test_reducer.cpp` — added 6 new tests for no-audio sleep initiation paths;
  total 76/76 native tests pass.
- `pio test -e native`: 76/76 pass 2026-05-10
- `pio run -e lolin_d32_pro`: SUCCESS (15s) 2026-05-10

### What is done (Stage 5)
- `esp32/src/events.h` — added `BatteryCheckRequested(bars)` payload, `BrightnessLoaded(percent)` event,
  `makeBatteryCheckEvent()`, `makeBrightnessLoadedEvent()` constructors.
- `esp32/src/effects.h` — added `TriggerDiagnosticRestart` effect type.
- `esp32/src/reducer.cpp` — removed `(void)now_ms`; added timing constants (REDUCER_*); added effect
  factories (makePauseAudioEffect, makeResumeAudioEffect, makePersistBrightnessEffect,
  makePersistPlaybackModeEffect, makeTriggerDiagnosticRestartEffect); added `clampU8()` helper;
  updated BootInitCompleted to set idle_deadline_ms / night_light_deadline_ms;
  added BrightnessLoaded handler; added VolumeUpPressed / VolumeDownPressed (normal+nightlight);
  PlayPausePressed, NextTrackPressed, PrevTrackPressed, ModeToggleRequested,
  BatteryCheckRequested, DiagnosticEntryRequested, BrightnessSaveDeadlineFired handlers;
  NfcPlaybackStarted / MusicTrackStarted clear idle_deadline_ms;
  AudioStopped restores idle_deadline_ms in Normal session;
  NightLightTimeoutFired flushes pending brightness save before sleep.
- `esp32/src/dispatcher.h` — DISPATCHER_OWNS_BUTTONS flipped to 1; added
  dispatcherSetInitialVolume(), dispatcherSetInitialNightLightBrightness().
- `esp32/src/dispatcher.cpp` — DISPATCHER_OWNS_BUTTONS conditional includes
  (button_adapter.h, volume.h, battery.h, helpers.h, persistence_adapter.h, SD.h, zbox_config.h);
  SetVolume handler: calls setBtVolumeAndApply(percent) under OWNS_BUTTONS;
  added PersistBrightness, PersistPlaybackMode, TriggerDiagnosticRestart effect executors;
  added dispatcherSetInitialVolume(), dispatcherSetInitialNightLightBrightness().
- `esp32/src/persistence_adapter.cpp` — implemented NVS writes for brightness (key "night_light")
  and playback mode (key "playback_mode"); posts BrightnessPersisted / PlaybackModePersisted feedback.
- `esp32/src/volume.h/.cpp` — added getBtVolume(), setBtVolumeAndApply(percent); made saveBtVolume() non-static.
- `esp32/src/button_adapter.h` — new file: RawButtonEvent struct, buttonAdapterInit/StartTask/DecoderFeed/Tick/Reset.
- `esp32/src/button_adapter.cpp` — new file: ISR (CHANGE-triggered), per-button decoder state,
  double-click window (350ms), long-press (2000ms) for A→battery, B→mode toggle, C→sleep;
  emergency sleep on C hold 10s; A+B combo (2s) → DiagnosticEntryRequested;
  C+D combo (5s) → SleepRequested(Emergency). Posts semantic events to dispatcher queue.
- `esp32/src/main.cpp` — guarded buttonsInit() vs buttonAdapterInit() on DISPATCHER_OWNS_BUTTONS;
  added buttonAdapterStartTask() in both normal and night-light setup paths;
  dispatcherSetInitialVolume(getBtVolume()) after loadBtVolume();
  dispatcherSetInitialNightLightBrightness() in night-light path;
  guarded lastActivityMs = millis() at end of setup;
  guarded handleButtons() / nightLightTick() / volumeTick() in loop();
  guarded isPlaying/idle-timeout block in loop().
- `esp32/test/test_reducer/test_reducer.cpp` — added 25 Stage 5 tests (boot deadlines, volume/brightness
  clamp, brightness save deadline, nightlight timeout flush, play/pause/next/prev/mode toggle,
  battery check, diagnostic restart, idle timer management, brightness loaded).
- `pio test -e native`: 101/101 pass 2026-05-10
- `pio run -e lolin_d32_pro`: SUCCESS (84s) 2026-05-10

### What is done (Stage 6)
- `main.cpp` — removed dead `trackEndedFlag` check; removed `pendingPlaybackPath`/`pendingPlaybackUid`
  writes; replaced `g_btConnected` in JBL recovery with `getDiagnosticSnapshot().bt_state`;
  removed dead `#if !DISPATCHER_OWNS_BT_NFC` and `#if !DISPATCHER_OWNS_BUTTONS` blocks;
  updated heartbeat log to use AppState snapshot.
- `audio.cpp` — removed all dead `#else` legacy fallbacks (ledSetPlaying/Idle/trackEndedFlag);
  simplified start/stop/pause/resume/track-ended blocks to be unconditional dispatcher paths.
- `leds.cpp` — removed `isPlaying` reads from battery animation restore and volume overlay
  timeout; added `ledPreVolumeMode` to track pre-overlay state; removed `#include "state.h"` (added
  back — still needed for g_beatDetected/g_audioEnergy/runtimeIsNightLight).
- `volume.cpp` — removed `g_btConnected` check from `applyBtVolume()`.
- `sleep.cpp` — removed `enterDeepSleep()`/`enterEmergencyDeepSleep()` from public API; re-added
  as internal functions (used by diagnostic_mode.cpp forward declaration only); removed
  `#include "playback.h"`.
- `playback.cpp` — removed all dead functions: `playbackHandleBtConnected`,
  `playbackHandleTrackEnded`, `playbackHandleNfcTagPresent`, `playbackHandleNfcTagRemoved`,
  `playbackTogglePlayPause`, `playbackNextTrack`, `playbackPrevTrack`, `playbackToggleMode`,
  `playbackStartMusicTrackAt`, `startPlayback`, `stopPlayback`, `playSystemSoundSync`, and
  all internal helpers only used by dead functions.
- `playback.h` — removed all dead function declarations.
- `state.h`/`state.cpp` — removed `trackEndedFlag`, `pendingPlaybackPath`, `pendingPlaybackUid`.
- `nfc_adapter.cpp` — removed `#include "playback.h"`.
- `buttons.cpp` — wrapped entire body in `#if !DISPATCHER_OWNS_BUTTONS` guard.
- `diagnostic_mode.cpp` — forward-declared `enterDeepSleep()`/`enterEmergencyDeepSleep()`.
- Grep criteria verified:
  - `isPlaying|isPaused|trackEndedFlag|g_btConnected` → only in audio.cpp + bt_adapter.cpp + state.h/cpp ✅
  - `ledSetIdle|ledSetPlaying|ledSetWaitBt` → only in leds.h/cpp + dispatcher.cpp + main.cpp:221 (boot init) ✅
  - `pendingPlaybackPath|pendingPlaybackUid` → zero results ✅
- `pio run -e lolin_d32_pro`: SUCCESS — 2026-05-10
- `pio test -e native`: 101/101 pass — 2026-05-10

### What is in progress
- Stage 6 hardware soak: 100 sleep/wake cycles (manual, requires hardware).

### What is next
- Hardware soak: 100 sleep/wake cycles across all three paths (Normal/Emergency/NightLight).
- Sign off Stage 6 in 09_migration_stages.md when soak complete.

### Blockers
- None.

## Files Created / Modified

| File | Status |
|------|--------|
| docs/refactor/PROGRESS.md | modified |
| esp32/src/musicbox_assert.h | created (Stage 0) |
| esp32/src/musicbox_assert.cpp | created (Stage 1 — was missing; defines gInSleepExecutorPath + musicboxAssertFail) |
| esp32/src/app_state.h | created (Stage 0) |
| esp32/src/events.h | created (Stage 0) |
| esp32/src/effects.h | created (Stage 0) |
| esp32/src/reducer.h | created (Stage 0) |
| esp32/src/reducer.cpp | created (Stage 0) |
| esp32/src/led_scene.h | created (Stage 0) |
| esp32/src/led_scene.cpp | created (Stage 0) |
| esp32/test/test_reducer/test_reducer.cpp | created (Stage 0) |
| esp32/platformio.ini | modified (Stage 0 — added test_reducer to test_filter) |
| esp32/src/event_queue.h | created (Stage 1) |
| esp32/src/event_queue.cpp | created (Stage 1) |
| esp32/src/dispatcher.h | created (Stage 1) |
| esp32/src/dispatcher.cpp | created (Stage 1) |
| esp32/src/bt_adapter.h | created (Stage 1) |
| esp32/src/bt_adapter.cpp | created (Stage 1) |
| esp32/src/nfc_adapter.h | created (Stage 1) |
| esp32/src/nfc_adapter.cpp | created (Stage 1) |
| esp32/src/audio_adapter.h | created (Stage 1 stub) → rewritten (Stage 3 — full API) |
| esp32/src/audio_adapter.cpp | created (Stage 1 stub) → rewritten (Stage 3 — full impl) |
| esp32/src/persistence_adapter.h | created (Stage 1 stub) |
| esp32/src/persistence_adapter.cpp | created (Stage 1 stub) |
| esp32/src/main.cpp | modified (Stage 1 — dispatcher/adapter init + poll integration) |
| esp32/src/playback.h | modified (Stage 1 — PlaybackMode removed; Stage 2 — playbackStartMusicTrackAt added) |
| esp32/src/playback.cpp | modified (Stage 1 — NFC→Nfc, MUSIC→Music; Stage 2 — playbackStartMusicTrackAt impl) |
| esp32/src/dispatcher.h | modified (Stage 2 — DISPATCHER_OWNS_BT_NFC=1, dispatcherSetInitialPlaybackMode) |
| esp32/src/dispatcher.cpp | modified (Stage 2 — effect executor, LED scene application) |
| esp32/src/bt_adapter.cpp | modified (Stage 2 — removed dual-call when OWNS_BT_NFC=1) |
| esp32/src/nfc_adapter.cpp | modified (Stage 2 — removed playback dual-calls when OWNS_BT_NFC=1) |
| esp32/src/main.cpp | modified (Stage 2 — boot events, playback mode init, guarded loop steps 2+3; Stage 3 — trackEndedFlag comment) |
| esp32/src/events.h | modified (Stage 3 — added NfcPlaybackStarted, MusicTrackStarted, MusicTrackStartFailed, AudioCommandRejected constructors) |
| esp32/src/audio.h | modified (Stage 3 — added correlation API: audioStartNfcTrack, audioStartMusicTrack, audioStopWithId, audioPlaySystemSound) |
| esp32/src/audio.cpp | modified (Stage 3 — extended AudioCmd with cmd_id/uid/index/sound_id fields; audio task posts feedback events; per-track context tracking) |
| esp32/src/playback.h | modified (Stage 3 — added playbackGetMusicPath) |
| esp32/src/playback.cpp | modified (Stage 3 — implemented playbackGetMusicPath) |
| esp32/src/dispatcher.h | modified (Stage 3 — DISPATCHER_OWNS_AUDIO=1) |
| esp32/src/dispatcher.cpp | modified (Stage 3 — PendingAudioEffect registry, nextCmdId, executeEffect uses audio adapter, onAudioFeedbackEvent, checkPendingTimeouts; Stage 4 — ShutdownBt + EnterDeepSleep effect executors) |
| esp32/src/sleep.h | modified (Stage 4 — added app_state.h include + sleepExecuteDeepSleep declaration) |
| esp32/src/sleep.cpp | modified (Stage 4 — added sleepExecuteDeepSleep implementation) |
| esp32/src/bt_adapter.cpp | modified (Stage 4 — implemented btAdapterShutdown) |
| esp32/src/buttons.cpp | modified (Stage 4 — guarded sleep call sites with DISPATCHER_OWNS_SLEEP) |
| esp32/src/night_light.cpp | modified (Stage 4 — guarded enterDeepSleep with DISPATCHER_OWNS_SLEEP) |
| esp32/src/main.cpp | modified (Stage 4 — guarded idle timeout sleep with DISPATCHER_OWNS_SLEEP) |
| esp32/src/reducer.cpp | modified (Stage 4 — advanceSleepAfterAudioStopped helper; fixed no-audio sleep paths; Stage 5 — button/volume/brightness/deadline handlers) |
| esp32/test/test_reducer/test_reducer.cpp | modified (Stage 4 — 6 new sleep tests, total 76; Stage 5 — 25 new tests, total 101) |
| esp32/src/events.h | modified (Stage 5 — BatteryCheckRequested payload, BrightnessLoaded event) |
| esp32/src/effects.h | modified (Stage 5 — TriggerDiagnosticRestart effect) |
| esp32/src/dispatcher.h | modified (Stage 5 — OWNS_BUTTONS=1, new init functions) |
| esp32/src/dispatcher.cpp | modified (Stage 5 — PersistBrightness/PlaybackMode/DiagnosticRestart executors, new init functions, setBtVolumeAndApply in SetVolume) |
| esp32/src/persistence_adapter.cpp | modified (Stage 5 — implemented NVS saves for brightness and playback mode) |
| esp32/src/volume.h | modified (Stage 5 — getBtVolume(), setBtVolumeAndApply() added) |
| esp32/src/volume.cpp | modified (Stage 5 — getBtVolume(), setBtVolumeAndApply(), saveBtVolume() made non-static) |
| esp32/src/button_adapter.h | created (Stage 5) |
| esp32/src/button_adapter.cpp | created (Stage 5 — ISR, decoder state machine, task) |
| esp32/src/main.cpp | modified (Stage 5 — buttonAdapterInit/StartTask, dispatcherSetInitialVolume/Brightness, guarded old button/idle code) |

## Test Results (latest run)

`pio test -e native`: 101/101 passing (test_uid_format 7, test_mapping 8, test_reducer 65, test_volume 21) — 2026-05-10
`pio run -e lolin_d32_pro`: SUCCESS — 2026-05-10 (Stage 6)
Last hardware checklist stage: Stage 3 HW checklist — ✅ verified 2026-05-10
Stage 4 HW checklist: ✅ 13/13 verified 2026-05-10
Stage 5 HW checklist: ✅ 13/13 verified 2026-05-10
Stage 6 grep criteria: ✅ all 3 clean 2026-05-10
Stage 6 hardware soak: ⬜ pending (100 sleep/wake cycles)

## Notes

- PlatformIO native test linking: reducer.cpp and led_scene.cpp are included via
  `#include "reducer.cpp"` / `#include "led_scene.cpp"` in test_reducer.cpp because
  PlatformIO native tests do not link project src/ files into test executables.
  The `build_src_filter` setting in platformio.ini controls only the firmware build.
- cmd_id in effects emitted by reducer is always 0 (dispatcher fills real values post-reduce).
  Tests verify effect type/params; cmd_id is intentionally left as 0.
- sizeof(AppState) is estimated at ~109–113 bytes (well within 128-byte limit). The
  static_assert confirms this at compile time.
