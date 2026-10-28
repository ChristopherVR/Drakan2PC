#pragma once
#include <atomic>

// Refresh period of the display while presentation is vsync-locked, 0 otherwise. Written by the GS bridge's host loop
// (gs_gsr_bridge.cpp), read by the engine tick hook (drakan_frame_rate.h).
namespace drakan_present_clock {
inline std::atomic<double> g_lockedRefresh{0.0};
inline double lockedRefreshSeconds() {
    return g_lockedRefresh.load(std::memory_order_relaxed);
}
} // namespace drakan_present_clock
