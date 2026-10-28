#pragma once
// Drakan perf instrumentation (see docs/reference/perf.md).
//   PS2X_PERF=1          once a second on stderr: guest flips/s, vblanks/s, EE (game) thread CPU ms per flip, other
//   threads' CPU. PS2X_HOSTPROF=<s>    sample the host RIP of every thread for <s> seconds (starts after
//   PS2X_HOSTPROF_DELAY seconds),
//                        write hostprof.csv (resolve with scripts/perf/hostprof.py and the linker .map).
#include <atomic>
#include <cstdint>

namespace ps2perf {
extern std::atomic<uint64_t> g_flips; // DISPFB changes (guest display flips)
extern std::atomic<uint64_t> g_gifPackets, g_gifBytes;
extern std::atomic<uint64_t> g_vu1Cycles, g_vu1Runs, g_vu1Pairs;

extern std::atomic<uint64_t> g_flips2;
inline void onDispfb(uint64_t oldValue, uint64_t newValue) {
    if (oldValue != newValue)
        g_flips.fetch_add(1, std::memory_order_relaxed);
}
inline void onDispfb2(uint64_t oldValue, uint64_t newValue) {
    if (oldValue != newValue)
        g_flips2.fetch_add(1, std::memory_order_relaxed);
}

// Starts the monitor threads according to the environment; vsync is read through the callback.
void start(void* gameThreadNativeHandle, uint64_t (*vsyncTick)(void*), void* vsyncUser);
void stop();
} // namespace ps2perf
