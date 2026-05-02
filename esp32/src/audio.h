#pragma once
#include <stdint.h>

void audioInit();
void audioStartFile(const char *path);
void audioStop();
bool audioIsRunning();
bool audioIsReady();
void audioDeleteTaskForSleep();
void audioSetBtVolumePercent(int percent);
bool audioBtIsConnected();
uint32_t audioGetTaskHWM();
