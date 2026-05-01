#pragma once
#include <Arduino.h>

#define PLOG_LINE_LEN 96
#define PLOG_LINES 80

void plogInit();
void plogWrite(const char* line);
void plogFlushToSd();
void plogMark(const char* tag);

#define PLOGF(fmt, ...) do { \
    char _b[PLOG_LINE_LEN]; \
    snprintf(_b, sizeof(_b), "<%lu> " fmt, millis(), ##__VA_ARGS__); \
    plogWrite(_b); \
} while(0)
