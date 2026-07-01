#pragma once
#include <Arduino.h>
#include "persistent_log.h"

#ifdef LOGI
#undef LOGI
#endif
#ifdef LOGW
#undef LOGW
#endif
#ifdef LOGE
#undef LOGE
#endif

// Master switch. Defined only in debug builds (see platformio.ini).
// In release LOG_ENABLED is absent -> all logging expands to nothing, so the
// format arguments are never evaluated and no vsnprintf/Serial cost remains.
#if defined(LOG_ENABLED)
#define LOGI(fmt, ...) logWritef("INFO", false, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) logWritef("WARN", true, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) logWritef("ERROR", true, fmt, ##__VA_ARGS__)
#define LOGC(fmt, ...) logWritef("CRIT", true, fmt, ##__VA_ARGS__)
#else
#define LOGI(fmt, ...) ((void)0)
#define LOGW(fmt, ...) ((void)0)
#define LOGE(fmt, ...) ((void)0)
#define LOGC(fmt, ...) ((void)0)
#endif

#define LOG(fmt, ...) LOGI(fmt, ##__VA_ARGS__)
#define LOGLN(msg) LOGI("%s\n", msg)
