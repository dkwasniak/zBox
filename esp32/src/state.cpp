#include "state.h"

volatile bool g_btConnected = false;
bool btVolumeApplied = false;
unsigned long btWaitStart = 0;
bool jblRecoveryDone = false;
bool sdReady = false;
std::map<String, String> figurineMap;
std::map<String, String> systemSoundMap;
volatile char lastNfcUid[30] = {};
volatile bool isPlaying = false;
volatile bool trackEndedFlag = false;
String pendingPlaybackPath;
String pendingPlaybackUid;
bool nfcReady = false;
unsigned long bootStart = 0;
bool bootTimingDone = false;
unsigned long lastActivityMs = 0;
