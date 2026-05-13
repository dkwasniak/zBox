#pragma once
#include <stdint.h>
#include "app_state.h"

// ---------------------------------------------------------------------------
// LedSceneType — every scene the LED executor can render
// ---------------------------------------------------------------------------
enum class LedSceneType : uint8_t {
    Off,
    BootProgress,
    WakeProgress,       // pre-boot; never returned by deriveLedScene()
    WaitBt,
    Idle,
    Playing,
    NightLight,
    VolumeOverlay,
    SleepReady,
    BatteryPreview,
    ModeChange,
    WarningFlash,
    SyncEntry
};

// ---------------------------------------------------------------------------
// LedSceneParams — scene type plus optional parameters
// ---------------------------------------------------------------------------
struct LedSceneParams {
    LedSceneType type;
    union {
        struct { uint8_t step; uint8_t total; } boot_progress;
        struct { uint8_t percent; }             night_light;
        struct { uint8_t percent; }             volume;
        struct { uint8_t bars; }                battery;
    } params;
};

// ---------------------------------------------------------------------------
// deriveLedScene — pure function; determines current scene from AppState
// ---------------------------------------------------------------------------
LedSceneParams deriveLedScene(const AppState& s);
