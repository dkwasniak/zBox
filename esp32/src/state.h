#pragma once
#include <Arduino.h>
#include <map>
#include "musicbox_config.h"

// Wyłącznie prawdziwie shared globals (czytane/pisane przez wiele modułów)

extern volatile bool g_btConnected;
extern bool btVolumeApplied;
extern unsigned long btWaitStart;
extern bool jblRecoveryDone;
extern bool sdReady;
extern std::map<String, String> figurineMap;
extern std::map<String, String> systemSoundMap;
extern volatile char lastNfcUid[30];
extern volatile bool isPlaying;      // kanoniczne źródło prawdy
extern volatile bool trackEndedFlag;
extern String pendingPlaybackPath;
extern String pendingPlaybackUid;
extern bool nfcReady;
extern unsigned long bootStart;
extern bool bootTimingDone;
extern unsigned long lastActivityMs;
