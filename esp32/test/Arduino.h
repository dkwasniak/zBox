#pragma once
#include <string>
#include <cstdio>
#include <algorithm>

struct String : public std::string {
    String() = default;
    String(const char* s) : std::string(s) {}
    String(int val, int base = 10) {
        char buf[32];
        if (base == 16) snprintf(buf, sizeof(buf), "%x", val);  // "5" nie "05"
        else snprintf(buf, sizeof(buf), "%d", val);
        assign(buf);
    }
    using std::string::operator+=;
    void toUpperCase() { std::transform(begin(), end(), begin(), ::toupper); }
    bool isEmpty() const { return empty(); }
};

inline unsigned long millis() { return 0; }
#define HEX 16

// GPIO stubs (used by button_adapter native tests)
#define INPUT_PULLUP 2
#define CHANGE       3
inline void pinMode(int, int) {}
inline void attachInterruptArg(int, void(*)(void*), void*, int) {}
inline int  digitalPinToInterrupt(int pin) { return pin; }
