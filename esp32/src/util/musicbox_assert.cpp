#include "musicbox_assert.h"
#include "logging.h"
#include <esp_system.h>

bool gInSleepExecutorPath = false;

void musicboxAssertFail(const char* file, int line, const char* msg) {
    LOGC("[ASSERT] %s:%d: %s\n", file, line, msg);
    if (!gInSleepExecutorPath) {
        esp_restart();
    }
    // Inside sleep executor: log only — restarting here could corrupt the BT/JBL
    // shutdown sequence and cause a BT stack crash on next boot.
}
