#pragma once

#include <algorithm>
#include <cstdint>

namespace drakan_audio_mix {

constexpr bool isArokhCall(uint32_t id) {
    if ((id >> 16) != 2) // SYSTEM.SDB
        return false;
    const uint32_t sample = id & 0xffffu;
    return (sample >= 0x4e && sample <= 0x50) || sample == 0x85 || (sample >= 0x18c && sample <= 0x190) ||
           sample == 0x1ed || (sample >= 0x36c && sample <= 0x372);
}

// Give Rynn's call and her response when a summon is refused a 6 dB lift with room for the rest of the mix.
constexpr float arokhCallGain(float gain) {
    return gain > 0.0f && gain < 1.0f ? std::min(gain * 2.0f, 1.0f) : gain;
}

void setVolumes(int voices, int effects);
} // namespace drakan_audio_mix
