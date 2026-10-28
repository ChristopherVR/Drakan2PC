// Deterministic CPU regression tests for the production scheduler deadline methods.
// Run: powershell -ExecutionPolicy Bypass -File scripts/tests/run_scheduler_policy_tests.ps1
// The fixture generator imports current source instead of duplicating scheduling logic.
// Guest execution, EE timer registers, condition-variable waits and renderer callbacks
// are doubles; this is not a complete runtime integration test. CHECK stays active in Release.

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <mutex>
#include <vector>
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
Clock::time_point testClock{1s};
unsigned clockReads = 0;
Clock::time_point testNow() {
    ++clockReads;
    return testClock;
}
#define SCHEDULER_POLICY_DECLARATIONS
#include "scheduler_policy_fixture.h"
#undef SCHEDULER_POLICY_DECLARATIONS
struct FakeCv {
    unsigned waits = 0;
    std::chrono::nanoseconds oversleep{};
    template <class L, class P>
    bool wait_until(L&, Clock::time_point until, P pred) {
        if (pred())
            return true;
        if (++waits > 1000u)
            throw std::logic_error("Scheduler kept pacing future events");
        testClock = std::max(testClock, until) + oversleep;
        return false;
    }
    template <class L, class P>
    void wait(L&, P) {
        throw std::logic_error("Unexpected indefinite idle wait");
    }
    void notify_one() {}
};
struct Memory {
    uint64_t* cycle;
    uint64_t timer = UINT64_MAX;
    uint64_t cyclesUntilNextEeTimerInterrupt() const {
        return timer == UINT64_MAX ? timer : timer > *cycle ? timer - *cycle : 0;
    }
};
struct Runtime {
    Memory mem;
    Memory& memory() { return mem; }
};
class EeScheduler {
public:
    static constexpr uint64_t kEeClockHz = eeHz;
    struct ScheduledEvent {
        uint64_t deadlineCycle;
        Clock::time_point hostDeadline;
        EeEvent event;
        uint64_t sequence;
    };
    uint64_t m_eeCycle = 0, m_vsyncTick = 0, sequence = 0;
    std::mutex m_eventMutex;
    std::deque<EeEvent> m_events;
    std::vector<ScheduledEvent> m_deadlines;
    std::vector<EeEventType> fired;
    std::atomic<bool> m_stopRequested{false}, m_checkpointPending{false};
    std::atomic<uint64_t> m_nextDeadlineCycle{0};
    FakeCv m_eventCv;
    Runtime m_runtime{{&m_eeCycle}};
    void processDueDeadlines();
    void updateNextDeadline();
    void waitForEvent();
    void scheduleEvent(uint64_t cycle, Clock::time_point host, EeEvent event) {
        m_deadlines.push_back({cycle, host, event, ++sequence});
        updateNextDeadline();
    }
    void processEvent(EeEvent e) {
        fired.push_back(e.type);
        if (e.type == EeEventType::VBlankStart)
            ++m_vsyncTick;
    }
    void accountCycles(uint32_t cycles) { m_eeCycle += cycles; }
};

#define SCHEDULER_POLICY_IMPLEMENTATION
#include "scheduler_policy_fixture.h"
#undef SCHEDULER_POLICY_IMPLEMENTATION

int checks = 0;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(x)) {                                                                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #x);                                                           \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
void setup(bool host) {
    testClock = Clock::time_point(1s);
    g_hostVblank = host;
    g_nextVblankHost = Clock::time_point::max();
    g_vblankAudit = {};
}
int main() {
    for (bool host : {false, true}) {
        for (auto type :
             {EeEventType::VBlankStart, EeEventType::VBlankEnd, EeEventType::Alarm, EeEventType::DelayedCall}) {
            setup(host);
            EeScheduler s;
            s.scheduleEvent(1000000, testClock - 1us, {type});
            s.processDueDeadlines();
            const bool edge = type == EeEventType::VBlankStart || type == EeEventType::VBlankEnd;
            CHECK(s.fired.empty() == !(host && edge));
            CHECK(s.m_eeCycle == 0);
            CHECK(s.m_eventCv.waits == 0);
            if (!(host && edge))
                CHECK(s.m_nextDeadlineCycle.load() == 1000000);
        }
    }
    for (bool host : {false, true}) {
        setup(host);
        EeScheduler s;
        s.m_eeCycle = 1000;
        s.scheduleEvent(500, testClock + 10ms, {EeEventType::VBlankStart});
        s.processDueDeadlines();
        CHECK(s.m_eventCv.waits == (host ? 0u : 1u));
        CHECK(s.fired.empty() == host);
        CHECK(s.m_nextDeadlineCycle.load() == (host ? 0 : 500 + kVBlankDurationCycles));
    }
    setup(true);
    {
        EeScheduler s;
        s.scheduleEvent(100000, testClock + 10ms, {EeEventType::VBlankStart});
        s.scheduleEvent(65536, testClock, {EeEventType::DelayedCall});
        CHECK(s.m_nextDeadlineCycle.load() == 65536);
        CHECK(g_nextVblankHost == testClock + 10ms);
        s.waitForEvent();
        CHECK(s.m_eeCycle == 65536);
        s.processDueDeadlines();
        CHECK(s.fired.size() == 1);
        CHECK(s.fired[0] == EeEventType::DelayedCall);
        CHECK(s.m_nextDeadlineCycle.load() == 0);
    }
    setup(true);
    {
        EeScheduler s;
        const auto start = testClock;
        s.m_runtime.mem.timer = eeHz / 20; // 50 ms, longer than a vblank period.
        s.scheduleEvent(kVBlankPeriodCycles, start + kVBlankPeriod, {EeEventType::VBlankStart});
        unsigned iterations = 0;
        while (s.m_eeCycle < s.m_runtime.mem.timer && ++iterations < 20) {
            s.waitForEvent();
            s.processDueDeadlines();
        }
        CHECK(iterations < 20);
        CHECK(s.m_eeCycle == s.m_runtime.mem.timer);
        CHECK(testClock - start >= 50ms && testClock - start < 51ms);
        CHECK(s.m_vsyncTick == 2);
    }
    setup(true);
    {
        EeScheduler s;
        s.m_runtime.mem.timer = eeHz / 20;
        s.m_eventCv.oversleep = 1s;
        s.scheduleEvent(kVBlankPeriodCycles, testClock + kVBlankPeriod, {EeEventType::VBlankStart});
        s.waitForEvent();
        CHECK(s.m_eeCycle == s.m_runtime.mem.timer);
    }
    setup(true);
    {
        EeScheduler s;
        s.m_eventCv.oversleep = 1s;
        s.scheduleEvent(kVBlankPeriodCycles, testClock + 1ms, {EeEventType::VBlankStart});
        s.scheduleEvent(65536, testClock + 100ms, {EeEventType::DelayedCall});
        s.waitForEvent();
        CHECK(s.m_eeCycle == 65536);
    }
    setup(false);
    {
        EeScheduler s;
        s.scheduleEvent(kVBlankPeriodCycles, testClock + kVBlankPeriod, {EeEventType::VBlankStart});
        s.waitForEvent();
        CHECK(s.m_eeCycle == kVBlankPeriodCycles);
        s.processDueDeadlines();
        CHECK(s.m_vsyncTick == 1);
    }
    setup(true);
    {
        EeScheduler s;
        s.scheduleEvent(123, testClock + 1ms, {EeEventType::VBlankEnd});
        s.processDueDeadlines();
        CHECK(!hostVblankDue());
        testClock += 1ms;
        CHECK(hostVblankDue());
        s.processDueDeadlines();
        CHECK(s.m_deadlines.empty());
        CHECK(g_nextVblankHost == Clock::time_point::max());
        CHECK(s.m_nextDeadlineCycle.load() == 0);
    }
    setup(true);
    {
        EeScheduler s;
        g_vblankAudit.enabled = true;
        s.scheduleEvent(1000000, testClock - 1ms, {EeEventType::VBlankStart});
        s.processDueDeadlines();
        CHECK(g_vblankAudit.starts == 1);
        CHECK(g_vblankAudit.ends == 1);
        CHECK(g_vblankAudit.maxDelay == 1ms);
    }
    std::printf("%d scheduler deadline/idle checks passed\n", checks);
}
