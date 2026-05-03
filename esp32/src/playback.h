#pragma once
#include <Arduino.h>

enum class PlaybackMode : uint8_t {
    NFC = 0,
    MUSIC = 1,
};

void playbackInit();
PlaybackMode playbackGetMode();
bool playbackIsNfcMode();
bool playbackIsMusicMode();
void playbackToggleMode();
void playbackHandleBtConnected();
void playbackHandleTrackEnded();
void playbackHandleNfcTagPresent(const char *uid);
void playbackHandleNfcTagRemoved();
void playbackTogglePlayPause();
void playbackNextTrack();
void playbackPrevTrack();

bool ensureJblReady();
void startPlayback(const String &uid);
void stopPlayback();
void playSystemSoundSync(const char *name, uint32_t timeoutMs = 10000);
