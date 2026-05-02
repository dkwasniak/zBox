#pragma once
#include <stdint.h>
#include <stdbool.h>

struct Button {
    uint8_t pin;
    const char *name;
    volatile bool pressed;                // ISR flag, czyszczone w handleButtons po release
    volatile unsigned long lastInterrupt; // debounce timestamp (ISR)
    unsigned long pressStart;             // millis() pierwszego naciśnięcia, 0 gdy zwolniony
    bool longHandled;                     // akcja long-press już wystrzeliła
};

struct NfcEvent {
    bool tagPresent;
    char uid[30];
};
