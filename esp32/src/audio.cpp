#include "audio.h"
#include <SD.h>
#include "AudioTools.h"
#include "AudioTools/Communication/A2DPStream.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "musicbox_config.h"
#include "logging.h"
#include "state.h"
#include "leds.h"
#include "persistent_log.h"

// =============================================================================
// Private types
// =============================================================================

enum class AudioCmdType : uint8_t { PLAY, STOP, PAUSE, RESUME, VOLUME };
struct AudioCmd {
    AudioCmdType type;
    char path[256];
    int volumePercent;
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
        LOG("[AUDIO] Decoder: SR=%d Hz, Ch=%d, Bits=%d\n",
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
    LOG("[BT] initial state captured: connected=%d\n", (int)g_btConnected);
}

static bool audioSendCmd(const AudioCmd &cmd, TickType_t timeout, bool front = false)
{
    if (!audioQueue) return false;
    BaseType_t ok = front
        ? xQueueSendToFront(audioQueue, &cmd, timeout)
        : xQueueSend(audioQueue, &cmd, timeout);
    if (ok != pdTRUE)
    {
        PLOGF("[AUDIO] Queue full, cmd dropped: %d", (int)cmd.type);
        return false;
    }
    return true;
}

// =============================================================================
// Private task
// =============================================================================

static void audioTaskFunc(void *param)
{
    File f;
    static uint8_t audioBuf[AUDIO_BUF_SIZE];

    // Telemetria — aktywna przez pierwsze 30s każdego tracka (6 okien po 5s)
    uint32_t telSdBytes = 0, telWrittenBytes = 0, telDrops = 0;
    unsigned long telWindowStart = 0;
    int telWindows = 0;

    for (;;)
    {
        static unsigned long lastAudioHb = 0;
        if (millis() - lastAudioHb > 5000) {
            lastAudioHb = millis();
            PLOGF("[AUDIO] alive isPlaying=%d hwm=%u", (int)isPlaying, uxTaskGetStackHighWaterMark(NULL));
        }

        AudioCmd cmd;
        if (xQueueReceive(audioQueue, &cmd, 0) == pdTRUE)
        {
            if (cmd.type == AudioCmdType::PLAY)
            {
                if (f) f.close();
                f = SD.open(cmd.path);
                if (f)
                {
                    isPlaying = true;
                    isPaused = false;
                    ledSetPlaying();
                    const char *logPath = cmd.path;
                    const size_t logPathLen = strlen(cmd.path);
                    if (logPathLen > 56) logPath = cmd.path + (logPathLen - 56);
                    PLOGF("[AUDIO] Playing: %s%s",
                          logPathLen > 56 ? "..." : "",
                          logPath);
                    telSdBytes = telWrittenBytes = telDrops = 0;
                    telWindowStart = millis();
                    telWindows = 0;
                }
                else
                {
                    isPlaying = false;
                    isPaused = false;
                    LOG("[AUDIO] Open failed: %s\n", cmd.path);
                    a2dp.clear();
                }
            }
            else if (cmd.type == AudioCmdType::STOP)
            {
                if (f) f.close();
                isPlaying = false;
                isPaused = false;
                ledSetIdle();
                PLOGF("[AUDIO] Stopped");
                telWindows = 6; // wyłącz telemetrię po stopie
                a2dp.clear();
            }
            else if (cmd.type == AudioCmdType::PAUSE)
            {
                if (f && isPlaying)
                {
                    isPlaying = false;
                    isPaused = true;
                    ledSetIdle();
                    PLOGF("[AUDIO] Paused");
                    a2dp.clear();
                }
            }
            else if (cmd.type == AudioCmdType::RESUME)
            {
                if (f && isPaused)
                {
                    isPlaying = true;
                    isPaused = false;
                    ledSetPlaying();
                    PLOGF("[AUDIO] Resumed");
                }
            }
            else if (cmd.type == AudioCmdType::VOLUME)
            {
                a2dp.setVolume(cmd.volumePercent / 100.0);
                PLOGF("[VOL] Applied in audio task: %d%%", cmd.volumePercent);
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

                // Log co 5s przez pierwsze 30s tracka
                if (telWindows < 6)
                {
                    unsigned long now = millis();
                    if (now - telWindowStart >= 5000)
                    {
                        float s = (now - telWindowStart) / 1000.0f;
                        LOG("[AUDIO_TEL] SD=%u B/s dec_in=%u B/s drops=%u (write<n)\n",
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
            f.close();
            isPlaying = false;
            isPaused = false;
            trackEndedFlag = true;   // loop() wyczyści lastNfcUid i wywoła ledSetIdle()
            PLOGF("[AUDIO] Track ended heap=%u largest=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
            telWindows = 6;
            a2dp.clear();
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
    LOG("[T+%4lu] Audio task started (core 1, prio 2)\n", millis() - bootStart);
}

void audioStartFile(const char *path)
{
    if (!path) return;
    AudioCmd cmd;
    cmd.type = AudioCmdType::PLAY;
    cmd.volumePercent = 0;
    strlcpy(cmd.path, path, sizeof(cmd.path));
    audioSendCmd(cmd, pdMS_TO_TICKS(50));
}

void audioStop()
{
    AudioCmd cmd = { AudioCmdType::STOP, {}, 0 };
    audioSendCmd(cmd, pdMS_TO_TICKS(100), true);
}

void audioPause()
{
    AudioCmd cmd = { AudioCmdType::PAUSE, {}, 0 };
    audioSendCmd(cmd, pdMS_TO_TICKS(50), true);
}

void audioResume()
{
    AudioCmd cmd = { AudioCmdType::RESUME, {}, 0 };
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

    AudioCmd cmd = { AudioCmdType::VOLUME, {}, percent };
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
    PLOGF("[BT] polled connected=%d", (int)g_btConnected);
}

bool audioRestartDiscovery()
{
    if (!a2dpStarted)
        return false;

    PLOGF("[BT] Restarting A2DP in discovery mode");
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
