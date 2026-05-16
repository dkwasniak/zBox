#pragma once
#include "app_state.h"

// Stage 1: stubs — persistence effects implemented in Stage 5+.
// These will wrap NVS/Preferences writes and post BrightnessPersisted /
// PlaybackModePersisted feedback events to the dispatcher.
void persistenceAdapterSaveBrightness(uint8_t percent);
void persistenceAdapterSavePlaybackMode(PlaybackMode mode);
void persistenceAdapterSaveVolume(uint8_t level);
