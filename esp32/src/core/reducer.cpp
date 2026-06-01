#include "reducer.h"
#include <cstring>
#include "volume_scale.h"
#include "zbox_config.h"

static constexpr uint8_t REDUCER_BRIGHTNESS_STEP = 10;
static constexpr uint8_t REDUCER_BRIGHTNESS_MIN = 10;
static constexpr uint8_t REDUCER_BRIGHTNESS_MAX = 100;
static constexpr uint32_t REDUCER_IDLE_TIMEOUT_MS = 10UL * 60 * 1000;
static constexpr uint32_t REDUCER_NIGHT_LIGHT_TIMEOUT_MS = 15UL * 60 * 1000;
static constexpr uint32_t REDUCER_BRIGHTNESS_SAVE_MS = 1500;
static constexpr uint32_t REDUCER_VOLUME_OVERLAY_MS = 1000;
static constexpr uint32_t REDUCER_BATTERY_PREVIEW_MS = 3000;
static constexpr uint32_t REDUCER_SLEEP_TRANSITION_TIMEOUT_MS = 8000;

static Effect makeSetOutputVolumeEffect(uint8_t percent) {
    Effect e{};
    e.type = EffectType::SetOutputVolume;
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

static Effect makePlaySystemSoundEffect(uint8_t sound_id) {
    Effect e{};
    e.type = EffectType::PlaySystemSound;
    e.payload.system_sound.sound_id = sound_id;
    e.payload.system_sound.cmd_id = 0;
    return e;
}

static void addPlaySystemSoundWithFixedVolume(EffectBuilder& fx, uint8_t sound_id) {
    fx.add(makeSetOutputVolumeEffect(SYSTEM_SOUND_VOL_PERCENT));
    fx.add(makePlaySystemSoundEffect(sound_id));
}

static uint8_t modeSoundIdForPlaybackMode(PlaybackMode mode) {
    return mode == PlaybackMode::Music ? SOUND_ID_MUSIC_MODE : SOUND_ID_NFC_MODE;
}

static void startModeAnnouncement(AppState& next, EffectBuilder& fx) {
    next.audio_state = AudioState::StartingSystemSound;
    addPlaySystemSoundWithFixedVolume(fx, modeSoundIdForPlaybackMode(next.playback_mode));
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

static Effect makeStartBtHeadphonesModeEffect() {
    Effect e{};
    e.type = EffectType::StartBtHeadphonesMode;
    return e;
}

static Effect makeStopBtHeadphonesModeEffect() {
    Effect e{};
    e.type = EffectType::StopBtHeadphonesMode;
    return e;
}

static Effect makeEnterDeepSleepEffect(RequestedSleepKind kind) {
    Effect e{};
    e.type = EffectType::EnterDeepSleep;
    e.payload.deep_sleep.kind = kind;
    return e;
}

static Effect makeLogDiagnosticEffect(uint8_t code) {
    Effect e{};
    e.type = EffectType::LogDiagnostic;
    e.payload.diagnostic.code = code;
    return e;
}

static Effect makeTriggerSyncRestartEffect() {
    Effect e{};
    e.type = EffectType::TriggerSyncRestart;
    return e;
}

static uint8_t clampU8(int val, uint8_t lo, uint8_t hi) {
    if (val < (int)lo) return lo;
    if (val > (int)hi) return hi;
    return (uint8_t)val;
}

static void copyUid(char* dst, const char* src, size_t dst_size) {
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static bool isAudioActive(AudioState s) {
    return s == AudioState::PlayingFile ||
           s == AudioState::StartingFile ||
           s == AudioState::PlayingSystemSound ||
           s == AudioState::StartingSystemSound ||
           s == AudioState::Paused ||
           s == AudioState::StoppingForOutputChange ||
           s == AudioState::StoppingForModeChange;
}

// Arms the 8s sleep-transition watchdog when entering an intermediate sleep state.
// Clears it on ReadyToSleep. Checks next.sleep_transition_deadline_ms (not s.)
// so multiple calls in one reduce flow don't reset the original budget.
static void updateSleepDeadline(AppState& next, uint32_t now_ms) {
    if (next.sleep_state == SleepState::ReadyToSleep) {
        next.sleep_transition_deadline_ms = 0;
    } else if (next.sleep_state != SleepState::Awake &&
               next.sleep_transition_deadline_ms == 0) {
        next.sleep_transition_deadline_ms = now_ms + REDUCER_SLEEP_TRANSITION_TIMEOUT_MS;
    }
}

static void startPendingPlaybackIfAny(const AppState& s, AppState& next, EffectBuilder& fx) {
    if (s.pending_playback.kind == PendingPlaybackKind::NfcUid) {
        next.audio_state = AudioState::StartingFile;
        next.pending_playback.kind = PendingPlaybackKind::None;
        fx.add(makeStartNfcPlaybackEffect(s.pending_playback.uid));
    } else if (s.pending_playback.kind == PendingPlaybackKind::MusicCurrentTrack) {
        uint16_t idx = s.current_track.valid ? s.current_track.index : 0;
        next.current_track.valid = true;
        next.current_track.index = idx;
        next.audio_state = AudioState::StartingFile;
        next.pending_playback.kind = PendingPlaybackKind::None;
        fx.add(makeStartMusicTrackEffect(idx));
    } else if (s.pending_playback.kind == PendingPlaybackKind::MusicSpecificTrack) {
        next.current_track.valid = true;
        next.current_track.index = s.pending_playback.track_index;
        next.audio_state = AudioState::StartingFile;
        next.pending_playback.kind = PendingPlaybackKind::None;
        fx.add(makeStartMusicTrackEffect(s.pending_playback.track_index));
    }
}

static void storeCurrentPlaybackAsPending(const AppState& s, AppState& next) {
    if (s.playback_mode == PlaybackMode::Nfc && s.last_nfc_uid[0] != '\0') {
        next.pending_playback.kind = PendingPlaybackKind::NfcUid;
        copyUid(next.pending_playback.uid, s.last_nfc_uid, sizeof(next.pending_playback.uid));
    } else if (s.playback_mode == PlaybackMode::Music) {
        next.pending_playback.kind = PendingPlaybackKind::MusicSpecificTrack;
        next.pending_playback.track_index = s.current_track.valid ? s.current_track.index : 0;
    } else {
        next.pending_playback.kind = PendingPlaybackKind::None;
    }
}

static void finishSleepAfterAudioStopped(AppState& next, const AppState& s, EffectBuilder& fx, uint32_t now_ms) {
    if (s.requested_sleep_kind == RequestedSleepKind::Normal) {
        next.sleep_state = SleepState::WaitingPowerOffSound;
        next.audio_state = AudioState::StartingSystemSound;
        addPlaySystemSoundWithFixedVolume(fx, SOUND_ID_POWER_OFF);
    } else if (s.bt_headphones_mode_active) {
        next.sleep_state = SleepState::WaitingBtHeadphonesStop;
        fx.add(makeStopBtHeadphonesModeEffect());
    } else {
        next.sleep_state = SleepState::ReadyToSleep;
        fx.add(makeEnterDeepSleepEffect(s.requested_sleep_kind));
    }
    updateSleepDeadline(next, now_ms);
}

ReduceResult reduce(const AppState& s, const Event& ev, uint32_t now_ms) {
    AppState next = s;
    EffectBuilder fx;
    const bool in_sleep = (s.sleep_state != SleepState::Awake);

    switch (ev.type) {
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
            next.output_mode = AudioOutputMode::LocalSpeaker;
            next.bt_headphones_state = BtHeadphonesState::Inactive;
            if (s.session_mode == SessionMode::NightLight) {
                next.night_light_deadline_ms = now_ms + REDUCER_NIGHT_LIGHT_TIMEOUT_MS;
            } else {
                next.idle_deadline_ms = now_ms + REDUCER_IDLE_TIMEOUT_MS;
                if (s.playback_mode == PlaybackMode::Music &&
                    s.pending_playback.kind == PendingPlaybackKind::None) {
                    next.pending_playback.kind = PendingPlaybackKind::MusicCurrentTrack;
                }
            }
            next.audio_state = AudioState::StartingSystemSound;
            addPlaySystemSoundWithFixedVolume(fx, SOUND_ID_STARTUP);
            break;

        case EventType::PlaybackModeLoaded:
            next.playback_mode = ev.payload.playback_mode_loaded.mode;
            break;

        case EventType::BrightnessLoaded:
            next.night_light_brightness_percent = ev.payload.brightness_loaded.percent;
            break;

        case EventType::MappingsLoaded:
            next.total_track_count = ev.payload.mappings_loaded.total_track_count;
            break;

        case EventType::VolumeLoaded:
            next.output_volume_level = clampVolumeLevel(ev.payload.volume_loaded.level);
            break;

        case EventType::NfcPreScanCompleted:
            if (ev.payload.nfc_prescan.uid_found) {
                next.pending_playback.kind = PendingPlaybackKind::NfcUid;
                copyUid(next.pending_playback.uid, ev.payload.nfc_prescan.uid, sizeof(next.pending_playback.uid));
                copyUid(next.last_nfc_uid, ev.payload.nfc_prescan.uid, sizeof(next.last_nfc_uid));
            } else {
                next.pending_playback.kind = PendingPlaybackKind::None;
            }
            break;

        case EventType::BtHeadphonesModeRequested:
            if (s.session_mode != SessionMode::Normal || in_sleep) break;
            if (!s.bt_headphones_mode_active) {
                next.bt_headphones_mode_active = true;
                next.bt_headphones_state = BtHeadphonesState::WaitingForHeadphones;
                if (isAudioActive(s.audio_state)) {
                    storeCurrentPlaybackAsPending(s, next);
                    next.audio_state = AudioState::StoppingForOutputChange;
                    fx.add(makeStopAudioEffect());
                } else {
                    fx.add(makeStartBtHeadphonesModeEffect());
                }
            } else {
                next.bt_headphones_mode_active = false;
                next.bt_headphones_state = BtHeadphonesState::Stopping;
                next.output_mode = AudioOutputMode::LocalSpeaker;
                if (isAudioActive(s.audio_state)) {
                    storeCurrentPlaybackAsPending(s, next);
                    next.audio_state = AudioState::StoppingForOutputChange;
                    fx.add(makeStopAudioEffect());
                } else {
                    fx.add(makeStopBtHeadphonesModeEffect());
                }
            }
            break;

        case EventType::BtConnected:
            if (!s.bt_headphones_mode_active) break;
            next.bt_headphones_state = BtHeadphonesState::Connected;
            next.output_mode = AudioOutputMode::BtHeadphones;
            fx.add(makeSetOutputVolumeEffect(volumeLevelToPercent(s.output_volume_level)));
            if (s.audio_state == AudioState::Idle) {
                startPendingPlaybackIfAny(s, next, fx);
            }
            break;

        case EventType::BtDisconnected:
            if (!s.bt_headphones_mode_active) break;
            next.bt_headphones_state = BtHeadphonesState::WaitingForHeadphones;
            next.output_mode = AudioOutputMode::LocalSpeaker;
            break;

        case EventType::BtHeadphonesModeStopped:
        case EventType::BtHeadphonesModeStopFailed:
            next.bt_headphones_state = BtHeadphonesState::Inactive;
            next.bt_headphones_mode_active = false;
            next.output_mode = AudioOutputMode::LocalSpeaker;
            if (s.sleep_state == SleepState::WaitingBtHeadphonesStop) {
                next.sleep_state = SleepState::ReadyToSleep;
                fx.add(makeEnterDeepSleepEffect(s.requested_sleep_kind));
                updateSleepDeadline(next, now_ms);
            } else if (s.audio_state == AudioState::Idle) {
                startPendingPlaybackIfAny(s, next, fx);
            }
            break;

        case EventType::BtHeadphonesModeStartFailed:
            next.bt_headphones_mode_active = false;
            next.bt_headphones_state = BtHeadphonesState::Inactive;
            next.output_mode = AudioOutputMode::LocalSpeaker;
            fx.add(makeLogDiagnosticEffect(10));
            break;

        case EventType::NfcTagDetected:
            if (in_sleep || s.playback_mode == PlaybackMode::Music) break;
            if (s.nfc_card_played && strcmp(ev.payload.nfc_detected.uid, s.last_nfc_uid) == 0) break;
            next.nfc_card_played = false;
            copyUid(next.last_nfc_uid, ev.payload.nfc_detected.uid, sizeof(next.last_nfc_uid));
            next.audio_state = AudioState::StartingFile;
            next.pending_playback.kind = PendingPlaybackKind::None;
            fx.add(makeStartNfcPlaybackEffect(ev.payload.nfc_detected.uid));
            break;

        case EventType::NfcTagRemoved:
            if (in_sleep) break;
            if (s.playback_mode == PlaybackMode::Nfc &&
                (s.audio_state == AudioState::PlayingFile || s.audio_state == AudioState::StartingFile)) {
                next.audio_state = AudioState::Stopping;
                fx.add(makeStopAudioEffect());
            }
            next.nfc_card_played = false;
            next.pending_playback.kind = PendingPlaybackKind::None;
            break;

        case EventType::NfcPlaybackStarted:
            next.audio_state = AudioState::PlayingFile;
            next.idle_deadline_ms = 0;
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
            next.idle_deadline_ms = 0;
            break;

        case EventType::MusicTrackStartFailed:
            next.audio_state = AudioState::Idle;
            fx.add(makeLogDiagnosticEffect(2));
            break;

        case EventType::AudioCommandRejected:
            if (s.audio_state == AudioState::StartingFile) {
                next.audio_state = AudioState::Idle;
                next.pending_playback.kind = PendingPlaybackKind::None;
                fx.add(makeLogDiagnosticEffect(
                    s.playback_mode == PlaybackMode::Music ? 2 : 1));
            } else if (s.audio_state == AudioState::StoppingForModeChange) {
                startModeAnnouncement(next, fx);
            } else if (s.audio_state == AudioState::StoppingForOutputChange) {
                next.audio_state = AudioState::Idle;
                if (s.bt_headphones_state == BtHeadphonesState::Stopping) {
                    fx.add(makeStopBtHeadphonesModeEffect());
                } else if (s.bt_headphones_mode_active &&
                           s.bt_headphones_state == BtHeadphonesState::WaitingForHeadphones) {
                    fx.add(makeStartBtHeadphonesModeEffect());
                } else {
                    startPendingPlaybackIfAny(s, next, fx);
                }
            } else if (s.audio_state == AudioState::Stopping) {
                next.audio_state = AudioState::Idle;
                if (s.session_mode == SessionMode::Normal) {
                    next.idle_deadline_ms = now_ms + REDUCER_IDLE_TIMEOUT_MS;
                }
            }
            break;

        case EventType::AudioStopped:
            if (s.sleep_state == SleepState::PreparingDeepSleep) {
                finishSleepAfterAudioStopped(next, s, fx, now_ms);
            } else if (s.audio_state == AudioState::StoppingForOutputChange) {
                next.audio_state = AudioState::Idle;
                if (s.bt_headphones_state == BtHeadphonesState::Stopping) {
                    fx.add(makeStopBtHeadphonesModeEffect());
                } else if (s.bt_headphones_mode_active &&
                           s.bt_headphones_state == BtHeadphonesState::WaitingForHeadphones) {
                    fx.add(makeStartBtHeadphonesModeEffect());
                } else {
                    startPendingPlaybackIfAny(s, next, fx);
                }
            } else if (s.audio_state == AudioState::StoppingForModeChange) {
                startModeAnnouncement(next, fx);
            } else {
                next.audio_state = AudioState::Idle;
                if (s.session_mode == SessionMode::Normal) {
                    next.idle_deadline_ms = now_ms + REDUCER_IDLE_TIMEOUT_MS;
                }
            }
            break;

        case EventType::SystemSoundCompleted:
        case EventType::SystemSoundFailed: {
            const uint8_t sound_id = (ev.type == EventType::SystemSoundCompleted)
                ? ev.payload.sound_completed.sound_id
                : ev.payload.sound_failed.sound_id;
            if (s.sleep_state == SleepState::WaitingPowerOffSound) {
                next.audio_state = AudioState::Idle;
                if (s.bt_headphones_mode_active) {
                    next.sleep_state = SleepState::WaitingBtHeadphonesStop;
                    fx.add(makeStopBtHeadphonesModeEffect());
                } else {
                    next.sleep_state = SleepState::ReadyToSleep;
                    fx.add(makeEnterDeepSleepEffect(s.requested_sleep_kind));
                }
                updateSleepDeadline(next, now_ms);
            } else {
                next.audio_state = AudioState::Idle;
                fx.add(makeSetOutputVolumeEffect(volumeLevelToPercent(s.output_volume_level)));
                if (sound_id == SOUND_ID_STARTUP &&
                    s.sleep_state == SleepState::Awake) {
                    startPendingPlaybackIfAny(s, next, fx);
                } else if (sound_id == SOUND_ID_MUSIC_MODE &&
                    s.playback_mode == PlaybackMode::Music &&
                    s.sleep_state == SleepState::Awake) {
                    const uint16_t idx = s.current_track.valid ? s.current_track.index : 0;
                    next.current_track.valid = true;
                    next.current_track.index = idx;
                    next.audio_state = AudioState::StartingFile;
                    fx.add(makeStartMusicTrackEffect(idx));
                }
            }
            break;
        }

        case EventType::TrackEnded:
            if (in_sleep) break;
            if (s.playback_mode == PlaybackMode::Music &&
                s.audio_state == AudioState::PlayingFile) {
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
                next.nfc_card_played = true;
            }
            break;

        case EventType::SleepHoldWarning:
            // Start the sleep sequence immediately at the hold threshold so the power-off
            // sound plays at the "you can release" moment. SleepRequested (fired on release)
            // is a no-op when sleep_state is already PreparingDeepSleep.
            if (s.sleep_state == SleepState::Awake) {
                next.sleep_state = SleepState::PreparingDeepSleep;
                next.requested_sleep_kind = RequestedSleepKind::Normal;
                next.idle_deadline_ms = 0;
                next.night_light_deadline_ms = 0;
                if (isAudioActive(s.audio_state)) {
                    next.audio_state = AudioState::Stopping;
                    fx.add(makeStopAudioEffect());
                    updateSleepDeadline(next, now_ms);
                } else {
                    finishSleepAfterAudioStopped(next, next, fx, now_ms);
                }
            }
            break;

        case EventType::SleepRequested: {
            const RequestedSleepKind kind = ev.payload.sleep_requested.kind;
            if (s.sleep_state == SleepState::Awake) {
                next.sleep_state = SleepState::PreparingDeepSleep;
                next.requested_sleep_kind = kind;
                next.idle_deadline_ms = 0;
                next.night_light_deadline_ms = 0;
                if (isAudioActive(s.audio_state)) {
                    next.audio_state = AudioState::Stopping;
                    fx.add(makeStopAudioEffect());
                    updateSleepDeadline(next, now_ms);
                } else {
                    finishSleepAfterAudioStopped(next, next, fx, now_ms);
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
                    next.audio_state = AudioState::Stopping;
                    fx.add(makeStopAudioEffect());
                    updateSleepDeadline(next, now_ms);
                } else {
                    finishSleepAfterAudioStopped(next, next, fx, now_ms);
                }
            }
            break;

        case EventType::NightLightTimeoutFired:
            if (s.sleep_state == SleepState::Awake) {
                next.sleep_state = SleepState::PreparingDeepSleep;
                next.requested_sleep_kind = RequestedSleepKind::NightLightTimeout;
                next.night_light_deadline_ms = 0;
                if (next.brightness_save_deadline_ms != 0) {
                    next.brightness_save_deadline_ms = 0;
                    fx.add(makePersistBrightnessEffect(s.night_light_brightness_percent));
                }
                next.sleep_state = SleepState::ReadyToSleep;
                fx.add(makeEnterDeepSleepEffect(RequestedSleepKind::NightLightTimeout));
                updateSleepDeadline(next, now_ms);
            }
            break;

        case EventType::SleepTimeoutFired:
            if (s.sleep_state != SleepState::Awake &&
                s.sleep_state != SleepState::ReadyToSleep) {
                next.sleep_state = SleepState::ReadyToSleep;
                next.sleep_transition_deadline_ms = 0;
                fx.add(makeEnterDeepSleepEffect(s.requested_sleep_kind));
            }
            break;

        case EventType::VolumeOverlayExpired:
            if (s.volume_overlay_deadline_ms == 0) break;
            if (now_ms < s.volume_overlay_deadline_ms) break;
            next.volume_overlay_deadline_ms = 0;
            fx.add(makePersistVolumeEffect(s.output_volume_level));
            break;

        case EventType::BatteryPreviewExpired:
            next.battery_preview_active = false;
            next.battery_preview_deadline_ms = 0;
            break;

        case EventType::VolumeUpPressed:
            if (in_sleep) break;
            if (s.session_mode == SessionMode::NightLight) {
                next.night_light_brightness_percent = clampU8(
                    (int)s.night_light_brightness_percent + REDUCER_BRIGHTNESS_STEP,
                    REDUCER_BRIGHTNESS_MIN, REDUCER_BRIGHTNESS_MAX);
                next.brightness_save_deadline_ms = now_ms + REDUCER_BRIGHTNESS_SAVE_MS;
                next.night_light_deadline_ms = now_ms + REDUCER_NIGHT_LIGHT_TIMEOUT_MS;
            } else {
                const uint8_t steppedLevel = stepVolumeLevelUp(s.output_volume_level);
                if (steppedLevel != s.output_volume_level) {
                    next.output_volume_level = steppedLevel;
                    next.volume_overlay_level = next.output_volume_level;
                    next.volume_overlay_deadline_ms = now_ms + REDUCER_VOLUME_OVERLAY_MS;
                    next.idle_deadline_ms = now_ms + REDUCER_IDLE_TIMEOUT_MS;
                    fx.add(makeSetOutputVolumeEffect(volumeLevelToPercent(next.output_volume_level)));
                }
            }
            break;

        case EventType::VolumeDownPressed:
            if (in_sleep) break;
            if (s.session_mode == SessionMode::NightLight) {
                next.night_light_brightness_percent = clampU8(
                    (int)s.night_light_brightness_percent - REDUCER_BRIGHTNESS_STEP,
                    REDUCER_BRIGHTNESS_MIN, REDUCER_BRIGHTNESS_MAX);
                next.brightness_save_deadline_ms = now_ms + REDUCER_BRIGHTNESS_SAVE_MS;
                next.night_light_deadline_ms = now_ms + REDUCER_NIGHT_LIGHT_TIMEOUT_MS;
            } else {
                const uint8_t steppedLevel = stepVolumeLevelDown(s.output_volume_level);
                if (steppedLevel != s.output_volume_level) {
                    next.output_volume_level = steppedLevel;
                    next.volume_overlay_level = next.output_volume_level;
                    next.volume_overlay_deadline_ms = now_ms + REDUCER_VOLUME_OVERLAY_MS;
                    next.idle_deadline_ms = now_ms + REDUCER_IDLE_TIMEOUT_MS;
                    fx.add(makeSetOutputVolumeEffect(volumeLevelToPercent(next.output_volume_level)));
                }
            }
            break;

        case EventType::PlayPausePressed:
            if (in_sleep || s.session_mode != SessionMode::Normal || s.playback_mode != PlaybackMode::Music) break;
            next.idle_deadline_ms = now_ms + REDUCER_IDLE_TIMEOUT_MS;
            if (s.audio_state == AudioState::PlayingFile) {
                next.audio_state = AudioState::Paused;
                fx.add(makePauseAudioEffect());
            } else if (s.audio_state == AudioState::Paused) {
                next.audio_state = AudioState::PlayingFile;
                next.idle_deadline_ms = 0;
                fx.add(makeResumeAudioEffect());
            } else if (s.audio_state == AudioState::Idle) {
                const uint16_t idx = s.current_track.valid ? s.current_track.index : 0;
                next.current_track.valid = true;
                next.current_track.index = idx;
                next.audio_state = AudioState::StartingFile;
                fx.add(makeStartMusicTrackEffect(idx));
            }
            break;

        case EventType::NextTrackPressed:
            if (in_sleep || s.session_mode != SessionMode::Normal || s.playback_mode != PlaybackMode::Music) break;
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
            if (in_sleep || s.session_mode != SessionMode::Normal || s.playback_mode != PlaybackMode::Music) break;
            {
                uint16_t pi = 0;
                if (s.current_track.valid && s.current_track.index > 0) {
                    pi = s.current_track.index - 1;
                } else if (s.total_track_count > 0) {
                    pi = s.total_track_count - 1;
                }
                next.current_track.valid = true;
                next.current_track.index = pi;
                next.audio_state = AudioState::StartingFile;
                fx.add(makeStartMusicTrackEffect(pi));
            }
            break;

        case EventType::ModeToggleRequested:
            if (in_sleep || s.session_mode != SessionMode::Normal) break;
            next.playback_mode = (s.playback_mode == PlaybackMode::Nfc) ? PlaybackMode::Music : PlaybackMode::Nfc;
            next.last_nfc_uid[0] = '\0';
            next.nfc_card_played = false;
            next.idle_deadline_ms = now_ms + REDUCER_IDLE_TIMEOUT_MS;
            if (isAudioActive(s.audio_state)) {
                next.audio_state = AudioState::StoppingForModeChange;
                fx.add(makeStopAudioEffect());
            } else {
                startModeAnnouncement(next, fx);
            }
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
