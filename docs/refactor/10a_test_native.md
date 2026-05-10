# Part 11a/12: Native Test Plan

← Prev: [09_migration_stages.md](09_migration_stages.md) | → Next: [10b_test_hardware.md](10b_test_hardware.md)

---

## Native Tests (Reducer Logic Only)

Native tests run with `pio test -e native`. No hardware, no FreeRTOS, no Arduino.
Each test specifies: initial state + event → expected next state + expected effects.

### Test Case Format

```
TEST: <name>
  Given:  { field=value, ... }  (only fields that differ from default zero state)
  Event:  EventType [payload]
  Expect state: { field=value, ... }  (only changed fields)
  Expect effects: [EffectType(params), ...]  (ordered, exact count)
```

### Clock Control in Tests

Deadline-related tests pass `fake_now` as `now_ms`:

```cpp
// Pattern: set a deadline in the past to simulate expiry
AppState s = defaultState();
s.idle_deadline_ms = 1000;
uint32_t fake_now = 1001;  // past the deadline

ReduceResult r = reduce(s, makeEvent(EventType::IdleTimeoutFired), fake_now);
// assert: r.next_state.idle_deadline_ms == 0
// assert: contains EnterDeepSleep effect
```

For deadline-setting tests, verify `next_state.*_deadline_ms == fake_now + EXPECTED_INTERVAL`.

---

### Boot Transition Tests

```
TEST: boot_started_transitions_to_wake_hold_check
  Given:  { boot=ColdBootInit, bt=Unknown, session=Normal }
  Event:  BootStarted
  Expect state: { boot=WakeHoldCheck }
  Expect effects: []

TEST: wake_cause_normal_transitions_boot_init
  Given:  { boot=WakeHoldCheck }
  Event:  WakeCauseResolvedNormal
  Expect state: { boot=NormalBootInit }
  Expect effects: []

TEST: wake_cause_nightlight_sets_session
  Given:  { boot=WakeHoldCheck, session=Normal }
  Event:  WakeCauseResolvedNightLight
  Expect state: { boot=NightLightBootInit, session=NightLight }
  Expect effects: []

TEST: boot_init_completed_bt_starts_waiting
  Given:  { boot=NormalBootInit, bt=Unknown }
  Event:  BootInitCompleted
  Expect state: { boot=Ready, bt=WaitingForSpeaker }
  Expect effects: []

TEST: boot_nightlight_bt_disabled
  Given:  { boot=NightLightBootInit, session=NightLight }
  Event:  BootInitCompleted
  Expect state: { boot=Ready, bt=Disabled }
  Expect effects: []

TEST: nfc_prescan_found_sets_pending
  Given:  { boot=NormalBootInit, bt=Unknown }
  Event:  NfcPreScanCompleted(uid_found=true, uid="04:AA:BB")
  Expect state: { pending=NfcUid("04:AA:BB"), last_nfc_uid="04:AA:BB" }
  Expect effects: []

TEST: nfc_prescan_not_found_no_pending
  Given:  { boot=NormalBootInit, bt=Unknown }
  Event:  NfcPreScanCompleted(uid_found=false)
  Expect state: { pending=None }
  Expect effects: []
```

### BT and Pending Playback Tests

```
TEST: bt_connected_no_pending
  Given:  { bt=WaitingForSpeaker, pending=None }
  Event:  BtConnected
  Expect state: { bt=Connected, bt_volume_applied=false }
  Expect effects: [SetVolume(current_volume)]

TEST: bt_connected_with_pending_nfc
  Given:  { bt=WaitingForSpeaker, pending=NfcUid("04:AA:BB"), audio=Idle }
  Event:  BtConnected
  Expect state: { bt=Connected, audio=StartingFile, bt_volume_applied=false }
  Expect effects: [SetVolume(vol), StartNfcPlaybackByUid("04:AA:BB", cmd_id≠0)]

TEST: bt_connected_with_pending_music
  Given:  { bt=WaitingForSpeaker, pending=MusicCurrentTrack, current_track={valid=true, index=3} }
  Event:  BtConnected
  Expect state: { bt=Connected, audio=StartingFile }
  Expect effects: [SetVolume(vol), StartMusicTrackByIndex(3, cmd_id≠0)]

TEST: bt_disconnected_clears_volume_flag
  Given:  { bt=Connected, bt_volume_applied=true, audio=PlayingFile }
  Event:  BtDisconnected
  Expect state: { bt=WaitingForSpeaker, bt_volume_applied=false, audio=Idle }
  Expect effects: [StopAudio(cmd_id≠0)]
```

### NFC Playback Tests

```
TEST: nfc_tag_detected_bt_ready
  Given:  { bt=Connected, session=Normal, playback=Nfc, audio=Idle }
  Event:  NfcTagDetected(uid="04:AA:BB")
  Expect state: { audio=StartingFile, last_nfc_uid="04:AA:BB" }
  Expect effects: [StartNfcPlaybackByUid("04:AA:BB", cmd_id≠0)]

TEST: nfc_tag_detected_bt_not_ready
  Given:  { bt=WaitingForSpeaker, session=Normal, playback=Nfc }
  Event:  NfcTagDetected(uid="04:AA:BB")
  Expect state: { pending=NfcUid("04:AA:BB"), last_nfc_uid="04:AA:BB" }
  Expect effects: []

TEST: nfc_tag_removed_stops_audio
  Given:  { bt=Connected, audio=PlayingFile, playback=Nfc }
  Event:  NfcTagRemoved
  Expect state: { audio=Stopping }
  Expect effects: [StopAudio(cmd_id≠0)]

TEST: nfc_tag_detected_ignored_in_music_mode
  Given:  { bt=Connected, playback=Music, audio=PlayingFile }
  Event:  NfcTagDetected(uid="04:AA:BB")
  Expect state: { (unchanged) }
  Expect effects: []
```

### Sleep Path Tests

```
TEST: sleep_requested_normal_stops_audio
  Given:  { sleep=Awake, audio=PlayingFile, bt=Connected }
  Event:  SleepRequested(Normal)
  Expect state: { sleep=PreparingDeepSleep, requested_sleep_kind=Normal, audio=Stopping }
  Expect effects: [StopAudio(cmd_id≠0)]

TEST: audio_stopped_normal_sleep_plays_poweroff
  Given:  { sleep=PreparingDeepSleep, requested_sleep_kind=Normal, bt=Connected }
  Event:  AudioStopped(cmd_id)
  Expect state: { sleep=WaitingPowerOffSound, audio=StartingSystemSound }
  Expect effects: [PlaySystemSound(sound_id=power_off, cmd_id≠0)]

TEST: system_sound_done_shuts_down_bt
  Given:  { sleep=WaitingPowerOffSound, requested_sleep_kind=Normal }
  Event:  SystemSoundCompleted(power_off, cmd_id)
  Expect state: { sleep=WaitingBtShutdown }
  Expect effects: [ShutdownBt]

TEST: bt_shutdown_done_enters_sleep
  Given:  { sleep=WaitingBtShutdown, requested_sleep_kind=Normal }
  Event:  BtShutdownCompleted
  Expect state: { sleep=ReadyToSleep }
  Expect effects: [EnterDeepSleep(Normal)]

TEST: emergency_sleep_skips_sound
  Given:  { sleep=Awake, audio=PlayingFile, bt=Connected }
  Event:  SleepRequested(Emergency)
  Expect state: { sleep=PreparingDeepSleep, requested_sleep_kind=Emergency }
  Expect effects: [StopAudio(cmd_id≠0)]

TEST: audio_stopped_emergency_skips_sound
  Given:  { sleep=PreparingDeepSleep, requested_sleep_kind=Emergency, bt=Connected }
  Event:  AudioStopped(cmd_id)
  Expect state: { sleep=WaitingBtShutdown }
  Expect effects: [ShutdownBt]

TEST: track_ended_ignored_during_sleep_prep
  Given:  { sleep=PreparingDeepSleep, audio=Stopping }
  Event:  TrackEnded
  Expect state: { (unchanged) }
  Expect effects: []

TEST: emergency_sleep_upgrade_from_normal_prep
  Given:  { sleep=PreparingDeepSleep, requested_sleep_kind=Normal }
  Event:  SleepRequested(Emergency)
  Expect state: { sleep=PreparingDeepSleep, requested_sleep_kind=Emergency }
  Expect effects: []

TEST: system_sound_failed_still_advances_sleep
  Given:  { sleep=WaitingPowerOffSound, requested_sleep_kind=Normal }
  Event:  SystemSoundFailed(power_off, PlaybackTimeout, cmd_id)
  Expect state: { sleep=WaitingBtShutdown }
  Expect effects: [ShutdownBt]
```

### Failure and Recovery Tests

```
TEST: nfc_playback_failed_transitions_to_idle
  Given:  { audio=StartingFile, bt=Connected }
  Event:  NfcPlaybackStartFailed("04:AA:BB", MappingNotFound, cmd_id)
  Expect state: { audio=Idle, pending=None }
  Expect effects: [LogDiagnostic(code)]

TEST: bt_shutdown_failed_still_enters_sleep
  Given:  { sleep=WaitingBtShutdown, requested_sleep_kind=Normal }
  Event:  BtShutdownFailed(DisableTimeout)
  Expect state: { sleep=ReadyToSleep }
  Expect effects: [EnterDeepSleep(Normal)]

TEST: jbl_recovery_timeout_triggers_pulse
  Given:  { bt=Connected, jbl_recovery_deadline_ms=1000 }
  Event:  JblRecoveryTimeoutFired
  now_ms: 1001
  Expect state: { bt=RecoveryPulsePending, jbl_recovery_deadline_ms=0 }
  Expect effects: [TriggerBtRecoveryPulse(cmd_id≠0)]

TEST: music_track_ended_advances_track
  Given:  { audio=PlayingFile, playback=Music, current_track={valid=true, index=2}, bt=Connected }
  Event:  TrackEnded
  Expect state: { audio=StartingFile, current_track={valid=true, index=3} }
  Expect effects: [StartMusicTrackByIndex(3, cmd_id≠0)]

TEST: music_track_ended_wraps_to_first
  Given:  { audio=PlayingFile, playback=Music, current_track={valid=true, index=<last>},
            total_track_count=5, bt=Connected }
  Event:  TrackEnded
  Expect state: { current_track={valid=true, index=0} }
  Expect effects: [StartMusicTrackByIndex(0, cmd_id≠0)]
```

### LED Derivation Tests

```
TEST: derive_scene_sleep_prep_returns_sleep_ready
  Given state: { sleep=PreparingDeepSleep }
  Expect: deriveLedScene(state).type == LedSceneType::SleepReady

TEST: derive_scene_bt_waiting_returns_wait_bt
  Given state: { boot=Ready, bt=WaitingForSpeaker, sleep=Awake }
  Expect: deriveLedScene(state).type == LedSceneType::WaitBt

TEST: derive_scene_playing_returns_playing
  Given state: { boot=Ready, bt=Connected, audio=PlayingFile, sleep=Awake }
  Expect: deriveLedScene(state).type == LedSceneType::Playing

TEST: derive_scene_night_light_returns_night_light
  Given state: { session=NightLight, boot=Ready }
  Expect: deriveLedScene(state).type == LedSceneType::NightLight

TEST: volume_overlay_params_update
  Given state A: { volume_overlay_deadline_ms=<active>, volume_overlay_level_percent=40 }
  Given state B: { volume_overlay_deadline_ms=<active>, volume_overlay_level_percent=60 }
  Expect: deriveLedScene(A).params.volume.percent == 40
  Expect: deriveLedScene(B).params.volume.percent == 60
```

**Button Decoder Tests** — see [11_button_adapter.md §Native Testing](11_button_adapter.md).
Run via `pio test -e native` as part of `test_button_decoder` suite.

**Mock Fidelity Contract** — see [10b_test_hardware.md §Adapter Mock Requirements](10b_test_hardware.md).
