#include "audio.h"
#include <SD.h>
#include "AudioTools.h"
#include "AudioTools/Communication/A2DPStream.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "zbox_config.h"
#include "logging.h"
#include "state.h"
#include "leds.h"
#include "persistent_log.h"
#include "dispatcher.h"
#include "event_queue.h"
#include "events.h"

// =============================================================================
// Private types
// =============================================================================

enum class AudioCmdType : uint8_t { PLAY, STOP, PAUSE, RESUME, VOLUME };
struct AudioCmd {
    AudioCmdType type;
    char path[256];
    int volumePercent;
    uint16_t gapMs;
    // Stage 3+ correlation fields (cmd_id=0 means old-style call — no events posted)
    CmdId    cmd_id;
    uint8_t  sound_id;       // for system sound playback
    char     uid[24];        // for NFC track playback (feedback correlation)
    uint16_t track_index;    // for music track playback (feedback correlation)
    bool     is_nfc_track;   // true = NFC, false = music index
    bool     is_system_sound;// true = system sound (uses sound_id, posts SystemSoundCompleted)
};

// =============================================================================
// Beat detection passthrough
// =============================================================================

// Wkłada się między dekoder MP3 a A2DPStream.
// write() dostaje zdekodowane PCM (int16_t), mierzy energię okna,
// porównuje z lokalną średnią i ustawia g_beatDetected.
// Overhead: ~90K operacji/s — pomijalny na 240 MHz.
class BeatTracker : public AudioOutput
{
public:
    void setSink(Print *s) { _sink = s; }
    void reset()
    {
        memset(_hist, 0, sizeof(_hist));
        _hIdx = 0;
        _hCount = 0;
        _lastBeat = 0;
    }

    size_t write(const uint8_t *data, size_t len) override
    {
        if (len >= 2) detectBeat(data, len);
        return _sink ? _sink->write(data, len) : len;
    }

private:
    Print *_sink = nullptr;

    static const int HIST = 43;  // ~1 s historii przy typowym rozmiarze ramki
    uint32_t _hist[HIST] = {};
    int _hIdx   = 0;
    int _hCount = 0;
    unsigned long _lastBeat = 0;

    void detectBeat(const uint8_t *data, size_t len)
    {
        const int16_t *s = (const int16_t *)data;
        int n = (int)(len / 2);

        // Energia okna: mean(sample^2)
        uint64_t sum = 0;
        for (int i = 0; i < n; i++) { int32_t v = s[i]; sum += (uint32_t)(v * v); }
        uint32_t e = (uint32_t)(sum / (uint32_t)n);

        // Aktualizuj historię
        _hist[_hIdx] = e;
        _hIdx = (_hIdx + 1) % HIST;
        if (_hCount < HIST) _hCount++;

        // Lokalna średnia
        uint64_t avg = 0;
        for (int i = 0; i < _hCount; i++) avg += _hist[i];
        avg /= _hCount;

        // Energia → jasność 40-200 (ciągła, oddycha z muzyką)
        if (avg > 0)
        {
            uint32_t ratio = (uint32_t)((uint64_t)e * 120 / avg); // 100 = średnia
            g_audioEnergy = (uint8_t)(ratio < 40 ? 40 : ratio > 200 ? 200 : ratio);
        }

        // Beat: energia > 1.2× średnia + cooldown 150 ms
        unsigned long now = millis();
        if (avg > 0 && (uint64_t)e > avg + avg / 5 && (now - _lastBeat) > 150)
        {
            _lastBeat = now;
            g_beatDetected = true;
        }
    }
};

// =============================================================================
// Private objects (static — owned by this module)
// =============================================================================

struct AudioInfoLogger : public AudioInfoSupport {
    AudioInfo lastInfo;
    void setAudioInfo(AudioInfo info) override {
        lastInfo = info;
        LOGI("[AUDIO] Decoder: SR=%d Hz, Ch=%d, Bits=%d\n",
            info.sample_rate, info.channels, info.bits_per_sample);
    }
    AudioInfo audioInfo() override { return lastInfo; }
} audioInfoLogger;

static A2DPStream a2dp;
static MP3DecoderHelix mp3Decoder;
static BeatTracker beatTracker;
static EncodedAudioStream decoderStream(&beatTracker, &mp3Decoder);
static QueueHandle_t audioQueue = NULL;
static TaskHandle_t audioTaskHandle = NULL;
static bool a2dpStarted = false;

static void clearAudioTransport()
{
    a2dp.clear();
    g_audioEnergy = 0;
    g_beatDetected = false;
    beatTracker.reset();
}

static void resetDecoderForNewTrack()
{
    // A new MP3 file must start with a fresh decoder state, otherwise
    // leftover compressed frames/PCM from the previous file can leak in.
    clearAudioTransport();
    decoderStream.end();
    decoderStream.begin();
}

static size_t bytesForSilenceMs(uint16_t durationMs)
{
    AudioInfo info = audioInfoLogger.audioInfo();
    const uint32_t sampleRate = info.sample_rate > 0 ? (uint32_t)info.sample_rate : 44100U;
    const uint32_t channels = info.channels > 0 ? (uint32_t)info.channels : 2U;
    const uint32_t bitsPerSample = info.bits_per_sample > 0 ? (uint32_t)info.bits_per_sample : 16U;
    const uint32_t bytesPerSampleFrame = channels * (bitsPerSample / 8U);
    return (sampleRate * bytesPerSampleFrame * durationMs) / 1000U;
}

static void writeSilenceGap(uint16_t durationMs)
{
    clearAudioTransport();

    static uint8_t zeroBuf[512] = {};
    size_t remaining = bytesForSilenceMs(durationMs);
    while (remaining > 0)
    {
        size_t chunk = remaining > sizeof(zeroBuf) ? sizeof(zeroBuf) : remaining;
        size_t written = a2dp.write(zeroBuf, chunk);
        if (written == 0)
            break;
        remaining -= written;
    }
}

static void startA2dpTransport()
{
    auto cfg = a2dp.defaultConfig(TX_MODE);
    cfg.name = BT_SPEAKER_NAME;
    cfg.auto_reconnect = true;
    cfg.wait_for_connection = false;
    a2dp.source().set_avrc_rn_events({});  // ESP jest master volume — ignoruj AVRCP notify od JBL
    a2dp.begin(cfg);
    a2dpStarted = true;
    // Bootstrap: na wypadek race condition gdy callback ominął pierwsze połączenie
    delay(100);
    g_btConnected = a2dp.source().is_connected();
    LOGI("[BT] initial state captured: connected=%d\n", (int)g_btConnected);
}

static bool audioSendCmd(const AudioCmd &cmd, TickType_t timeout, bool front = false)
{
    if (!audioQueue) return false;
    BaseType_t ok = front
        ? xQueueSendToFront(audioQueue, &cmd, timeout)
        : xQueueSend(audioQueue, &cmd, timeout);
    if (ok != pdTRUE)
    {
        LOGW("[AUDIO] Queue full, cmd dropped: %d\n", (int)cmd.type);
        return false;
    }
    return true;
}

// =============================================================================
// Private task
// =============================================================================

// Per-track context: set when a PLAY cmd is dequeued, used for feedback events.
// cmd_id == 0 means old-style call (no events posted; legacy behavior preserved).
static CmdId    s_curCmdId       = 0;
static bool     s_curIsNfc       = false;
static bool     s_curIsSystem    = false;
static char     s_curUid[24]     = {};
static uint16_t s_curIndex       = 0;
static uint8_t  s_curSoundId     = 0;

static void audioTaskFunc(void *param)
{
    File f;
    static uint8_t audioBuf[AUDIO_BUF_SIZE];

    uint32_t telSdBytes = 0, telWrittenBytes = 0, telDrops = 0;
    unsigned long telWindowStart = 0;
    int telWindows = 0;

    for (;;)
    {
        static unsigned long lastAudioHb = 0;
        if (millis() - lastAudioHb > 5000) {
            lastAudioHb = millis();
            LOGI("[AUDIO] alive isPlaying=%d hwm=%u\n", (int)isPlaying, uxTaskGetStackHighWaterMark(NULL));
        }

        AudioCmd cmd;
        if (xQueueReceive(audioQueue, &cmd, 0) == pdTRUE)
        {
            if (cmd.type == AudioCmdType::PLAY)
            {
                // Capture per-track context for feedback events.
                s_curCmdId    = cmd.cmd_id;
                s_curIsNfc    = cmd.is_nfc_track;
                s_curIsSystem = cmd.is_system_sound;
                memcpy(s_curUid, cmd.uid, sizeof(s_curUid));
                s_curIndex    = cmd.track_index;
                s_curSoundId  = cmd.sound_id;

                if (f) f.close();
                resetDecoderForNewTrack();
                if (cmd.gapMs > 0)
                    writeSilenceGap(cmd.gapMs);
                f = SD.open(cmd.path);
                if (f)
                {
                    isPlaying = true;
                    isPaused = false;
                    const char *logPath = cmd.path;
                    const size_t logPathLen = strlen(cmd.path);
                    if (logPathLen > 56) logPath = cmd.path + (logPathLen - 56);
                    LOGI("[AUDIO] Playing: %s%s cmd_id=%u\n",
                          logPathLen > 56 ? "..." : "", logPath, (unsigned)s_curCmdId);
                    telSdBytes = telWrittenBytes = telDrops = 0;
                    telWindowStart = millis();
                    telWindows = 0;
                    if (s_curCmdId != 0) {
                        // Post started event; LED managed by dispatcher.
                        if (s_curIsNfc) {
                            postEventFromTask(makeNfcPlaybackStartedEvent(s_curUid, s_curCmdId));
                        } else if (!s_curIsSystem) {
                            postEventFromTask(makeMusicTrackStartedEvent(s_curIndex, s_curCmdId));
                        }
                        // SystemSound started events are not posted; only Completed/Failed.
                    }
                }
                else
                {
                    isPlaying = false;
                    isPaused = false;
                    LOGE("[AUDIO] Open failed: %s cmd_id=%u\n", cmd.path, (unsigned)s_curCmdId);
                    if (s_curCmdId != 0) {
                        if (s_curIsSystem) {
                            postEventFromTask(makeSystemSoundFailedEvent(
                                s_curSoundId, SoundFailReason::FileNotFound, s_curCmdId));
                        } else if (s_curIsNfc) {
                            postEventFromTask(makeNfcPlaybackStartFailedEvent(
                                s_curUid, PlaybackFailReason::FileNotFound, s_curCmdId));
                        } else {
                            postEventFromTask(makeMusicTrackStartFailedEvent(
                                s_curIndex, PlaybackFailReason::FileNotFound, s_curCmdId));
                        }
                    }
                    s_curCmdId = 0;
                }
            }
            else if (cmd.type == AudioCmdType::STOP)
            {
                if (f) f.close();
                isPlaying = false;
                isPaused = false;
                LOGI("[AUDIO] Stopped cmd_id=%u\n", (unsigned)cmd.cmd_id);
                telWindows = 6;
                clearAudioTransport();
                s_curCmdId = 0;
                if (cmd.cmd_id != 0) {
                    postEventFromTask(makeAudioStoppedEvent(cmd.cmd_id));
                }
            }
            else if (cmd.type == AudioCmdType::PAUSE)
            {
                if (f && isPlaying)
                {
                    isPlaying = false;
                    isPaused = true;
                    clearAudioTransport();
                    LOGI("[AUDIO] Paused\n");
                }
            }
            else if (cmd.type == AudioCmdType::RESUME)
            {
                if (f && isPaused)
                {
                    isPlaying = true;
                    isPaused = false;
                    LOGI("[AUDIO] Resumed\n");
                }
            }
            else if (cmd.type == AudioCmdType::VOLUME)
            {
                a2dp.setVolume(cmd.volumePercent / 100.0);
                LOGI("[VOL] Applied in audio task: %d%%\n", cmd.volumePercent);
            }
        }

        if (f && isPaused)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        else if (f && f.available())
        {
            int n = f.read(audioBuf, AUDIO_BUF_SIZE);
            if (n > 0)
            {
                telSdBytes += n;
                size_t written = decoderStream.write(audioBuf, n);
                telWrittenBytes += written;
                if ((int)written < n) telDrops++;

                if (telWindows < 6)
                {
                    unsigned long now = millis();
                    if (now - telWindowStart >= 5000)
                    {
                        float s = (now - telWindowStart) / 1000.0f;
                        LOGI("[AUDIO_TEL] SD=%u B/s dec_in=%u B/s drops=%u (write<n)\n",
                            (uint32_t)(telSdBytes / s),
                            (uint32_t)(telWrittenBytes / s),
                            telDrops);
                        telSdBytes = telWrittenBytes = telDrops = 0;
                        telWindowStart = now;
                        telWindows++;
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        else if (f && !f.available())
        {
            CmdId endedCmdId   = s_curCmdId;
            bool  endedIsSystem = s_curIsSystem;
            uint8_t endedSoundId = s_curSoundId;

            f.close();
            isPlaying = false;
            isPaused = false;
            s_curCmdId = 0;
            LOGI("[AUDIO] Track ended heap=%u largest=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
            telWindows = 6;
            clearAudioTransport();

            if (endedCmdId != 0) {
                if (endedIsSystem) {
                    postEventFromTask(makeSystemSoundCompletedEvent(endedSoundId, endedCmdId));
                } else {
                    postEventFromTask(makeEvent(EventType::TrackEnded));
                }
                // LED managed by dispatcher from AppState.
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

// =============================================================================
// Public API
// =============================================================================

void audioInit()
{
    startA2dpTransport();
    beatTracker.setSink(&a2dp);
    decoderStream.begin();
    mp3Decoder.addNotifyAudioChange(audioInfoLogger);

    audioQueue = xQueueCreate(5, sizeof(AudioCmd));
    xTaskCreatePinnedToCore(audioTaskFunc, "audio", 8192, NULL, 2, &audioTaskHandle, 1);
    LOGI("Audio task started (core 1, prio 2)\n");
}

void audioStartFile(const char *path)
{
    audioStartFileWithGap(path, 0);
}

void audioStartFileWithGap(const char *path, uint16_t gapMs)
{
    if (!path) return;
    AudioCmd cmd;
    cmd.type = AudioCmdType::PLAY;
    cmd.volumePercent = 0;
    cmd.gapMs = gapMs;
    strlcpy(cmd.path, path, sizeof(cmd.path));
    audioSendCmd(cmd, pdMS_TO_TICKS(50));
}

void audioStop()
{
    AudioCmd cmd = { AudioCmdType::STOP, {}, 0, 0 };
    audioSendCmd(cmd, pdMS_TO_TICKS(100), true);
}

void audioPause()
{
    AudioCmd cmd = { AudioCmdType::PAUSE, {}, 0, 0 };
    audioSendCmd(cmd, pdMS_TO_TICKS(50), true);
}

void audioResume()
{
    AudioCmd cmd = { AudioCmdType::RESUME, {}, 0, 0 };
    audioSendCmd(cmd, pdMS_TO_TICKS(50));
}

bool audioIsRunning()
{
    return isPlaying || isPaused;
}

bool audioIsPaused()
{
    return isPaused;
}

bool audioIsReady()
{
    return audioQueue != NULL && audioTaskHandle != NULL;
}

void audioDeleteTaskForSleep()
{
    if (audioTaskHandle)
    {
        vTaskDelete(audioTaskHandle);
        audioTaskHandle = NULL;
    }
}

void audioSetBtVolumePercent(int percent)
{
    if (!audioQueue)
    {
        a2dp.setVolume(percent / 100.0);
        return;
    }

    AudioCmd cmd = { AudioCmdType::VOLUME, {}, percent, 0 };
    audioSendCmd(cmd, 0);
}

bool audioBtIsConnected()
{
    return a2dp.source().is_connected();
}

void audioPollBtConnection()
{
    if (!a2dpStarted)
        return;

    bool connected = a2dp.source().is_connected();
    if (connected == g_btConnected)
        return;

    g_btConnected = connected;
    LOGI("[BT] polled connected=%d\n", (int)g_btConnected);
}

bool audioRestartDiscovery()
{
    if (!a2dpStarted)
        return false;

    LOGC("[RECOVERY] Restarting A2DP in discovery mode\n");
    a2dp.clear();
    a2dp.source().end(false);  // czyści connected_bda/last_connection i pozwala ruszyć discovery po nazwie
    a2dpStarted = false;
    g_btConnected = false;
    delay(200);
    startA2dpTransport();
    return true;
}

uint32_t audioGetTaskHWM()
{
    return audioTaskHandle ? uxTaskGetStackHighWaterMark(audioTaskHandle) : 0;
}

// =============================================================================
// Stage 3+ correlation API (called by audio_adapter when DISPATCHER_OWNS_AUDIO=1)
// =============================================================================

static bool audioSendPlayCmd(const char *path, CmdId cmd_id,
                             const char *uid, uint16_t index,
                             uint8_t sound_id, bool is_nfc, bool is_system,
                             uint16_t gapMs = 0)
{
    if (!path) return false;
    AudioCmd cmd{};
    cmd.type          = AudioCmdType::PLAY;
    cmd.gapMs         = gapMs;
    cmd.cmd_id        = cmd_id;
    cmd.is_nfc_track  = is_nfc;
    cmd.is_system_sound = is_system;
    cmd.sound_id      = sound_id;
    cmd.track_index   = index;
    if (uid) strlcpy(cmd.uid, uid, sizeof(cmd.uid));
    strlcpy(cmd.path, path, sizeof(cmd.path));
    return audioSendCmd(cmd, pdMS_TO_TICKS(50));
}

bool audioStartNfcTrack(const char *path, const char *uid, CmdId cmd_id)
{
    return audioSendPlayCmd(path, cmd_id, uid, 0, 0, /*is_nfc=*/true, /*is_system=*/false);
}

bool audioStartMusicTrack(const char *path, uint16_t index, CmdId cmd_id)
{
    return audioSendPlayCmd(path, cmd_id, nullptr, index, 0, /*is_nfc=*/false, /*is_system=*/false);
}

bool audioStopWithId(CmdId cmd_id)
{
    AudioCmd cmd{};
    cmd.type   = AudioCmdType::STOP;
    cmd.cmd_id = cmd_id;
    return audioSendCmd(cmd, pdMS_TO_TICKS(100), /*front=*/true);
}

bool audioPlaySystemSound(const char *path, uint8_t sound_id, CmdId cmd_id)
{
    return audioSendPlayCmd(path, cmd_id, nullptr, 0, sound_id, /*is_nfc=*/false, /*is_system=*/true, /*gapMs=*/180);
}
