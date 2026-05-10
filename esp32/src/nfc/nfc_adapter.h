#pragma once

// Drains the NFC internal queue, calls old playback handlers (dual-call),
// and posts NfcTagDetected / NfcTagRemoved to the dispatcher queue.
// Call from main loop when not in night-light mode.
void nfcAdapterDrain();
