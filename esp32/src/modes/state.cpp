#include "state.h"

volatile bool g_beatDetected = false;
volatile uint8_t g_audioEnergy = 0;
bool sdReady = false;
unsigned long bootStart = 0;
static RuntimeSessionMode runtimeSessionMode = RuntimeSessionMode::NORMAL;

RuntimeSessionMode runtimeGetSessionMode()
{
    return runtimeSessionMode;
}

void runtimeSetSessionMode(RuntimeSessionMode mode)
{
    runtimeSessionMode = mode;
}

bool runtimeIsNightLight()
{
    return runtimeSessionMode == RuntimeSessionMode::NIGHT_LIGHT;
}
