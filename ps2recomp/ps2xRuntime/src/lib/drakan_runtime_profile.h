#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>

// Opt-in CPU elapsed timing. Buckets can overlap across threads and nest;
// do not sum them as CPU usage. No locks, allocations or GPU operations.
namespace drakan_runtime_profile {
using Clock = std::chrono::steady_clock;
enum Id {
    GuestDispatch,
    SchedulerIdle,
    WorkerIdle,
    WorkerLock,
    Gif,
    Scene,
    Vblank,
    DisplayCommit,
    OtherRecord,
    DrainRead,
    DrainWrite,
    DrainTransfer,
    DrainShutdown,
    CommitCapacity,
    CommitRetirement,
    Handoff,
    FairWait,
    Count
};
struct Sample {
    std::atomic<unsigned long long> calls{0}, ns{0};
};
class Profile {
    std::array<Sample, Count> samples{};
    Clock::time_point begin{}; // report is called by the host thread only
public:
    const bool enabled;
    Profile()
        : enabled([] {
              const char* v = std::getenv("DRAKAN_RUNTIME_PROFILE");
              return v && std::atoi(v) > 0;
          }()) {
        if (enabled)
            begin = Clock::now();
    }
    void add(Id id, Clock::duration elapsed) {
        samples[id].ns.fetch_add(
            static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()),
            std::memory_order_relaxed);
        samples[id].calls.fetch_add(1, std::memory_order_relaxed);
    }
    void report() {
        if (!enabled)
            return;
        const auto now = Clock::now();
        const double seconds = std::chrono::duration<double>(now - begin).count();
        if (seconds < 2)
            return;
        static constexpr const char* names[] = {
            "guestDispatch", "schedulerIdle",  "workerIdle",       "workerLock", "gif",        "scene",
            "vblank",        "displayCommit",  "otherRecord",      "drainRead",  "drainWrite", "drainTransfer",
            "drainShutdown", "commitCapacity", "commitRetirement", "handoff",    "fairWait"};
        // Atomic exchanges bound storage. In-flight calls land in the next window;
        // calls/ns may straddle a report, so aggregate several steady windows.
        std::fprintf(stderr, "[runtime-profile] %.6fs calls/inclusive_ms:", seconds);
        for (unsigned i = 0; i < Count; ++i) {
            const auto calls = samples[i].calls.exchange(0, std::memory_order_relaxed);
            const auto ns = samples[i].ns.exchange(0, std::memory_order_relaxed);
            if (calls || ns)
                std::fprintf(stderr, " %s=%llu/%.3f", names[i], calls, ns * 1e-6);
        }
        std::fputc('\n', stderr);
        begin = Clock::now();
    }
};
inline Profile& profile() {
    static Profile value;
    return value;
}
class Scope {
    Profile* owner = nullptr;
    Id id;
    Clock::time_point begin{};

public:
    explicit Scope(Id bucket) : id(bucket) {
        auto& p = profile();
        if (p.enabled) {
            owner = &p;
            begin = Clock::now();
        }
    }
    ~Scope() {
        if (owner)
            owner->add(id, Clock::now() - begin);
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
} // namespace drakan_runtime_profile
