#pragma once

#ifdef NATIVE_BUILD
#include <cassert>
#define MUSICBOX_ASSERT(cond, msg) assert(cond)
#else
extern bool gInSleepExecutorPath;
void musicboxAssertFail(const char* file, int line, const char* msg);

#define MUSICBOX_ASSERT(cond, msg)              \
    do {                                        \
        if (!(cond)) {                          \
            musicboxAssertFail(__FILE__, __LINE__, (msg)); \
        }                                       \
    } while (0)
#endif
