// Diagnostics that run beside PS2Runtime::run()'s host loop: a stall watchdog and a sampling pc profiler. Both only
// read guest state (live pc samples, the scheduler snapshot, guest RAM); neither pauses or changes the guest.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

class EeScheduler;
class PS2Memory;

// PS2X_PROFILE=<seconds>: samples the live guest pc about every 0.5 ms on its own thread and prints the 25 hottest
// addresses when it stops (needs the code generated with always_write_pc = true for instruction-level precision).
class PcProfiler {
public:
    // Starts the sampling thread when PS2X_PROFILE is set; otherwise does nothing.
    void startFromEnvironment(const EeScheduler& scheduler);
    // Stops sampling early (if still running), prints the result and joins the thread.
    void stop();
    ~PcProfiler() { stop(); }

private:
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
};

// Polled once per host frame. Three jobs:
// * Always on: when the guest's per-frame input boundary (drakan_present_clock::g_guestHeartbeat) stops advancing for
//   8 s, dump the live pc, threads and semaphores (once per stall, at most 6 times a run), so a freeze leaves a trace
//   in drakan.log (docs/journal/2026-11-01/quick-save-freeze.md). PS2X_WATCHDOG_DUMP_DIR=<absolute dir> also writes
//   one RAM + register snapshot on the first stall.
// * PS2X_WATCHDOG=<seconds>: the same dump periodically. PS2X_PEEK=<hex,...> adds guest words to each dump.
// * PS2X_MAX_SECONDS=<n>: poll() returns false after n seconds, so the caller stops the run.
class StallWatchdog {
public:
    using HistoryFn = std::string (*)(); // the recent dispatch pcs, formatted
    StallWatchdog(const EeScheduler& scheduler, PS2Memory& memory, HistoryFn dispatchHistory);

    // False once PS2X_MAX_SECONDS has passed.
    bool poll();
    // Seconds since the watchdog was created (the start of the run).
    double secondsSinceStart() const;

private:
    using Clock = std::chrono::steady_clock;
    bool heartbeatStalled(Clock::time_point now);
    void dump(Clock::time_point now, bool stall);
    void writeForensicSnapshot(int liveThread);
    void printPeekedWords();

    const EeScheduler& m_scheduler;
    PS2Memory& m_memory;
    HistoryFn m_dispatchHistory;

    double m_periodSeconds = 0.0;
    double m_maxSeconds = 0.0;
    Clock::time_point m_start;
    Clock::time_point m_lastDump;

    uint64_t m_lastBeat = 0;
    Clock::time_point m_beatSince;
    bool m_stallReported = false;
    int m_stallDumps = 0;
    bool m_snapshotAttempted = false;
};
