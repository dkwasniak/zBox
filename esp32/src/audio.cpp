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

enum class AudioCmdType : uint8_t { PLAY, STOP, VOLUME };
struct AudioCmd {
    AudioCmdType type;
    char path[256];
    int volumePercent;
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
static EncodedAudioStream decoderStream(&a2dp, &mp3Decoder);
static QueueHandle_t audioQueue = NULL;
static TaskHandle_t audioTaskHandle = NULL;
static bool a2dpStarted = false;

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
                    ledSetPlaying();
                    PLOGF("[AUDIO] Playing: %s", cmd.path);
                    telSdBytes = telWrittenBytes = telDrops = 0;
                    telWindowStart = millis();
                    telWindows = 0;
                }
                else
                {
                    LOG("[AUDIO] Open failed: %s\n", cmd.path);
                    a2dp.clear();
                }
            }
            else if (cmd.type == AudioCmdType::STOP)
            {
                if (f) f.close();
                isPlaying = false;
                ledSetIdle();
                PLOGF("[AUDIO] Stopped");
                telWindows = 6; // wyłącz telemetrię po stopie
                a2dp.clear();
            }
            else if (cmd.type == AudioCmdType::VOLUME)
            {
                a2dp.setVolume(cmd.volumePercent / 100.0);
                PLOGF("[VOL] Applied in audio task: %d%%", cmd.volumePercent);
            }
        }

        if (f && f.available())
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

bool audioIsRunning()
{
    return isPlaying;
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

uint32_t audioGetTaskHWM()
{
    return audioTaskHandle ? uxTaskGetStackHighWaterMark(audioTaskHandle) : 0;
}
