#pragma once
#include <array>
#include "app_state.h"
#include "musicbox_assert.h"

static constexpr uint8_t MAX_EFFECTS = 8;
static_assert(MAX_EFFECTS == 8, "update budget analysis in 02_events_effects.md if changed");

static constexpr uint8_t SOUND_ID_POWER_OFF = 1;
static constexpr uint8_t SOUND_ID_NFC_MODE = 2;
static constexpr uint8_t SOUND_ID_MUSIC_MODE = 3;
static constexpr uint8_t SOUND_ID_STARTUP = 4;

enum class EffectType : uint8_t {
    StartNfcPlaybackByUid,
    StartMusicTrackByIndex,
    StopAudio,
    PauseAudio,
    ResumeAudio,
    PlaySystemSound,

    SetOutputVolume,
    PersistVolume,

    PersistBrightness,
    PersistPlaybackMode,

    StartBtHeadphonesMode,
    StopBtHeadphonesMode,

    EnterDeepSleep,

    LogDiagnostic,
    TriggerSyncRestart,
};

struct Effect {
    EffectType type;
    union {
        struct { char uid[24]; CmdId cmd_id; } nfc_playback;
        struct { uint16_t index; CmdId cmd_id; } music_track;
        struct { CmdId cmd_id; } audio_control;
        struct { uint8_t sound_id; CmdId cmd_id; } system_sound;
        struct { uint8_t level_percent; } volume;
        struct { uint8_t level_percent; } brightness;
        struct { PlaybackMode mode; } playback_mode;
        struct { RequestedSleepKind kind; } deep_sleep;
        struct { uint8_t code; } diagnostic;
    } payload;
};

class EffectBuilder {
public:
    void add(const Effect& e) {
        MUSICBOX_ASSERT(count < MAX_EFFECTS, "effect budget overflow");
        effects[count++] = e;
    }

    uint8_t count = 0;
    std::array<Effect, MAX_EFFECTS> effects{};
};
