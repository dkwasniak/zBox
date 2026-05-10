#pragma once
#include <array>
#include "app_state.h"
#include "events.h"
#include "effects.h"

// ---------------------------------------------------------------------------
// ReduceResult — output of a single reducer call
// ---------------------------------------------------------------------------
struct ReduceResult {
    AppState next_state;
    std::array<Effect, MAX_EFFECTS> effects;
    uint8_t effect_count;
};

// ---------------------------------------------------------------------------
// reduce — pure state transition function
//
// Contract:
//   - No hardware includes, no side effects, no dynamic allocation.
//   - now_ms is passed in so deadlines can be computed without calling millis().
//   - Effects are emitted with cmd_id = 0; dispatcher fills real CmdId values.
// ---------------------------------------------------------------------------
ReduceResult reduce(const AppState& state, const Event& event, uint32_t now_ms);
