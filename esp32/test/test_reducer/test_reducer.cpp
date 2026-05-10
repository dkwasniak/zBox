#include <unity.h>
#include <cstring>
#include "reducer.h"
#include "led_scene.h"

// Include implementation units directly (PlatformIO native tests don't auto-link src/)
#include "reducer.cpp"
#include "led_scene.cpp"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static AppState defaultState() {
    AppState s{};
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

void setUp()    {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Boot transition tests
// ---------------------------------------------------------------------------

void test_boot_started_transitions_to_wake_hold_check() {
    AppState s = defaultState();
    s.boot_state = BootState::ColdBootInit;
    auto r = reduce(s, makeEvent(EventType::BootStarted), 0);
    TEST_ASSERT_EQUAL_INT((int)BootState::WakeHoldCheck, (int)r.next_state.boot_state);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

void test_wake_cause_normal_transitions_boot_init() {
    AppState s = defaultState();
    s.boot_state = BootState::WakeHoldCheck;
    auto r = reduce(s, makeEvent(EventType::WakeCauseResolvedNormal), 0);
    TEST_ASSERT_EQUAL_INT((int)BootState::NormalBootInit, (int)r.next_state.boot_state);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

void test_wake_cause_nightlight_sets_session() {
    AppState s = defaultState();
    s.boot_state = BootState::WakeHoldCheck;
    s.session_mode = SessionMode::Normal;
    auto r = reduce(s, makeEvent(EventType::WakeCauseResolvedNightLight), 0);
    TEST_ASSERT_EQUAL_INT((int)BootState::NightLightBootInit, (int)r.next_state.boot_state);
    TEST_ASSERT_EQUAL_INT((int)SessionMode::NightLight, (int)r.next_state.session_mode);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

void test_boot_init_completed_bt_starts_waiting() {
    AppState s = defaultState();
    s.boot_state = BootState::NormalBootInit;
    s.bt_state = BtState::Unknown;
    s.playback_mode = PlaybackMode::Nfc;
    auto r = reduce(s, makeEvent(EventType::BootInitCompleted), 0);
    TEST_ASSERT_EQUAL_INT((int)BootState::Ready, (int)r.next_state.boot_state);
    TEST_ASSERT_EQUAL_INT((int)BtState::WaitingForSpeaker, (int)r.next_state.bt_state);
    TEST_ASSERT_EQUAL_INT((int)PendingPlaybackKind::None, (int)r.next_state.pending_playback.kind);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

// BootInitCompleted in Music mode → sets MusicCurrentTrack pending so BT-connect triggers autoplay
void test_boot_music_mode_sets_music_pending() {
    AppState s = defaultState();
    s.boot_state = BootState::NormalBootInit;
    s.playback_mode = PlaybackMode::Music;
    auto r = reduce(s, makeEvent(EventType::BootInitCompleted), 0);
    TEST_ASSERT_EQUAL_INT((int)BtState::WaitingForSpeaker, (int)r.next_state.bt_state);
    TEST_ASSERT_EQUAL_INT((int)PendingPlaybackKind::MusicCurrentTrack,
                          (int)r.next_state.pending_playback.kind);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

// BtConnected with MusicCurrentTrack pending but no valid track → starts from track 0
void test_bt_connected_music_pending_no_track_starts_from_zero() {
    AppState s = defaultState();
    s.bt_state = BtState::WaitingForSpeaker;
    s.pending_playback.kind = PendingPlaybackKind::MusicCurrentTrack;
    s.current_track.valid = false;
    auto r = reduce(s, makeEvent(EventType::BtConnected), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(r.next_state.current_track.valid);
    TEST_ASSERT_EQUAL_UINT16(0, r.next_state.current_track.index);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartMusicTrackByIndex));
    TEST_ASSERT_EQUAL_UINT16(0, findEffect(r, EffectType::StartMusicTrackByIndex)->payload.music_track.index);
    TEST_ASSERT_EQUAL_INT((int)PendingPlaybackKind::None, (int)r.next_state.pending_playback.kind);
}

void test_boot_nightlight_bt_disabled() {
    AppState s = defaultState();
    s.boot_state = BootState::NightLightBootInit;
    s.session_mode = SessionMode::NightLight;
    auto r = reduce(s, makeEvent(EventType::BootInitCompleted), 0);
    TEST_ASSERT_EQUAL_INT((int)BootState::Ready, (int)r.next_state.boot_state);
    TEST_ASSERT_EQUAL_INT((int)BtState::Disabled, (int)r.next_state.bt_state);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

void test_nfc_prescan_found_sets_pending() {
    AppState s = defaultState();
    s.boot_state = BootState::NormalBootInit;
    auto r = reduce(s, makeNfcPrescanEvent(true, "04:AA:BB"), 0);
    TEST_ASSERT_EQUAL_INT((int)PendingPlaybackKind::NfcUid,
                          (int)r.next_state.pending_playback.kind);
    TEST_ASSERT_EQUAL_STRING("04:AA:BB", r.next_state.pending_playback.uid);
    TEST_ASSERT_EQUAL_STRING("04:AA:BB", r.next_state.last_nfc_uid);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

void test_nfc_prescan_not_found_no_pending() {
    AppState s = defaultState();
    s.boot_state = BootState::NormalBootInit;
    auto r = reduce(s, makeNfcPrescanEvent(false, nullptr), 0);
    TEST_ASSERT_EQUAL_INT((int)PendingPlaybackKind::None,
                          (int)r.next_state.pending_playback.kind);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

// ---------------------------------------------------------------------------
// BT and pending playback tests
// ---------------------------------------------------------------------------

void test_bt_connected_no_pending() {
    AppState s = defaultState();
    s.bt_state = BtState::WaitingForSpeaker;
    s.pending_playback.kind = PendingPlaybackKind::None;
    s.music_volume_percent = 60;
    auto r = reduce(s, makeEvent(EventType::BtConnected), 0);
    TEST_ASSERT_EQUAL_INT((int)BtState::Connected, (int)r.next_state.bt_state);
    TEST_ASSERT_FALSE(r.next_state.bt_volume_applied);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetVolume));
    const Effect* e = findEffect(r, EffectType::SetVolume);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT8(60, e->payload.volume.level_percent);
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_bt_connected_with_pending_nfc() {
    AppState s = defaultState();
    s.bt_state = BtState::WaitingForSpeaker;
    s.pending_playback.kind = PendingPlaybackKind::NfcUid;
    strncpy(s.pending_playback.uid, "04:AA:BB", sizeof(s.pending_playback.uid));
    s.audio_state = AudioState::Idle;
    auto r = reduce(s, makeEvent(EventType::BtConnected), 0);
    TEST_ASSERT_EQUAL_INT((int)BtState::Connected, (int)r.next_state.bt_state);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_FALSE(r.next_state.bt_volume_applied);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetVolume));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartNfcPlaybackByUid));
    const Effect* e = findEffect(r, EffectType::StartNfcPlaybackByUid);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_STRING("04:AA:BB", e->payload.nfc_playback.uid);
    TEST_ASSERT_EQUAL_UINT8(2, r.effect_count);
    // pending cleared
    TEST_ASSERT_EQUAL_INT((int)PendingPlaybackKind::None,
                          (int)r.next_state.pending_playback.kind);
}

void test_bt_connected_with_pending_music() {
    AppState s = defaultState();
    s.bt_state = BtState::WaitingForSpeaker;
    s.pending_playback.kind = PendingPlaybackKind::MusicCurrentTrack;
    s.current_track.valid = true;
    s.current_track.index = 3;
    auto r = reduce(s, makeEvent(EventType::BtConnected), 0);
    TEST_ASSERT_EQUAL_INT((int)BtState::Connected, (int)r.next_state.bt_state);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetVolume));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartMusicTrackByIndex));
    const Effect* e = findEffect(r, EffectType::StartMusicTrackByIndex);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(3, e->payload.music_track.index);
    TEST_ASSERT_EQUAL_UINT8(2, r.effect_count);
}

void test_bt_disconnected_clears_volume_flag() {
    AppState s = defaultState();
    s.bt_state = BtState::Connected;
    s.bt_volume_applied = true;
    s.audio_state = AudioState::PlayingFile;
    auto r = reduce(s, makeEvent(EventType::BtDisconnected), 0);
    TEST_ASSERT_EQUAL_INT((int)BtState::WaitingForSpeaker, (int)r.next_state.bt_state);
    TEST_ASSERT_FALSE(r.next_state.bt_volume_applied);
    TEST_ASSERT_EQUAL_INT((int)AudioState::Idle, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StopAudio));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

// ---------------------------------------------------------------------------
// NFC playback tests
// ---------------------------------------------------------------------------

void test_nfc_tag_detected_bt_ready() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.bt_state = BtState::Connected;
    s.session_mode = SessionMode::Normal;
    s.playback_mode = PlaybackMode::Nfc;
    s.audio_state = AudioState::Idle;
    auto r = reduce(s, makeNfcDetectedEvent("04:AA:BB"), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_EQUAL_STRING("04:AA:BB", r.next_state.last_nfc_uid);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartNfcPlaybackByUid));
    const Effect* e = findEffect(r, EffectType::StartNfcPlaybackByUid);
    TEST_ASSERT_EQUAL_STRING("04:AA:BB", e->payload.nfc_playback.uid);
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_nfc_tag_detected_bt_not_ready() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.bt_state = BtState::WaitingForSpeaker;
    s.session_mode = SessionMode::Normal;
    s.playback_mode = PlaybackMode::Nfc;
    auto r = reduce(s, makeNfcDetectedEvent("04:AA:BB"), 0);
    TEST_ASSERT_EQUAL_INT((int)PendingPlaybackKind::NfcUid,
                          (int)r.next_state.pending_playback.kind);
    TEST_ASSERT_EQUAL_STRING("04:AA:BB", r.next_state.pending_playback.uid);
    TEST_ASSERT_EQUAL_STRING("04:AA:BB", r.next_state.last_nfc_uid);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

void test_nfc_tag_removed_stops_audio() {
    AppState s = defaultState();
    s.bt_state = BtState::Connected;
    s.audio_state = AudioState::PlayingFile;
    s.playback_mode = PlaybackMode::Nfc;
    auto r = reduce(s, makeEvent(EventType::NfcTagRemoved), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::Stopping, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StopAudio));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_nfc_tag_detected_ignored_in_music_mode() {
    AppState s = defaultState();
    s.bt_state = BtState::Connected;
    s.playback_mode = PlaybackMode::Music;
    s.audio_state = AudioState::PlayingFile;
    auto r = reduce(s, makeNfcDetectedEvent("04:AA:BB"), 0);
    // state unchanged
    TEST_ASSERT_EQUAL_INT((int)AudioState::PlayingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

// ---------------------------------------------------------------------------
// Sleep path tests
// ---------------------------------------------------------------------------

void test_sleep_requested_normal_idle_bt_connected_plays_sound() {
    AppState s = defaultState();
    s.sleep_state = SleepState::Awake;
    s.audio_state = AudioState::Idle;
    s.bt_state = BtState::Connected;
    auto r = reduce(s, makeSleepRequestedEvent(RequestedSleepKind::Normal), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::WaitingPowerOffSound, (int)r.next_state.sleep_state);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingSystemSound, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PlaySystemSound));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::StopAudio));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_sleep_requested_normal_idle_no_bt_shuts_down_bt() {
    AppState s = defaultState();
    s.sleep_state = SleepState::Awake;
    s.audio_state = AudioState::Idle;
    s.bt_state = BtState::WaitingForSpeaker;
    auto r = reduce(s, makeSleepRequestedEvent(RequestedSleepKind::Normal), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::WaitingBtShutdown, (int)r.next_state.sleep_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::ShutdownBt));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::StopAudio));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_sleep_requested_emergency_idle_shuts_down_bt() {
    AppState s = defaultState();
    s.sleep_state = SleepState::Awake;
    s.audio_state = AudioState::Idle;
    s.bt_state = BtState::Connected;
    auto r = reduce(s, makeSleepRequestedEvent(RequestedSleepKind::Emergency), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::WaitingBtShutdown, (int)r.next_state.sleep_state);
    TEST_ASSERT_EQUAL_INT((int)RequestedSleepKind::Emergency, (int)r.next_state.requested_sleep_kind);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::ShutdownBt));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::PlaySystemSound));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_idle_timeout_idle_bt_connected_plays_sound() {
    AppState s = defaultState();
    s.sleep_state = SleepState::Awake;
    s.audio_state = AudioState::Idle;
    s.bt_state = BtState::Connected;
    s.idle_deadline_ms = 1000;
    auto r = reduce(s, makeEvent(EventType::IdleTimeoutFired), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::WaitingPowerOffSound, (int)r.next_state.sleep_state);
    TEST_ASSERT_EQUAL_INT((int)RequestedSleepKind::Normal, (int)r.next_state.requested_sleep_kind);
    TEST_ASSERT_EQUAL_UINT32(0, r.next_state.idle_deadline_ms);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PlaySystemSound));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_idle_timeout_idle_no_bt_shuts_down_bt() {
    AppState s = defaultState();
    s.sleep_state = SleepState::Awake;
    s.audio_state = AudioState::Idle;
    s.bt_state = BtState::WaitingForSpeaker;
    auto r = reduce(s, makeEvent(EventType::IdleTimeoutFired), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::WaitingBtShutdown, (int)r.next_state.sleep_state);
    TEST_ASSERT_EQUAL_INT((int)RequestedSleepKind::Normal, (int)r.next_state.requested_sleep_kind);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::ShutdownBt));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_night_light_timeout_goes_to_bt_shutdown() {
    AppState s = defaultState();
    s.sleep_state = SleepState::Awake;
    s.session_mode = SessionMode::NightLight;
    s.bt_state = BtState::Disabled;
    s.night_light_deadline_ms = 1000;
    auto r = reduce(s, makeEvent(EventType::NightLightTimeoutFired), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::WaitingBtShutdown, (int)r.next_state.sleep_state);
    TEST_ASSERT_EQUAL_INT((int)RequestedSleepKind::NightLightTimeout,
                          (int)r.next_state.requested_sleep_kind);
    TEST_ASSERT_EQUAL_UINT32(0, r.next_state.night_light_deadline_ms);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::ShutdownBt));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_sleep_requested_normal_stops_audio() {
    AppState s = defaultState();
    s.sleep_state = SleepState::Awake;
    s.audio_state = AudioState::PlayingFile;
    s.bt_state = BtState::Connected;
    auto r = reduce(s, makeSleepRequestedEvent(RequestedSleepKind::Normal), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::PreparingDeepSleep, (int)r.next_state.sleep_state);
    TEST_ASSERT_EQUAL_INT((int)RequestedSleepKind::Normal, (int)r.next_state.requested_sleep_kind);
    TEST_ASSERT_EQUAL_INT((int)AudioState::Stopping, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StopAudio));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_audio_stopped_normal_sleep_plays_poweroff() {
    AppState s = defaultState();
    s.sleep_state = SleepState::PreparingDeepSleep;
    s.requested_sleep_kind = RequestedSleepKind::Normal;
    s.bt_state = BtState::Connected;
    auto r = reduce(s, makeAudioStoppedEvent(0), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::WaitingPowerOffSound, (int)r.next_state.sleep_state);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingSystemSound, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PlaySystemSound));
    const Effect* e = findEffect(r, EffectType::PlaySystemSound);
    TEST_ASSERT_EQUAL_UINT8(SOUND_ID_POWER_OFF, e->payload.system_sound.sound_id);
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_system_sound_done_shuts_down_bt() {
    AppState s = defaultState();
    s.sleep_state = SleepState::WaitingPowerOffSound;
    s.requested_sleep_kind = RequestedSleepKind::Normal;
    auto r = reduce(s, makeSystemSoundCompletedEvent(SOUND_ID_POWER_OFF, 0), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::WaitingBtShutdown, (int)r.next_state.sleep_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::ShutdownBt));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_bt_shutdown_done_enters_sleep() {
    AppState s = defaultState();
    s.sleep_state = SleepState::WaitingBtShutdown;
    s.requested_sleep_kind = RequestedSleepKind::Normal;
    auto r = reduce(s, makeEvent(EventType::BtShutdownCompleted), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::ReadyToSleep, (int)r.next_state.sleep_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::EnterDeepSleep));
    const Effect* e = findEffect(r, EffectType::EnterDeepSleep);
    TEST_ASSERT_EQUAL_INT((int)RequestedSleepKind::Normal, (int)e->payload.deep_sleep.kind);
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_emergency_sleep_from_awake_stops_audio() {
    AppState s = defaultState();
    s.sleep_state = SleepState::Awake;
    s.audio_state = AudioState::PlayingFile;
    s.bt_state = BtState::Connected;
    auto r = reduce(s, makeSleepRequestedEvent(RequestedSleepKind::Emergency), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::PreparingDeepSleep, (int)r.next_state.sleep_state);
    TEST_ASSERT_EQUAL_INT((int)RequestedSleepKind::Emergency,
                          (int)r.next_state.requested_sleep_kind);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StopAudio));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_audio_stopped_emergency_skips_sound() {
    AppState s = defaultState();
    s.sleep_state = SleepState::PreparingDeepSleep;
    s.requested_sleep_kind = RequestedSleepKind::Emergency;
    s.bt_state = BtState::Connected;
    auto r = reduce(s, makeAudioStoppedEvent(0), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::WaitingBtShutdown, (int)r.next_state.sleep_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::ShutdownBt));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::PlaySystemSound));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_track_ended_ignored_during_sleep_prep() {
    AppState s = defaultState();
    s.sleep_state = SleepState::PreparingDeepSleep;
    s.audio_state = AudioState::Stopping;
    auto r = reduce(s, makeEvent(EventType::TrackEnded), 0);
    // state unchanged (still PreparingDeepSleep, Stopping)
    TEST_ASSERT_EQUAL_INT((int)SleepState::PreparingDeepSleep, (int)r.next_state.sleep_state);
    TEST_ASSERT_EQUAL_INT((int)AudioState::Stopping, (int)r.next_state.audio_state);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

void test_emergency_sleep_upgrade_from_normal_prep() {
    AppState s = defaultState();
    s.sleep_state = SleepState::PreparingDeepSleep;
    s.requested_sleep_kind = RequestedSleepKind::Normal;
    auto r = reduce(s, makeSleepRequestedEvent(RequestedSleepKind::Emergency), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::PreparingDeepSleep, (int)r.next_state.sleep_state);
    TEST_ASSERT_EQUAL_INT((int)RequestedSleepKind::Emergency,
                          (int)r.next_state.requested_sleep_kind);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

void test_system_sound_failed_still_advances_sleep() {
    AppState s = defaultState();
    s.sleep_state = SleepState::WaitingPowerOffSound;
    s.requested_sleep_kind = RequestedSleepKind::Normal;
    auto r = reduce(s, makeSystemSoundFailedEvent(SOUND_ID_POWER_OFF,
                                                  SoundFailReason::PlaybackTimeout, 0), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::WaitingBtShutdown, (int)r.next_state.sleep_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::ShutdownBt));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

// ---------------------------------------------------------------------------
// Failure and recovery tests
// ---------------------------------------------------------------------------

void test_nfc_playback_failed_transitions_to_idle() {
    AppState s = defaultState();
    s.audio_state = AudioState::StartingFile;
    s.bt_state = BtState::Connected;
    auto r = reduce(s, makeNfcPlaybackStartFailedEvent("04:AA:BB",
                        PlaybackFailReason::MappingNotFound, 0), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::Idle, (int)r.next_state.audio_state);
    TEST_ASSERT_EQUAL_INT((int)PendingPlaybackKind::None,
                          (int)r.next_state.pending_playback.kind);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::LogDiagnostic));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_bt_shutdown_failed_still_enters_sleep() {
    AppState s = defaultState();
    s.sleep_state = SleepState::WaitingBtShutdown;
    s.requested_sleep_kind = RequestedSleepKind::Normal;
    auto r = reduce(s, makeEvent(EventType::BtShutdownFailed), 0);
    TEST_ASSERT_EQUAL_INT((int)SleepState::ReadyToSleep, (int)r.next_state.sleep_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::EnterDeepSleep));
    const Effect* e = findEffect(r, EffectType::EnterDeepSleep);
    TEST_ASSERT_EQUAL_INT((int)RequestedSleepKind::Normal, (int)e->payload.deep_sleep.kind);
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_jbl_recovery_timeout_triggers_pulse() {
    AppState s = defaultState();
    s.bt_state = BtState::Connected;
    s.jbl_recovery_deadline_ms = 1000;
    auto r = reduce(s, makeEvent(EventType::JblRecoveryTimeoutFired), 1001);
    TEST_ASSERT_EQUAL_INT((int)BtState::RecoveryPulsePending, (int)r.next_state.bt_state);
    TEST_ASSERT_EQUAL_UINT32(0, r.next_state.jbl_recovery_deadline_ms);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::TriggerBtRecoveryPulse));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_music_track_ended_advances_track() {
    AppState s = defaultState();
    s.audio_state = AudioState::PlayingFile;
    s.playback_mode = PlaybackMode::Music;
    s.current_track.valid = true;
    s.current_track.index = 2;
    s.total_track_count = 10;
    s.bt_state = BtState::Connected;
    auto r = reduce(s, makeEvent(EventType::TrackEnded), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(r.next_state.current_track.valid);
    TEST_ASSERT_EQUAL_UINT16(3, r.next_state.current_track.index);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartMusicTrackByIndex));
    const Effect* e = findEffect(r, EffectType::StartMusicTrackByIndex);
    TEST_ASSERT_EQUAL_UINT16(3, e->payload.music_track.index);
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

void test_music_track_ended_wraps_to_first() {
    AppState s = defaultState();
    s.audio_state = AudioState::PlayingFile;
    s.playback_mode = PlaybackMode::Music;
    s.current_track.valid = true;
    s.current_track.index = 4; // last track (total=5)
    s.total_track_count = 5;
    s.bt_state = BtState::Connected;
    auto r = reduce(s, makeEvent(EventType::TrackEnded), 0);
    TEST_ASSERT_EQUAL_UINT16(0, r.next_state.current_track.index);
    const Effect* e = findEffect(r, EffectType::StartMusicTrackByIndex);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(0, e->payload.music_track.index);
}

// ---------------------------------------------------------------------------
// LED derivation tests
// ---------------------------------------------------------------------------

void test_derive_scene_sleep_prep_returns_sleep_ready() {
    AppState s = defaultState();
    s.sleep_state = SleepState::PreparingDeepSleep;
    auto p = deriveLedScene(s);
    TEST_ASSERT_EQUAL_INT((int)LedSceneType::SleepReady, (int)p.type);
}

void test_derive_scene_bt_waiting_returns_wait_bt() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.bt_state = BtState::WaitingForSpeaker;
    s.sleep_state = SleepState::Awake;
    auto p = deriveLedScene(s);
    TEST_ASSERT_EQUAL_INT((int)LedSceneType::WaitBt, (int)p.type);
}

void test_derive_scene_playing_returns_playing() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.bt_state = BtState::Connected;
    s.audio_state = AudioState::PlayingFile;
    s.sleep_state = SleepState::Awake;
    auto p = deriveLedScene(s);
    TEST_ASSERT_EQUAL_INT((int)LedSceneType::Playing, (int)p.type);
}

void test_derive_scene_night_light_returns_night_light() {
    AppState s = defaultState();
    s.session_mode = SessionMode::NightLight;
    s.boot_state = BootState::Ready;
    auto p = deriveLedScene(s);
    TEST_ASSERT_EQUAL_INT((int)LedSceneType::NightLight, (int)p.type);
}

void test_volume_overlay_params_update() {
    AppState sA = defaultState();
    sA.boot_state = BootState::Ready;
    sA.bt_state = BtState::Connected;
    sA.volume_overlay_deadline_ms = 5000;  // non-zero = active
    sA.volume_overlay_level_percent = 40;

    AppState sB = sA;
    sB.volume_overlay_level_percent = 60;

    auto pA = deriveLedScene(sA);
    auto pB = deriveLedScene(sB);

    TEST_ASSERT_EQUAL_INT((int)LedSceneType::VolumeOverlay, (int)pA.type);
    TEST_ASSERT_EQUAL_INT((int)LedSceneType::VolumeOverlay, (int)pB.type);
    TEST_ASSERT_EQUAL_UINT8(40, pA.params.volume.percent);
    TEST_ASSERT_EQUAL_UINT8(60, pB.params.volume.percent);
}

// ---------------------------------------------------------------------------
// Stage 5: Button / volume / brightness / deadlines
// ---------------------------------------------------------------------------

// BootInitCompleted in Normal session sets idle deadline
void test_boot_init_normal_sets_idle_deadline() {
    AppState s = defaultState();
    s.boot_state = BootState::NormalBootInit;
    s.session_mode = SessionMode::Normal;
    auto r = reduce(s, makeEvent(EventType::BootInitCompleted), 1000);
    TEST_ASSERT_EQUAL_UINT32(1000 + 10UL * 60 * 1000, r.next_state.idle_deadline_ms);
}

// BootInitCompleted in NightLight session sets night_light deadline
void test_boot_init_nightlight_sets_deadline() {
    AppState s = defaultState();
    s.boot_state = BootState::NightLightBootInit;
    s.session_mode = SessionMode::NightLight;
    auto r = reduce(s, makeEvent(EventType::BootInitCompleted), 2000);
    TEST_ASSERT_EQUAL_UINT32(2000 + 15UL * 60 * 1000, r.next_state.night_light_deadline_ms);
    TEST_ASSERT_EQUAL_UINT32(0, r.next_state.idle_deadline_ms);
}

// VolumeUpPressed in Normal session → increase music_volume_percent, emit SetVolume + PersistVolume
void test_volume_up_normal_increases_volume_and_emits_effect() {
    AppState s = defaultState();
    s.session_mode = SessionMode::Normal;
    s.music_volume_percent = 50;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::VolumeUpPressed), 1000);
    TEST_ASSERT_EQUAL_UINT8(55, r.next_state.music_volume_percent);
    TEST_ASSERT_EQUAL_UINT8(55, r.next_state.volume_overlay_level_percent);
    TEST_ASSERT_NOT_EQUAL(0, r.next_state.volume_overlay_deadline_ms);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetVolume));
    const Effect* e = findEffect(r, EffectType::SetVolume);
    TEST_ASSERT_EQUAL_UINT8(55, e->payload.volume.level_percent);
    TEST_ASSERT_EQUAL_UINT8(2, r.effect_count);
}

// VolumeDownPressed in Normal session → decrease
void test_volume_down_normal_decreases_volume() {
    AppState s = defaultState();
    s.session_mode = SessionMode::Normal;
    s.music_volume_percent = 50;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::VolumeDownPressed), 1000);
    TEST_ASSERT_EQUAL_UINT8(45, r.next_state.music_volume_percent);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetVolume));
    const Effect* e = findEffect(r, EffectType::SetVolume);
    TEST_ASSERT_EQUAL_UINT8(45, e->payload.volume.level_percent);
}

// VolumeUpPressed clamps at max
void test_volume_up_clamps_at_max() {
    AppState s = defaultState();
    s.session_mode = SessionMode::Normal;
    s.music_volume_percent = 98;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::VolumeUpPressed), 0);
    TEST_ASSERT_EQUAL_UINT8(100, r.next_state.music_volume_percent);
}

// VolumeDownPressed clamps at min
void test_volume_down_clamps_at_min() {
    AppState s = defaultState();
    s.session_mode = SessionMode::Normal;
    s.music_volume_percent = 2;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::VolumeDownPressed), 0);
    TEST_ASSERT_EQUAL_UINT8(0, r.next_state.music_volume_percent);
}

// VolumeUpPressed in NightLight → brightness (no SetVolume effect)
void test_volume_up_nightlight_increases_brightness() {
    AppState s = defaultState();
    s.session_mode = SessionMode::NightLight;
    s.night_light_brightness_percent = 50;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::VolumeUpPressed), 1000);
    TEST_ASSERT_EQUAL_UINT8(60, r.next_state.night_light_brightness_percent);
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::SetVolume));
    TEST_ASSERT_NOT_EQUAL(0, r.next_state.brightness_save_deadline_ms);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

// VolumeDownPressed in NightLight → brightness decrease
void test_volume_down_nightlight_decreases_brightness() {
    AppState s = defaultState();
    s.session_mode = SessionMode::NightLight;
    s.night_light_brightness_percent = 50;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::VolumeDownPressed), 0);
    TEST_ASSERT_EQUAL_UINT8(40, r.next_state.night_light_brightness_percent);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

// Brightness clamps at min
void test_brightness_clamps_at_min() {
    AppState s = defaultState();
    s.session_mode = SessionMode::NightLight;
    s.night_light_brightness_percent = 12;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::VolumeDownPressed), 0);
    TEST_ASSERT_EQUAL_UINT8(10, r.next_state.night_light_brightness_percent);
}

// VolumeUpPressed emits PersistVolume with the new level
void test_volume_up_emits_persist_volume() {
    AppState s = defaultState();
    s.session_mode = SessionMode::Normal;
    s.music_volume_percent = 50;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::VolumeUpPressed), 1000);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PersistVolume));
    const Effect* e = findEffect(r, EffectType::PersistVolume);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT8(55, e->payload.volume.level_percent);
}

// VolumeDownPressed emits PersistVolume with the new level
void test_volume_down_emits_persist_volume() {
    AppState s = defaultState();
    s.session_mode = SessionMode::Normal;
    s.music_volume_percent = 50;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::VolumeDownPressed), 1000);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PersistVolume));
    const Effect* e = findEffect(r, EffectType::PersistVolume);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT8(45, e->payload.volume.level_percent);
}

// BtConnected restores volume via SetVolume but must NOT persist (not a user change)
void test_bt_connected_does_not_emit_persist_volume() {
    AppState s = defaultState();
    s.bt_state = BtState::WaitingForSpeaker;
    s.pending_playback.kind = PendingPlaybackKind::None;
    s.music_volume_percent = 70;
    auto r = reduce(s, makeEvent(EventType::BtConnected), 0);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::SetVolume));
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::PersistVolume));
}

// Brightness clamps at max
void test_brightness_clamps_at_max() {
    AppState s = defaultState();
    s.session_mode = SessionMode::NightLight;
    s.night_light_brightness_percent = 95;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::VolumeUpPressed), 0);
    TEST_ASSERT_EQUAL_UINT8(100, r.next_state.night_light_brightness_percent);
}

// BrightnessSaveDeadlineFired → emits PersistBrightness, clears deadline
void test_brightness_save_deadline_emits_persist() {
    AppState s = defaultState();
    s.session_mode = SessionMode::NightLight;
    s.night_light_brightness_percent = 70;
    s.brightness_save_deadline_ms = 1000;
    auto r = reduce(s, makeEvent(EventType::BrightnessSaveDeadlineFired), 1001);
    TEST_ASSERT_EQUAL_UINT32(0, r.next_state.brightness_save_deadline_ms);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PersistBrightness));
    const Effect* e = findEffect(r, EffectType::PersistBrightness);
    TEST_ASSERT_EQUAL_UINT8(70, e->payload.brightness.level_percent);
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

// NightLightTimeoutFired with pending brightness save → flushes before sleep
void test_nightlight_timeout_flushes_brightness_save() {
    AppState s = defaultState();
    s.session_mode = SessionMode::NightLight;
    s.sleep_state = SleepState::Awake;
    s.bt_state = BtState::Disabled;
    s.night_light_brightness_percent = 80;
    s.night_light_deadline_ms = 1000;
    s.brightness_save_deadline_ms = 5000;  // pending
    auto r = reduce(s, makeEvent(EventType::NightLightTimeoutFired), 1001);
    TEST_ASSERT_EQUAL_UINT32(0, r.next_state.brightness_save_deadline_ms);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PersistBrightness));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::ShutdownBt));
    TEST_ASSERT_EQUAL_UINT8(2, r.effect_count);
}

// PlayPausePressed in Music mode while playing → pause
void test_play_pause_playing_emits_pause() {
    AppState s = defaultState();
    s.session_mode = SessionMode::Normal;
    s.playback_mode = PlaybackMode::Music;
    s.audio_state = AudioState::PlayingFile;
    s.bt_state = BtState::Connected;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::PlayPausePressed), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::Paused, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PauseAudio));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

// PlayPausePressed while paused → resume
void test_play_pause_paused_emits_resume() {
    AppState s = defaultState();
    s.session_mode = SessionMode::Normal;
    s.playback_mode = PlaybackMode::Music;
    s.audio_state = AudioState::Paused;
    s.bt_state = BtState::Connected;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::PlayPausePressed), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::PlayingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::ResumeAudio));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

// PlayPausePressed in NFC mode → ignored
void test_play_pause_ignored_in_nfc_mode() {
    AppState s = defaultState();
    s.session_mode = SessionMode::Normal;
    s.playback_mode = PlaybackMode::Nfc;
    s.audio_state = AudioState::Idle;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::PlayPausePressed), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::Idle, (int)r.next_state.audio_state);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

// NextTrackPressed → advances track index
void test_next_track_advances_index() {
    AppState s = defaultState();
    s.session_mode = SessionMode::Normal;
    s.playback_mode = PlaybackMode::Music;
    s.bt_state = BtState::Connected;
    s.current_track.valid = true;
    s.current_track.index = 3;
    s.total_track_count = 10;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::NextTrackPressed), 0);
    TEST_ASSERT_EQUAL_UINT16(4, r.next_state.current_track.index);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartMusicTrackByIndex));
    const Effect* e = findEffect(r, EffectType::StartMusicTrackByIndex);
    TEST_ASSERT_EQUAL_UINT16(4, e->payload.music_track.index);
}

// PrevTrackPressed → goes to previous track
void test_prev_track_goes_back() {
    AppState s = defaultState();
    s.session_mode = SessionMode::Normal;
    s.playback_mode = PlaybackMode::Music;
    s.bt_state = BtState::Connected;
    s.current_track.valid = true;
    s.current_track.index = 5;
    s.total_track_count = 10;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::PrevTrackPressed), 0);
    TEST_ASSERT_EQUAL_UINT16(4, r.next_state.current_track.index);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartMusicTrackByIndex));
}

// PrevTrackPressed at first track → wraps to last
void test_prev_track_wraps_to_last() {
    AppState s = defaultState();
    s.session_mode = SessionMode::Normal;
    s.playback_mode = PlaybackMode::Music;
    s.bt_state = BtState::Connected;
    s.current_track.valid = true;
    s.current_track.index = 0;
    s.total_track_count = 5;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::PrevTrackPressed), 0);
    TEST_ASSERT_EQUAL_UINT16(4, r.next_state.current_track.index);
}

// ModeToggleRequested → toggles playback_mode, emits mode sound and PersistPlaybackMode
void test_mode_toggle_nfc_to_music_emits_persist() {
    AppState s = defaultState();
    s.session_mode = SessionMode::Normal;
    s.playback_mode = PlaybackMode::Nfc;
    s.bt_state = BtState::Connected;
    s.audio_state = AudioState::Idle;
    s.current_track.valid = true;
    s.current_track.index = 0;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::ModeToggleRequested), 0);
    TEST_ASSERT_EQUAL_INT((int)PlaybackMode::Music, (int)r.next_state.playback_mode);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PlaySystemSound));
    const Effect* se = findEffect(r, EffectType::PlaySystemSound);
    TEST_ASSERT_EQUAL_UINT8(SOUND_ID_MUSIC_MODE, se->payload.system_sound.sound_id);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PersistPlaybackMode));
    const Effect* pe = findEffect(r, EffectType::PersistPlaybackMode);
    TEST_ASSERT_EQUAL_INT((int)PlaybackMode::Music, (int)pe->payload.playback_mode.mode);
    // Music must NOT start immediately — deferred until SystemSoundCompleted
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::StartMusicTrackByIndex));
}

// SystemSoundCompleted(SOUND_ID_MUSIC_MODE) while in Music+BT → autostart music
void test_music_mode_sound_completed_starts_music() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Music;
    s.bt_state = BtState::Connected;
    s.audio_state = AudioState::PlayingSystemSound;
    s.sleep_state = SleepState::Awake;
    s.current_track.valid = true;
    s.current_track.index = 2;
    auto r = reduce(s, makeSystemSoundCompletedEvent(SOUND_ID_MUSIC_MODE, 5), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartMusicTrackByIndex));
    TEST_ASSERT_EQUAL_UINT16(2, findEffect(r, EffectType::StartMusicTrackByIndex)->payload.music_track.index);
}

// SystemSoundFailed(SOUND_ID_MUSIC_MODE) → autostart music even on sound failure
void test_music_mode_sound_failed_starts_music() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Music;
    s.bt_state = BtState::Connected;
    s.audio_state = AudioState::PlayingSystemSound;
    s.sleep_state = SleepState::Awake;
    s.current_track.valid = false;
    auto r = reduce(s, makeSystemSoundFailedEvent(SOUND_ID_MUSIC_MODE, SoundFailReason::FileNotFound, 5), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::StartingFile, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StartMusicTrackByIndex));
}

// SystemSoundCompleted(SOUND_ID_NFC_MODE) → no music autostart
void test_nfc_mode_sound_completed_no_music_start() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Nfc;
    s.bt_state = BtState::Connected;
    s.audio_state = AudioState::PlayingSystemSound;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeSystemSoundCompletedEvent(SOUND_ID_NFC_MODE, 5), 0);
    TEST_ASSERT_EQUAL_INT((int)AudioState::Idle, (int)r.next_state.audio_state);
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::StartMusicTrackByIndex));
}

// SystemSoundCompleted(SOUND_ID_MUSIC_MODE) while mode already switched back to Nfc → no music
void test_music_mode_sound_completed_mode_already_nfc_no_start() {
    AppState s = defaultState();
    s.playback_mode = PlaybackMode::Nfc; // user switched back before sound ended
    s.bt_state = BtState::Connected;
    s.audio_state = AudioState::PlayingSystemSound;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeSystemSoundCompletedEvent(SOUND_ID_MUSIC_MODE, 5), 0);
    TEST_ASSERT_FALSE(hasEffect(r, EffectType::StartMusicTrackByIndex));
}

// ModeToggleRequested Music→Nfc → stops audio, emits mode sound and persist
void test_mode_toggle_music_to_nfc_stops_audio() {
    AppState s = defaultState();
    s.session_mode = SessionMode::Normal;
    s.playback_mode = PlaybackMode::Music;
    s.bt_state = BtState::Connected;
    s.audio_state = AudioState::PlayingFile;
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::ModeToggleRequested), 0);
    TEST_ASSERT_EQUAL_INT((int)PlaybackMode::Nfc, (int)r.next_state.playback_mode);
    TEST_ASSERT_EQUAL_INT((int)AudioState::Stopping, (int)r.next_state.audio_state);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::StopAudio));
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PlaySystemSound));
    const Effect* se = findEffect(r, EffectType::PlaySystemSound);
    TEST_ASSERT_EQUAL_UINT8(SOUND_ID_NFC_MODE, se->payload.system_sound.sound_id);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::PersistPlaybackMode));
}

// BatteryCheckRequested → sets battery_bars and battery_preview_active
void test_battery_check_sets_preview() {
    AppState s = defaultState();
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeBatteryCheckEvent(3), 1000);
    TEST_ASSERT_EQUAL_UINT8(3, r.next_state.battery_bars);
    TEST_ASSERT_TRUE(r.next_state.battery_preview_active);
    TEST_ASSERT_NOT_EQUAL(0, r.next_state.battery_preview_deadline_ms);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

// SyncModeRequested → sets sync_mode, emits TriggerSyncRestart
void test_diagnostic_entry_emits_restart() {
    AppState s = defaultState();
    s.sleep_state = SleepState::Awake;
    auto r = reduce(s, makeEvent(EventType::SyncModeRequested), 0);
    TEST_ASSERT_TRUE(r.next_state.sync_mode);
    TEST_ASSERT_TRUE(hasEffect(r, EffectType::TriggerSyncRestart));
    TEST_ASSERT_EQUAL_UINT8(1, r.effect_count);
}

// NfcPlaybackStarted disables idle timer
void test_nfc_playback_started_disables_idle_timer() {
    AppState s = defaultState();
    s.session_mode = SessionMode::Normal;
    s.audio_state = AudioState::StartingFile;
    s.idle_deadline_ms = 9000;
    s.bt_state = BtState::Connected;
    strncpy(s.pending_playback.uid, "04:AA:BB", sizeof(s.pending_playback.uid));
    auto r = reduce(s, makeNfcPlaybackStartedEvent("04:AA:BB", 1), 0);
    TEST_ASSERT_EQUAL_UINT32(0, r.next_state.idle_deadline_ms);
}

// AudioStopped in Normal session (not in sleep) restores idle timer
void test_audio_stopped_normal_restores_idle_timer() {
    AppState s = defaultState();
    s.session_mode = SessionMode::Normal;
    s.sleep_state = SleepState::Awake;
    s.audio_state = AudioState::Stopping;
    s.idle_deadline_ms = 0;
    auto r = reduce(s, makeAudioStoppedEvent(0), 5000);
    TEST_ASSERT_EQUAL_INT((int)AudioState::Idle, (int)r.next_state.audio_state);
    TEST_ASSERT_NOT_EQUAL(0, r.next_state.idle_deadline_ms);
}

// Full night-light boot sequence: WakeCauseResolvedNightLight → BrightnessLoaded → BootInitCompleted.
// Mirrors the three events now posted by main.cpp before dispatcherStartTask().
void test_night_light_boot_sequence_produces_ready_state() {
    AppState s = defaultState();
    s.boot_state = BootState::ColdBootInit;

    // Step 1: wake cause
    auto r1 = reduce(s, makeEvent(EventType::WakeCauseResolvedNightLight), 0);
    TEST_ASSERT_EQUAL_INT((int)BootState::NightLightBootInit, (int)r1.next_state.boot_state);
    TEST_ASSERT_EQUAL_INT((int)SessionMode::NightLight, (int)r1.next_state.session_mode);

    // Step 2: brightness loaded
    auto r2 = reduce(r1.next_state, makeBrightnessLoadedEvent(75), 0);
    TEST_ASSERT_EQUAL_UINT8(75, r2.next_state.night_light_brightness_percent);

    // Step 3: boot init completed at t=5000
    auto r3 = reduce(r2.next_state, makeEvent(EventType::BootInitCompleted), 5000);
    TEST_ASSERT_EQUAL_INT((int)BootState::Ready, (int)r3.next_state.boot_state);
    TEST_ASSERT_EQUAL_INT((int)BtState::Disabled, (int)r3.next_state.bt_state);
    TEST_ASSERT_EQUAL_INT((int)SessionMode::NightLight, (int)r3.next_state.session_mode);
    TEST_ASSERT_EQUAL_UINT8(75, r3.next_state.night_light_brightness_percent);
    TEST_ASSERT_EQUAL_UINT32(5000 + 15UL * 60 * 1000, r3.next_state.night_light_deadline_ms);
    TEST_ASSERT_EQUAL_UINT32(0, r3.next_state.idle_deadline_ms);
}

// BrightnessLoaded event sets initial brightness in AppState
void test_brightness_loaded_sets_state() {
    AppState s = defaultState();
    auto r = reduce(s, makeBrightnessLoadedEvent(75), 0);
    TEST_ASSERT_EQUAL_UINT8(75, r.next_state.night_light_brightness_percent);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

// ---------------------------------------------------------------------------
// Sleep hold warning tests
// ---------------------------------------------------------------------------

void test_sleep_hold_warning_sets_warn_active() {
    AppState s = defaultState();
    s.sleep_state = SleepState::Awake;
    s.sleep_warn_active = false;
    auto r = reduce(s, makeEvent(EventType::SleepHoldWarning), 0);
    TEST_ASSERT_TRUE(r.next_state.sleep_warn_active);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

void test_sleep_hold_warning_ignored_during_sleep() {
    AppState s = defaultState();
    s.sleep_state = SleepState::PreparingDeepSleep;
    s.sleep_warn_active = false;
    auto r = reduce(s, makeEvent(EventType::SleepHoldWarning), 0);
    TEST_ASSERT_FALSE(r.next_state.sleep_warn_active);
    TEST_ASSERT_EQUAL_UINT8(0, r.effect_count);
}

void test_sleep_requested_clears_warn_active() {
    AppState s = defaultState();
    s.sleep_state = SleepState::Awake;
    s.sleep_warn_active = true;
    s.audio_state = AudioState::Idle;
    s.bt_state = BtState::WaitingForSpeaker;
    auto r = reduce(s, makeSleepRequestedEvent(RequestedSleepKind::Normal), 0);
    TEST_ASSERT_FALSE(r.next_state.sleep_warn_active);
    TEST_ASSERT_EQUAL_INT((int)SleepState::WaitingBtShutdown, (int)r.next_state.sleep_state);
}

// ---------------------------------------------------------------------------
// LED scene: sleep hold warning
// ---------------------------------------------------------------------------

void test_derive_scene_sleep_warn_returns_warning_flash() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.bt_state = BtState::Connected;
    s.sleep_state = SleepState::Awake;
    s.sleep_warn_active = true;
    auto p = deriveLedScene(s);
    TEST_ASSERT_EQUAL_INT((int)LedSceneType::WarningFlash, (int)p.type);
}

void test_derive_scene_sleep_overrides_warn() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.bt_state = BtState::Connected;
    s.sleep_state = SleepState::PreparingDeepSleep;
    s.sleep_warn_active = true;
    auto p = deriveLedScene(s);
    TEST_ASSERT_EQUAL_INT((int)LedSceneType::SleepReady, (int)p.type);
}

void test_derive_scene_warn_overrides_playing() {
    AppState s = defaultState();
    s.boot_state = BootState::Ready;
    s.bt_state = BtState::Connected;
    s.sleep_state = SleepState::Awake;
    s.audio_state = AudioState::PlayingFile;
    s.sleep_warn_active = true;
    auto p = deriveLedScene(s);
    TEST_ASSERT_EQUAL_INT((int)LedSceneType::WarningFlash, (int)p.type);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    UNITY_BEGIN();

    // Boot
    RUN_TEST(test_boot_started_transitions_to_wake_hold_check);
    RUN_TEST(test_wake_cause_normal_transitions_boot_init);
    RUN_TEST(test_wake_cause_nightlight_sets_session);
    RUN_TEST(test_boot_init_completed_bt_starts_waiting);
    RUN_TEST(test_boot_nightlight_bt_disabled);
    RUN_TEST(test_nfc_prescan_found_sets_pending);
    RUN_TEST(test_nfc_prescan_not_found_no_pending);

    // BT
    RUN_TEST(test_boot_music_mode_sets_music_pending);
    RUN_TEST(test_bt_connected_no_pending);
    RUN_TEST(test_bt_connected_with_pending_nfc);
    RUN_TEST(test_bt_connected_with_pending_music);
    RUN_TEST(test_bt_connected_music_pending_no_track_starts_from_zero);
    RUN_TEST(test_bt_disconnected_clears_volume_flag);

    // NFC
    RUN_TEST(test_nfc_tag_detected_bt_ready);
    RUN_TEST(test_nfc_tag_detected_bt_not_ready);
    RUN_TEST(test_nfc_tag_removed_stops_audio);
    RUN_TEST(test_nfc_tag_detected_ignored_in_music_mode);

    // Sleep
    RUN_TEST(test_sleep_requested_normal_idle_bt_connected_plays_sound);
    RUN_TEST(test_sleep_requested_normal_idle_no_bt_shuts_down_bt);
    RUN_TEST(test_sleep_requested_emergency_idle_shuts_down_bt);
    RUN_TEST(test_idle_timeout_idle_bt_connected_plays_sound);
    RUN_TEST(test_idle_timeout_idle_no_bt_shuts_down_bt);
    RUN_TEST(test_night_light_timeout_goes_to_bt_shutdown);
    RUN_TEST(test_sleep_requested_normal_stops_audio);
    RUN_TEST(test_audio_stopped_normal_sleep_plays_poweroff);
    RUN_TEST(test_system_sound_done_shuts_down_bt);
    RUN_TEST(test_bt_shutdown_done_enters_sleep);
    RUN_TEST(test_emergency_sleep_from_awake_stops_audio);
    RUN_TEST(test_audio_stopped_emergency_skips_sound);
    RUN_TEST(test_track_ended_ignored_during_sleep_prep);
    RUN_TEST(test_emergency_sleep_upgrade_from_normal_prep);
    RUN_TEST(test_system_sound_failed_still_advances_sleep);

    // Failure and recovery
    RUN_TEST(test_nfc_playback_failed_transitions_to_idle);
    RUN_TEST(test_bt_shutdown_failed_still_enters_sleep);
    RUN_TEST(test_jbl_recovery_timeout_triggers_pulse);
    RUN_TEST(test_music_track_ended_advances_track);
    RUN_TEST(test_music_track_ended_wraps_to_first);

    // LED derivation
    RUN_TEST(test_derive_scene_sleep_prep_returns_sleep_ready);
    RUN_TEST(test_derive_scene_bt_waiting_returns_wait_bt);
    RUN_TEST(test_derive_scene_playing_returns_playing);
    RUN_TEST(test_derive_scene_night_light_returns_night_light);
    RUN_TEST(test_volume_overlay_params_update);

    // Stage 5: Buttons / volume / brightness / deadlines
    RUN_TEST(test_boot_init_normal_sets_idle_deadline);
    RUN_TEST(test_boot_init_nightlight_sets_deadline);
    RUN_TEST(test_volume_up_normal_increases_volume_and_emits_effect);
    RUN_TEST(test_volume_down_normal_decreases_volume);
    RUN_TEST(test_volume_up_clamps_at_max);
    RUN_TEST(test_volume_down_clamps_at_min);
    RUN_TEST(test_volume_up_nightlight_increases_brightness);
    RUN_TEST(test_volume_down_nightlight_decreases_brightness);
    RUN_TEST(test_brightness_clamps_at_min);
    RUN_TEST(test_brightness_clamps_at_max);
    RUN_TEST(test_brightness_save_deadline_emits_persist);
    RUN_TEST(test_nightlight_timeout_flushes_brightness_save);
    RUN_TEST(test_play_pause_playing_emits_pause);
    RUN_TEST(test_play_pause_paused_emits_resume);
    RUN_TEST(test_play_pause_ignored_in_nfc_mode);
    RUN_TEST(test_next_track_advances_index);
    RUN_TEST(test_prev_track_goes_back);
    RUN_TEST(test_prev_track_wraps_to_last);
    RUN_TEST(test_mode_toggle_nfc_to_music_emits_persist);
    RUN_TEST(test_mode_toggle_music_to_nfc_stops_audio);
    RUN_TEST(test_music_mode_sound_completed_starts_music);
    RUN_TEST(test_music_mode_sound_failed_starts_music);
    RUN_TEST(test_nfc_mode_sound_completed_no_music_start);
    RUN_TEST(test_music_mode_sound_completed_mode_already_nfc_no_start);
    RUN_TEST(test_battery_check_sets_preview);
    RUN_TEST(test_diagnostic_entry_emits_restart);
    RUN_TEST(test_nfc_playback_started_disables_idle_timer);
    RUN_TEST(test_audio_stopped_normal_restores_idle_timer);
    RUN_TEST(test_night_light_boot_sequence_produces_ready_state);
    RUN_TEST(test_brightness_loaded_sets_state);

    // Volume persistence
    RUN_TEST(test_volume_up_emits_persist_volume);
    RUN_TEST(test_volume_down_emits_persist_volume);
    RUN_TEST(test_bt_connected_does_not_emit_persist_volume);

    // Sleep hold warning
    RUN_TEST(test_sleep_hold_warning_sets_warn_active);
    RUN_TEST(test_sleep_hold_warning_ignored_during_sleep);
    RUN_TEST(test_sleep_requested_clears_warn_active);
    RUN_TEST(test_derive_scene_sleep_warn_returns_warning_flash);
    RUN_TEST(test_derive_scene_sleep_overrides_warn);
    RUN_TEST(test_derive_scene_warn_overrides_playing);

    return UNITY_END();
}
