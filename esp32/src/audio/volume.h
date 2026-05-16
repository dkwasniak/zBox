#pragma once
#include <stdint.h>

void loadOutputVolume();
void applyOutputVolume();
void volumeTick();
void setOutputVolumeAndApply(uint8_t percent);
uint8_t getOutputVolumeLevel();
