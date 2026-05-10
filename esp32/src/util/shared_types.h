#pragma once
#include <stdint.h>
#include <stdbool.h>

struct Button {
    uint8_t pin;
    const char *name;
    volatile bool pressed;                // ISR flag, cleared in handleButtons after release
    volatile unsigned long lastInterrupt; // debounce timestamp (ISR)
    unsigned long pressStart;             // millis() of the first press, 0 when released
    bool longHandled;                     // long-press action has already fired
    unsigned long lastReleaseMs;          // last release time - used for single/double click
    uint8_t clickCount;                   // number of clicks pending resolution
    bool clickSuppressed;                 // click suppressed by long press or combo
};

struct NfcEvent {
    bool tagPresent;
    char uid[30];
};
