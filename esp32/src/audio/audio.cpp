#include "audio.h"
#include <Arduino.h>
#include <SD.h>
#include <climits>
#include <cstring>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "BluetoothA2DPSource.h"
#include "dispatcher.h"
#include "event_queue.h"
#include "events.h"
#include "leds.h"
#include "logging.h"
#include "pcm_volume.h"
#include "persistent_log.h"
#include "persistence_adapter.h"
#include "peripheral_power.h"
#include "state.h"
#include "volume_scale.h"
#include "zbox_config.h"
#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <esp_bt_main.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

namespace {

static char s_btTargetName[64] = BT_DEFAULT_NAME;
static char s_btTargetMac[18] = {};

static void writeSilenceGap(uint16_t durationMs); // forward declaration
constexpr size_t VOLUME_SCRATCH_SAMPLES = 256;
constexpr uint16_t TRACK_TRANSITION_SILENCE_MS = 40;
constexpr size_t LOCAL_AUDIO_READ_CHUNK_SIZE = AUDIO_BUF_SIZE;
constexpr size_t BT_AUDIO_READ_CHUNK_SIZE = 512;
constexpr size_t BT_A2DP_WRITE_FRAME_SIZE = 512;
constexpr size_t BT_PCM_BUFFER_SIZE = 512 * 30;
constexpr uint16_t BT_PCM_WRITE_TIMEOUT_MS = 12;
static_assert(LOCAL_AUDIO_READ_CHUNK_SIZE <= AUDIO_BUF_SIZE, "local audio read chunk exceeds buffer");
static_assert(BT_AUDIO_READ_CHUNK_SIZE <= AUDIO_BUF_SIZE, "BT audio read chunk exceeds buffer");

enum class AudioCmdType : uint8_t { PLAY, STOP, PAUSE, RESUME, VOLUME };

struct AudioCmd {
    AudioCmdType type;
    char path[256];
    int volumePercent;
    uint16_t gapMs;
    CmdId cmd_id;
    uint8_t sound_id;
    char uid[24];
    uint16_t track_index;
    bool is_nfc_track;
    bool is_system_sound;
};

class BeatTracker : public AudioOutput {
public:
    void setSink(Print* sink) { sink_ = sink; }
    void setVolumePercent(int percent) { volume_percent_ = constrain(percent, 0, 100); }

    void reset() {
        memset(hist_, 0, sizeof(hist_));
        hist_index_ = 0;
        hist_count_ = 0;
        last_beat_ms_ = 0;
        dither_state_ = {};
    }

    size_t write(const uint8_t* data, size_t len) override {
        if (len >= 2) detectBeat(data, len);
        if (!sink_) return len;

        const int percent = volume_percent_;
        if (percent >= 100) {
            return sink_->write(data, len);
        }
        if (percent <= 0) {
            static uint8_t silence[512] = {};
            size_t remaining = len;
            size_t written_total = 0;
            while (remaining > 0) {
                const size_t chunk = remaining > sizeof(silence) ? sizeof(silence) : remaining;
                const size_t written = sink_->write(silence, chunk);
                written_total += written;
                if (written != chunk) break;
                remaining -= written;
            }
            return written_total;
        }

        size_t written_total = 0;
        size_t offset = 0;
        while (offset < len) {
            const size_t bytes = (len - offset) > (VOLUME_SCRATCH_SAMPLES * sizeof(int16_t))
                ? (VOLUME_SCRATCH_SAMPLES * sizeof(int16_t))
                : (len - offset);
            const size_t samples = bytes / sizeof(int16_t);
            const int16_t* in = reinterpret_cast<const int16_t*>(data + offset);
            for (size_t i = 0; i < samples; ++i) {
                scratch_[i] = scalePcm16Sample(in[i], static_cast<uint8_t>(percent), dither_state_);
            }
            const size_t written = sink_->write(reinterpret_cast<const uint8_t*>(scratch_),
                                                samples * sizeof(int16_t));
            written_total += written;
            if (written != samples * sizeof(int16_t)) break;
            offset += samples * sizeof(int16_t);
        }
        return written_total;
    }

private:
    void detectBeat(const uint8_t* data, size_t len) {
        const int16_t* s = reinterpret_cast<const int16_t*>(data);
        const int n = (int)(len / 2);
        uint64_t sum = 0;
        for (int i = 0; i < n; i++) {
            const int32_t v = s[i];
            sum += (uint32_t)(v * v);
        }
        const uint32_t e = n > 0 ? (uint32_t)(sum / (uint32_t)n) : 0;

        hist_[hist_index_] = e;
        hist_index_ = (hist_index_ + 1) % HIST;
        if (hist_count_ < HIST) hist_count_++;

        uint64_t avg = 0;
        for (int i = 0; i < hist_count_; i++) avg += hist_[i];
        avg = hist_count_ > 0 ? avg / hist_count_ : 0;

        if (avg > 0) {
            const uint32_t ratio = (uint32_t)((uint64_t)e * 150 / avg);
            g_audioEnergy = (uint8_t)(ratio < 55 ? 55 : ratio > 240 ? 240 : ratio);
        }

        const unsigned long now = millis();
        if (avg > 0 && (uint64_t)e > avg + avg / 5 && (now - last_beat_ms_) > 150) {
            last_beat_ms_ = now;
            g_beatDetected = true;
        }
    }

    static constexpr int HIST = 43;
    Print* sink_ = nullptr;
    int volume_percent_ = volumeLevelToPercent(OUTPUT_VOL_LEVEL_DEFAULT);
    uint32_t hist_[HIST] = {};
    int hist_index_ = 0;
    int hist_count_ = 0;
    unsigned long last_beat_ms_ = 0;
    PcmVolumeDitherState dither_state_{};
    int16_t scratch_[VOLUME_SCRATCH_SAMPLES] = {};
};

class BtFrameAlignedSink : public Print {
public:
    void setSink(Print* sink) { sink_ = sink; }

    void reset() {
        pending_len_ = 0;
        memset(pending_, 0, sizeof(pending_));
    }

    size_t write(uint8_t value) override {
        return write(&value, 1);
    }

    size_t write(const uint8_t* data, size_t len) override {
        if (!sink_) return len;

        size_t consumed = 0;
        while (consumed < len) {
            const size_t room = sizeof(pending_) - pending_len_;
            const size_t take = (len - consumed) < room ? (len - consumed) : room;
            memcpy(pending_ + pending_len_, data + consumed, take);
            pending_len_ += take;
            consumed += take;

            if (pending_len_ == sizeof(pending_)) {
                const size_t written = sink_->write(pending_, sizeof(pending_));
                if (written != sizeof(pending_)) {
                    pending_len_ = 0;
                    return consumed;
                }
                pending_len_ = 0;
            }
        }
        return len;
    }

    int availableForWrite() override {
        return sink_ ? sink_->availableForWrite() : 0;
    }

private:
    Print* sink_ = nullptr;
    uint8_t pending_[BT_A2DP_WRITE_FRAME_SIZE] = {};
    size_t pending_len_ = 0;
};

class BtPcmBufferSink : public Print {
public:
    bool begin() {
        if (buffer_) return true;
        buffer_ = static_cast<uint8_t*>(
            heap_caps_malloc(BT_PCM_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!buffer_) {
            buffer_ = static_cast<uint8_t*>(
                heap_caps_malloc(BT_PCM_BUFFER_SIZE, MALLOC_CAP_8BIT));
        }
        if (!buffer_) {
            LOGE("[BT] PCM buffer allocation failed size=%u\n", (unsigned)BT_PCM_BUFFER_SIZE);
            return false;
        }
        clear();
        LOGI("[BT] PCM buffer ready size=%u\n", (unsigned)BT_PCM_BUFFER_SIZE);
        return true;
    }

    size_t write(uint8_t value) override {
        return write(&value, 1);
    }

    size_t write(const uint8_t* data, size_t len) override {
        if (!buffer_ || !data || len == 0) return 0;

        size_t consumed = 0;
        const unsigned long started = millis();
        while (consumed < len) {
            const size_t written = writeSome(data + consumed, len - consumed);
            consumed += written;
            if (consumed == len) break;

            if (millis() - started >= BT_PCM_WRITE_TIMEOUT_MS) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        return consumed;
    }

    int availableForWrite() override {
        if (!buffer_) return 0;
        portENTER_CRITICAL(&mux_);
        const size_t free_bytes = freeLocked();
        portEXIT_CRITICAL(&mux_);
        return (int)free_bytes;
    }

    int32_t readForA2dp(uint8_t* data, int32_t len) {
        if (!data || len <= 0) {
            clear();
            return 0;
        }
        if (!buffer_) {
            memset(data, 0, (size_t)len);
            return len;
        }

        const size_t requested = (size_t)len;
        const size_t copied = readSome(data, requested);
        if (copied < requested) {
            memset(data + copied, 0, requested - copied);
        }
        return len;
    }

    void clear() {
        portENTER_CRITICAL(&mux_);
        read_pos_ = 0;
        write_pos_ = 0;
        used_ = 0;
        portEXIT_CRITICAL(&mux_);
    }

private:
    size_t writeSome(const uint8_t* data, size_t len) {
        portENTER_CRITICAL(&mux_);
        const size_t free_bytes = freeLocked();
        const size_t to_write = len < free_bytes ? len : free_bytes;
        size_t first = to_write;
        if (first > BT_PCM_BUFFER_SIZE - write_pos_) first = BT_PCM_BUFFER_SIZE - write_pos_;
        if (first > 0) {
            memcpy(buffer_ + write_pos_, data, first);
        }
        const size_t second = to_write - first;
        if (second > 0) {
            memcpy(buffer_, data + first, second);
        }
        write_pos_ = (write_pos_ + to_write) % BT_PCM_BUFFER_SIZE;
        used_ += to_write;
        portEXIT_CRITICAL(&mux_);
        return to_write;
    }

    size_t readSome(uint8_t* data, size_t len) {
        portENTER_CRITICAL(&mux_);
        const size_t to_read = len < used_ ? len : used_;
        size_t first = to_read;
        if (first > BT_PCM_BUFFER_SIZE - read_pos_) first = BT_PCM_BUFFER_SIZE - read_pos_;
        if (first > 0) {
            memcpy(data, buffer_ + read_pos_, first);
        }
        const size_t second = to_read - first;
        if (second > 0) {
            memcpy(data + first, buffer_, second);
        }
        read_pos_ = (read_pos_ + to_read) % BT_PCM_BUFFER_SIZE;
        used_ -= to_read;
        portEXIT_CRITICAL(&mux_);
        return to_read;
    }

    size_t freeLocked() const {
        return BT_PCM_BUFFER_SIZE - used_;
    }

    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    uint8_t* buffer_ = nullptr;
    size_t read_pos_ = 0;
    size_t write_pos_ = 0;
    size_t used_ = 0;
};

struct AudioInfoLogger : public AudioInfoSupport {
    AudioInfo lastInfo{};

    void setAudioInfo(AudioInfo info) override {
        lastInfo = info;
        LOGI("[AUDIO] Decoder: SR=%d Hz, Ch=%d, Bits=%d\n",
             info.sample_rate, info.channels, info.bits_per_sample);
    }

    AudioInfo audioInfo() override { return lastInfo; }
} audioInfoLogger;

static I2SStream i2s;
static BluetoothA2DPSource btA2dp;
static A2DPNoVolumeControl btNoVolumeControl;
static MP3DecoderHelix mp3Decoder;
static BeatTracker beatTracker;
static BtPcmBufferSink btPcmSink;
static BtFrameAlignedSink btFrameSink;
static EncodedAudioStream decoderStream(&beatTracker, &mp3Decoder);
static QueueHandle_t audioQueue = nullptr;
static TaskHandle_t audioTaskHandle = nullptr;
static TaskHandle_t localTransportInitRequester = nullptr;
static Print* activeSink = nullptr;
static bool localTransportStarted = false;
static bool btTransportStarted = false;
static bool activeSinkIsBt = false;
static bool s_isPlaying = false;
static bool s_isPaused = false;
static volatile int outputVolumePercent = volumeLevelToPercent(OUTPUT_VOL_LEVEL_DEFAULT);

static CmdId s_curCmdId = 0;
static bool s_curIsNfc = false;
static bool s_curIsSystem = false;
static char s_curUid[24] = {};
static uint16_t s_curIndex = 0;
static uint8_t s_curSoundId = 0;

static int32_t btAudioDataCallback(uint8_t* data, int32_t len) {
    return btPcmSink.readForA2dp(data, len);
}

static void selectSink(Print* sink, bool is_bt_output) {
    activeSink = sink;
    activeSinkIsBt = is_bt_output;
    beatTracker.setSink(sink);
    LOGI("[AUDIO] sink=%s core=%d\n", is_bt_output ? "BT" : "NS", xPortGetCoreID());
}

static void clearActiveTransport() {
    btFrameSink.reset();
    if (btTransportStarted) {
        btPcmSink.clear();
    }
    g_audioEnergy = 0;
    g_beatDetected = false;
    beatTracker.reset();
}

static void resetDecoderForNewTrack() {
    clearActiveTransport();
    decoderStream.end();
    // Flush any PCM data the decoder pushed to I2S DMA during end(). Without this,
    // residual bytes from the previous track play at the start of the next one.
    writeSilenceGap(TRACK_TRANSITION_SILENCE_MS);
    decoderStream.begin();
}

static void flushStoppedPlayback() {
    clearActiveTransport();
    decoderStream.end();
    writeSilenceGap(TRACK_TRANSITION_SILENCE_MS);
    decoderStream.begin();
}

static void ensureLocalTransport() {
    if (localTransportStarted) return;
    LOGI("[AUDIO] init NS/I2S transport core=%d\n", xPortGetCoreID());
    nsPowerOn();
    delay(30);
    auto cfg = i2s.defaultConfig(TX_MODE);
    cfg.pin_bck = AUDIO_I2S_BCLK;
    cfg.pin_ws = AUDIO_I2S_LRCK;
    cfg.pin_data = AUDIO_I2S_DOUT;
    i2s.begin(cfg);
    localTransportStarted = true;
    LOGI("[AUDIO] NS/I2S transport ready core=%d\n", xPortGetCoreID());
}

static bool parseBtMac(const char* text, esp_bd_addr_t out) {
    if (!text || strlen(text) != 17) return false;

    unsigned int bytes[ESP_BD_ADDR_LEN] = {};
    if (sscanf(text, "%02x:%02x:%02x:%02x:%02x:%02x",
               &bytes[0], &bytes[1], &bytes[2],
               &bytes[3], &bytes[4], &bytes[5]) != ESP_BD_ADDR_LEN) {
        return false;
    }

    for (int i = 0; i < ESP_BD_ADDR_LEN; ++i) {
        if (bytes[i] > 0xFF) return false;
        out[i] = static_cast<uint8_t>(bytes[i]);
    }
    return true;
}

static bool ensureBtTransport() {
    if (btTransportStarted) return true;
    if (!btPcmSink.begin()) return false;
    LOGI("[BT] start transport target=%s mac=%s ctl=%d bd=%d core=%d\n",
         s_btTargetName,
         s_btTargetMac[0] ? s_btTargetMac : "-",
         (int)esp_bt_controller_get_status(),
         (int)esp_bluedroid_get_status(),
         xPortGetCoreID());
    // BT headphones mode uses zBox-side PCM attenuation; do not depend on remote AVRCP volume.
    btA2dp.set_avrc_rn_events({});
    btA2dp.set_volume_control(&btNoVolumeControl);
    btA2dp.set_auto_reconnect(true);
    esp_bd_addr_t targetAddr{};
    if (parseBtMac(s_btTargetMac, targetAddr)) {
        btA2dp.set_auto_reconnect(targetAddr);
        LOGI("[BT] using saved target MAC for reconnect: %s\n", s_btTargetMac);
    }
    btPcmSink.clear();
    btA2dp.start_raw(s_btTargetName, btAudioDataCallback);
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
        LOGE("[BT] transport start failed bd=%d ctl=%d\n",
             (int)esp_bluedroid_get_status(),
             (int)esp_bt_controller_get_status());
        return false;
    }
    btTransportStarted = true;
    delay(100);
    LOGI("[BT] transport started connected=%d bd=%d ctl=%d core=%d\n",
         (int)btA2dp.is_connected(),
         (int)esp_bluedroid_get_status(),
         (int)esp_bt_controller_get_status(),
         xPortGetCoreID());
    return true;
}

static size_t bytesForSilenceMs(uint16_t durationMs) {
    const AudioInfo info = audioInfoLogger.audioInfo();
    const uint32_t sampleRate = info.sample_rate > 0 ? (uint32_t)info.sample_rate : 44100U;
    const uint32_t channels = info.channels > 0 ? (uint32_t)info.channels : 2U;
    const uint32_t bitsPerSample = info.bits_per_sample > 0 ? (uint32_t)info.bits_per_sample : 16U;
    const uint32_t bytesPerFrame = channels * (bitsPerSample / 8U);
    return (sampleRate * bytesPerFrame * durationMs) / 1000U;
}

static void writeSilenceGap(uint16_t durationMs) {
    static uint8_t zeroBuf[512] = {};
    size_t remaining = bytesForSilenceMs(durationMs);
    while (remaining > 0 && activeSink) {
        const size_t chunk = remaining > sizeof(zeroBuf) ? sizeof(zeroBuf) : remaining;
        const size_t written = activeSink->write(zeroBuf, chunk);
        if (written == 0) break;
        remaining -= written;
    }
}

static bool audioSendCmd(const AudioCmd& cmd, TickType_t timeout, bool front = false) {
    if (!audioQueue) return false;
    BaseType_t ok = front
        ? xQueueSendToFront(audioQueue, &cmd, timeout)
        : xQueueSend(audioQueue, &cmd, timeout);
    if (ok != pdTRUE) {
        LOGW("[AUDIO] Queue full, cmd dropped: %d\n", (int)cmd.type);
        return false;
    }
    return true;
}

static void localTransportInitTask(void*) {
    ensureLocalTransport();
    if (localTransportInitRequester) {
        xTaskNotifyGive(localTransportInitRequester);
    }
    vTaskDelete(nullptr);
}

static void audioTaskFunc(void*) {
    File f;
    static uint8_t audioBuf[AUDIO_BUF_SIZE];

    uint32_t telSdBytes = 0;
    uint32_t telWrittenBytes = 0;
    uint32_t telDrops = 0;
    unsigned long telWindowStart = 0;
    int telWindows = 0;
    int btStartupWrites = 0;

    for (;;) {
        static unsigned long lastAudioHb = 0;
        if (millis() - lastAudioHb > 5000) {
            lastAudioHb = millis();
            LOGI("[AUDIO] alive isPlaying=%d hwm=%u\n",
                 (int)s_isPlaying, uxTaskGetStackHighWaterMark(nullptr));
        }

        AudioCmd cmd{};
        if (xQueueReceive(audioQueue, &cmd, 0) == pdTRUE) {
            if (cmd.type == AudioCmdType::PLAY) {
                s_curCmdId = cmd.cmd_id;
                s_curIsNfc = cmd.is_nfc_track;
                s_curIsSystem = cmd.is_system_sound;
                memcpy(s_curUid, cmd.uid, sizeof(s_curUid));
                s_curIndex = cmd.track_index;
                s_curSoundId = cmd.sound_id;

                if (f) f.close();
                resetDecoderForNewTrack();
                if (cmd.gapMs > 0) writeSilenceGap(cmd.gapMs);
                f = SD.open(cmd.path);
                if (f) {
                    s_isPlaying = true;
                    s_isPaused = false;
                    const char* logPath = cmd.path;
                    const size_t logPathLen = strlen(cmd.path);
                    if (logPathLen > 56) logPath = cmd.path + (logPathLen - 56);
                    LOGI("[AUDIO] Playing: %s%s cmd_id=%u\n",
                         logPathLen > 56 ? "..." : "", logPath, (unsigned)s_curCmdId);
                    telSdBytes = telWrittenBytes = telDrops = 0;
                    telWindowStart = millis();
                    telWindows = 0;
                    btStartupWrites = 0;
                    if (s_curCmdId != 0) {
                        if (s_curIsNfc) {
                            postEventFromTask(makeNfcPlaybackStartedEvent(s_curUid, s_curCmdId));
                        } else if (!s_curIsSystem) {
                            postEventFromTask(makeMusicTrackStartedEvent(s_curIndex, s_curCmdId));
                        }
                    }
                } else {
                    s_isPlaying = false;
                    s_isPaused = false;
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
            } else if (cmd.type == AudioCmdType::STOP) {
                if (f) f.close();
                s_isPlaying = false;
                s_isPaused = false;
                LOGI("[AUDIO] Stopped cmd_id=%u\n", (unsigned)cmd.cmd_id);
                telWindows = 6;
                flushStoppedPlayback();
                s_curCmdId = 0;
                if (cmd.cmd_id != 0) {
                    postEventFromTask(makeAudioStoppedEvent(cmd.cmd_id));
                }
            } else if (cmd.type == AudioCmdType::PAUSE) {
                if (f && s_isPlaying) {
                    s_isPlaying = false;
                    s_isPaused = true;
                    clearActiveTransport();
                    LOGI("[AUDIO] Paused\n");
                }
            } else if (cmd.type == AudioCmdType::RESUME) {
                if (f && s_isPaused) {
                    s_isPlaying = true;
                    s_isPaused = false;
                    LOGI("[AUDIO] Resumed\n");
                }
            } else if (cmd.type == AudioCmdType::VOLUME) {
                outputVolumePercent = constrain(cmd.volumePercent, 0, 100);
                beatTracker.setVolumePercent(outputVolumePercent);
                LOGI("[VOL] Applied output volume in audio task: %d%%\n", (int)outputVolumePercent);
            }
        }

        if (f && s_isPaused) {
            vTaskDelay(pdMS_TO_TICKS(10));
        } else if (f && f.available()) {
            const unsigned long writeStartMs = millis();
            const size_t readChunkSize = activeSinkIsBt
                ? BT_AUDIO_READ_CHUNK_SIZE
                : LOCAL_AUDIO_READ_CHUNK_SIZE;
            const int n = f.read(audioBuf, readChunkSize);
            if (n > 0) {
                telSdBytes += n;
                const size_t written = decoderStream.write(audioBuf, n);
                const unsigned long writeElapsedMs = millis() - writeStartMs;
                if (activeSinkIsBt && btStartupWrites < 8) {
                    LOGI("[AUDIO_BT_START] write=%d read=%d dec_in=%u bt_free=%d elapsed_ms=%lu\n",
                         btStartupWrites,
                         n,
                         (unsigned)written,
                         btPcmSink.availableForWrite(),
                         writeElapsedMs);
                    btStartupWrites++;
                }
                if (writeElapsedMs > 120) {
                    LOGW("[AUDIO] slow decode/write sink=%s chunk=%d elapsed_ms=%lu bt_free=%d core=%d\n",
                         activeSinkIsBt ? "BT" : "NS",
                         n,
                         writeElapsedMs,
                         activeSinkIsBt ? btPcmSink.availableForWrite() : -1,
                         xPortGetCoreID());
                }
                telWrittenBytes += written;
                if ((int)written < n) telDrops++;

                if (telWindows < 6) {
                    const unsigned long now = millis();
                    if (now - telWindowStart >= 5000) {
                        const float seconds = (now - telWindowStart) / 1000.0f;
                        LOGI("[AUDIO_TEL] SD=%u B/s dec_in=%u B/s drops=%u (write<n)\n",
                             (uint32_t)(telSdBytes / seconds),
                             (uint32_t)(telWrittenBytes / seconds),
                             telDrops);
                        telSdBytes = telWrittenBytes = telDrops = 0;
                        telWindowStart = now;
                        telWindows++;
                    }
                }
            }
            if (activeSinkIsBt) {
                taskYIELD();
            } else {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        } else if (f && !f.available()) {
            const CmdId endedCmdId = s_curCmdId;
            const bool endedIsSystem = s_curIsSystem;
            const uint8_t endedSoundId = s_curSoundId;

            f.close();
            s_isPlaying = false;
            s_isPaused = false;
            s_curCmdId = 0;
            LOGI("[AUDIO] Track ended heap=%u largest=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
            telWindows = 6;
            clearActiveTransport();
            if (!endedIsSystem) {
                writeSilenceGap(TRACK_TRANSITION_SILENCE_MS);
            }

            if (endedCmdId != 0) {
                if (endedIsSystem) {
                    postEventFromTask(makeSystemSoundCompletedEvent(endedSoundId, endedCmdId));
                } else {
                    postEventFromTask(makeEvent(EventType::TrackEnded));
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static bool audioSendPlayCmd(const char* path, CmdId cmd_id,
                             const char* uid, uint16_t index,
                             uint8_t sound_id, bool is_nfc, bool is_system,
                             uint16_t gapMs = 0) {
    if (!path) return false;
    AudioCmd cmd{};
    cmd.type = AudioCmdType::PLAY;
    cmd.gapMs = gapMs;
    cmd.cmd_id = cmd_id;
    cmd.is_nfc_track = is_nfc;
    cmd.is_system_sound = is_system;
    cmd.sound_id = sound_id;
    cmd.track_index = index;
    cmd.volumePercent = 0;
    if (uid) strlcpy(cmd.uid, uid, sizeof(cmd.uid));
    strlcpy(cmd.path, path, sizeof(cmd.path));
    return audioSendCmd(cmd, pdMS_TO_TICKS(50));
}

}  // namespace

void audioInit() {
    persistenceAdapterLoadBtTarget(s_btTargetName, sizeof(s_btTargetName));
    persistenceAdapterLoadBtTargetMac(s_btTargetMac, sizeof(s_btTargetMac));
    LOGI("[AUDIO] BT target: %s mac=%s\n", s_btTargetName, s_btTargetMac[0] ? s_btTargetMac : "-");
    btPcmSink.begin();
    // I2S must be installed from core 0 so its DMA ISR stays away from NFC SoftSPI on core 1.
    localTransportInitRequester = xTaskGetCurrentTaskHandle();
    xTaskCreatePinnedToCore(localTransportInitTask, "i2s_init", 2048, nullptr, 3, nullptr, 0);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    localTransportInitRequester = nullptr;

    beatTracker.setVolumePercent(outputVolumePercent);
    selectSink(&i2s, false);
    decoderStream.begin();
    mp3Decoder.addNotifyAudioChange(audioInfoLogger);

    audioQueue = xQueueCreate(5, sizeof(AudioCmd));
    xTaskCreatePinnedToCore(audioTaskFunc, "audio", 8192, nullptr, 2, &audioTaskHandle, 1);
    LOGI("Audio task started (core 1, prio 2)\n");
}

void audioStartFile(const char* path) {
    audioStartFileWithGap(path, 0);
}

void audioStartFileWithGap(const char* path, uint16_t gapMs) {
    if (!path) return;
    AudioCmd cmd{};
    cmd.type = AudioCmdType::PLAY;
    cmd.gapMs = gapMs;
    strlcpy(cmd.path, path, sizeof(cmd.path));
    audioSendCmd(cmd, pdMS_TO_TICKS(50));
}

void audioStop() {
    AudioCmd cmd{};
    cmd.type = AudioCmdType::STOP;
    audioSendCmd(cmd, pdMS_TO_TICKS(100), true);
}

void audioPause() {
    AudioCmd cmd{};
    cmd.type = AudioCmdType::PAUSE;
    audioSendCmd(cmd, pdMS_TO_TICKS(50), true);
}

void audioResume() {
    AudioCmd cmd{};
    cmd.type = AudioCmdType::RESUME;
    audioSendCmd(cmd, pdMS_TO_TICKS(50));
}

bool audioIsRunning() {
    return s_isPlaying || s_isPaused;
}

bool audioIsPaused() {
    return s_isPaused;
}

bool audioIsReady() {
    return audioQueue != nullptr && audioTaskHandle != nullptr;
}

void audioDeleteTaskForSleep() {
    if (audioTaskHandle) {
        vTaskDelete(audioTaskHandle);
        audioTaskHandle = nullptr;
    }
    if (localTransportStarted) {
        decoderStream.end();
        i2s.end();
        localTransportStarted = false;
    }
    pinMode(AUDIO_I2S_BCLK, OUTPUT);
    pinMode(AUDIO_I2S_LRCK, OUTPUT);
    pinMode(AUDIO_I2S_DOUT, OUTPUT);
    digitalWrite(AUDIO_I2S_BCLK, LOW);
    digitalWrite(AUDIO_I2S_LRCK, LOW);
    digitalWrite(AUDIO_I2S_DOUT, LOW);
    nsPowerOff();
}

void audioSetOutputVolumePercent(int percent) {
    outputVolumePercent = constrain(percent, 0, 100);
    beatTracker.setVolumePercent(outputVolumePercent);
    if (!audioQueue) return;

    AudioCmd cmd{};
    cmd.type = AudioCmdType::VOLUME;
    cmd.volumePercent = outputVolumePercent;
    audioSendCmd(cmd, 0);
}

bool audioStartBtHeadphonesMode() {
    ensureLocalTransport();
    if (!ensureBtTransport()) return false;
    btFrameSink.setSink(&btPcmSink);
    selectSink(&btFrameSink, true);
    clearActiveTransport();
    LOGI("[BT] Headphones mode started\n");
    return true;
}

void audioStopBtHeadphonesMode() {
    if (btTransportStarted) {
        ensureLocalTransport();
        selectSink(&i2s, false);
        clearActiveTransport();
        if (btA2dp.is_discovery_active()) {
            btA2dp.cancel_discovery();
        }
        btPcmSink.clear();
        btA2dp.end(false);
        btTransportStarted = false;
    }
    ensureLocalTransport();
    selectSink(&i2s, false);
    clearActiveTransport();
    LOGI("[BT] Headphones mode stopped\n");
}

bool audioBtHeadphonesAreConnected() {
    return btTransportStarted && btA2dp.is_connected();
}

bool audioBtHeadphonesModeIsRunning() {
    return btTransportStarted;
}

uint32_t audioGetTaskHWM() {
    return audioTaskHandle ? uxTaskGetStackHighWaterMark(audioTaskHandle) : 0;
}

bool audioStartNfcTrack(const char* path, const char* uid, CmdId cmd_id) {
    return audioSendPlayCmd(path, cmd_id, uid, 0, 0, true, false);
}

bool audioStartMusicTrack(const char* path, uint16_t index, CmdId cmd_id) {
    return audioSendPlayCmd(path, cmd_id, nullptr, index, 0, false, false);
}

bool audioStopWithId(CmdId cmd_id) {
    AudioCmd cmd{};
    cmd.type = AudioCmdType::STOP;
    cmd.cmd_id = cmd_id;
    return audioSendCmd(cmd, pdMS_TO_TICKS(100), true);
}

bool audioPlaySystemSound(const char* path, uint8_t sound_id, CmdId cmd_id) {
    return audioSendPlayCmd(path, cmd_id, nullptr, 0, sound_id, false, true, 180);
}
