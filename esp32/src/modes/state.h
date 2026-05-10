#pragma once
#include <Arduino.h>
#include <map>
#include "zbox_config.h"

enum class RuntimeSessionMode : uint8_t {
    NORMAL = 0,
    NIGHT_LIGHT = 1,
};

// Only truly shared globals (read/written by multiple modules)

extern volatile bool g_btConnected;
extern volatile bool g_beatDetected;
extern volatile uint8_t g_audioEnergy;  // 0-255, current audio energy (for LED)
extern bool btVolumeApplied;
extern unsigned long btWaitStart;
extern bool jblRecoveryDone;
extern bool sdReady;
extern std::map<String, String> figurineMap;
extern std::map<String, String> systemSoundMap;
extern volatile char lastNfcUid[30];
extern volatile bool isPlaying;
extern volatile bool isPaused;
extern bool nfcReady;
extern unsigned long bootStart;
extern bool bootTimingDone;
extern unsigned long lastActivityMs;

RuntimeSessionMode runtimeGetSessionMode();
void runtimeSetSessionMode(RuntimeSessionMode mode);
bool runtimeIsNightLight();
