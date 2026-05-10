#pragma once
#include <stdint.h>

void loadBtVolume();
void applyBtVolume();
void volumeTick();
void setBtVolumeAndApply(uint8_t percent);
uint8_t getBtVolume();
