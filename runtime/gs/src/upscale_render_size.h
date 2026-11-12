// Size of the rectangle the world is rendered into under a temporal upscaler (backend neutral, unit tested).
#pragma once
#include <algorithm>
#include <cstdint>

namespace gsr::upscale {

struct Size {
    uint32_t width = 0, height = 0;
};

// Even, at least 16 (the upscaler's minimum).
constexpr uint32_t evenAtLeast16(float v) {
    const uint32_t n = static_cast<uint32_t>(v + 0.5f) & ~1u;
    return n < 16u ? 16u : n;
}

// The upscaler writes outW x outH pixels (the GS frame times the internal scale) and reads a smaller render
// rectangle. ratio = render / output of the quality mode (2/3 for quality), 1 for DLAA.
//
// Plain rule: the same ratio on both axes of the output.
//
// View rule (viewW, viewH = the pixels the picture finally covers on screen, both non-zero): the GS frame is square
// (512 x 512) while the screen is not, and the internal scale is one integer chosen for the wider axis. At
// 3840 x 2160 that is scale 8, an output of 4096 x 4096 of which the present pass keeps 3840 x 2160: the vertical
// axis is rendered at 1.9 times the lines the screen has, and the plain rule renders 2730 x 2730 pixels where the
// quality mode of a 3840 x 2160 picture needs 2560 x 1440. The view rule gives each axis the ratio against what the
// screen shows of it, never more than the plain rule: min(output, view) * ratio.
constexpr Size renderSize(uint32_t outW, uint32_t outH, float ratio, bool dlaa, uint32_t viewW = 0,
                          uint32_t viewH = 0) {
    if (dlaa)
        return {outW, outH};
    const bool view = viewW && viewH;
    const uint32_t baseW = view ? std::min(outW, viewW) : outW, baseH = view ? std::min(outH, viewH) : outH;
    return {std::min(outW, evenAtLeast16(static_cast<float>(baseW) * ratio)),
            std::min(outH, evenAtLeast16(static_cast<float>(baseH) * ratio))};
}

} // namespace gsr::upscale
