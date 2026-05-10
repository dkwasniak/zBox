#include "reducer.h"
#include <cstring>

// ---------------------------------------------------------------------------
// Stage 5 timing constants (duplicated from zbox_config.h to keep reducer
// free of Arduino/hardware includes for native tests)
// ---------------------------------------------------------------------------
static constexpr uint8_t  REDUCER_VOLUME_STEP            = 5;
static constexpr uint8_t  REDUCER_VOLUME_MIN             = 0;
static constexpr uint8_t  REDUCER_VOLUME_MAX             = 100;
static constexpr uint8_t  REDUCER_BRIGHTNESS_STEP        = 10;
static constexpr uint8_t  REDUCER_BRIGHTNESS_MIN         = 10;
static constexpr uint8_t  REDUCER_BRIGHTNESS_MAX         = 100;
static constexpr uint32_t REDUCER_IDLE_TIMEOUT_MS        = 10UL * 60 * 1000;
static constexpr uint32_t REDUCER_NIGHT_LIGHT_TIMEOUT_MS = 15UL * 60 * 1000;
static constexpr uint32_t REDUCER_BRIGHTNESS_SAVE_MS     = 1500;
static constexpr uint32_t REDUCER_VOLUME_OVERLAY_MS      = 1000;
static constexpr uint32_t REDUCER_BATTERY_PREVIEW_MS     = 3000;

// ---------------------------------------------------------------------------
// Effect factory helpers (cmd_id = 0; dispatcher fills real value)
// ---------------------------------------------------------------------------

static Effect makeSetVolumeEffect(uint8_t percent) {
    Effect e{};
    e.type = EffectType::SetVolume;
    e.payload.volume.level_percent = percent;
    return e;
}

static Effect makeStartNfcPlaybackEffect(const char* uid) {
    Effect e{};
    e.type = EffectType::StartNfcPlaybackByUid;
    strncpy(e.payload.nfc_playback.uid, uid, sizeof(e.payload.nfc_playback.uid) - 1);
    e.payload.nfc_playback.uid[sizeof(e.payload.nfc_playback.uid) - 1] = '\0';
    e.payload.nfc_playback.cmd_id = 0;
    return e;
}

static Effect makeStartMusicTrackEffect(uint16_t index) {
    Effect e{};
    e.type = EffectType::StartMusicTrackByIndex;
    e.payload.music_track.index = index;
    e.payload.music_track.cmd_id = 0;
    return e;
}

static Effect makeStopAudioEffect() {
    Effect e{};
    e.type = EffectType::StopAudio;
    e.payload.audio_control.cmd_id = 0;
    return e;
}

static Effect makePlaySystemSoundEffect(uint8_t sound_id) {
    Effect e{};
    e.type = EffectType::PlaySystemSound;
    e.payload.system_sound.sound_id = sound_id;
    e.payload.system_sound.cmd_id = 0;
    return e;
}

static Effect makeShutdownBtEffect() {
    Effect e{};
    e.type = EffectType::ShutdownBt;
    return e;
}

static Effect makeEnterDeepSleepEffect(RequestedSleepKind kind) {
    Effect e{};
    e.type = EffectType::EnterDeepSleep;
    e.payload.deep_sleep.kind = kind;
    return e;
}

static Effect makeTriggerBtRecoveryPulseEffect() {
    Effect e{};
    e.type = EffectType::TriggerBtRecoveryPulse;
    return e;
}

static Effect makeLogDiagnosticEffect(uint8_t code) {
    Effect e{};
    e.type = EffectType::LogDiagnostic;
    e.payload.diagnostic.code = code;
    return e;
}

static Effect makePauseAudioEffect() {
    Effect e{};
    e.type = EffectType::PauseAudio;
    e.payload.audio_control.cmd_id = 0;
    return e;
}

static Effect makeResumeAudioEffect() {
    Effect e{};
    e.type = EffectType::ResumeAudio;
    e.payload.audio_control.cmd_id = 0;
    return e;
}

static Effect makePersistVolumeEffect(uint8_t percent) {
    Effect e{};
    e.type = EffectType::PersistVolume;
    e.payload.volume.level_percent = percent;
    return e;
}

static Effect makePersistBrightnessEffect(uint8_t percent) {
    Effect e{};
    e.type = EffectType::PersistBrightness;
    e.payload.brightness.level_percent = percent;
    return e;
}

static Effect makePersistPlaybackModeEffect(PlaybackMode mode) {
    Effect e{};
    e.type = EffectType::PersistPlaybackMode;
    e.payload.playback_mode.mode = mode;
    return e;
}

static Effect makeTriggerSyncRestartEffect() {
    Effect e{};
    e.type = EffectType::TriggerSyncRestart;
    return e;
}

// ---------------------------------------------------------------------------
// Stage 5 helpers
// ---------------------------------------------------------------------------

static uint8_t clampU8(int val, uint8_t lo, uint8_t hi) {
    if (val < (int)lo) return lo;
    if (val > (int)hi) return hi;
    return (uint8_t)val;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void copyUid(char* dst, const char* src, size_t dst_size) {
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static bool isAudioActive(AudioState s) {
    return s == AudioState::PlayingFile   ||
           s == AudioState::StartingFile  ||
           s == AudioState::PlayingSystemSound ||
           s == AudioState::Paused;
}

// Advance sleep state after audio has stopped (or was never active).
// Called both from AudioStopped and from the immediate-no-audio paths.
static void advanceSleepAfterAudioStopped(AppState& next, RequestedSleepKind kind,
                                          BtState bt_state, EffectBuilder& fx) {
    if (kind == RequestedSleepKind::Normal && bt_state == BtState::Connected) {
        next.sleep_state = SleepState::WaitingPowerOffSound;
        next.audio_state = AudioState::StartingSystemSound;
        fx.add(makePlaySystemSoundEffect(SOUND_ID_POWER_OFF));
    } else {
        next.sleep_state = SleepState::WaitingBtShutdown;
        fx.add(makeShutdownBtEffect());
    }
}

// ---------------------------------------------------------------------------
// reduce
// ---------------------------------------------------------------------------

ReduceResult reduce(const AppState& s, const Event& ev, uint32_t now_ms) {
    AppState next = s;
    EffectBuilder fx;

    const bool in_sleep = (s.sleep_state != SleepState::Awake);

    switch (ev.type) {

        // ----------------------------------------------------------------
        // Boot transitions
        // ----------------------------------------------------------------
        case EventType::BootStarted:
            next.boot_state = BootState::WakeHoldCheck;
            break;

        case EventType::WakeCauseResolvedNormal:
            next.boot_state = BootState::NormalBootInit;
            break;

        case EventType::WakeCauseResolvedNightLight:
            next.boot_state = BootState::NightLightBootInit;
            next.session_mode = SessionMode::NightLight;
            break;

        case EventType::BootInitCompleted:
            next.boot_state = BootState::Ready;
            if (s.session_mode == SessionMode::NightLight) {
                next.bt_state = BtState::Disabled;
                next.night_light_deadline_ms = now_ms + REDUCER_NIGHT_LIGHT_TIMEOUT_MS;
            } else {
                next.bt_state = BtState::WaitingForSpeaker;
                next.idle_deadline_ms = now_ms + REDUCER_IDLE_TIMEOUT_MS;
                if (s.playback_mode == PlaybackMode::Music) {
                    next.pending_playback.kind = PendingPlaybackKind::MusicCurrentTrack;
                }
            }
            break;

        case EventType::BrightnessLoaded:
            next.night_light_brightness_percent = ev.payload.brightness_loaded.percent;
            break;

        case EventType::MappingsLoaded:
            next.total_track_count = ev.payload.mappings_loaded.total_track_count;
            break;

        case EventType::VolumeLoaded:
            next.music_volume_percent = ev.payload.volume_loaded.percent;
            break;

        case EventType::NfcPreScanCompleted:
            if (ev.payload.nfc_prescan.uid_found) {
                next.pending_playback.kind = PendingPlaybackKind::NfcUid;
                copyUid(next.pending_playback.uid, ev.payload.nfc_prescan.uid,
                        sizeof(next.pending_playback.uid));
                copyUid(next.last_nfc_uid, ev.payload.nfc_prescan.uid,
                        sizeof(next.last_nfc_uid));
            } else {
                next.pending_playback.kind = PendingPlaybackKind::None;
            }
            break;

        // ----------------------------------------------------------------
        // BT state
        // ----------------------------------------------------------------
        case EventType::BtConnected:
            next.bt_state = BtState::Connected;
            next.bt_volume_applied = false;
            fx.add(makeSetVolumeEffect(s.music_volume_percent));

            if (s.pending_playback.kind == PendingPlaybackKind::NfcUid) {
                fx.add(makeStartNfcPlaybackEffect(s.pending_playback.uid));
                next.audio_state = AudioState::StartingFile;
                next.pending_playback.kind = PendingPlaybackKind::None;
            } else if (s.pending_playback.kind == PendingPlaybackKind::MusicCurrentTrack) {
                uint16_t idx = s.current_track.valid ? s.current_track.index : 0;
                next.current_track.valid = true;
                next.current_track.index = idx;
                next.audio_state = AudioState::StartingFile;
                next.pending_playback.kind = PendingPlaybackKind::None;
                fx.add(makeStartMusicTrackEffect(idx));
            } else if (s.pending_playback.kind == PendingPlaybackKind::MusicSpecificTrack) {
                fx.add(makeStartMusicTrackEffect(s.pending_playback.track_index));
                next.audio_state = AudioState::StartingFile;
                next.pending_playback.kind = PendingPlaybackKind::None;
            }
            break;

        case EventType::BtDisconnected:
            next.bt_state = BtState::WaitingForSpeaker;
            next.bt_volume_applied = false;
            if (isAudioActive(s.audio_state)) {
                fx.add(makeStopAudioEffect());
                next.audio_state = AudioState::Idle;
            }
            break;

        case EventType::BtRecoveryPulseCompleted:
            if (s.bt_state == BtState::RecoveryPulsePending) {
                next.bt_state = BtState::WaitingForSpeaker;
            }
            break;

        case EventType::BtShutdownCompleted:
        case EventType::BtShutdownFailed:
            if (s.sleep_state == SleepState::WaitingBtShutdown) {
                next.sleep_state = SleepState::ReadyToSleep;
                fx.add(makeEnterDeepSleepEffect(s.requested_sleep_kind));
            }
            break;

        // ----------------------------------------------------------------
        // NFC
        // ----------------------------------------------------------------
        case EventType::NfcTagDetected:
            if (in_sleep) break;
            if (s.playback_mode == PlaybackMode::Music) break;
            copyUid(next.last_nfc_uid, ev.payload.nfc_detected.uid,
                    sizeof(next.last_nfc_uid));
            if (s.bt_state == BtState::Connected) {
                fx.add(makeStartNfcPlaybackEffect(ev.payload.nfc_detected.uid));
                next.audio_state = AudioState::StartingFile;
                next.pending_playback.kind = PendingPlaybackKind::None;
            } else {
                next.pending_playback.kind = PendingPlaybackKind::NfcUid;
                copyUid(next.pending_playback.uid, ev.payload.nfc_detected.uid,
                        sizeof(next.pending_playback.uid));
            }
            break;

        case EventType::NfcTagRemoved:
            if (in_sleep) break;
            if (s.playback_mode == PlaybackMode::Nfc &&
                (s.audio_state == AudioState::PlayingFile ||
                 s.audio_state == AudioState::StartingFile)) {
                fx.add(makeStopAudioEffect());
                next.audio_state = AudioState::Stopping;
            }
            next.pending_playback.kind = PendingPlaybackKind::None;
            break;

        // ----------------------------------------------------------------
        // Audio feedback
        // ----------------------------------------------------------------
        case EventType::NfcPlaybackStarted:
            next.audio_state = AudioState::PlayingFile;
            next.idle_deadline_ms = 0;  // disable idle timer while playing
            break;

        case EventType::NfcPlaybackStartFailed:
            next.audio_state = AudioState::Idle;
            next.pending_playback.kind = PendingPlaybackKind::None;
            fx.add(makeLogDiagnosticEffect(1));
            break;

        case EventType::MusicTrackStarted:
            next.audio_state = AudioState::PlayingFile;
            next.current_track.valid = true;
            next.current_track.index = ev.payload.track_started.index;
            next.idle_deadline_ms = 0;  // disable idle timer while playing
            break;

        case EventType::MusicTrackStartFailed:
            next.audio_state = AudioState::Idle;
            fx.add(makeLogDiagnosticEffect(2));
            break;

        case EventType::AudioStopped:
            if (s.sleep_state == SleepState::PreparingDeepSleep) {
                advanceSleepAfterAudioStopped(next, s.requested_sleep_kind, s.bt_state, fx);
            } else {
                next.audio_state = AudioState::Idle;
                if (s.session_mode == SessionMode::Normal) {
                    next.idle_deadline_ms = now_ms + REDUCER_IDLE_TIMEOUT_MS;
                }
            }
            break;

        case EventType::SystemSoundCompleted:
        case EventType::SystemSoundFailed: {
            uint8_t sound_id = (ev.type == EventType::SystemSoundCompleted)
                               ? ev.payload.sound_completed.sound_id
                               : ev.payload.sound_failed.sound_id;
            if (s.sleep_state == SleepState::WaitingPowerOffSound) {
                next.sleep_state = SleepState::WaitingBtShutdown;
                next.audio_state = AudioState::Idle;
                fx.add(makeShutdownBtEffect());
            } else {
                next.audio_state = AudioState::Idle;
                if (sound_id == SOUND_ID_MUSIC_MODE &&
                    s.playback_mode == PlaybackMode::Music &&
                    s.bt_state == BtState::Connected &&
                    s.sleep_state == SleepState::Awake) {
                    uint16_t idx = s.current_track.valid ? s.current_track.index : 0;
                    next.audio_state = AudioState::StartingFile;
                    next.current_track.valid = true;
                    next.current_track.index = idx;
                    fx.add(makeStartMusicTrackEffect(idx));
                }
            }
            break;
        }

        case EventType::TrackEnded:
            if (in_sleep) break;
            if (s.playback_mode == PlaybackMode::Music &&
                s.audio_state == AudioState::PlayingFile &&
                s.bt_state == BtState::Connected) {
                uint16_t next_index = s.current_track.index + 1;
                if (s.total_track_count > 0 && next_index >= s.total_track_count) {
                    next_index = 0;
                }
                next.current_track.valid = true;
                next.current_track.index = next_index;
                next.audio_state = AudioState::StartingFile;
                fx.add(makeStartMusicTrackEffect(next_index));
            } else if (s.playback_mode == PlaybackMode::Nfc) {
                next.audio_state = AudioState::Idle;
            }
            break;

        // ----------------------------------------------------------------
        // Sleep
        // ----------------------------------------------------------------
        case EventType::SleepHoldWarning:
            if (s.sleep_state == SleepState::Awake) {
                next.sleep_warn_active = true;
            }
            break;

        case EventType::SleepRequested: {
            RequestedSleepKind kind = ev.payload.sleep_requested.kind;
            if (s.sleep_state == SleepState::Awake) {
                next.sleep_warn_active = false;
                next.sleep_state = SleepState::PreparingDeepSleep;
                next.requested_sleep_kind = kind;
                if (isAudioActive(s.audio_state)) {
                    fx.add(makeStopAudioEffect());
                    next.audio_state = AudioState::Stopping;
                } else {
                    // No audio to stop — advance immediately
                    advanceSleepAfterAudioStopped(next, kind, s.bt_state, fx);
                }
            } else if (s.sleep_state == SleepState::PreparingDeepSleep) {
                if (kind == RequestedSleepKind::Emergency) {
                    next.requested_sleep_kind = RequestedSleepKind::Emergency;
                }
            }
            break;
        }

        case EventType::IdleTimeoutFired:
            if (s.sleep_state == SleepState::Awake) {
                next.sleep_state = SleepState::PreparingDeepSleep;
                next.requested_sleep_kind = RequestedSleepKind::Normal;
                next.idle_deadline_ms = 0;
                if (isAudioActive(s.audio_state)) {
                    fx.add(makeStopAudioEffect());
                    next.audio_state = AudioState::Stopping;
                } else {
                    advanceSleepAfterAudioStopped(next, RequestedSleepKind::Normal, s.bt_state, fx);
                }
            }
            break;

        case EventType::NightLightTimeoutFired:
            if (s.sleep_state == SleepState::Awake) {
                next.sleep_state = SleepState::PreparingDeepSleep;
                next.requested_sleep_kind = RequestedSleepKind::NightLightTimeout;
                next.night_light_deadline_ms = 0;
                // Flush pending brightness save before sleep
                if (next.brightness_save_deadline_ms != 0) {
                    next.brightness_save_deadline_ms = 0;
                    fx.add(makePersistBrightnessEffect(s.night_light_brightness_percent));
                }
                // Night-light: audio never active — advance immediately to BT shutdown
                advanceSleepAfterAudioStopped(next, RequestedSleepKind::NightLightTimeout,
                                              s.bt_state, fx);
            }
            break;

        // ----------------------------------------------------------------
        // Timer expirations
        // ----------------------------------------------------------------
        case EventType::VolumeOverlayExpired:
            next.volume_overlay_deadline_ms = 0;
            break;

        case EventType::BatteryPreviewExpired:
            next.battery_preview_active = false;
            next.battery_preview_deadline_ms = 0;
            break;

        case EventType::JblRecoveryTimeoutFired:
            if (s.bt_state == BtState::Connected) {
                next.bt_state = BtState::RecoveryPulsePending;
                next.jbl_recovery_deadline_ms = 0;
                fx.add(makeTriggerBtRecoveryPulseEffect());
            }
            break;

        case EventType::BtReconnectTimeoutFired:
            if (s.bt_state == BtState::WaitingForSpeaker) {
                next.bt_reconnect_deadline_ms = 0;
                fx.add({ EffectType::TriggerBtDiscoveryRestart });
            }
            break;

        // ----------------------------------------------------------------
        // Stage 5: Button events
        // ----------------------------------------------------------------
        case EventType::VolumeUpPressed:
            if (in_sleep) break;
            if (s.session_mode == SessionMode::NightLight) {
                next.night_light_brightness_percent =
                    clampU8((int)s.night_light_brightness_percent + REDUCER_BRIGHTNESS_STEP,
                            REDUCER_BRIGHTNESS_MIN, REDUCER_BRIGHTNESS_MAX);
                next.brightness_save_deadline_ms = now_ms + REDUCER_BRIGHTNESS_SAVE_MS;
                next.night_light_deadline_ms     = now_ms + REDUCER_NIGHT_LIGHT_TIMEOUT_MS;
            } else {
                next.music_volume_percent =
                    clampU8((int)s.music_volume_percent + REDUCER_VOLUME_STEP,
                            REDUCER_VOLUME_MIN, REDUCER_VOLUME_MAX);
                next.volume_overlay_level_percent = next.music_volume_percent;
                next.volume_overlay_deadline_ms   = now_ms + REDUCER_VOLUME_OVERLAY_MS;
                next.idle_deadline_ms             = now_ms + REDUCER_IDLE_TIMEOUT_MS;
                fx.add(makeSetVolumeEffect(next.music_volume_percent));
                fx.add(makePersistVolumeEffect(next.music_volume_percent));
            }
            break;

        case EventType::VolumeDownPressed:
            if (in_sleep) break;
            if (s.session_mode == SessionMode::NightLight) {
                next.night_light_brightness_percent =
                    clampU8((int)s.night_light_brightness_percent - REDUCER_BRIGHTNESS_STEP,
                            REDUCER_BRIGHTNESS_MIN, REDUCER_BRIGHTNESS_MAX);
                next.brightness_save_deadline_ms = now_ms + REDUCER_BRIGHTNESS_SAVE_MS;
                next.night_light_deadline_ms     = now_ms + REDUCER_NIGHT_LIGHT_TIMEOUT_MS;
            } else {
                next.music_volume_percent =
                    clampU8((int)s.music_volume_percent - REDUCER_VOLUME_STEP,
                            REDUCER_VOLUME_MIN, REDUCER_VOLUME_MAX);
                next.volume_overlay_level_percent = next.music_volume_percent;
                next.volume_overlay_deadline_ms   = now_ms + REDUCER_VOLUME_OVERLAY_MS;
                next.idle_deadline_ms             = now_ms + REDUCER_IDLE_TIMEOUT_MS;
                fx.add(makeSetVolumeEffect(next.music_volume_percent));
                fx.add(makePersistVolumeEffect(next.music_volume_percent));
            }
            break;

        case EventType::PlayPausePressed:
            if (in_sleep) break;
            if (s.session_mode != SessionMode::Normal) break;
            if (s.playback_mode != PlaybackMode::Music) break;
            next.idle_deadline_ms = now_ms + REDUCER_IDLE_TIMEOUT_MS;
            if (s.audio_state == AudioState::PlayingFile) {
                next.audio_state = AudioState::Paused;
                fx.add(makePauseAudioEffect());
            } else if (s.audio_state == AudioState::Paused) {
                next.audio_state = AudioState::PlayingFile;
                next.idle_deadline_ms = 0;  // will be cleared when playing
                fx.add(makeResumeAudioEffect());
            } else if (s.audio_state == AudioState::Idle &&
                       s.bt_state == BtState::Connected) {
                uint16_t idx = s.current_track.valid ? s.current_track.index : 0;
                next.audio_state = AudioState::StartingFile;
                fx.add(makeStartMusicTrackEffect(idx));
            }
            break;

        case EventType::NextTrackPressed:
            if (in_sleep) break;
            if (s.session_mode != SessionMode::Normal) break;
            if (s.playback_mode != PlaybackMode::Music) break;
            if (s.bt_state != BtState::Connected) break;
            {
                uint16_t ni = s.current_track.valid ? s.current_track.index + 1 : 0;
                if (s.total_track_count > 0 && ni >= s.total_track_count) ni = 0;
                next.current_track.valid = true;
                next.current_track.index = ni;
                next.audio_state = AudioState::StartingFile;
                fx.add(makeStartMusicTrackEffect(ni));
            }
            break;

        case EventType::PrevTrackPressed:
            if (in_sleep) break;
            if (s.session_mode != SessionMode::Normal) break;
            if (s.playback_mode != PlaybackMode::Music) break;
            if (s.bt_state != BtState::Connected) break;
            {
                uint16_t pi = 0;
                if (s.current_track.valid && s.current_track.index > 0)
                    pi = s.current_track.index - 1;
                else if (s.total_track_count > 0)
                    pi = s.total_track_count - 1;
                next.current_track.valid = true;
                next.current_track.index = pi;
                next.audio_state = AudioState::StartingFile;
                fx.add(makeStartMusicTrackEffect(pi));
            }
            break;

        case EventType::ModeToggleRequested:
            if (in_sleep) break;
            if (s.session_mode != SessionMode::Normal) break;
            next.playback_mode = (s.playback_mode == PlaybackMode::Nfc)
                                 ? PlaybackMode::Music : PlaybackMode::Nfc;
            next.last_nfc_uid[0] = '\0';
            next.idle_deadline_ms = now_ms + REDUCER_IDLE_TIMEOUT_MS;
            if (isAudioActive(s.audio_state)) {
                fx.add(makeStopAudioEffect());
                next.audio_state = AudioState::Stopping;
            }
            fx.add(makePlaySystemSoundEffect(
                next.playback_mode == PlaybackMode::Music
                    ? SOUND_ID_MUSIC_MODE : SOUND_ID_NFC_MODE));
            // Music autostart deferred to SystemSoundCompleted/Failed so the
            // mode-change sound plays fully before music begins.
            fx.add(makePersistPlaybackModeEffect(next.playback_mode));
            break;

        case EventType::BatteryCheckRequested:
            if (in_sleep) break;
            next.battery_bars = ev.payload.battery_check.bars;
            next.battery_preview_active = true;
            next.battery_preview_deadline_ms = now_ms + REDUCER_BATTERY_PREVIEW_MS;
            next.last_nfc_uid[0] = '\0';
            next.idle_deadline_ms = now_ms + REDUCER_IDLE_TIMEOUT_MS;
            break;

        case EventType::SyncModeRequested:
            next.sync_mode = true;
            fx.add(makeTriggerSyncRestartEffect());
            break;

        case EventType::BrightnessSaveDeadlineFired:
            next.brightness_save_deadline_ms = 0;
            fx.add(makePersistBrightnessEffect(s.night_light_brightness_percent));
            break;

        default:
            break;
    }

    return { next, fx.effects, fx.count };
}
