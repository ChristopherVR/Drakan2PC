// Sizes of a world frame under a temporal upscaler (backend neutral, unit tested): the rectangle the world is
// rendered into, the size the upscaler writes, and the texture LOD bias that goes with them.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gsr::upscale {

struct Size {
    uint32_t width = 0, height = 0;
    constexpr bool operator==(const Size&) const = default;
};

// Even, at least 16 (the upscaler's minimum).
constexpr uint32_t evenAtLeast16(float v) {
    const uint32_t n = static_cast<uint32_t>(v + 0.5f) & ~1u;
    return n < 16u ? 16u : n;
}

// The upscaler writes outW x outH pixels and reads a smaller render rectangle. ratio = render / output of the
// quality mode (2/3 for quality), 1 for DLAA.
//
// Plain rule: the same ratio on both axes of the output.
//
// View rule (viewW, viewH = the pixels the picture finally covers on screen, both non-zero): each axis gets the
// ratio against what the screen shows of it, never more than the plain rule: min(output, view) * ratio. plan()
// below supersedes it (it also makes the upscaler write the screen's size); this stays for a caller that has a
// fixed output.
constexpr Size renderSize(uint32_t outW, uint32_t outH, float ratio, bool dlaa, uint32_t viewW = 0,
                          uint32_t viewH = 0) {
    if (dlaa)
        return {outW, outH};
    const bool view = viewW && viewH;
    const uint32_t baseW = view ? std::min(outW, viewW) : outW, baseH = view ? std::min(outH, viewH) : outH;
    return {std::min(outW, evenAtLeast16(static_cast<float>(baseW) * ratio)),
            std::min(outH, evenAtLeast16(static_cast<float>(baseH) * ratio))};
}

// How the finished frame reaches the window: `displayW x displayH` native pixels of the frame are shown (the
// display circuit, starting at the frame's top-left corner) and cover `windowW x windowH` window pixels. All zero
// when that is not known or the presenter cannot show a window-sized picture unscaled (see Plan::windowSized).
struct View {
    uint32_t displayW = 0, displayH = 0, windowW = 0, windowH = 0;
    constexpr bool known() const { return displayW && displayH && windowW && windowH; }
};

struct Plan {
    Size target;  // frame * internal scale: the region of the world target the result is written back to
    Size resolve; // what the upscaler writes
    Size render;  // what the upscaler reads: the rectangle the world is rendered into
    // resolve has exactly one pixel per window pixel of the frame, on both axes: the presenter can show it unscaled.
    // False: resolve is the target and the presenter scales it as it scales any frame.
    bool windowSized = false;
};

// The GS frame is square (512 x 512) while the window is not, and the internal scale is one integer chosen for the
// wider axis: at 3840 x 2160 that is scale 8, a 4096 x 4096 target of which the window shows 3840 x 2160. An
// upscaler that writes the target does 2.02 times the work the window can show (16.8 against 8.3 megapixels), and
// its "quality" input of 2730 x 2730 is 2.02 times the 2560 x 1440 that quality mode means at that window.
//
// Rule: the upscaler writes the window pixels the frame covers, when that is a whole number of pixels on both axes
// and not more than the target on either; the render rectangle is the mode's ratio of that. Otherwise (unknown
// view, a window larger than the target, a fractional mapping) it writes the target, as before. DLAA keeps the
// target on purpose: it is the mode that anti-aliases the full internal resolution, and the presenter's downscale
// of it is supersampling the player chose.
constexpr Plan plan(uint32_t frameW, uint32_t frameH, uint32_t scale, float ratio, bool dlaa, View view = {}) {
    Plan p;
    p.target = {frameW * scale, frameH * scale};
    p.resolve = p.target;
    if (!dlaa && view.known()) {
        const uint64_t w = uint64_t(frameW) * view.windowW, h = uint64_t(frameH) * view.windowH;
        const bool whole = w % view.displayW == 0 && h % view.displayH == 0;
        const uint64_t windowW = w / view.displayW, windowH = h / view.displayH;
        if (whole && windowW >= 16 && windowH >= 16 && windowW <= p.target.width && windowH <= p.target.height &&
            (windowW < p.target.width || windowH < p.target.height)) {
            p.resolve = {static_cast<uint32_t>(windowW), static_cast<uint32_t>(windowH)};
            p.windowSized = true;
        }
    }
    p.render = dlaa ? p.resolve
                    : Size{std::min(p.resolve.width, evenAtLeast16(static_cast<float>(p.resolve.width) * ratio)),
                           std::min(p.resolve.height, evenAtLeast16(static_cast<float>(p.resolve.height) * ratio))};
    return p;
}

// Texture LOD bias of the world draws, added to a LOD measured in render pixels: log2(render / resolve), the
// upscaler vendors' rule (textures are chosen for the resolution the picture is shown at, not the one it is
// rendered at). The wider of the two axis ratios, so neither axis is sharper than its output resolution allows.
inline float lodBias(const Plan& p) {
    if (!p.render.width || !p.render.height || !p.resolve.width || !p.resolve.height)
        return 0.0f;
    const float x = static_cast<float>(p.render.width) / static_cast<float>(p.resolve.width),
                y = static_cast<float>(p.render.height) / static_cast<float>(p.resolve.height);
    return std::log2(std::min(1.0f, std::max(x, y)));
}

// The resolve pixel a target pixel shows when the result is written back to the target without filtering (nearest):
// both the write-back and the presenter's test for untouched world pixels use this one mapping.
constexpr uint32_t resolveIndex(uint32_t targetIndex, uint32_t targetSize, uint32_t resolveSize) {
    return targetSize ? std::min(resolveSize ? resolveSize - 1 : 0u,
                                 static_cast<uint32_t>((uint64_t(targetIndex) * 2 + 1) * resolveSize /
                                                       (uint64_t(targetSize) * 2)))
                      : 0u;
}

} // namespace gsr::upscale
