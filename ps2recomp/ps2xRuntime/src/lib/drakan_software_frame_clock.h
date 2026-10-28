#pragma once
#include "drakan_frame_metrics.h"
#include <atomic>
#include <cstdint>
#include <mutex>

namespace drakan_presentation {
// Couple a completed software image with its published revision. The window
// copies that image before the producer can replace it with the next one.
inline std::mutex g_frameLatchMutex;
struct FrameKey {
    uint64_t value;
    bool completed;
};
class FrameClock {
public:
    void reset(bool enabled) {
        enabled_.store(false, std::memory_order_release);
        revision_.store(0, std::memory_order_relaxed);
        lastFlip_.store(0, std::memory_order_relaxed);
        enabled_.store(enabled, std::memory_order_release);
    }
    void commit(int64_t now) {
        if (!enabled_.load(std::memory_order_acquire))
            return;
        g_frameMetrics.frame(now);
        lastFlip_.store(now, std::memory_order_relaxed);
        revision_.fetch_add(1, std::memory_order_release);
    }
    uint64_t completedFrames() const { return revision_.load(std::memory_order_acquire); }
    FrameKey select(uint64_t vsync, int64_t now) const {
        if (enabled_.load(std::memory_order_acquire)) {
            const auto revision = revision_.load(std::memory_order_acquire);
            const auto last = lastFlip_.load(std::memory_order_relaxed);
            if (revision != 0 && now >= last && now - last <= 500000000)
                return {revision, true};
        }
        return {vsync, false};
    }

private:
    std::atomic<uint64_t> revision_{0};
    std::atomic<int64_t> lastFlip_{0};
    std::atomic<bool> enabled_{false};
};
inline FrameClock g_frameClock;
} // namespace drakan_presentation
