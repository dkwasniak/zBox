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
    unsigned long lastReleaseMs;          // ostatnie puszczenie - do single/double click
    uint8_t clickCount;                   // liczba klików oczekujących na rozstrzygnięcie
    bool clickSuppressed;                 // klik zablokowany przez long press lub combo
};

struct NfcEvent {
    bool tagPresent;
    char uid[30];
};
