#pragma once
#include <cstdint>

namespace gsr::alpha {

// AFAIL changes write destinations, not the ALPHA equation. FB_ONLY and
// RGB_ONLY still blend the fragments that failed the alpha test.
constexpr bool blendForPass(bool blendEnabled, std::uint8_t colorMask) {
    return blendEnabled && (colorMask & 7) != 0;
}

struct FailPass {
    std::uint8_t colorMask;
    bool depthWrite;
};
constexpr FailPass failurePass(unsigned afail, std::uint8_t mask, bool depthWrite) {
    switch (afail) {
        case 1:
            return {mask, false};
        case 2:
            return {0, depthWrite};
        case 3:
            return {static_cast<std::uint8_t>(mask & 7), false};
        default:
            return {0, false};
    }
}

constexpr unsigned storedAlpha(unsigned sourceAlpha, bool fba, bool framebuffer16) {
    const unsigned value = fba ? sourceAlpha | 128u : sourceAlpha;
    return framebuffer16 ? (value & 128u) : value;
}

} // namespace gsr::alpha
