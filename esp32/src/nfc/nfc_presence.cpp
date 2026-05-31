#include "nfc_presence.h"

#include <string.h>

static bool hasUid(const char* uid)
{
    return uid && uid[0] != '\0';
}

static void copyUid(char* dst, const char* src, size_t dst_size)
{
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

void nfcPresenceReset(NfcPresenceState& state)
{
    state.current_uid[0] = '\0';
    state.lost_pending = false;
    state.lost_started_ms = 0;
}

NfcPresenceResult nfcPresenceUpdate(
    NfcPresenceState& state,
    const char* read_uid,
    uint32_t now_ms,
    uint32_t lost_ms)
{
    NfcPresenceResult result{};
    result.action = NfcPresenceAction::None;

    if (hasUid(read_uid)) {
        state.lost_pending = false;
        state.lost_started_ms = 0;

        if (strcmp(read_uid, state.current_uid) != 0) {
            copyUid(state.current_uid, read_uid, sizeof(state.current_uid));
            result.action = NfcPresenceAction::Detected;
            copyUid(result.uid, read_uid, sizeof(result.uid));
        }
        return result;
    }

    if (!hasUid(state.current_uid)) {
        state.lost_pending = false;
        state.lost_started_ms = 0;
        return result;
    }

    if (!state.lost_pending) {
        state.lost_pending = true;
        state.lost_started_ms = now_ms;
        return result;
    }

    if ((uint32_t)(now_ms - state.lost_started_ms) >= lost_ms) {
        result.action = NfcPresenceAction::Removed;
        copyUid(result.uid, state.current_uid, sizeof(result.uid));
        nfcPresenceReset(state);
    }

    return result;
}
