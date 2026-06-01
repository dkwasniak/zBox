#include <unity.h>
#include <cstring>
#include "reducer.h"
#include "led_scene.h"
#include "volume_scale.h"

#include "reducer.cpp"
#include "led_scene.cpp"

static AppState defaultState() {
    AppState s{};
    s.session_mode = SessionMode::Normal;
    s.playback_mode = PlaybackMode::Nfc;
    s.output_mode = AudioOutputMode::LocalSpeaker;
    s.bt_headphones_state = BtHeadphonesState::Inactive;
    s.output_volume_level = OUTPUT_VOL_LEVEL_DEFAULT;
    return s;
}

static bool hasEffect(const ReduceResult& r, EffectType type) {
    for (uint8_t i = 0; i < r.effect_count; i++) {
        if (r.effects[i].type == type) return true;
    }
    return false;
}

static const Effect* findEffect(const ReduceResult& r, EffectType type) {
    for (uint8_t i = 0; i < r.effect_count; i++) {
        if (r.effects[i].type == type) return &r.effects[i];
    }
    return nullptr;
}

void setUp() {}
void tearDown() {}

void test_boot_init_normal_is_ready_without_bt_wait() {
    AppState s = defaultState();
    s.boot_state = BootState::NormalBootInit;
    auto r = reduce(s, makeEvent(EventType::BootInitCompleted), 1000);
    TEST_ASSERT_EQUAL_INT((int)BootState::Ready, (int)r.next_state.boot_state);
    TEST_ASSERT_EQUAL_INT((int)AudioOutputMode::LocalSpeaker, (int)r.next_state.output_mode);
    TEST_ASSERT_EQUAL_INT((int)BtHeadphonesState::Inactive, (int)r.next_state.bt_headphones_state);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingSystemSound, (int)r.next_state.audio_state);
    TEST_ASSERT_EQUAL_UINT32(1000 + 10UL * 60 * 1000, r.next_state.idle_deadline_ms);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetOutputVolume));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PlaySystemSound));
    TEST_ASSERT_EQUAL_UINT8(20, findEffect(r, EffectType::SetOutputVolume)->payload.volume.level_percent);
    TEST_ASSERT_EQUAL_UINT8(SOUND_ID_STARTUP, findEffect(r, EffectType::PlaySystemSound)->payload.system_sound.sound_id);
}

void test_boot_init_music_autostarts_local_playback() {
    AppState s = defaultState();
    s.boot_state = BootState::NormalBootInit;
    s.playback_mode = PlaybackMode::Music;
    auto r = reduce(s, makeEvent(EventType::BootInitCompleted), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingSystemSound, (int)r.next_state.audio_state);
    TEST_ASSERT_EQUAL_INT((int)PendingPlaybackKind::MusicCurrentTrack, (int)r.next_state.pending_playback.kind);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetOutputVolume));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PlaySystemSound));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::StartMusicTrackByIndex));
}

void test_playback_mode_loaded_updates_state_before_boot_completion() {
    AppState s = defaultState();
    auto r = reduce(s, makePlaybackModeLoadedEvent(PlaybackMode::Music), 0);
    TEST_ASSERT_EQUAL_INT((int)PlaybackMode::Music, (int)r.next_state.playback_mode);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

void test_boot_init_prescanned_nfc_autostarts() {
    AppState s = defaultState();
    s.boot_state = BootState::NormalBootInit;
    s.pending_playback.kind = PendingPlaybackKind::NfcUid;
    strcpy(s.pending_playback.uid, "04:AA:BB");
    auto r = reduce(s, makeEvent(EventType::BootInitCompleted), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingSystemSound, (int)r.next_state.audio_state);
    TEST_ASSERT_EQUAL_INT((int)PendingPlaybackKind::NfcUid, (int)r.next_state.pending_playback.kind);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetOutputVolume));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PlaySystemSound));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::StartNfcPlaybackByUid));
}

void test_sleep_hold_warning_starts_sleep_when_audio_idle() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.audio_state = AudioState::Idle;
    auto r = reduce(s, makeEvent(EventType::SleepHoldWarning), 1000);
    TEST_ASSERT_EQUAL_INT((int)SleepState::WaitingPowerOffSound, (int)r.next_state.sleep_state);
    TEST_ASSERT_EQUAL_INT((int)RequestedSleepKind::Normal, (int)r.next_state.requested_sleep_kind);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PlaySystemSound));
    TEST_ASSERT_EQUAL_UINT8(SOUND_ID_POWER_OFF, findEffect(r, EffectType::PlaySystemSound)->payload.system_sound.sound_id);
    TEST_ASSERT_NOT_EQUAL(0, r.next_state.sleep_transition_deadline_ms);
    TEST_ASSERT_EQUAL_UINT32(0, r.next_state.idle_deadline_ms);
}

void test_sleep_hold_warning_stops_audio_when_playing() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.audio_state = AudioState::PlayingFile;
    auto r = reduce(s, makeEvent(EventType::SleepHoldWarning), 1000);
    TEST_ASSERT_EQUAL_INT((int)SleepState::PreparingDeepSleep, (int)r.next_state.sleep_state);
    TEST_ASSERT_EQUAL_INT((int)RequestedSleepKind::Normal, (int)r.next_state.requested_sleep_kind);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StopAudio));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::PlaySystemSound));
}

void test_sleep_hold_warning_ignored_when_already_preparing() {
    AppState s = defaultState();
    s.sleep_state = SleepState::PreparingDeepSleep;
    s.requested_sleep_kind = RequestedSleepKind::Normal;
    auto r = reduce(s, makeEvent(EventType::SleepHoldWarning), 1000);
    TEST_ASSERT_EQUAL_INT((int)SleepState::PreparingDeepSleep, (int)r.next_state.sleep_state);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

void test_nfc_detected_starts_immediately_on_local_output() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    auto r = reduce(s, makeNfcDetectedEvent("04:AA:BB"), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartNfcPlaybackByUid));
}

void test_playpause_idle_in_music_starts_current_track() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Music;
    s.current_track.valid = true;
    s.current_track.index = 3;
    auto r = reduce(s, makeEvent(EventType::PlayPausePressed), 2000);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartMusicTrackByIndex));
    TEST_ASSERT_EQUAL_UINT16(3, findEffect(r, EffectType::StartMusicTrackByIndex)->payload.music_track.index);
}

void test_track_end_in_music_advances_without_bt_dependency() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Music;
    s.audio_state = AudioState::PlayingFile;
    s.current_track.valid = true;
    s.current_track.index = 1;
    s.total_track_count = 3;
    auto r = reduce(s, makeEvent(EventType::TrackEnded), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_EQUAL_UINT16(2, r.next_state.current_track.index);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartMusicTrackByIndex));
}

void test_volume_loaded_uses_level_without_conversion() {
    AppState s = defaultState();
    auto r = reduce(s, makeVolumeLoadedEvent(11), 0);
    TEST_ASSERT_EQUAL_UINT8(11, r.next_state.output_volume_level);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

void test_volume_up_steps_one_level_and_uses_scaled_percent() {
    AppState s = defaultState();
    s.output_volume_level = 7;
    auto r = reduce(s, makeEvent(EventType::VolumeUpPressed), 500);
    TEST_ASSERT_EQUAL_UINT8(8, r.next_state.output_volume_level);
    TEST_ASSERT_EQUAL_UINT8(8, r.next_state.volume_overlay_level);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetOutputVolume));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::PersistVolume)); // deferred to VolumeOverlayExpired
    TEST_ASSERT_EQUAL_UINT8(25, findEffect(r, EffectType::SetOutputVolume)->payload.volume.level_percent);
}

void test_volume_down_steps_one_level_and_clamps_at_mute() {
    AppState s = defaultState();
    s.output_volume_level = 1;
    auto r = reduce(s, makeEvent(EventType::VolumeDownPressed), 500);
    TEST_ASSERT_EQUAL_UINT8(0, r.next_state.output_volume_level);
    TEST_ASSERT_EQUAL_UINT8(0, r.next_state.volume_overlay_level);
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::PersistVolume)); // deferred to VolumeOverlayExpired
    TEST_ASSERT_EQUAL_UINT8(0, findEffect(r, EffectType::SetOutputVolume)->payload.volume.level_percent);
}

void test_volume_up_clamps_at_max_level() {
    AppState s = defaultState();
    s.output_volume_level = OUTPUT_VOL_LEVEL_MAX;
    auto r = reduce(s, makeEvent(EventType::VolumeUpPressed), 500);
    TEST_ASSERT_EQUAL_UINT8(OUTPUT_VOL_LEVEL_MAX, r.next_state.output_volume_level);
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::SetOutputVolume));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::PersistVolume));
}

void test_volume_down_at_mute_is_noop() {
    AppState s = defaultState();
    s.output_volume_level = OUTPUT_VOL_LEVEL_MIN;
    auto r = reduce(s, makeEvent(EventType::VolumeDownPressed), 500);
    TEST_ASSERT_EQUAL_UINT8(OUTPUT_VOL_LEVEL_MIN, r.next_state.output_volume_level);
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::SetOutputVolume));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::PersistVolume));
}

void test_long_a_request_enters_bt_headphones_wait_state() {
    AppState s = defaultState();
    auto r = reduce(s, makeEvent(EventType::BtHeadphonesModeRequested), 0);
    TEST_ASSERT_TRUE(r.next_state.bt_headphones_mode_active);
    TEST_ASSERT_EQUAL_INT((int)BtHeadphonesState::WaitingForHeadphones, (int)r.next_state.bt_headphones_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartBtHeadphonesMode));
}

void test_bt_connected_switches_output_to_headphones() {
    AppState s = defaultState();
    s.bt_headphones_mode_active = true;
    s.bt_headphones_state = BtHeadphonesState::WaitingForHeadphones;
    s.output_volume_level = 10;
    auto r = reduce(s, makeEvent(EventType::BtConnected), 0);
    TEST_ASSERT_EQUAL_INT((int)BtHeadphonesState::Connected, (int)r.next_state.bt_headphones_state);
    TEST_ASSERT_EQUAL_INT((int)AudioOutputMode::BtHeadphones, (int)r.next_state.output_mode);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetOutputVolume));
    TEST_ASSERT_EQUAL_UINT8(45, findEffect(r, EffectType::SetOutputVolume)->payload.volume.level_percent);
}

void test_bt_disconnected_keeps_bt_output_and_waits() {
    AppState s = defaultState();
    s.bt_headphones_mode_active = true;
    s.bt_headphones_state = BtHeadphonesState::Connected;
    s.output_mode = AudioOutputMode::BtHeadphones;
    auto r = reduce(s, makeEvent(EventType::BtDisconnected), 0);
    TEST_ASSERT_EQUAL_INT((int)BtHeadphonesState::WaitingForHeadphones, (int)r.next_state.bt_headphones_state);
    TEST_ASSERT_EQUAL_INT((int)AudioOutputMode::BtHeadphones, (int)r.next_state.output_mode);
    TEST_ASSERT_TRUE(r.next_state.bt_headphones_mode_active);
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::StopAudio));
}

void test_bt_disconnected_while_playing_stops_without_pending_resume() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Music;
    s.audio_state = AudioState::PlayingFile;
    s.current_track.valid = true;
    s.current_track.index = 4;
    s.bt_headphones_mode_active = true;
    s.bt_headphones_state = BtHeadphonesState::Connected;
    s.output_mode = AudioOutputMode::BtHeadphones;
    s.pending_playback.kind = PendingPlaybackKind::MusicSpecificTrack;
    s.pending_playback.track_index = 4;

    auto r = reduce(s, makeEvent(EventType::BtDisconnected), 0);

    TEST_ASSERT_TRUE(r.next_state.bt_headphones_mode_active);
    TEST_ASSERT_EQUAL_INT((int)BtHeadphonesState::WaitingForHeadphones, (int)r.next_state.bt_headphones_state);
    TEST_ASSERT_EQUAL_INT((int)AudioOutputMode::BtHeadphones, (int)r.next_state.output_mode);
    TEST_ASSERT_EQUAL_INT((int)AudioState::Stopping, (int)r.next_state.audio_state);
    TEST_ASSERT_EQUAL_INT((int)PendingPlaybackKind::None, (int)r.next_state.pending_playback.kind);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StopAudio));
}

void test_bt_reconnected_after_disconnect_does_not_resume_stopped_track() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Music;
    s.audio_state = AudioState::Idle;
    s.bt_headphones_mode_active = true;
    s.bt_headphones_state = BtHeadphonesState::WaitingForHeadphones;
    s.output_mode = AudioOutputMode::BtHeadphones;
    s.pending_playback.kind = PendingPlaybackKind::None;

    auto r = reduce(s, makeEvent(EventType::BtConnected), 0);

    TEST_ASSERT_EQUAL_INT((int)BtHeadphonesState::Connected, (int)r.next_state.bt_headphones_state);
    TEST_ASSERT_EQUAL_INT((int)AudioOutputMode::BtHeadphones, (int)r.next_state.output_mode);
    TEST_ASSERT_EQUAL_INT((int)AudioState::Idle, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetOutputVolume));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::StartMusicTrackByIndex));
}

void test_second_bt_request_exits_headphones_mode() {
    AppState s = defaultState();
    s.bt_headphones_mode_active = true;
    s.bt_headphones_state = BtHeadphonesState::Connected;
    s.output_mode = AudioOutputMode::BtHeadphones;
    auto r = reduce(s, makeEvent(EventType::BtHeadphonesModeRequested), 0);
    TEST_ASSERT_FALSE(r.next_state.bt_headphones_mode_active);
    TEST_ASSERT_EQUAL_INT((int)BtHeadphonesState::Stopping, (int)r.next_state.bt_headphones_state);
    TEST_ASSERT_EQUAL_INT((int)AudioOutputMode::LocalSpeaker, (int)r.next_state.output_mode);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StopBtHeadphonesMode));
}

void test_bt_request_while_music_playing_stops_audio_before_output_switch() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Music;
    s.audio_state = AudioState::PlayingFile;
    s.current_track.valid = true;
    s.current_track.index = 4;

    auto r = reduce(s, makeEvent(EventType::BtHeadphonesModeRequested), 0);

    TEST_ASSERT_TRUE(r.next_state.bt_headphones_mode_active);
    TEST_ASSERT_EQUAL_INT((int)BtHeadphonesState::WaitingForHeadphones, (int)r.next_state.bt_headphones_state);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StoppingForOutputChange, (int)r.next_state.audio_state);
    TEST_ASSERT_EQUAL_INT((int)PendingPlaybackKind::MusicSpecificTrack, (int)r.next_state.pending_playback.kind);
    TEST_ASSERT_EQUAL_UINT16(4, r.next_state.pending_playback.track_index);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StopAudio));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::StartBtHeadphonesMode));
}

void test_audio_stopped_for_output_switch_starts_bt_before_pending_music() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Music;
    s.audio_state = AudioState::StoppingForOutputChange;
    s.bt_headphones_mode_active = true;
    s.bt_headphones_state = BtHeadphonesState::WaitingForHeadphones;
    s.pending_playback.kind = PendingPlaybackKind::MusicSpecificTrack;
    s.pending_playback.track_index = 4;

    auto r = reduce(s, makeAudioStoppedEvent(7), 0);

    TEST_ASSERT_EQUAL_INT((int)AudioState::Idle, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartBtHeadphonesMode));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::StartMusicTrackByIndex));
}

void test_bt_connected_after_output_switch_starts_pending_music() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Music;
    s.audio_state = AudioState::Idle;
    s.bt_headphones_mode_active = true;
    s.bt_headphones_state = BtHeadphonesState::WaitingForHeadphones;
    s.pending_playback.kind = PendingPlaybackKind::MusicSpecificTrack;
    s.pending_playback.track_index = 4;

    auto r = reduce(s, makeEvent(EventType::BtConnected), 0);

    TEST_ASSERT_EQUAL_INT((int)AudioOutputMode::BtHeadphones, (int)r.next_state.output_mode);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetOutputVolume));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartMusicTrackByIndex));
    TEST_ASSERT_EQUAL_UINT16(4, findEffect(r, EffectType::StartMusicTrackByIndex)->payload.music_track.index);
}

void test_bt_request_to_local_while_nfc_playing_stops_audio_before_bt_stop() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Nfc;
    s.audio_state = AudioState::PlayingFile;
    s.bt_headphones_mode_active = true;
    s.bt_headphones_state = BtHeadphonesState::Connected;
    s.output_mode = AudioOutputMode::BtHeadphones;
    strcpy(s.last_nfc_uid, "04:AA:BB");

    auto r = reduce(s, makeEvent(EventType::BtHeadphonesModeRequested), 0);

    TEST_ASSERT_FALSE(r.next_state.bt_headphones_mode_active);
    TEST_ASSERT_EQUAL_INT((int)BtHeadphonesState::Stopping, (int)r.next_state.bt_headphones_state);
    TEST_ASSERT_EQUAL_INT((int)AudioOutputMode::LocalSpeaker, (int)r.next_state.output_mode);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StoppingForOutputChange, (int)r.next_state.audio_state);
    TEST_ASSERT_EQUAL_INT((int)PendingPlaybackKind::NfcUid, (int)r.next_state.pending_playback.kind);
    TEST_ASSERT_EQUAL_STRING("04:AA:BB", r.next_state.pending_playback.uid);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StopAudio));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::StopBtHeadphonesMode));
}

void test_bt_stopped_after_output_switch_starts_pending_nfc_locally() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Nfc;
    s.audio_state = AudioState::Idle;
    s.bt_headphones_mode_active = false;
    s.bt_headphones_state = BtHeadphonesState::Stopping;
    s.output_mode = AudioOutputMode::LocalSpeaker;
    s.pending_playback.kind = PendingPlaybackKind::NfcUid;
    strcpy(s.pending_playback.uid, "04:AA:BB");

    auto r = reduce(s, makeEvent(EventType::BtHeadphonesModeStopped), 0);

    TEST_ASSERT_EQUAL_INT((int)AudioOutputMode::LocalSpeaker, (int)r.next_state.output_mode);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartNfcPlaybackByUid));
    TEST_ASSERT_EQUAL_STRING("04:AA:BB", findEffect(r, EffectType::StartNfcPlaybackByUid)->payload.nfc_playback.uid);
}

void test_bt_mode_stopped_completes_exit() {
    AppState s = defaultState();
    s.bt_headphones_mode_active = true;
    s.bt_headphones_state = BtHeadphonesState::Stopping;
    auto r = reduce(s, makeEvent(EventType::BtHeadphonesModeStopped), 0);
    TEST_ASSERT_FALSE(r.next_state.bt_headphones_mode_active);
    TEST_ASSERT_EQUAL_INT((int)BtHeadphonesState::Inactive, (int)r.next_state.bt_headphones_state);
    TEST_ASSERT_EQUAL_INT((int)AudioOutputMode::LocalSpeaker, (int)r.next_state.output_mode);
}

void test_idle_sleep_local_skips_bt_shutdown() {
    AppState s = defaultState();
    auto r = reduce(s, makeEvent(EventType::IdleTimeoutFired), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::WaitingPowerOffSound, (int)r.next_state.sleep_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PlaySystemSound));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::StopBtHeadphonesMode));
}

void test_system_sound_after_local_sleep_enters_deep_sleep_directly() {
    AppState s = defaultState();
    s.sleep_state = SleepState::WaitingPowerOffSound;
    s.requested_sleep_kind = RequestedSleepKind::Normal;
    auto r = reduce(s, makeSystemSoundCompletedEvent(SOUND_ID_POWER_OFF, 0), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::ReadyToSleep, (int)r.next_state.sleep_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::EnterDeepSleep));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::StopBtHeadphonesMode));
}

void test_startup_sound_completion_restores_user_volume_and_starts_pending_music() {
    AppState s = defaultState();
    s.audio_state = AudioState::StartingSystemSound;
    s.playback_mode = PlaybackMode::Music;
    s.output_volume_level = 10;
    s.pending_playback.kind = PendingPlaybackKind::MusicCurrentTrack;
    s.current_track.valid = true;
    s.current_track.index = 2;
    auto r = reduce(s, makeSystemSoundCompletedEvent(SOUND_ID_STARTUP, 0), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetOutputVolume));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartMusicTrackByIndex));
    TEST_ASSERT_EQUAL_UINT8(45, findEffect(r, EffectType::SetOutputVolume)->payload.volume.level_percent);
    TEST_ASSERT_EQUAL_UINT16(2, findEffect(r, EffectType::StartMusicTrackByIndex)->payload.music_track.index);
}

void test_system_sound_failure_restores_user_volume_when_not_sleeping() {
    AppState s = defaultState();
    s.audio_state = AudioState::StartingSystemSound;
    s.output_volume_level = 8;
    auto r = reduce(s, makeSystemSoundFailedEvent(SOUND_ID_NFC_MODE, SoundFailReason::FileNotFound, 0), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::Idle, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetOutputVolume));
    TEST_ASSERT_EQUAL_UINT8(25, findEffect(r, EffectType::SetOutputVolume)->payload.volume.level_percent);
}

void test_audio_command_rejected_during_nfc_start_returns_to_idle() {
    AppState s = defaultState();
    s.audio_state = AudioState::StartingFile;
    s.playback_mode = PlaybackMode::Nfc;
    s.pending_playback.kind = PendingPlaybackKind::NfcUid;
    strcpy(s.pending_playback.uid, "04:AA:BB");
    auto r = reduce(s, makeAudioCommandRejectedEvent(17, PlaybackFailReason::AudioCommandRejected), 1000);
    TEST_ASSERT_EQUAL_INT((int)AudioState::Idle, (int)r.next_state.audio_state);
    TEST_ASSERT_EQUAL_INT((int)PendingPlaybackKind::None, (int)r.next_state.pending_playback.kind);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::LogDiagnostic));
}

void test_audio_command_rejected_during_music_start_returns_to_idle() {
    AppState s = defaultState();
    s.audio_state = AudioState::StartingFile;
    s.playback_mode = PlaybackMode::Music;
    auto r = reduce(s, makeAudioCommandRejectedEvent(18, PlaybackFailReason::AudioCommandRejected), 1000);
    TEST_ASSERT_EQUAL_INT((int)AudioState::Idle, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::LogDiagnostic));
}

void test_audio_command_rejected_during_mode_toggle_plays_mode_sound() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Music;
    s.audio_state = AudioState::StoppingForModeChange;
    auto r = reduce(s, makeAudioCommandRejectedEvent(19, PlaybackFailReason::AudioCommandRejected), 1000);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingSystemSound, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetOutputVolume));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PlaySystemSound));
    TEST_ASSERT_EQUAL_UINT8(SOUND_ID_MUSIC_MODE, findEffect(r, EffectType::PlaySystemSound)->payload.system_sound.sound_id);
}

void test_poweroff_system_sound_failure_enters_sleep_path() {
    AppState s = defaultState();
    s.sleep_state = SleepState::WaitingPowerOffSound;
    s.requested_sleep_kind = RequestedSleepKind::Normal;
    auto r = reduce(s, makeSystemSoundFailedEvent(SOUND_ID_POWER_OFF, SoundFailReason::AudioCommandRejected, 20), 1000);
    TEST_ASSERT_EQUAL_INT((int)SleepState::ReadyToSleep, (int)r.next_state.sleep_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::EnterDeepSleep));
}

void test_system_sound_after_bt_sleep_stops_bt_first() {
    AppState s = defaultState();
    s.sleep_state = SleepState::WaitingPowerOffSound;
    s.requested_sleep_kind = RequestedSleepKind::Normal;
    s.bt_headphones_mode_active = true;
    auto r = reduce(s, makeSystemSoundCompletedEvent(SOUND_ID_POWER_OFF, 0), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::WaitingBtHeadphonesStop, (int)r.next_state.sleep_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StopBtHeadphonesMode));
}

void test_bt_stop_during_sleep_enters_deep_sleep() {
    AppState s = defaultState();
    s.sleep_state = SleepState::WaitingBtHeadphonesStop;
    s.requested_sleep_kind = RequestedSleepKind::Normal;
    s.bt_headphones_mode_active = true;
    auto r = reduce(s, makeEvent(EventType::BtHeadphonesModeStopped), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::ReadyToSleep, (int)r.next_state.sleep_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::EnterDeepSleep));
}

void test_mode_toggle_to_music_plays_mode_sound_and_persists() {
    AppState s = defaultState();
    auto r = reduce(s, makeEvent(EventType::ModeToggleRequested), 0);
    TEST_ASSERT_EQUAL_INT((int)PlaybackMode::Music, (int)r.next_state.playback_mode);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingSystemSound, (int)r.next_state.audio_state);
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::StopAudio));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetOutputVolume));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PlaySystemSound));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PersistPlaybackMode));
    TEST_ASSERT_EQUAL_UINT8(20, findEffect(r, EffectType::SetOutputVolume)->payload.volume.level_percent);
    TEST_ASSERT_EQUAL_UINT8(SOUND_ID_MUSIC_MODE, findEffect(r, EffectType::PlaySystemSound)->payload.system_sound.sound_id);
}

void test_mode_toggle_while_playing_stops_before_mode_sound() {
    AppState s = defaultState();
    s.audio_state = AudioState::PlayingFile;
    auto r = reduce(s, makeEvent(EventType::ModeToggleRequested), 1000);
    TEST_ASSERT_EQUAL_INT((int)PlaybackMode::Music, (int)r.next_state.playback_mode);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StoppingForModeChange, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StopAudio));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::PlaySystemSound));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PersistPlaybackMode));
}

void test_audio_stopped_after_mode_toggle_plays_music_mode_sound() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Music;
    s.audio_state = AudioState::StoppingForModeChange;
    auto r = reduce(s, makeAudioStoppedEvent(7), 1000);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingSystemSound, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetOutputVolume));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PlaySystemSound));
    TEST_ASSERT_EQUAL_UINT8(SOUND_ID_MUSIC_MODE, findEffect(r, EffectType::PlaySystemSound)->payload.system_sound.sound_id);
}

void test_audio_stopped_after_mode_toggle_plays_nfc_mode_sound() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Nfc;
    s.audio_state = AudioState::StoppingForModeChange;
    auto r = reduce(s, makeAudioStoppedEvent(7), 1000);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingSystemSound, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetOutputVolume));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PlaySystemSound));
    TEST_ASSERT_EQUAL_UINT8(SOUND_ID_NFC_MODE, findEffect(r, EffectType::PlaySystemSound)->payload.system_sound.sound_id);
}

void test_mode_sound_completion_autostarts_music() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Music;
    s.current_track.valid = true;
    s.current_track.index = 2;
    auto r = reduce(s, makeSystemSoundCompletedEvent(SOUND_ID_MUSIC_MODE, 0), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetOutputVolume));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartMusicTrackByIndex));
    TEST_ASSERT_EQUAL_UINT8(18, findEffect(r, EffectType::SetOutputVolume)->payload.volume.level_percent);
}

void test_battery_check_updates_preview_and_idle_deadline() {
    AppState s = defaultState();
    auto r = reduce(s, makeBatteryCheckEvent(4), 3000);
    TEST_ASSERT_TRUE(r.next_state.battery_preview_active);
    TEST_ASSERT_EQUAL_UINT8(4, r.next_state.battery_bars);
    TEST_ASSERT_EQUAL_UINT32(3000 + 3000, r.next_state.battery_preview_deadline_ms);
    TEST_ASSERT_EQUAL_UINT32(3000 + 10UL * 60 * 1000, r.next_state.idle_deadline_ms);
}

void test_led_wait_scene_only_in_active_bt_mode() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.bt_headphones_mode_active = true;
    s.bt_headphones_state = BtHeadphonesState::WaitingForHeadphones;
    auto scene = deriveLedScene(s);
    TEST_ASSERT_EQUAL_INT((int)LedSceneType::WaitBt, (int)scene.type);

    s.bt_headphones_mode_active = false;
    scene = deriveLedScene(s);
    TEST_ASSERT_NOT_EQUAL((int)LedSceneType::WaitBt, (int)scene.type);
}

void test_led_scene_off_when_bt_headphones_connected() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.bt_headphones_mode_active = true;
    s.bt_headphones_state = BtHeadphonesState::Connected;
    s.output_mode = AudioOutputMode::BtHeadphones;
    auto scene = deriveLedScene(s);
    TEST_ASSERT_EQUAL_INT((int)LedSceneType::Off, (int)scene.type);
}

void test_led_scene_off_when_output_is_bt_headphones() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.audio_state = AudioState::PlayingFile;
    s.output_mode = AudioOutputMode::BtHeadphones;
    auto scene = deriveLedScene(s);
    TEST_ASSERT_EQUAL_INT((int)LedSceneType::Off, (int)scene.type);
}

void test_led_volume_overlay_shown_in_bt_headphones_mode() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.bt_headphones_mode_active = true;
    s.bt_headphones_state = BtHeadphonesState::Connected;
    s.volume_overlay_deadline_ms = 1;
    s.volume_overlay_level = 12;
    auto scene = deriveLedScene(s);
    TEST_ASSERT_EQUAL_INT((int)LedSceneType::VolumeOverlay, (int)scene.type);
    TEST_ASSERT_EQUAL_UINT8(12, scene.params.volume.level);
}

void test_led_battery_preview_shown_in_bt_headphones_mode() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.bt_headphones_mode_active = true;
    s.bt_headphones_state = BtHeadphonesState::Connected;
    s.battery_preview_active = true;
    s.battery_bars = 4;
    auto scene = deriveLedScene(s);
    TEST_ASSERT_EQUAL_INT((int)LedSceneType::BatteryPreview, (int)scene.type);
    TEST_ASSERT_EQUAL_UINT8(4, scene.params.battery.bars);
}

void test_led_volume_overlay_for_mute_shows_zero_leds() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.volume_overlay_deadline_ms = 1;
    s.volume_overlay_level = 0;
    auto scene = deriveLedScene(s);
    TEST_ASSERT_EQUAL_INT((int)LedSceneType::VolumeOverlay, (int)scene.type);
    TEST_ASSERT_EQUAL_UINT8(0, scene.params.volume.level);
}

void test_led_volume_overlay_for_level_twelve_shows_twelve_leds() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.volume_overlay_deadline_ms = 1;
    s.volume_overlay_level = 12;
    auto scene = deriveLedScene(s);
    TEST_ASSERT_EQUAL_INT((int)LedSceneType::VolumeOverlay, (int)scene.type);
    TEST_ASSERT_EQUAL_UINT8(12, scene.params.volume.level);
}

void test_nfc_track_end_sets_played_flag() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Nfc;
    s.audio_state = AudioState::PlayingFile;
    strcpy(s.last_nfc_uid, "04:AA:BB");
    auto r = reduce(s, makeEvent(EventType::TrackEnded), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::Idle, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(r.next_state.nfc_card_played);
}

void test_nfc_redetect_same_card_after_track_ended_is_ignored() {
    AppState s = defaultState();
    s.audio_state = AudioState::Idle;
    s.nfc_card_played = true;
    strcpy(s.last_nfc_uid, "04:AA:BB");
    auto r = reduce(s, makeNfcDetectedEvent("04:AA:BB"), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::Idle, (int)r.next_state.audio_state);
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::StartNfcPlaybackByUid));
}

void test_nfc_different_card_after_track_ended_plays() {
    AppState s = defaultState();
    s.audio_state = AudioState::Idle;
    s.nfc_card_played = true;
    strcpy(s.last_nfc_uid, "04:AA:BB");
    auto r = reduce(s, makeNfcDetectedEvent("04:CC:DD"), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartNfcPlaybackByUid));
    TEST_ASSERT_FALSE(r.next_state.nfc_card_played);
}

void test_nfc_removal_clears_played_flag() {
    AppState s = defaultState();
    s.audio_state = AudioState::Idle;
    s.nfc_card_played = true;
    strcpy(s.last_nfc_uid, "04:AA:BB");
    auto r = reduce(s, makeEvent(EventType::NfcTagRemoved), 0);
    TEST_ASSERT_FALSE(r.next_state.nfc_card_played);
}

void test_nfc_reinsertion_after_removal_plays() {
    AppState s = defaultState();
    s.audio_state = AudioState::Idle;
    s.nfc_card_played = false;
    strcpy(s.last_nfc_uid, "04:AA:BB");
    auto r = reduce(s, makeNfcDetectedEvent("04:AA:BB"), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartNfcPlaybackByUid));
}

// ----- Bug 1: sleep timeout -----

void test_sleep_timeout_in_waiting_bt_stops_forces_ready_to_sleep() {
    AppState s = defaultState();
    s.sleep_state = SleepState::WaitingBtHeadphonesStop;
    s.requested_sleep_kind = RequestedSleepKind::Normal;
    s.sleep_transition_deadline_ms = 1000;
    auto r = reduce(s, makeEvent(EventType::SleepTimeoutFired), 5000);
    TEST_ASSERT_EQUAL_INT((int)SleepState::ReadyToSleep, (int)r.next_state.sleep_state);
    TEST_ASSERT_EQUAL_UINT32(0, r.next_state.sleep_transition_deadline_ms);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::EnterDeepSleep));
}

void test_sleep_timeout_in_awake_is_noop() {
    AppState s = defaultState();
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::SleepTimeoutFired), 5000);
    TEST_ASSERT_EQUAL_INT((int)SleepState::Awake, (int)r.next_state.sleep_state);
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::EnterDeepSleep));
}

void test_sleep_timeout_in_ready_to_sleep_is_noop() {
    AppState s = defaultState();
    s.sleep_state = SleepState::ReadyToSleep;
    s.requested_sleep_kind = RequestedSleepKind::Normal;
    auto r = reduce(s, makeEvent(EventType::SleepTimeoutFired), 5000);
    TEST_ASSERT_EQUAL_INT((int)SleepState::ReadyToSleep, (int)r.next_state.sleep_state);
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::EnterDeepSleep));
}

void test_sleep_requested_arms_transition_deadline() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    auto r = reduce(s, makeSleepRequestedEvent(RequestedSleepKind::Normal), 1000);
    TEST_ASSERT_NOT_EQUAL((int)SleepState::Awake, (int)r.next_state.sleep_state);
    TEST_ASSERT_NOT_EQUAL(0, r.next_state.sleep_transition_deadline_ms);
}

void test_idle_timeout_arms_transition_deadline() {
    AppState s = defaultState();
    auto r = reduce(s, makeEvent(EventType::IdleTimeoutFired), 2000);
    TEST_ASSERT_NOT_EQUAL((int)SleepState::Awake, (int)r.next_state.sleep_state);
    TEST_ASSERT_NOT_EQUAL(0, r.next_state.sleep_transition_deadline_ms);
}

void test_ready_to_sleep_via_bt_stop_clears_deadline() {
    AppState s = defaultState();
    s.sleep_state = SleepState::WaitingBtHeadphonesStop;
    s.requested_sleep_kind = RequestedSleepKind::Normal;
    s.bt_headphones_mode_active = true;
    s.sleep_transition_deadline_ms = 9000;
    auto r = reduce(s, makeEvent(EventType::BtHeadphonesModeStopped), 5000);
    TEST_ASSERT_EQUAL_INT((int)SleepState::ReadyToSleep, (int)r.next_state.sleep_state);
    TEST_ASSERT_EQUAL_UINT32(0, r.next_state.sleep_transition_deadline_ms);
}

void test_sleep_transition_intermediate_does_not_reset_deadline() {
    // WaitingPowerOffSound → WaitingBtHeadphonesStop should preserve original budget
    AppState s = defaultState();
    s.sleep_state = SleepState::WaitingPowerOffSound;
    s.requested_sleep_kind = RequestedSleepKind::Normal;
    s.bt_headphones_mode_active = true;
    s.sleep_transition_deadline_ms = 5000; // original budget already armed
    auto r = reduce(s, makeSystemSoundCompletedEvent(SOUND_ID_POWER_OFF, 0), 2000);
    TEST_ASSERT_EQUAL_INT((int)SleepState::WaitingBtHeadphonesStop, (int)r.next_state.sleep_state);
    TEST_ASSERT_EQUAL_UINT32(5000, r.next_state.sleep_transition_deadline_ms); // unchanged
}

// ----- Bug 2: volume persist deferred -----

void test_volume_up_does_not_emit_persist_volume() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    auto r = reduce(s, makeEvent(EventType::VolumeUpPressed), 0);
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::PersistVolume));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetOutputVolume));
}

void test_volume_down_does_not_emit_persist_volume() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    auto r = reduce(s, makeEvent(EventType::VolumeDownPressed), 0);
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::PersistVolume));
}

void test_volume_overlay_expired_emits_persist_and_clears_deadline() {
    AppState s = defaultState();
    s.volume_overlay_deadline_ms = 1000;
    s.output_volume_level = 8;
    auto r = reduce(s, makeEvent(EventType::VolumeOverlayExpired), 1500); // now >= deadline
    TEST_ASSERT_EQUAL_UINT32(0, r.next_state.volume_overlay_deadline_ms);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PersistVolume));
}

void test_volume_overlay_expired_stale_does_nothing() {
    AppState s = defaultState();
    s.volume_overlay_deadline_ms = 2000; // deadline in future
    auto r = reduce(s, makeEvent(EventType::VolumeOverlayExpired), 500); // now < deadline
    TEST_ASSERT_EQUAL_UINT32(2000, r.next_state.volume_overlay_deadline_ms); // unchanged
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::PersistVolume));
}

void test_volume_overlay_expired_when_deadline_zero_does_nothing() {
    AppState s = defaultState();
    s.volume_overlay_deadline_ms = 0; // already cleared
    auto r = reduce(s, makeEvent(EventType::VolumeOverlayExpired), 5000);
    TEST_ASSERT_EQUAL_UINT32(0, r.next_state.volume_overlay_deadline_ms);
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::PersistVolume));
}

// ----- Additional task: no Playing animation in BT mode -----

void test_led_playing_scene_hidden_in_bt_headphones_mode() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.audio_state = AudioState::PlayingFile;
    s.output_mode = AudioOutputMode::BtHeadphones;
    auto scene = deriveLedScene(s);
    TEST_ASSERT_NOT_EQUAL((int)LedSceneType::Playing, (int)scene.type);
    TEST_ASSERT_EQUAL_INT((int)LedSceneType::Off, (int)scene.type);
}

void test_led_playing_scene_shown_on_local_speaker() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.audio_state = AudioState::PlayingFile;
    s.output_mode = AudioOutputMode::LocalSpeaker;
    auto scene = deriveLedScene(s);
    TEST_ASSERT_EQUAL_INT((int)LedSceneType::Playing, (int)scene.type);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_boot_init_normal_is_ready_without_bt_wait);
    RUN_TEST(test_boot_init_music_autostarts_local_playback);
    RUN_TEST(test_playback_mode_loaded_updates_state_before_boot_completion);
    RUN_TEST(test_boot_init_prescanned_nfc_autostarts);
    RUN_TEST(test_nfc_detected_starts_immediately_on_local_output);
    RUN_TEST(test_nfc_track_end_sets_played_flag);
    RUN_TEST(test_nfc_redetect_same_card_after_track_ended_is_ignored);
    RUN_TEST(test_nfc_different_card_after_track_ended_plays);
    RUN_TEST(test_nfc_removal_clears_played_flag);
    RUN_TEST(test_nfc_reinsertion_after_removal_plays);
    RUN_TEST(test_playpause_idle_in_music_starts_current_track);
    RUN_TEST(test_track_end_in_music_advances_without_bt_dependency);
    RUN_TEST(test_volume_loaded_uses_level_without_conversion);
    RUN_TEST(test_volume_up_steps_one_level_and_uses_scaled_percent);
    RUN_TEST(test_volume_down_steps_one_level_and_clamps_at_mute);
    RUN_TEST(test_volume_up_clamps_at_max_level);
    RUN_TEST(test_volume_down_at_mute_is_noop);
    RUN_TEST(test_long_a_request_enters_bt_headphones_wait_state);
    RUN_TEST(test_bt_connected_switches_output_to_headphones);
    RUN_TEST(test_bt_disconnected_keeps_bt_output_and_waits);
    RUN_TEST(test_bt_disconnected_while_playing_stops_without_pending_resume);
    RUN_TEST(test_bt_reconnected_after_disconnect_does_not_resume_stopped_track);
    RUN_TEST(test_second_bt_request_exits_headphones_mode);
    RUN_TEST(test_bt_request_while_music_playing_stops_audio_before_output_switch);
    RUN_TEST(test_audio_stopped_for_output_switch_starts_bt_before_pending_music);
    RUN_TEST(test_bt_connected_after_output_switch_starts_pending_music);
    RUN_TEST(test_bt_request_to_local_while_nfc_playing_stops_audio_before_bt_stop);
    RUN_TEST(test_bt_stopped_after_output_switch_starts_pending_nfc_locally);
    RUN_TEST(test_bt_mode_stopped_completes_exit);
    RUN_TEST(test_idle_sleep_local_skips_bt_shutdown);
    RUN_TEST(test_system_sound_after_local_sleep_enters_deep_sleep_directly);
    RUN_TEST(test_startup_sound_completion_restores_user_volume_and_starts_pending_music);
    RUN_TEST(test_system_sound_failure_restores_user_volume_when_not_sleeping);
    RUN_TEST(test_audio_command_rejected_during_nfc_start_returns_to_idle);
    RUN_TEST(test_audio_command_rejected_during_music_start_returns_to_idle);
    RUN_TEST(test_audio_command_rejected_during_mode_toggle_plays_mode_sound);
    RUN_TEST(test_poweroff_system_sound_failure_enters_sleep_path);
    RUN_TEST(test_system_sound_after_bt_sleep_stops_bt_first);
    RUN_TEST(test_bt_stop_during_sleep_enters_deep_sleep);
    RUN_TEST(test_mode_toggle_to_music_plays_mode_sound_and_persists);
    RUN_TEST(test_mode_toggle_while_playing_stops_before_mode_sound);
    RUN_TEST(test_audio_stopped_after_mode_toggle_plays_music_mode_sound);
    RUN_TEST(test_audio_stopped_after_mode_toggle_plays_nfc_mode_sound);
    RUN_TEST(test_mode_sound_completion_autostarts_music);
    RUN_TEST(test_battery_check_updates_preview_and_idle_deadline);
    RUN_TEST(test_led_wait_scene_only_in_active_bt_mode);
    RUN_TEST(test_led_scene_off_when_bt_headphones_connected);
    RUN_TEST(test_led_scene_off_when_output_is_bt_headphones);
    RUN_TEST(test_led_volume_overlay_shown_in_bt_headphones_mode);
    RUN_TEST(test_led_battery_preview_shown_in_bt_headphones_mode);
    RUN_TEST(test_led_volume_overlay_for_mute_shows_zero_leds);
    RUN_TEST(test_led_volume_overlay_for_level_twelve_shows_twelve_leds);
    RUN_TEST(test_sleep_timeout_in_waiting_bt_stops_forces_ready_to_sleep);
    RUN_TEST(test_sleep_timeout_in_awake_is_noop);
    RUN_TEST(test_sleep_timeout_in_ready_to_sleep_is_noop);
    RUN_TEST(test_sleep_hold_warning_starts_sleep_when_audio_idle);
    RUN_TEST(test_sleep_hold_warning_stops_audio_when_playing);
    RUN_TEST(test_sleep_hold_warning_ignored_when_already_preparing);
    RUN_TEST(test_sleep_requested_arms_transition_deadline);
    RUN_TEST(test_idle_timeout_arms_transition_deadline);
    RUN_TEST(test_ready_to_sleep_via_bt_stop_clears_deadline);
    RUN_TEST(test_sleep_transition_intermediate_does_not_reset_deadline);
    RUN_TEST(test_volume_up_does_not_emit_persist_volume);
    RUN_TEST(test_volume_down_does_not_emit_persist_volume);
    RUN_TEST(test_volume_overlay_expired_emits_persist_and_clears_deadline);
    RUN_TEST(test_volume_overlay_expired_stale_does_nothing);
    RUN_TEST(test_volume_overlay_expired_when_deadline_zero_does_nothing);
    RUN_TEST(test_led_playing_scene_hidden_in_bt_headphones_mode);
    RUN_TEST(test_led_playing_scene_shown_on_local_speaker);
    return UNITY_END();
}
