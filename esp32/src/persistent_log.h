#pragma once
#include <Arduino.h>

#define PLOG_LINE_LEN 96
#define PLOG_LINES 40

void plogInit(bool sdAvailable = true);
void plogWrite(const char* line);
void plogFlushToSd();
void plogMark(const char* level, const char* tag);
void logWritef(const char* level, bool persistent, const char* fmt, ...);
void formatUptime(char* out, size_t outSize, unsigned long ms);
