#pragma once

#include <array>
#include <stdint.h>

static constexpr uint8_t OUTPUT_VOL_LEVEL_MIN = 0;
static constexpr uint8_t OUTPUT_VOL_LEVEL_MAX = 12;
static constexpr uint8_t OUTPUT_VOL_LEVEL_DEFAULT = 7;
static constexpr uint8_t OUTPUT_VOL_PERCENT_MIN = 0;
static constexpr uint8_t OUTPUT_VOL_PERCENT_MAX = 80;

static constexpr std::array<uint8_t, OUTPUT_VOL_LEVEL_MAX + 1> OUTPUT_VOL_LEVEL_TO_PERCENT = {
    0, 1, 2, 4, 6, 9, 13, 18, 25, 34, 45, 60, 80
};

static_assert(OUTPUT_VOL_LEVEL_TO_PERCENT.size() == OUTPUT_VOL_LEVEL_MAX + 1,
              "volume level table size must match level range");

inline uint8_t clampVolumeLevel(int level) {
    if (level < OUTPUT_VOL_LEVEL_MIN) return OUTPUT_VOL_LEVEL_MIN;
    if (level > OUTPUT_VOL_LEVEL_MAX) return OUTPUT_VOL_LEVEL_MAX;
    return static_cast<uint8_t>(level);
}

inline uint8_t volumeLevelToPercent(uint8_t level) {
    return OUTPUT_VOL_LEVEL_TO_PERCENT[clampVolumeLevel(level)];
}

inline uint8_t stepVolumeLevelUp(uint8_t level) {
    return clampVolumeLevel(static_cast<int>(level) + 1);
}

inline uint8_t stepVolumeLevelDown(uint8_t level) {
    return clampVolumeLevel(static_cast<int>(level) - 1);
}
