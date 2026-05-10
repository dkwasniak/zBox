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

#define LOGI(fmt, ...) logWritef("INFO", false, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) logWritef("WARN", true, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) logWritef("ERROR", true, fmt, ##__VA_ARGS__)
#define LOGC(fmt, ...) logWritef("CRIT", true, fmt, ##__VA_ARGS__)

#define LOG(fmt, ...) LOGI(fmt, ##__VA_ARGS__)
#define LOGLN(msg) LOGI("%s\n", msg)
