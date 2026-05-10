#pragma once
#include <stdint.h>
#include "app_state.h"

void audioInit();
void audioStartFile(const char *path);
void audioStop();
void audioStartFileWithGap(const char *path, uint16_t gapMs);
void audioPause();
void audioResume();
bool audioIsRunning();
bool audioIsPaused();
bool audioIsReady();
void audioDeleteTaskForSleep();
void audioSetBtVolumePercent(int percent);
bool audioBtIsConnected();
void audioPollBtConnection();
bool audioRestartDiscovery();
uint32_t audioGetTaskHWM();

// Stage 3+ correlation API (called by audio_adapter when DISPATCHER_OWNS_AUDIO=1).
// Returns false if the audio queue is full (caller should post AudioCommandRejected).
bool audioStartNfcTrack(const char *path, const char *uid, CmdId cmd_id);
bool audioStartMusicTrack(const char *path, uint16_t index, CmdId cmd_id);
bool audioStopWithId(CmdId cmd_id);
bool audioPlaySystemSound(const char *path, uint8_t sound_id, CmdId cmd_id);
