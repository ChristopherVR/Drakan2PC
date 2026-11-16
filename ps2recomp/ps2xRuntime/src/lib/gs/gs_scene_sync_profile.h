#pragma once
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace drakan_pacing {
// Called on the EE producer, not the GS/VU worker. No timers when disabled.
class SceneSyncProfile {
    using Clock = std::chrono::steady_clock;
    struct Row {
        uint64_t calls = 0, pendingBefore = 0, pendingAfter = 0;
        double transfer = 0, vu = 0, maximum = 0;
    };
    std::array<Row, 6> rows{};
    // The scene model and resident geometry capture of a world's start (drakan_dlss.cpp sceneSink), timed apart.
    uint64_t captureCalls = 0;
    double captureMs = 0, captureMax = 0;
    double seconds = 0;
    Clock::time_point window{}, start{}, middle{};

public:
    SceneSyncProfile() {
        if (const char* s = std::getenv("PS2X_SCENE_PACING")) {
            const double n = std::strtod(s, nullptr);
            if (n > 0) {
                seconds = n >= 2 && n <= 60 ? n : 2;
                window = Clock::now();
            }
        }
    }
    bool enabled() const { return seconds > 0; }
    Clock::time_point captureBegin() const { return seconds ? Clock::now() : Clock::time_point{}; }
    void captureEnd(Clock::time_point begin) {
        if (!seconds)
            return;
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        ++captureCalls;
        captureMs += ms;
        if (ms > captureMax)
            captureMax = ms;
    }
    void begin() {
        if (seconds)
            start = Clock::now();
    }
    void transfersDone() {
        if (seconds)
            middle = Clock::now();
    }
    void finish(uint32_t pc, bool end, bool before, bool after) {
        if (!seconds)
            return;
        const auto now = Clock::now();
        const unsigned slot = pc == 0x139010u   ? (end ? 1 : 0)
                              : pc == 0x147690u ? 2
                              : pc == 0x1e93b8u ? 3
                              : pc == 0x1e97a0u ? 4
                                                : 5;
        auto& r = rows[slot];
        ++r.calls;
        r.pendingBefore += before;
        r.pendingAfter += after;
        r.transfer += std::chrono::duration<double, std::milli>(middle - start).count();
        r.vu += std::chrono::duration<double, std::milli>(now - middle).count();
        const double total = std::chrono::duration<double, std::milli>(now - start).count();
        if (total > r.maximum)
            r.maximum = total;
        const double elapsed = std::chrono::duration<double>(now - window).count();
        if (elapsed < seconds)
            return;
        static constexpr const char* names[] = {"phaseBegin", "phaseEnd", "endWorld", "camera", "preview", "gui"};
        std::fprintf(stderr,
                     "[scene-pacing] %.3fs calls/pending_before/pending_after/transfer_ms/vu_ms/max_ms:", elapsed);
        for (unsigned i = 0; i < rows.size(); ++i) {
            const auto& x = rows[i];
            if (x.calls)
                std::fprintf(stderr, " %s=%llu/%llu/%llu/%.3f/%.3f/%.3f", names[i], (unsigned long long)x.calls,
                             (unsigned long long)x.pendingBefore, (unsigned long long)x.pendingAfter, x.transfer, x.vu,
                             x.maximum);
        }
        if (captureCalls)
            std::fprintf(stderr, " capture=%llu/%.3f/%.3f", (unsigned long long)captureCalls, captureMs, captureMax);
        std::fputc('\n', stderr);
        rows = {};
        captureCalls = 0;
        captureMs = captureMax = 0;
        window = now;
    }
};
} // namespace drakan_pacing
