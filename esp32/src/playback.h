#pragma once
#include <Arduino.h>

bool ensureJblReady();
void startPlayback(const String &uid);
void stopPlayback();
void playSystemSoundSync(const char *name, uint32_t timeoutMs = 10000);
