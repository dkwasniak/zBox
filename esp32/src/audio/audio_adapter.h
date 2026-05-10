#pragma once
#include "app_state.h"

// Initialise the audio adapter (currently a no-op; placeholder for Stage 3+ setup).
void audioAdapterInit();

// Stage 3+: deterministic audio command/ack API.
// All functions return immediately. Feedback arrives via the dispatcher event queue.
// cmd_id is assigned by the dispatcher before calling; never 0.

// Resolve uid → path via figurineMap, enqueue PLAY, post NfcPlaybackStarted or failure.
// Returns false if mapping not found (synchronous; NfcPlaybackStartFailed posted by adapter).
bool audioAdapterStartNfcPlayback(const char *uid, CmdId cmd_id);

// Resolve index → path via music library, enqueue PLAY, post MusicTrackStarted or failure.
// Returns false if index out of range (synchronous; MusicTrackStartFailed posted by adapter).
bool audioAdapterStartMusicTrack(uint16_t index, CmdId cmd_id);

// Enqueue STOP to front of audio queue. Posts AudioStopped when confirmed by audio task.
void audioAdapterStop(CmdId cmd_id);

// Pause/resume — Stage 3 stubs (no feedback events yet; migrated fully in Stage 5).
void audioAdapterPause(CmdId cmd_id);
void audioAdapterResume(CmdId cmd_id);

// Resolve sound_id → path via systemSoundMap, enqueue PLAY, post SystemSoundCompleted/Failed.
// Returns false if sound mapping not found (synchronous; SystemSoundFailed posted by adapter).
bool audioAdapterPlaySystemSound(uint8_t sound_id, CmdId cmd_id);
