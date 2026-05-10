#pragma once
#include <Arduino.h>
#include "app_state.h"

void playbackInit();
PlaybackMode playbackGetMode();
bool playbackIsNfcMode();
bool playbackIsMusicMode();

// Resolve music library index to a file path.
// Returns true and writes to out_path if index is valid; false if out of range.
bool playbackGetMusicPath(uint16_t index, char *out_path, size_t out_size);
