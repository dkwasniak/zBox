#include <unity.h>
#include <cstring>

#include "events.h"

static Event s_posted[16];
static uint8_t s_posted_count = 0;
static bool s_lookup_mapping_ok = false;
static bool s_lookup_sound_ok = false;
static bool s_music_path_ok = false;
static bool s_start_nfc_ok = true;
static bool s_start_music_ok = true;
static bool s_play_sound_ok = true;
static bool s_stop_ok = true;
static char s_last_path[256] = {};
static char s_last_uid[24] = {};
static uint16_t s_last_index = 0;
static uint8_t s_last_sound_id = 0;
static CmdId s_last_cmd_id = 0;

bool postEventFromTask(const Event& ev) {
    if (s_posted_count < 16) s_posted[s_posted_count++] = ev;
    return true;
}

bool lookupMappingPath(const char* uid, char* out_path, size_t out_size) {
    if (!s_lookup_mapping_ok) return false;
    strlcpy(out_path, "/music/mapped.mp3", out_size);
    strlcpy(s_last_uid, uid ? uid : "", sizeof(s_last_uid));
    return true;
}

bool lookupSystemSoundPath(const char* name, char* out_path, size_t out_size) {
    if (!s_lookup_sound_ok) return false;
    strlcpy(out_path, "/sys/sound.mp3", out_size);
    (void)name;
    return true;
}

bool playbackGetMusicPath(uint16_t index, char* out_path, size_t out_size) {
    if (!s_music_path_ok) return false;
    s_last_index = index;
    strlcpy(out_path, "/music/track.mp3", out_size);
    return true;
}

bool audioStartNfcTrack(const char* path, const char* uid, CmdId cmd_id) {
    strlcpy(s_last_path, path ? path : "", sizeof(s_last_path));
    strlcpy(s_last_uid, uid ? uid : "", sizeof(s_last_uid));
    s_last_cmd_id = cmd_id;
    return s_start_nfc_ok;
}

bool audioStartMusicTrack(const char* path, uint16_t index, CmdId cmd_id) {
    strlcpy(s_last_path, path ? path : "", sizeof(s_last_path));
    s_last_index = index;
    s_last_cmd_id = cmd_id;
    return s_start_music_ok;
}

bool audioPlaySystemSound(const char* path, uint8_t sound_id, CmdId cmd_id) {
    strlcpy(s_last_path, path ? path : "", sizeof(s_last_path));
    s_last_sound_id = sound_id;
    s_last_cmd_id = cmd_id;
    return s_play_sound_ok;
}

bool audioStopWithId(CmdId cmd_id) {
    s_last_cmd_id = cmd_id;
    return s_stop_ok;
}

void audioPause() {}
void audioResume() {}

void logWritef(const char*, bool, const char*, ...) {}
void plogInit(bool) {}
void plogWrite(const char*) {}
void plogFlushToSd() {}
void plogMark(const char*, const char*) {}
void formatUptime(char*, size_t, unsigned long) {}

#include "audio_adapter.cpp"

static void resetState() {
    s_posted_count = 0;
    s_lookup_mapping_ok = false;
    s_lookup_sound_ok = false;
    s_music_path_ok = false;
    s_start_nfc_ok = true;
    s_start_music_ok = true;
    s_play_sound_ok = true;
    s_stop_ok = true;
    s_last_path[0] = '\0';
    s_last_uid[0] = '\0';
    s_last_index = 0;
    s_last_sound_id = 0;
    s_last_cmd_id = 0;
}

static const Event* lastEvent() {
    return s_posted_count ? &s_posted[s_posted_count - 1] : nullptr;
}

void setUp() { resetState(); }
void tearDown() {}

void test_nfc_mapping_missing_posts_start_failed() {
    TEST_ASSERT_FALSE(audioAdapterStartNfcPlayback("04:AA", 7));
    TEST_ASSERT_EQUAL_UINT8(1, s_posted_count);
    TEST_ASSERT_EQUAL_INT((int)EventType::NfcPlaybackStartFailed, (int)lastEvent()->type);
    TEST_ASSERT_EQUAL_INT((int)PlaybackFailReason::MappingNotFound, (int)lastEvent()->payload.nfc_fail.reason);
}

void test_nfc_queue_reject_posts_audio_command_rejected() {
    s_lookup_mapping_ok = true;
    s_start_nfc_ok = false;
    TEST_ASSERT_FALSE(audioAdapterStartNfcPlayback("04:AA", 8));
    TEST_ASSERT_EQUAL_INT((int)EventType::AudioCommandRejected, (int)lastEvent()->type);
    TEST_ASSERT_EQUAL_INT((int)PlaybackFailReason::AudioCommandRejected, (int)lastEvent()->payload.cmd_rejected.reason);
}

void test_music_index_missing_posts_track_failed() {
    TEST_ASSERT_FALSE(audioAdapterStartMusicTrack(3, 9));
    TEST_ASSERT_EQUAL_INT((int)EventType::MusicTrackStartFailed, (int)lastEvent()->type);
    TEST_ASSERT_EQUAL_INT((int)PlaybackFailReason::MappingNotFound, (int)lastEvent()->payload.track_fail.reason);
}

void test_unknown_system_sound_posts_failed() {
    TEST_ASSERT_FALSE(audioAdapterPlaySystemSound(99, 10));
    TEST_ASSERT_EQUAL_INT((int)EventType::SystemSoundFailed, (int)lastEvent()->type);
    TEST_ASSERT_EQUAL_INT((int)SoundFailReason::FileNotFound, (int)lastEvent()->payload.sound_failed.reason);
}

void test_missing_system_sound_mapping_posts_failed() {
    TEST_ASSERT_FALSE(audioAdapterPlaySystemSound(SOUND_ID_POWER_OFF, 11));
    TEST_ASSERT_EQUAL_INT((int)EventType::SystemSoundFailed, (int)lastEvent()->type);
    TEST_ASSERT_EQUAL_INT((int)SoundFailReason::FileNotFound, (int)lastEvent()->payload.sound_failed.reason);
}

void test_system_sound_queue_reject_posts_audio_command_rejected() {
    s_lookup_sound_ok = true;
    s_play_sound_ok = false;
    TEST_ASSERT_FALSE(audioAdapterPlaySystemSound(SOUND_ID_POWER_OFF, 12));
    TEST_ASSERT_EQUAL_INT((int)EventType::AudioCommandRejected, (int)lastEvent()->type);
}

void test_stop_queue_reject_posts_best_effort_audio_stopped() {
    s_stop_ok = false;
    audioAdapterStop(13);
    TEST_ASSERT_EQUAL_INT((int)EventType::AudioStopped, (int)lastEvent()->type);
    TEST_ASSERT_EQUAL_UINT16(13, lastEvent()->payload.audio_stopped.cmd_id);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_nfc_mapping_missing_posts_start_failed);
    RUN_TEST(test_nfc_queue_reject_posts_audio_command_rejected);
    RUN_TEST(test_music_index_missing_posts_track_failed);
    RUN_TEST(test_unknown_system_sound_posts_failed);
    RUN_TEST(test_missing_system_sound_mapping_posts_failed);
    RUN_TEST(test_system_sound_queue_reject_posts_audio_command_rejected);
    RUN_TEST(test_stop_queue_reject_posts_best_effort_audio_stopped);
    return UNITY_END();
}

