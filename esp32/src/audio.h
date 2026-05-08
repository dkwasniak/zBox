#pragma once
#include <stdint.h>

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
