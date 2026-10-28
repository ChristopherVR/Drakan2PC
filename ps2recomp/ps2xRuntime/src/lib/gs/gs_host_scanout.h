#pragma once
#include "runtime/gs/gs_types.h"
#include <atomic>

namespace gs_cpu {
inline std::atomic<bool> g_flickerFilter{false};
inline void setFlickerFilter(bool enabled) {
    g_flickerFilter.store(enabled, std::memory_order_relaxed);
}
// Drakan captures a completed rendered framebuffer, rather than individual TV
// fields. Bobbing that image discards half its rows and changes its position
// with field parity. Only change the host request; guest registers stay intact.
inline GSPresentationRequest completedFrameScanout(GSPresentationRequest request) {
    request.smode2 &= ~uint64_t{1};
    // Match Windows' progressive presentation: libgs' two nearby reads from
    // the same framebuffer are a TV anti-flicker blur, not extra image detail.
    const auto originX = [](uint64_t fb) { return (fb >> 32) & 2047u; };
    const auto originY = [](uint64_t fb) { return int((fb >> 43) & 2047u); };
    const auto width = [](uint64_t display) { return (((display >> 32) & 4095u) + 1) / (((display >> 23) & 15u) + 1); };
    const int deltaY = originY(request.dispfb1) - originY(request.dispfb2);
    if (!g_flickerFilter.load(std::memory_order_relaxed) && (request.pmode & 3) == 3 && (request.pmode & 32) &&
        (request.dispfb1 & 0xfffffu) == (request.dispfb2 & 0xfffffu) &&
        originX(request.dispfb1) == originX(request.dispfb2) && deltaY >= -2 && deltaY <= 2 &&
        width(request.display1) == width(request.display2)) {
        request.pmode &= ~uint64_t{2};
        // Dual-CRT composition reads the actual first display source. Preserve
        // that selection when removing the blend, rather than enabling the
        // single-circuit transfer-source heuristic as an unrelated side effect.
        request.hasPreferredSource = false;
    }
    return request;
}
} // namespace gs_cpu
