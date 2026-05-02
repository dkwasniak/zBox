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

enum class AudioCmdType : uint8_t { PLAY, STOP };
struct AudioCmd {
    AudioCmdType type;
    char path[256];
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

// =============================================================================
// Private callbacks / task
// =============================================================================

static void onBtStateChange(esp_a2d_connection_state_t state, void *)
{
    g_btConnected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
    PLOGF("[BT] state=%d connected=%d", (int)state, (int)g_btConnected);
}

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
            if (f) f.close();
            if (cmd.type == AudioCmdType::PLAY)
            {
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
            else
            {
                isPlaying = false;
                ledSetIdle();
                PLOGF("[AUDIO] Stopped");
                telWindows = 6; // wyłącz telemetrię po stopie
                a2dp.clear();
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
    a2dp.source().set_avrc_rn_events({});  // ESP jest master volume — ignoruj AVRCP notify od JBL
    a2dp.source().set_on_connection_state_changed(onBtStateChange);  // PRZED begin()
    a2dp.begin(cfg);
    // Bootstrap: na wypadek race condition gdy callback ominął pierwsze połączenie
    delay(100);
    g_btConnected = a2dp.source().is_connected();
    LOG("[BT] initial state captured: connected=%d\n", (int)g_btConnected);
    decoderStream.begin();
    mp3Decoder.addNotifyAudioChange(audioInfoLogger);

    audioQueue = xQueueCreate(3, sizeof(AudioCmd));
    xTaskCreatePinnedToCore(audioTaskFunc, "audio", 8192, NULL, 2, &audioTaskHandle, 1);
    LOG("[T+%4lu] Audio task started (core 1, prio 2)\n", millis() - bootStart);
}

void audioStartFile(const char *path)
{
    AudioCmd cmd;
    cmd.type = AudioCmdType::PLAY;
    strlcpy(cmd.path, path, sizeof(cmd.path));
    xQueueSend(audioQueue, &cmd, 0);
}

void audioStop()
{
    if (!audioQueue) return;
    AudioCmd cmd = { AudioCmdType::STOP, {} };
    xQueueSend(audioQueue, &cmd, 0);
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
    a2dp.setVolume(percent / 100.0);
}

bool audioBtIsConnected()
{
    return a2dp.source().is_connected();
}

uint32_t audioGetTaskHWM()
{
    return audioTaskHandle ? uxTaskGetStackHighWaterMark(audioTaskHandle) : 0;
}
