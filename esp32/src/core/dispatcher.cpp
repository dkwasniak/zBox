#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>
#include "dispatcher.h"
#include "event_queue.h"
#include "reducer.h"
#include "led_scene.h"
#include "leds.h"
#include "audio.h"
#include "nfc_module.h"
#include "logging.h"
#include "musicbox_assert.h"

#if DISPATCHER_OWNS_BT_NFC
#include "volume.h"
#include "state.h"
#include "playback.h"
#include "bt_adapter.h"
#endif

#if DISPATCHER_OWNS_AUDIO
#include "audio_adapter.h"
#endif

#if DISPATCHER_OWNS_SLEEP
#include "sleep.h"
#endif

#if DISPATCHER_OWNS_BUTTONS
#include "button_adapter.h"
#include "volume.h"
#include "battery.h"
#include "helpers.h"
#include "persistence_adapter.h"
#include <SD.h>
#include "zbox_config.h"
#endif

// ----- Shared state (protected by spinlock for getDiagnosticSnapshot) -----
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static AppState s_state{};

// ----- LED scene cache (prevent spurious re-application of unchanged scenes) -----
static bool           s_has_last_led_scene = false;
static LedSceneParams s_last_led_scene{};

// ----- Transition ring buffer (last 32 transitions) -----
struct TransitionEntry {
    uint32_t   timestamp_ms;
    EventType  event_type;
    BtHeadphonesState bt_state;
    AudioState audio_state;
    uint8_t    effect_count;
};
static constexpr uint8_t RING_SIZE = 32;
static TransitionEntry s_ring[RING_SIZE];
static uint8_t s_ring_head  = 0;
static uint8_t s_ring_count = 0;

// ----- Pending audio effects registry (cmd_id correlation + timeout) -----
struct PendingAudioEffect {
    CmdId      cmd_id;
    EffectType type;
    uint32_t   deadline_ms;  // 0 = no timeout
    bool       active;
};
static constexpr uint8_t MAX_PENDING_AUDIO = 8;
static PendingAudioEffect s_pending[MAX_PENDING_AUDIO];
static uint16_t           s_next_cmd_id = 1;  // 0 reserved as "unassigned"

static CmdId nextCmdId() {
    CmdId id = s_next_cmd_id++;
    if (s_next_cmd_id == 0) s_next_cmd_id = 1;  // skip 0 on wraparound
    return id;
}

static void registerPending(CmdId id, EffectType type, uint32_t deadline_ms) {
    for (auto& p : s_pending) {
        if (!p.active) { p = { id, type, deadline_ms, true }; return; }
    }
    LOGW("[DISP] pending registry full, cmd_id=%u dropped\n", (unsigned)id);
}

static void completePending(CmdId id) {
    if (id == 0) return;
    for (auto& p : s_pending) {
        if (p.active && p.cmd_id == id) {
            LOGI("[DISP] pending cmd_id=%u completed (type=%d)\n", (unsigned)id, (int)p.type);
            p.active = false;
            return;
        }
    }
}

static void checkPendingTimeouts(uint32_t now) {
    for (auto& p : s_pending) {
        if (!p.active || p.deadline_ms == 0 || now < p.deadline_ms) continue;
        LOGW("[DISP] cmd_id=%u timed out (type=%d)\n", (unsigned)p.cmd_id, (int)p.type);
        CmdId id = p.cmd_id;
        p.active = false;
        switch (p.type) {
            case EffectType::StartNfcPlaybackByUid:
                postEventFromTask(makeNfcPlaybackStartFailedEvent(
                    "", PlaybackFailReason::AudioStartTimeout, id));
                break;
            case EffectType::StartMusicTrackByIndex:
                postEventFromTask(makeMusicTrackStartFailedEvent(
                    0, PlaybackFailReason::AudioStartTimeout, id));
                break;
            case EffectType::PlaySystemSound:
                postEventFromTask(makeSystemSoundFailedEvent(
                    0, SoundFailReason::PlaybackTimeout, id));
                break;
            case EffectType::StopAudio:
                postEventFromTask(makeAudioStoppedEvent(id));  // best-effort
                break;
            default: break;
        }
    }
}

// Extract cmd_id from audio feedback events for pending registry cleanup.
static void onAudioFeedbackEvent(const Event& ev) {
    CmdId id = 0;
    switch (ev.type) {
        case EventType::NfcPlaybackStarted:     id = ev.payload.playback_started.cmd_id; break;
        case EventType::NfcPlaybackStartFailed: id = ev.payload.nfc_fail.cmd_id;         break;
        case EventType::MusicTrackStarted:      id = ev.payload.track_started.cmd_id;    break;
        case EventType::MusicTrackStartFailed:  id = ev.payload.track_fail.cmd_id;       break;
        case EventType::AudioStopped:           id = ev.payload.audio_stopped.cmd_id;    break;
        case EventType::SystemSoundCompleted:   id = ev.payload.sound_completed.cmd_id;  break;
        case EventType::SystemSoundFailed:      id = ev.payload.sound_failed.cmd_id;     break;
        case EventType::AudioCommandRejected:   id = ev.payload.cmd_rejected.cmd_id;     break;
        default: return;
    }
    completePending(id);
}

static void ringPush(uint32_t ts, const Event& ev, const ReduceResult& r) {
    s_ring[s_ring_head] = {
        ts, ev.type,
        r.next_state.bt_headphones_state,
        r.next_state.audio_state,
        r.effect_count
    };
    s_ring_head = (s_ring_head + 1) % RING_SIZE;
    if (s_ring_count < RING_SIZE) s_ring_count++;
}

// ----- LED scene application -----

// Compare only scene fields that affect applyLedScene().
static bool sameLedScene(const LedSceneParams& a, const LedSceneParams& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case LedSceneType::VolumeOverlay:
            return a.params.volume.level == b.params.volume.level;
        case LedSceneType::BatteryPreview:
            return a.params.battery.bars == b.params.battery.bars;
        case LedSceneType::NightLight:
            return a.params.night_light.percent == b.params.night_light.percent;
        default:
            return true;
    }
}

static void applyLedScene(const LedSceneParams& scene) {
    switch (scene.type) {
        case LedSceneType::WaitBt:      ledSetWaitBt();   break;
        case LedSceneType::Idle:        ledSetIdle();     break;
        case LedSceneType::Playing:     ledSetPlaying();  break;
        case LedSceneType::SleepReady:    ledSetSleepReady(); break;
        case LedSceneType::WarningFlash:  ledSetWarningFlash(); break;
        case LedSceneType::NightLight:
            ledSetNightLight(scene.params.night_light.percent); break;
        case LedSceneType::VolumeOverlay:
            ledShowVolume(scene.params.volume.level); break;
        case LedSceneType::BatteryPreview:
            ledShowBattery(scene.params.battery.bars); break;
        case LedSceneType::SyncEntry:     ledSetSyncEntry(); break;
        default: break; // BootProgress and WakeProgress handled by old boot code
    }
}

// ----- Effect execution (Stage 2: BT/NFC domain; Stage 3: audio domain) -----
#if DISPATCHER_OWNS_BT_NFC
static void executeEffect(const Effect& eff, uint32_t now) {
    switch (eff.type) {
        case EffectType::SetOutputVolume:
#if DISPATCHER_OWNS_BUTTONS
            setOutputVolumeAndApply(eff.payload.volume.level_percent);
#else
            applyOutputVolume();
#endif
            break;

#if DISPATCHER_OWNS_BUTTONS
        case EffectType::PersistVolume:
            persistenceAdapterSaveVolume(eff.payload.volume.level_percent);
            break;

        case EffectType::PersistBrightness:
            persistenceAdapterSaveBrightness(eff.payload.brightness.level_percent);
            break;

        case EffectType::PersistPlaybackMode:
            persistenceAdapterSavePlaybackMode(eff.payload.playback_mode.mode);
            break;

        case EffectType::TriggerSyncRestart: {
            audioStop();
            File f = SD.open(SYNC_PENDING_PATH, FILE_WRITE);
            if (f) f.close();
            LOGC("[SYNC] Service mode restart requested via dispatcher — rebooting\n");
            ESP.restart();
            break;
        }
#endif

#if DISPATCHER_OWNS_AUDIO
        // Audio effects: go through audio adapter with cmd_id correlation.
        case EffectType::StartNfcPlaybackByUid: {
            CmdId cid = nextCmdId();
            registerPending(cid, eff.type, now + 5000);
            audioAdapterStartNfcPlayback(eff.payload.nfc_playback.uid, cid);
            break;
        }
        case EffectType::StartMusicTrackByIndex: {
            CmdId cid = nextCmdId();
            registerPending(cid, eff.type, now + 5000);
            audioAdapterStartMusicTrack(eff.payload.music_track.index, cid);
            break;
        }
        case EffectType::StopAudio: {
            CmdId cid = nextCmdId();
            registerPending(cid, eff.type, now + 2000);
            audioAdapterStop(cid);
            break;
        }
        case EffectType::PlaySystemSound: {
            CmdId cid = nextCmdId();
            registerPending(cid, eff.type, now + 30000);
            audioAdapterPlaySystemSound(eff.payload.system_sound.sound_id, cid);
            break;
        }
        case EffectType::PauseAudio:
            audioAdapterPause(eff.payload.audio_control.cmd_id);
            break;
        case EffectType::ResumeAudio:
            audioAdapterResume(eff.payload.audio_control.cmd_id);
            break;
#else
        // Stage 2 fallback: direct audio calls without cmd_id.
        case EffectType::StartNfcPlaybackByUid:
            startPlayback(String(eff.payload.nfc_playback.uid));
            break;
        case EffectType::StartMusicTrackByIndex:
            playbackStartMusicTrackAt((int)eff.payload.music_track.index);
            break;
        case EffectType::StopAudio:
            audioStop();
            break;
#endif // DISPATCHER_OWNS_AUDIO

        case EffectType::StartBtHeadphonesMode:
            btAdapterStartHeadphonesMode(0);
            break;

#if DISPATCHER_OWNS_SLEEP
        case EffectType::StopBtHeadphonesMode:
            btAdapterStopHeadphonesMode(0);
            break;
        case EffectType::EnterDeepSleep:
            sleepExecuteDeepSleep(eff.payload.deep_sleep.kind);
            // never returns
            break;
#endif

        default: break;
    }
}

static void executeEffects(const ReduceResult& result) {
    const uint32_t now = millis();
    for (uint8_t i = 0; i < result.effect_count; i++) {
        executeEffect(result.effects[i], now);
    }
}
#endif

// ----- Deadline polling -----
static void checkDeadlines(const AppState& s) {
    uint32_t now = millis();
    if (s.idle_deadline_ms              && now >= s.idle_deadline_ms)
        postEventFromTask(makeEvent(EventType::IdleTimeoutFired));
    if (s.night_light_deadline_ms       && now >= s.night_light_deadline_ms)
        postEventFromTask(makeEvent(EventType::NightLightTimeoutFired));
    if (s.volume_overlay_deadline_ms    && now >= s.volume_overlay_deadline_ms)
        postEventFromTask(makeEvent(EventType::VolumeOverlayExpired));
    if (s.battery_preview_deadline_ms   && now >= s.battery_preview_deadline_ms)
        postEventFromTask(makeEvent(EventType::BatteryPreviewExpired));
    if (s.sleep_transition_deadline_ms  && now >= s.sleep_transition_deadline_ms)
        postEventFromTask(makeEvent(EventType::SleepTimeoutFired));
    if (s.brightness_save_deadline_ms   && now >= s.brightness_save_deadline_ms)
        postEventFromTask(makeEvent(EventType::BrightnessSaveDeadlineFired));
#if DISPATCHER_OWNS_AUDIO
    checkPendingTimeouts(now);
#endif
}

// ----- Dispatcher task -----
static void dispatcherTask(void*) {
    esp_task_wdt_add(NULL);

    uint32_t lastHwmMs = 0;

    while (true) {
        esp_task_wdt_reset();

        Event ev;
        if (xQueueReceive(g_dispatcherQueue, &ev, pdMS_TO_TICKS(10)) == pdTRUE) {
#if DISPATCHER_OWNS_AUDIO
            onAudioFeedbackEvent(ev);  // clean up pending registry before reduce
#endif
            portENTER_CRITICAL(&s_mux);
            AppState before = s_state;
            portEXIT_CRITICAL(&s_mux);

            uint32_t now = millis();
            ReduceResult result = reduce(before, ev, now);

            LOGI("[DISP] ev=%d → {bt=%d, audio=%d, boot=%d} +%u fx\n",
                 (int)ev.type,
                 (int)result.next_state.bt_headphones_state,
                 (int)result.next_state.audio_state,
                 (int)result.next_state.boot_state,
                 result.effect_count);

            ringPush(now, ev, result);

            portENTER_CRITICAL(&s_mux);
            s_state = result.next_state;
            portEXIT_CRITICAL(&s_mux);

#if DISPATCHER_OWNS_BT_NFC
            executeEffects(result);
            if (result.next_state.boot_state == BootState::Ready) {
                LedSceneParams scene = deriveLedScene(result.next_state);
                if (!s_has_last_led_scene || !sameLedScene(scene, s_last_led_scene)) {
                    applyLedScene(scene);
                    s_last_led_scene = scene;
                    s_has_last_led_scene = true;
                }
            }
#endif

            checkDeadlines(result.next_state);
        } else {
            // No event in window — still poll deadlines
            AppState snap;
            portENTER_CRITICAL(&s_mux);
            snap = s_state;
            portEXIT_CRITICAL(&s_mux);
            checkDeadlines(snap);
        }

        uint32_t now = millis();
        if (now - lastHwmMs >= 30000) {
            lastHwmMs = now;
            LOGI("[DISP] HWM: app=%u led=%u audio=%u nfc=%u\n",
                 uxTaskGetStackHighWaterMark(NULL),
                 ledGetTaskHWM(),
                 audioGetTaskHWM(),
                 nfcGetTaskHWM());
        }
    }
}

// ----- Public API -----
void dispatcherInit() {
    g_dispatcherQueue = xQueueCreate(DISPATCHER_QUEUE_DEPTH, sizeof(Event));
    MUSICBOX_ASSERT(g_dispatcherQueue != nullptr, "dispatcher queue alloc failed");
}

void dispatcherStartTask() {
    BaseType_t rc = xTaskCreatePinnedToCore(
        dispatcherTask, "app",
        8192, nullptr,
        1, nullptr,
        1   // core 1
    );
    MUSICBOX_ASSERT(rc == pdPASS, "dispatcher task create failed");
}

AppState getDiagnosticSnapshot() {
    AppState snap;
    portENTER_CRITICAL(&s_mux);
    snap = s_state;
    portEXIT_CRITICAL(&s_mux);
    return snap;
}
