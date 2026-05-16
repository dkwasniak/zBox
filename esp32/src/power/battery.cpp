#include "battery.h"
#include "zbox_config.h"
#include "helpers.h"
#include "logging.h"
#include <Arduino.h>

namespace {
constexpr int BAT_SAMPLE_COUNT = 64;
constexpr int BAT_TRIM_COUNT = 8;
constexpr float BAT_FILTER_ALPHA = 0.15f;
constexpr float BAT_FILTER_ALPHA_LARGE_STEP = 0.03f;
constexpr float BAT_FILTER_LARGE_STEP_V = 0.20f;
static uint16_t s_batterySamples[BAT_SAMPLE_COUNT];

static void sortSamples(uint16_t *values, int count)
{
    for (int i = 1; i < count; ++i)
    {
        uint16_t key = values[i];
        int j = i - 1;
        while (j >= 0 && values[j] > key)
        {
            values[j + 1] = values[j];
            --j;
        }
        values[j + 1] = key;
    }
}
}

static void ensureBatteryAdcConfigured()
{
    static bool configured = false;
    if (configured)
        return;

    pinMode(BAT_ADC_PIN, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);

    // The first reading after configuration can be unstable, so we discard it.
    analogReadMilliVolts(BAT_ADC_PIN);
    configured = true;
}

BatteryReading readBatteryReading()
{
    ensureBatteryAdcConfigured();

    for (int i = 0; i < BAT_SAMPLE_COUNT; i++)
    {
        s_batterySamples[i] = (uint16_t)analogReadMilliVolts(BAT_ADC_PIN);
        delayMicroseconds(250);
    }

    sortSamples(s_batterySamples, BAT_SAMPLE_COUNT);

    long sum = 0;
    for (int i = BAT_TRIM_COUNT; i < BAT_SAMPLE_COUNT - BAT_TRIM_COUNT; ++i)
        sum += s_batterySamples[i];

    constexpr int keptSamples = BAT_SAMPLE_COUNT - (2 * BAT_TRIM_COUNT);
    float vPinInstant = (sum / (float)keptSamples) / 1000.0f; // mV -> V on GPIO35 (VBAT/2)

    static bool hasFiltered = false;
    static float vPinFiltered = 0.0f;
    if (!hasFiltered)
    {
        vPinFiltered = vPinInstant;
        hasFiltered = true;
    }
    else
    {
        float delta = fabsf(vPinInstant - vPinFiltered);
        float alpha = delta > BAT_FILTER_LARGE_STEP_V ? BAT_FILTER_ALPHA_LARGE_STEP : BAT_FILTER_ALPHA;
        vPinFiltered += (vPinInstant - vPinFiltered) * alpha;
    }

    float vPin = vPinFiltered;
    float vBat = vPin * BAT_ADC_DIVIDER_RATIO * BAT_ADC_CALIBRATION;
    int bars = batteryBars(vBat);
    LOGI("[BAT] ADC pin voltage: instant=%.3fV filtered=%.3fV\n", vPinInstant, vPin);
    return {vPin, vBat, bars, batteryColorName(bars)};
}

float readBatteryVoltage()
{
    return readBatteryReading().batteryVoltage;
}
