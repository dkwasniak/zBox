#pragma once
#include <stdint.h>
#include <stdbool.h>

struct RawButtonEvent {
    uint8_t  button_id;      // 0=A, 1=B, 2=C, 3=D
    bool     pressed;        // true = FALLING (press), false = RISING (release)
    uint32_t timestamp_ms;
};

// Creates rawButtonQueue and attaches GPIO ISRs (replaces buttonsInit()).
void buttonAdapterInit();

// Starts the button adapter FreeRTOS task. Call after buttonAdapterInit().
void buttonAdapterStartTask();

// Internal pure-logic decoder (exposed for native testing only).
void buttonDecoderFeed(RawButtonEvent raw);
void buttonDecoderTick(uint32_t now_ms);
void buttonDecoderReset();
