#pragma once

#include <stdint.h>
#include <climits>

struct PcmVolumeDitherState {
    uint32_t value = 0x6d2b79f5UL;
};

inline uint16_t pcmVolumeNextRandom15(PcmVolumeDitherState& state) {
    state.value = state.value * 1664525UL + 1013904223UL;
    return static_cast<uint16_t>((state.value >> 16) & 0x7fffU);
}

inline int16_t scalePcm16Sample(int16_t sample, uint8_t percent, PcmVolumeDitherState& dither) {
    if (percent == 0) return 0;
    if (percent >= 100) return sample;

    const int32_t gain_q15 = (static_cast<int32_t>(percent) * 32768 + 50) / 100;
    int32_t scaled_q15 = static_cast<int32_t>(sample) * gain_q15;

    if (sample != 0) {
        const int32_t a = pcmVolumeNextRandom15(dither);
        const int32_t b = pcmVolumeNextRandom15(dither);
        scaled_q15 += (a - b);
    }

    const int32_t scaled = scaled_q15 >= 0
        ? (scaled_q15 + 16384) >> 15
        : -(((-scaled_q15) + 16384) >> 15);
    if (scaled > INT16_MAX) return INT16_MAX;
    if (scaled < INT16_MIN) return INT16_MIN;
    return static_cast<int16_t>(scaled);
}
