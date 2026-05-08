#pragma once
#include <Arduino.h>
#include <map>
#include "musicbox_config.h"

enum class RuntimeSessionMode : uint8_t {
    NORMAL = 0,
    NIGHT_LIGHT = 1,
};

// Wyłącznie prawdziwie shared globals (czytane/pisane przez wiele modułów)

extern volatile bool g_btConnected;
extern volatile bool g_beatDetected;
extern volatile uint8_t g_audioEnergy;  // 0-255, bieżąca energia audio (dla LED)
extern bool btVolumeApplied;
extern unsigned long btWaitStart;
extern bool jblRecoveryDone;
extern bool sdReady;
extern std::map<String, String> figurineMap;
extern std::map<String, String> systemSoundMap;
extern volatile char lastNfcUid[30];
extern volatile bool isPlaying;      // kanoniczne źródło prawdy
extern volatile bool isPaused;
extern volatile bool trackEndedFlag;
extern String pendingPlaybackPath;
extern String pendingPlaybackUid;
extern bool nfcReady;
extern unsigned long bootStart;
extern bool bootTimingDone;
extern unsigned long lastActivityMs;

RuntimeSessionMode runtimeGetSessionMode();
void runtimeSetSessionMode(RuntimeSessionMode mode);
bool runtimeIsNightLight();
