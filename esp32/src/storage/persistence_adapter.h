#pragma once
#include <stddef.h>
#include "app_state.h"

void persistenceAdapterSaveBrightness(uint8_t percent);
void persistenceAdapterSavePlaybackMode(PlaybackMode mode);
void persistenceAdapterSaveVolume(uint8_t level);

// BT target speaker name — persisted to NVS, changeable from web portal.
// No event posted: call sites handle their own feedback.
void persistenceAdapterSaveBtTarget(const char* name);
void persistenceAdapterLoadBtTarget(char* buf, size_t len);
