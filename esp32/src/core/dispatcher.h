#pragma once
#include "app_state.h"

// Handoff constants — 0 = observe-only, 1 = dispatcher is authoritative.
// Flip one at a time, one per migration stage.
#define DISPATCHER_OWNS_BT_NFC   1   // Stage 2: flipped to 1
#define DISPATCHER_OWNS_AUDIO    1   // Stage 3: flipped to 1
#define DISPATCHER_OWNS_SLEEP    1   // Stage 4: flipped to 1
#define DISPATCHER_OWNS_BUTTONS  1   // Stage 5: flipped to 1

// Call during setup() before starting any adapter that may post events.
void dispatcherInit();

// Set initial AppState fields before the dispatcher task starts.
// Must be called after setup() initializes playback mode (Stage 2+).
void dispatcherSetInitialPlaybackMode(PlaybackMode mode);
void dispatcherSetInitialVolume(uint8_t percent);
void dispatcherSetInitialNightLightBrightness(uint8_t percent);

// Creates the "app" FreeRTOS task. Call after all hardware adapters are ready.
void dispatcherStartTask();

// Returns a snapshot copy of current AppState. Safe from any task context.
AppState getDiagnosticSnapshot();
