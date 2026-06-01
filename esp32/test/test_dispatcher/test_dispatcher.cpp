#include <unity.h>

#include <freertos/FreeRTOS.h>
#include "events.h"
#include "effects.h"
#include "led_scene.h"
#include "event_queue.h"
#include "reducer.h"

static Event s_posted[16];
static uint8_t s_posted_count = 0;
static uint8_t s_led_clear_count = 0;

QueueHandle_t g_dispatcherQueue = nullptr;

bool postEventFromTask(const Event& ev) {
    if (s_posted_count < 16) s_posted[s_posted_count++] = ev;
    return true;
}

bool postEventFromIsr(const Event&, BaseType_t*) { return true; }

void logWritef(const char*, bool, const char*, ...) {}
void plogInit(bool) {}
void plogWrite(const char*) {}
void plogFlushToSd() {}
void plogMark(const char*, const char*) {}
void formatUptime(char*, size_t, unsigned long) {}
void esp_restart() {}
struct EspStub { void restart() {} } ESP;

void ledSetWaitBt() {}
void ledSetIdle() {}
void ledSetPlaying() {}
void ledSetSleepReady() {}
void ledSetWarningFlash() {}
void ledSetNightLight(uint8_t) {}
void ledShowVolume(uint8_t) {}
void ledShowBattery(uint8_t) {}
void ledSetSyncEntry() {}
void ledClear() { s_led_clear_count++; }
uint32_t ledGetTaskHWM() { return 0; }

uint32_t audioGetTaskHWM() { return 0; }
uint32_t nfcGetTaskHWM() { return 0; }
void setOutputVolumeAndApply(uint8_t) {}
void applyOutputVolume() {}
void persistenceAdapterSaveVolume(uint8_t) {}
void persistenceAdapterSaveBrightness(uint8_t) {}
void persistenceAdapterSavePlaybackMode(PlaybackMode) {}
void audioStop() {}
bool audioAdapterStartNfcPlayback(const char*, CmdId) { return true; }
bool audioAdapterStartMusicTrack(uint16_t, CmdId) { return true; }
void audioAdapterStop(CmdId) {}
bool audioAdapterPlaySystemSound(uint8_t, CmdId) { return true; }
void audioAdapterPause(CmdId) {}
void audioAdapterResume(CmdId) {}
void btAdapterStartHeadphonesMode(CmdId) {}
void btAdapterStopHeadphonesMode(CmdId) {}
void sleepExecuteDeepSleep(RequestedSleepKind) {}
LedSceneParams deriveLedScene(const AppState&) { return {}; }
ReduceResult reduce(const AppState& s, const Event&, uint32_t) { return {s, {}, 0}; }

#include "dispatcher.cpp"

static void resetState() {
    s_posted_count = 0;
    s_led_clear_count = 0;
    for (auto& p : s_pending) p = {};
}

static const Event* lastEvent() {
    return s_posted_count ? &s_posted[s_posted_count - 1] : nullptr;
}

void setUp() { resetState(); }
void tearDown() {}

void test_nfc_pending_timeout_posts_start_failed() {
    registerPending(3, EffectType::StartNfcPlaybackByUid, 100);
    checkPendingTimeouts(100);
    TEST_ASSERT_EQUAL_INT((int)EventType::NfcPlaybackStartFailed, (int)lastEvent()->type);
    TEST_ASSERT_EQUAL_INT((int)PlaybackFailReason::AudioStartTimeout, (int)lastEvent()->payload.nfc_fail.reason);
}

void test_system_sound_pending_timeout_posts_failed() {
    registerPending(4, EffectType::PlaySystemSound, 100);
    checkPendingTimeouts(100);
    TEST_ASSERT_EQUAL_INT((int)EventType::SystemSoundFailed, (int)lastEvent()->type);
    TEST_ASSERT_EQUAL_INT((int)SoundFailReason::PlaybackTimeout, (int)lastEvent()->payload.sound_failed.reason);
}

void test_stop_pending_timeout_posts_audio_stopped() {
    registerPending(5, EffectType::StopAudio, 100);
    checkPendingTimeouts(100);
    TEST_ASSERT_EQUAL_INT((int)EventType::AudioStopped, (int)lastEvent()->type);
    TEST_ASSERT_EQUAL_UINT16(5, lastEvent()->payload.audio_stopped.cmd_id);
}

void test_audio_feedback_completes_pending_before_timeout() {
    registerPending(6, EffectType::PlaySystemSound, 100);
    onAudioFeedbackEvent(makeSystemSoundCompletedEvent(SOUND_ID_POWER_OFF, 6));
    checkPendingTimeouts(100);
    TEST_ASSERT_EQUAL_UINT8(0, s_posted_count);
}

void test_off_led_scene_clears_leds() {
    LedSceneParams scene{};
    scene.type = LedSceneType::Off;
    applyLedScene(scene);
    TEST_ASSERT_EQUAL_UINT8(1, s_led_clear_count);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_nfc_pending_timeout_posts_start_failed);
    RUN_TEST(test_system_sound_pending_timeout_posts_failed);
    RUN_TEST(test_stop_pending_timeout_posts_audio_stopped);
    RUN_TEST(test_audio_feedback_completes_pending_before_timeout);
    RUN_TEST(test_off_led_scene_clears_leds);
    return UNITY_END();
}
