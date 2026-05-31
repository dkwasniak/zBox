#pragma once

#include <stdint.h>
#include <stddef.h>

enum class NfcPresenceAction : uint8_t {
    None,
    Detected,
    Removed,
};

struct NfcPresenceState {
    char current_uid[24];
    bool lost_pending;
    uint32_t lost_started_ms;
};

struct NfcPresenceResult {
    NfcPresenceAction action;
    char uid[24];
};

void nfcPresenceReset(NfcPresenceState& state);
NfcPresenceResult nfcPresenceUpdate(
    NfcPresenceState& state,
    const char* read_uid,
    uint32_t now_ms,
    uint32_t lost_ms);
