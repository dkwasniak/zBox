#pragma once
#include "Arduino.h"

struct File {
    explicit operator bool() const { return true; }
    void close() {}
};

static constexpr int FILE_WRITE = 1;

struct SDClass {
    File open(const char*, int = 0) { return File{}; }
};

inline SDClass SD;

