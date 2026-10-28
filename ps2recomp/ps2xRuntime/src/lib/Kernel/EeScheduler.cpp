#include "runtime/ee_scheduler.h"
#include "runtime/gs/gs_capture.h"

#include "ps2_log.h"
#include "ps2_runtime_macros.h"
#include "../drakan_config.h"
#include "../drakan_host.h"
#include "../drakan_runtime_profile.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
constexpr int KE_OK = 0;
constexpr int KE_ERROR = -1;
constexpr int KE_ILLEGAL_PRIORITY = -403;
constexpr int KE_ILLEGAL_THID = -406;
constexpr int KE_UNKNOWN_THID = -407;
constexpr int KE_UNKNOWN_SEMID = -408;
constexpr int KE_UNKNOWN_EVFID = -409;
constexpr int KE_DORMANT = -413;
constexpr int KE_NOT_DORMANT = -414;
constexpr int KE_NOT_SUSPEND = -415;
constexpr int KE_NOT_WAIT = -416;
constexpr int KE_RELEASE_WAIT = -418;
constexpr int KE_SEMA_ZERO = -419;
constexpr int KE_SEMA_OVF = -420;
constexpr int KE_EVF_COND = -421;
constexpr int KE_WAIT_DELETE = -425;

constexpr uint32_t WEF_OR = 0x01u;
constexpr uint32_t WEF_CLEAR = 0x10u;
constexpr uint32_t WEF_CLEAR_ALL = 0x20u;
constexpr auto kVBlankPeriod = std::chrono::microseconds(16667);
constexpr auto kVBlankDuration = std::chrono::microseconds(500);
constexpr uint64_t kAlarmTickMicroseconds = 64u;
constexpr uint32_t kDebugPublishDispatchInterval = 4096u;

constexpr uint64_t microsecondsToEeCycles(uint64_t microseconds) {
    return (microseconds * EeScheduler::kEeClockHz + 999999ull) / 1000000ull;
}

std::chrono::nanoseconds eeCyclesToHostDuration(uint64_t cycles) {
    constexpr uint64_t kNanosecondsPerSecond = 1000000000ull;
    const uint64_t wholeSeconds = cycles / EeScheduler::kEeClockHz;
    const uint64_t remainingCycles = cycles % EeScheduler::kEeClockHz;
    const uint64_t remainingNanoseconds =
        (remainingCycles * kNanosecondsPerSecond + EeScheduler::kEeClockHz - 1u) / EeScheduler::kEeClockHz;
    return std::chrono::seconds(wholeSeconds) + std::chrono::nanoseconds(remainingNanoseconds);
}

constexpr uint64_t kVBlankPeriodCycles = microsecondsToEeCycles(16667u);
constexpr uint64_t kVBlankDurationCycles = microsecondsToEeCycles(500u);
constexpr uint64_t kAlarmTickCycles = microsecondsToEeCycles(kAlarmTickMicroseconds);

// Executor-local state keeps this opt-in policy out of the public runtime
// header. Guest cycle deadlines still govern alarms and delayed I/O.
thread_local bool g_hostVblank = false;
thread_local auto g_nextVblankHost = std::chrono::steady_clock::time_point::max();
thread_local uint64_t g_nextVblankPollCycle = 0u;
struct VblankAudit {
    bool enabled = false, trace = false;
    uint64_t starts = 0, ends = 0, lastCycle = 0;
    std::chrono::steady_clock::time_point begin{};
    std::chrono::steady_clock::duration maxDelay{};
};
thread_local VblankAudit g_vblankAudit;

bool hostVblank(EeEventType type) {
    return g_hostVblank && (type == EeEventType::VBlankStart || type == EeEventType::VBlankEnd);
}

bool hostVblankDue() {
    return g_hostVblank && std::chrono::steady_clock::now() >= g_nextVblankHost;
}

void reportVblankAudit(uint64_t cycle) {
    auto& audit = g_vblankAudit;
    if (!audit.enabled)
        return;
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - audit.begin).count();
    if (seconds < 2.0)
        return;
    std::fprintf(stderr,
                 "[sched-vblank] seconds=%.6f hostPolicy=%d starts=%llu ends=%llu maxDelayMs=%.3f eeCycles=%llu\n",
                 seconds, g_hostVblank ? 1 : 0, (unsigned long long)audit.starts, (unsigned long long)audit.ends,
                 std::chrono::duration<double, std::milli>(audit.maxDelay).count(),
                 (unsigned long long)(cycle - audit.lastCycle));
    audit.starts = audit.ends = 0;
    audit.maxDelay = {};
    audit.lastCycle = cycle;
    audit.begin = now;
}

template <typename Map>
int allocatePositiveId(int& nextId, const Map& objects) {
    const int first = std::max(1, nextId);
    int candidate = first;
    do {
        if (!objects.contains(candidate)) {
            nextId = (candidate == std::numeric_limits<int>::max()) ? 1 : candidate + 1;
            return candidate;
        }
        candidate = (candidate == std::numeric_limits<int>::max()) ? 1 : candidate + 1;
    } while (candidate != first);
    return 0;
}
} // namespace

// Set by checkpointDue() whenever it asks the running guest code to unwind (preemption / due event / stop) and cleared
// right before the scheduler (re)enters guest code. PS2Runtime::dispatchGuestBranch() and the generated fast-call
// macros use it to tell "callee returned" from "callee was unwound by a checkpoint" (see the comment in
// dispatchGuestBranch).
bool g_ps2Unwinding = false;
static const bool g_traceSched = std::getenv("PS2X_TRACE_SCHED") != nullptr;

void ps2TraceRingFlush(const char* why);
void ps2WatchFlush();

EeScheduler::EeScheduler(PS2Runtime& runtime) : m_runtime(runtime) {}

EeScheduler::~EeScheduler() {
    requestStop();
}

void EeScheduler::reset(uint8_t* rdram, const R5900Context& mainContext) {
    m_executorThread = std::this_thread::get_id();
    g_hostVblank = false;
    g_nextVblankHost = std::chrono::steady_clock::time_point::max();
    g_nextVblankPollCycle = 0u;
    m_rdram = rdram;
    m_readyQueues = {};
    m_threads.clear();
    m_semaphores.clear();
    m_eventFlags.clear();
    m_alarms.clear();
    m_intcHandlers.clear();
    m_dmacHandlers.clear();
    m_nextThreadId = kFirstThreadId;
    m_nextInvocationThreadId = -1;
    m_nextSemaphoreId = 1;
    m_nextEventFlagId = 1;
    m_nextAlarmId = 1;
    m_nextIntcHandlerId = 1;
    m_nextDmacHandlerId = 1;
    m_intcHeadOrder = 0;
    m_intcTailOrder = 1000;
    m_dmacHeadOrder = 0;
    m_dmacTailOrder = 1000;
    m_enabledIntcMask = 0xFFFFFFFFu;
    m_enabledDmacMask = 0xFFFFFFFFu;
    m_currentThreadId = 0;
    m_rescheduleRequested = false;
    m_timeSliceExpired = false;
    m_insideInterrupt = false;
    m_pendingEeTimerInterrupts = 0u;
    m_eeCycle = 0u;
    m_sliceEndCycle = kDefaultTimeSliceCycles;
    m_stopRequested.store(false, std::memory_order_release);
    m_checkpointPending.store(false, std::memory_order_release);
    m_debugPublishCountdown = 0u;
    {
        std::lock_guard lock(m_eventMutex);
        m_events.clear();
        m_deadlines.clear();
        m_pendingInvocations.clear();
    }
    m_eventSequence = 0;
    m_invocationSequence = 0;
    m_vsyncTick = 0;
    m_vsyncFlagAddress = 0;
    m_vsyncTickAddress = 0;
    m_gsVSyncCallback = 0;
    m_gsVSyncCallbackGp = 0;
    m_gsVSyncCallbackSp = 0;
    m_runtime.memory().gs().vsyncTick.store(0u, std::memory_order_release);
    m_runtime.memory().resetEeTimers();

    GuestThread main{};
    main.id = kMainThreadId;
    main.context = mainContext;
    main.entry = mainContext.pc;
    // $sp is live execution state, not the stable initial stack descriptor
    // returned by ReferThreadStatus. SetupThread records that metadata.
    main.stack = 0u;
    main.gp = getRegU32(&mainContext, 28);
    main.initialPriority = 0;
    main.currentPriority = 0;
    main.status = EeThreadStatus::Ready;
    m_threads.emplace(main.id, std::move(main));
    m_readyQueues[0].push_back(kMainThreadId);
    scheduleEvent(m_eeCycle + kVBlankPeriodCycles, std::chrono::steady_clock::now() + kVBlankPeriod,
                  EeEvent{EeEventType::VBlankStart, 0, 0});
    publishSnapshot();
}

void EeScheduler::run() {
    assertExecutor();
    // Initialise the executor-local policy and its cached deadline here.
    const auto worldRate = drakan_cfg::getString("video", "world_rate", "DRAKAN_WORLD_RATE", nullptr);
    g_hostVblank = drakan_host::asciiEqual(worldRate, "uncapped");
    g_nextVblankPollCycle = 0u;
    g_vblankAudit = {};
    const char* frameAudit = std::getenv("DRAKAN_FRAME_AUDIT");
    g_vblankAudit.enabled = frameAudit && std::atoi(frameAudit) != 0;
    g_vblankAudit.trace = frameAudit && std::atoi(frameAudit) >= 2;
    if (g_vblankAudit.enabled) {
        g_vblankAudit.begin = std::chrono::steady_clock::now();
        g_vblankAudit.lastCycle = m_eeCycle;
        std::fprintf(stderr, "[sched-vblank] executor policy=%s world_rate=%s periodUs=16667 pollCycles=1024\n",
                     g_hostVblank ? "host" : "cycle", worldRate.c_str());
    }
    {
        std::lock_guard lock(m_eventMutex);
        updateNextDeadline();
    }
    m_running.store(true, std::memory_order_release);

    // The EE FPU has no denormals: an operand with exponent 0 is zero and a result below the smallest normal number is
    // flushed to zero. MXCSR DAZ (bit 6) + FTZ (bit 15) give the host SSE operations exactly that behaviour on the
    // thread that runs all guest code (PS2X_FTZ=0 disables it for A/B tests).
#if !defined(__EMSCRIPTEN__)
    {
        const char* ftz = std::getenv("PS2X_FTZ");
        if (!(ftz && ftz[0] == '0'))
            _mm_setcsr(_mm_getcsr() | 0x8040u);
    }
#endif

    while (!m_stopRequested.load(std::memory_order_acquire)) {
        processPendingEvents();
        if (m_stopRequested.load(std::memory_order_acquire)) {
            break;
        }

        if (m_currentThreadId == 0) {
            GuestThread* next = selectReady();
            if (!next && m_pendingInvocations.empty()) {
                copyMainContextToRuntime();
                publishIdleDebugContext();
                publishSnapshot();
                {
                    drakan_runtime_profile::Scope profile(drakan_runtime_profile::SchedulerIdle);
                    waitForEvent();
                }
                continue;
            }
            if (next) {
                makeRunning(*next);
            } else {
                GuestThread* owner = &acquireInvocationThread();
                GuestInvocation invocation = std::move(m_pendingInvocations.front());
                m_pendingInvocations.pop_front();
                owner->status = EeThreadStatus::Running;
                m_currentThreadId = owner->id;
                renewTimeSlice();
                if (getRegU32(&invocation.context, 29) == 0u) {
                    SET_GPR_U32(&invocation.context, 29, invocationStackTop());
                }
                owner->invocations.push_back(std::move(invocation));
            }
        }

        GuestThread* running = currentThread();
        assert(running != nullptr);
        if (running->resumeCompletion) {
            auto completion = std::move(running->resumeCompletion);
            running->resumeCompletion = {};
            try {
                completion(running->activeContext());
            } catch (const EeDispatcherTransfer&) {
            }
            if (m_currentThreadId == 0) {
                continue;
            }
        }
        R5900Context& context = running->activeContext();
        if (m_debugPublishCountdown == 0u) {
            copyMainContextToRuntime();
            publishSnapshot();
            m_debugPublishCountdown = kDebugPublishDispatchInterval - 1u;
        } else {
            --m_debugPublishCountdown;
        }

        publishDebugContext(context);

        if (context.pc == 0u) {
            if (!running->invocations.empty()) {
                GuestInvocation completed = std::move(running->invocations.back());
                running->invocations.pop_back();
                if (completed.onComplete) {
                    try {
                        completed.onComplete(completed.context, running->activeContext());
                    } catch (const EeDispatcherTransfer&) {
                    }
                }
                continue;
            }
            if (running->id == kMainThreadId && std::getenv("PS2X_TRACE_RING")) {
                ps2TraceRingFlush("main thread returned to pc 0");
                ps2WatchFlush();
            }
            makeDormant(*running);
            m_currentThreadId = 0;
            copyMainContextToRuntime();
            publishIdleDebugContext();
            publishSnapshot();
            continue;
        }

        if (!m_pendingInvocations.empty() && running->invocations.empty()) {
            GuestInvocation invocation = std::move(m_pendingInvocations.front());
            m_pendingInvocations.pop_front();
            if (getRegU32(&invocation.context, 29) == 0u) {
                SET_GPR_U32(&invocation.context, 29, invocationStackTop());
            }
            running->invocations.push_back(std::move(invocation));
            continue;
        }

        if (!m_runtime.hasFunction(context.pc)) {
            if (!running->invocations.empty()) {
                context.pc = 0u;
            } else {
                std::fprintf(stderr,
                             "[sched] MISSING FUNCTION on tid=%d prio=%d status=%d pc=0x%x ra=0x%x sp=0x%x inv=%zu "
                             "suspend=%d wakeup=%u\n",
                             m_currentThreadId, running->currentPriority, (int)running->status, context.pc,
                             getRegU32(&context, 31), getRegU32(&context, 29), running->invocations.size(),
                             running->suspendCount, (unsigned)running->wakeupCount);
                m_runtime.reportMissingFunction(m_rdram, &context, context.pc, context.pc,
                                                PS2Runtime::GuestBranchKind::DirectJump, "EE scheduler");
                makeDormant(*running);
                m_currentThreadId = 0;
            }
            continue;
        }
        PS2Runtime::RecompiledFunction function = m_runtime.lookupFunction(context.pc);

        if (checkpointDue(kGuestDispatchCycles)) {
            continue;
        }

        try {
            m_insideInterrupt =
                !running->invocations.empty() && running->invocations.back().kind == GuestInvocationKind::Interrupt;
            g_ps2Unwinding = false;
            m_guestExecuting.store(true, std::memory_order_release);
            m_liveContext.store(&context, std::memory_order_relaxed);
            m_liveThreadId.store(m_currentThreadId, std::memory_order_relaxed);
            {
                drakan_runtime_profile::Scope profile(drakan_runtime_profile::GuestDispatch);
                function(m_rdram, &context, &m_runtime);
            }
            m_guestExecuting.store(false, std::memory_order_release);
            m_insideInterrupt = false;
        } catch (const EeDispatcherTransfer&) {
            m_guestExecuting.store(false, std::memory_order_release);
            m_insideInterrupt = false;
        } catch (...) {
            m_guestExecuting.store(false, std::memory_order_release);
            m_running.store(false, std::memory_order_release);
            publishSnapshot();
            throw;
        }

        processPendingEvents();
        if (m_rescheduleRequested && m_currentThreadId != 0) {
            GuestThread* preempted = currentThread();
            assert(preempted != nullptr);
            if (g_traceSched)
                std::fprintf(
                    stderr, "[sched] cyc=%llu preempt tid=%d pc=0x%x ra=0x%x sp=0x%x s0=0x%x inv=%zu slice=%d\n",
                    (unsigned long long)m_eeCycle, preempted->id, preempted->activeContext().pc,
                    getRegU32(&preempted->activeContext(), 31), getRegU32(&preempted->activeContext(), 29),
                    getRegU32(&preempted->activeContext(), 16), preempted->invocations.size(), (int)m_timeSliceExpired);
            enqueueReady(*preempted, !m_timeSliceExpired);
            m_currentThreadId = 0;
            m_rescheduleRequested = false;
            m_timeSliceExpired = false;
        }
    }

    m_guestExecuting.store(false, std::memory_order_release);
    m_running.store(false, std::memory_order_release);
    copyMainContextToRuntime();
    publishSnapshot();
}

void EeScheduler::requestStop() {
    m_stopRequested.store(true, std::memory_order_release);
    m_checkpointPending.store(true, std::memory_order_release);
    m_eventCv.notify_all();
}

void EeScheduler::postEvent(EeEvent event) {
    if (event.type == EeEventType::Stop) {
        requestStop();
        return;
    }

    {
        std::lock_guard lock(m_eventMutex);
        m_events.push_back(event);
        m_checkpointPending.store(true, std::memory_order_release);
    }
    m_eventCv.notify_one();
}

// Drakan input hooks run short guest functions to completion from inside a replaced function; while such a call is
// active the calling host thread must not be preempted (the callee would unwind with a resume pc that nobody
// continues).
thread_local int g_drakanNoPreempt = 0;
void ps2xNoPreemptEnter() noexcept {
    ++g_drakanNoPreempt;
}
void ps2xNoPreemptLeave() noexcept {
    --g_drakanNoPreempt;
}

bool EeScheduler::checkpointDue(uint32_t cycles) noexcept {
    accountCycles(cycles);
    if (g_drakanNoPreempt > 0) {
        return false;
    }

    // EE interrupts disabled (DI / Status.EIE clear): the game uses DI..EI as its critical sections against the SIF/RPC
    // server threads and interrupt handlers, so neither events, interrupt handlers nor thread preemption may run in
    // there. Bounded (about 13 ms of EE time) so a section that waits for an interrupt cannot hang the emulator.
    {
        static uint64_t s_maskedSince = 0;
        const GuestThread* cur = currentThread();
        if (cur != nullptr && (cur->activeContext().cop0_status & 0x10000u) == 0u) {
            if (s_maskedSince == 0u) {
                s_maskedSince = m_eeCycle;
            }
            if (m_eeCycle - s_maskedSince < 4000000ull && !m_stopRequested.load(std::memory_order_acquire)) {
                return false;
            }
        } else {
            s_maskedSince = 0u;
        }
    }

    // PS2X_FUZZ_UNWIND=<N>: unwind the host stack at about one of N safe points chosen at random (stability testing of
    // the resume machinery: every function must continue correctly from any call / back-edge; a wrong or missing resume
    // label shows up as [bad-resume] or a crash).
    static const uint32_t s_fuzzEvery = []() {
        const char* e = std::getenv("PS2X_FUZZ_UNWIND");
        return e ? static_cast<uint32_t>(std::strtoul(e, nullptr, 10)) : 0u;
    }();
    if (s_fuzzEvery != 0u) {
        static uint64_t s_fuzzState = 0x9E3779B97F4A7C15ull;
        s_fuzzState ^= s_fuzzState << 13;
        s_fuzzState ^= s_fuzzState >> 7;
        s_fuzzState ^= s_fuzzState << 17;
        if ((s_fuzzState >> 20) % s_fuzzEvery == 0u) {
            m_checkpointPending.store(true, std::memory_order_release);
        }
    }

    // Avoid a host-clock read at every generated checkpoint. Dispatch/event
    // boundaries still check without throttling; long guest functions poll at
    // most every 1024 accounted cycles, independently of the vblank cycle gate.
    bool vblankDue = false;
    if (g_hostVblank && m_eeCycle >= g_nextVblankPollCycle) {
        g_nextVblankPollCycle = m_eeCycle + 1024u;
        vblankDue = hostVblankDue();
    }
    if (vblankDue || m_checkpointPending.load(std::memory_order_acquire) ||
        m_stopRequested.load(std::memory_order_acquire)) {
        g_ps2Unwinding = true;
        return true;
    }

    const uint64_t nextEventCycle = m_nextDeadlineCycle.load(std::memory_order_acquire);
    if (nextEventCycle != 0u && m_eeCycle >= nextEventCycle) {
        m_checkpointPending.store(true, std::memory_order_release);
        g_ps2Unwinding = true;
        return true;
    }

    if (m_eeCycle < m_sliceEndCycle) {
        return false;
    }

    const GuestThread* running = currentThread();
    if (running != nullptr && hasReadyAtOrAbovePriority(running->currentPriority)) {
        m_rescheduleRequested = true;
        m_timeSliceExpired = true;
        g_ps2Unwinding = true;
        return true;
    }

    renewTimeSlice();
    return false;
}

void EeScheduler::accountCycles(uint32_t cycles) noexcept {
    const uint64_t elapsed = std::max<uint64_t>(1u, cycles);
    m_eeCycle += elapsed;
    m_pendingEeTimerInterrupts |= m_runtime.memory().advanceEeTimers(elapsed);
    m_runtime.advanceIopEeCycles(elapsed);
    if (m_pendingEeTimerInterrupts != 0u) {
        m_checkpointPending.store(true, std::memory_order_release);
    }
}

bool EeScheduler::sampleLive(uint32_t& pc, uint32_t& ra, uint32_t& sp, int& tid) const noexcept {
    const R5900Context* c = m_liveContext.load(std::memory_order_relaxed);
    if (!c)
        return false;
    pc = c->pc;
    ra = static_cast<uint32_t>(_mm_extract_epi32(c->r[31], 0));
    sp = static_cast<uint32_t>(_mm_extract_epi32(c->r[29], 0));
    tid = m_liveThreadId.load(std::memory_order_relaxed);
    return true;
}

bool EeScheduler::currentContextIsInvocation() const noexcept {
    const GuestThread* t = currentThread();
    return t != nullptr && !t->invocations.empty();
}

bool EeScheduler::isExecutingGuest() const noexcept {
    return m_guestExecuting.load(std::memory_order_acquire);
}

void EeScheduler::setupCurrentThread(uint32_t stack, uint32_t stackSize, uint32_t gp) {
    assertExecutor();
    GuestThread* target = currentThread();
    if (!target) {
        return;
    }

    target->stack = stack;
    target->stackSize = stackSize;
    target->gp = gp;
    publishSnapshot();
}

int EeScheduler::createThread(const EeThreadCreateParams& params) {
    assertExecutor();
    if (params.priority < 1 || params.priority >= kPriorityCount) {
        return KE_ILLEGAL_PRIORITY;
    }

    const int id = allocateThreadId();
    if (id == 0) {
        return KE_ERROR;
    }

    GuestThread thread{};
    thread.id = id;
    thread.entry = params.entry;
    thread.stack = params.stack;
    thread.stackSize = params.stackSize;
    thread.gp = params.gp;
    thread.attr = params.attr;
    thread.option = params.option;
    thread.initialPriority = params.priority;
    thread.currentPriority = params.priority;
    thread.status = EeThreadStatus::Dormant;
    if (g_traceSched)
        std::fprintf(stderr,
                     "[sched] cyc=%llu CreateThread tid=%d prio=%d entry=0x%x attr=0x%x stack=0x%x size=0x%x gp=0x%x\n",
                     (unsigned long long)m_eeCycle, id, params.priority, params.entry, params.attr, params.stack,
                     params.stackSize, params.gp);
    m_threads.emplace(id, std::move(thread));
    publishSnapshot();
    return id;
}

int EeScheduler::deleteThread(int id, uint32_t& ownedStack) {
    assertExecutor();
    ownedStack = 0;
    if (id <= kMainThreadId) {
        return KE_ILLEGAL_THID;
    }
    auto it = m_threads.find(id);
    if (it == m_threads.end()) {
        return KE_UNKNOWN_THID;
    }
    if (it->second.status != EeThreadStatus::Dormant) {
        return KE_NOT_DORMANT;
    }
    if (it->second.ownsStack) {
        ownedStack = it->second.stack;
    }
    m_threads.erase(it);
    publishSnapshot();
    return KE_OK;
}

int EeScheduler::startThread(int id, uint32_t arg, const R5900Context& caller, bool interruptSafe) {
    assertExecutor();
    GuestThread* target = thread(id);
    if (!target) {
        return KE_UNKNOWN_THID;
    }
    if (target->status != EeThreadStatus::Dormant) {
        return KE_NOT_DORMANT;
    }

    target->context = R5900Context{};
    target->context.pc = target->entry;
    target->arg = arg;
    target->suspendCount = 0;
    target->wakeupCount = 0;
    target->wait = {};
    SET_GPR_U32(&target->context, 4, arg);
    SET_GPR_U32(&target->context, 28, target->gp != 0u ? target->gp : getRegU32(&caller, 28));
    const uint32_t stackTop =
        target->stack != 0u ? (target->stack + target->stackSize) & ~0xFu : getRegU32(&caller, 29);
    SET_GPR_U32(&target->context, 29, stackTop);
    SET_GPR_U32(&target->context, 31, 0u);
    enqueueReady(*target);
    requestPreemptionIfHigher(*target, interruptSafe);
    publishSnapshot();
    return KE_OK;
}

[[noreturn]] void EeScheduler::exitCurrent(bool deleteThreadRecord) {
    assertExecutor();
    GuestThread* exiting = currentThread();
    assert(exiting != nullptr);
    const int id = exiting->id;
    const uint32_t ownedStack = deleteThreadRecord && exiting->ownsStack ? exiting->stack : 0u;
    if (id == kMainThreadId && std::getenv("PS2X_TRACE_RING"))
        ps2TraceRingFlush("main thread exited via ExitThread/exit");
    makeDormant(*exiting);
    m_currentThreadId = 0;
    if (deleteThreadRecord && id != kMainThreadId) {
        m_threads.erase(id);
    }
    if (ownedStack != 0u) {
        m_runtime.guestFree(ownedStack);
    }
    publishSnapshot();
    throw EeDispatcherTransfer{};
}

int EeScheduler::terminateThread(int id, uint32_t& ownedStack, bool interruptSafe) {
    assertExecutor();
    ownedStack = 0;
    if (id == 0 || id == m_currentThreadId) {
        return KE_ILLEGAL_THID;
    }
    GuestThread* target = thread(id);
    if (!target) {
        return KE_UNKNOWN_THID;
    }
    if (target->status == EeThreadStatus::Dormant) {
        return KE_DORMANT;
    }
    if (target->ownsStack) {
        ownedStack = target->stack;
        target->ownsStack = false;
    }
    makeDormant(*target);
    (void)interruptSafe;
    publishSnapshot();
    return KE_OK;
}

int EeScheduler::suspendThread(int id, bool interruptSafe) {
    assertExecutor();
    if (id == 0) {
        id = m_currentThreadId;
    }
    GuestThread* target = thread(id);
    if (!target) {
        return KE_UNKNOWN_THID;
    }
    if (target->status == EeThreadStatus::Dormant) {
        return KE_DORMANT;
    }

    ++target->suspendCount;
    switch (target->status) {
        case EeThreadStatus::Running:
            target->status = EeThreadStatus::Suspended;
            m_currentThreadId = 0;
            m_rescheduleRequested = true;
            break;
        case EeThreadStatus::Ready:
            removeReady(*target);
            target->status = EeThreadStatus::Suspended;
            break;
        case EeThreadStatus::Waiting:
            target->status = EeThreadStatus::WaitingSuspended;
            break;
        case EeThreadStatus::WaitingSuspended:
        case EeThreadStatus::Suspended:
            break;
        case EeThreadStatus::Dormant:
            break;
    }
    if (interruptSafe && m_insideInterrupt) {
        m_rescheduleRequested = true;
    }
    publishSnapshot();
    return KE_OK;
}

int EeScheduler::resumeThread(int id, bool interruptSafe) {
    assertExecutor();
    if (id == 0) {
        return KE_ILLEGAL_THID;
    }
    GuestThread* target = thread(id);
    if (!target) {
        return KE_UNKNOWN_THID;
    }
    if (target->suspendCount == 0) {
        return KE_NOT_SUSPEND;
    }
    --target->suspendCount;
    if (target->suspendCount != 0) {
        return KE_OK;
    }
    if (target->status == EeThreadStatus::WaitingSuspended) {
        target->status = EeThreadStatus::Waiting;
    } else if (target->status == EeThreadStatus::Suspended) {
        enqueueReady(*target);
        requestPreemptionIfHigher(*target, interruptSafe);
    }
    publishSnapshot();
    return KE_OK;
}

void EeScheduler::sleepCurrent() {
    assertExecutor();
    GuestThread* self = currentThread();
    assert(self != nullptr);
    if (self->wakeupCount != 0u) {
        --self->wakeupCount;
        setReturnS32(&self->activeContext(), KE_OK);
        return;
    }
    blockCurrent(EeWaitState{EeWaitReason::Sleep, std::monostate{}});
}

int EeScheduler::wakeupThread(int id, bool interruptSafe) {
    assertExecutor();
    if (id == 0 || id == m_currentThreadId) {
        return KE_ILLEGAL_THID;
    }
    GuestThread* target = thread(id);
    if (!target) {
        return KE_UNKNOWN_THID;
    }
    if (target->status == EeThreadStatus::Dormant) {
        return KE_DORMANT;
    }
    if ((target->status == EeThreadStatus::Waiting || target->status == EeThreadStatus::WaitingSuspended) &&
        target->wait.reason == EeWaitReason::Sleep) {
        makeReady(*target, KE_OK, interruptSafe);
    } else {
        ++target->wakeupCount;
    }
    publishSnapshot();
    return KE_OK;
}

int EeScheduler::cancelWakeup(int id) {
    assertExecutor();
    if (id == 0) {
        id = m_currentThreadId;
    }
    GuestThread* target = thread(id);
    if (!target) {
        return KE_UNKNOWN_THID;
    }
    const int old = static_cast<int>(target->wakeupCount);
    target->wakeupCount = 0;
    publishSnapshot();
    return old;
}

int EeScheduler::changePriority(int id, int priority, bool interruptSafe, int& oldPriority) {
    assertExecutor();
    if (priority < 1 || priority >= kPriorityCount) {
        return KE_ILLEGAL_PRIORITY;
    }
    if (id == 0) {
        id = m_currentThreadId;
    }
    GuestThread* target = thread(id);
    if (!target) {
        return KE_UNKNOWN_THID;
    }
    oldPriority = target->currentPriority;
    if (target->status == EeThreadStatus::Ready) {
        removeReady(*target);
        target->currentPriority = priority;
        enqueueReady(*target);
        requestPreemptionIfHigher(*target, interruptSafe);
    } else {
        target->currentPriority = priority;
        if (target->status == EeThreadStatus::Running) {
            for (int p = 0; p < target->currentPriority; ++p) {
                if (!m_readyQueues[p].empty()) {
                    m_rescheduleRequested = true;
                    break;
                }
            }
        }
    }
    publishSnapshot();
    return KE_OK;
}

int EeScheduler::rotateReadyQueue(int priority, bool interruptSafe) {
    assertExecutor();
    if (priority == 0) {
        const GuestThread* self = currentThread();
        priority = self ? self->currentPriority : 0;
    }
    if (priority < 0 || priority >= kPriorityCount) {
        return KE_ILLEGAL_PRIORITY;
    }

    GuestThread* self = currentThread();
    if (self && self->currentPriority == priority) {
        enqueueReady(*self);
        m_currentThreadId = 0;
        m_rescheduleRequested = true;
    } else {
        auto& queue = m_readyQueues[priority];
        if (queue.size() > 1u) {
            const int head = queue.front();
            queue.pop_front();
            queue.push_back(head);
        }
    }
    (void)interruptSafe;
    publishSnapshot();
    return KE_OK;
}

int EeScheduler::releaseWait(int id, bool interruptSafe) {
    assertExecutor();
    if (id == 0) {
        return KE_ILLEGAL_THID;
    }
    GuestThread* target = thread(id);
    if (!target) {
        return KE_UNKNOWN_THID;
    }
    if (target->status != EeThreadStatus::Waiting && target->status != EeThreadStatus::WaitingSuspended) {
        return KE_NOT_WAIT;
    }
    removeFromWaitObject(*target);
    makeReady(*target, KE_RELEASE_WAIT, interruptSafe);
    publishSnapshot();
    return KE_OK;
}

void EeScheduler::transferIfRequested(bool interruptSafe) {
    assertExecutor();
    if (interruptSafe || m_insideInterrupt || !m_rescheduleRequested) {
        return;
    }
    if (m_currentThreadId != 0) {
        GuestThread* self = currentThread();
        assert(self != nullptr);
        enqueueReady(*self, true);
        m_currentThreadId = 0;
    }
    m_rescheduleRequested = false;
    m_timeSliceExpired = false;
    publishSnapshot();
    throw EeDispatcherTransfer{};
}

int EeScheduler::createSemaphore(int initCount, int maxCount, uint32_t attr, uint32_t option) {
    assertExecutor();
    if (maxCount <= 0 || initCount < 0 || initCount > maxCount) {
        return KE_ERROR;
    }
    const int id = allocatePositiveId(m_nextSemaphoreId, m_semaphores);
    if (id == 0) {
        return KE_ERROR;
    }
    EeSemaphore semaphore{};
    semaphore.id = id;
    semaphore.count = initCount;
    semaphore.maxCount = maxCount;
    semaphore.initCount = initCount;
    semaphore.attr = attr;
    semaphore.option = option;
    m_semaphores.emplace(id, std::move(semaphore));
    publishSnapshot();
    return id;
}

int EeScheduler::deleteSemaphore(int id, bool interruptSafe) {
    assertExecutor();
    auto it = m_semaphores.find(id);
    if (it == m_semaphores.end()) {
        return KE_UNKNOWN_SEMID;
    }
    std::deque<int> waiters = std::move(it->second.waiters);
    m_semaphores.erase(it);
    for (const int threadId : waiters) {
        if (GuestThread* waiter = thread(threadId)) {
            makeReady(*waiter, KE_WAIT_DELETE, interruptSafe);
        }
    }
    publishSnapshot();
    return id;
}

int EeScheduler::signalSemaphore(int id, bool interruptSafe) {
    assertExecutor();
    EeSemaphore* object = semaphore(id);
    if (!object) {
        return KE_UNKNOWN_SEMID;
    }
    if (!object->waiters.empty()) {
        const int waiterId = object->waiters.front();
        object->waiters.pop_front();
        GuestThread* waiter = thread(waiterId);
        assert(waiter != nullptr);
        makeReady(*waiter, id, interruptSafe);
        publishSnapshot();
        return id;
    }
    if (object->count == object->maxCount) {
        return KE_SEMA_OVF;
    }
    ++object->count;
    publishSnapshot();
    return id;
}

int EeScheduler::pollSemaphore(int id) {
    assertExecutor();
    EeSemaphore* object = semaphore(id);
    if (!object) {
        return KE_UNKNOWN_SEMID;
    }
    if (object->count == 0) {
        return KE_SEMA_ZERO;
    }
    --object->count;
    publishSnapshot();
    return id;
}

void EeScheduler::waitSemaphore(int id) {
    assertExecutor();
    EeSemaphore* object = semaphore(id);
    if (!object) {
        GuestThread* self = currentThread();
        assert(self != nullptr);
        setReturnS32(&self->activeContext(), KE_UNKNOWN_SEMID);
        return;
    }
    if (object->count != 0) {
        --object->count;
        GuestThread* self = currentThread();
        assert(self != nullptr);
        setReturnS32(&self->activeContext(), id);
        publishSnapshot();
        return;
    }
    GuestThread* self = currentThread();
    assert(self != nullptr);
    object->waiters.push_back(self->id);
    blockCurrent(EeWaitState{EeWaitReason::Semaphore, EeSemaphoreWait{id}});
}

int EeScheduler::createEventFlag(uint32_t initialBits, uint32_t attr, uint32_t option) {
    assertExecutor();
    const int id = allocatePositiveId(m_nextEventFlagId, m_eventFlags);
    if (id == 0) {
        return KE_ERROR;
    }
    EeEventFlag flag{};
    flag.id = id;
    flag.attr = attr;
    flag.option = option;
    flag.initBits = initialBits;
    flag.bits = initialBits;
    m_eventFlags.emplace(id, std::move(flag));
    publishSnapshot();
    return id;
}

int EeScheduler::deleteEventFlag(int id, bool interruptSafe) {
    assertExecutor();
    auto it = m_eventFlags.find(id);
    if (it == m_eventFlags.end()) {
        return KE_UNKNOWN_EVFID;
    }
    std::deque<int> waiters = std::move(it->second.waiters);
    m_eventFlags.erase(it);
    for (const int threadId : waiters) {
        if (GuestThread* waiter = thread(threadId)) {
            makeReady(*waiter, KE_WAIT_DELETE, interruptSafe);
        }
    }
    publishSnapshot();
    return KE_OK;
}

int EeScheduler::setEventFlag(int id, uint32_t bits, bool interruptSafe) {
    assertExecutor();
    EeEventFlag* flag = eventFlag(id);
    if (!flag) {
        return KE_UNKNOWN_EVFID;
    }
    flag->bits |= bits;
    finishEventWaiters(*flag, interruptSafe);
    publishSnapshot();
    return KE_OK;
}

int EeScheduler::clearEventFlag(int id, uint32_t mask) {
    assertExecutor();
    EeEventFlag* flag = eventFlag(id);
    if (!flag) {
        return KE_UNKNOWN_EVFID;
    }
    flag->bits &= mask;
    publishSnapshot();
    return KE_OK;
}

int EeScheduler::pollEventFlag(int id, uint32_t bits, uint32_t mode, uint32_t& observedBits) {
    assertExecutor();
    EeEventFlag* flag = eventFlag(id);
    if (!flag) {
        return KE_UNKNOWN_EVFID;
    }
    if (!eventCondition(flag->bits, bits, mode)) {
        return KE_EVF_COND;
    }
    observedBits = flag->bits;
    if ((mode & WEF_CLEAR_ALL) != 0u) {
        flag->bits = 0;
    } else if ((mode & WEF_CLEAR) != 0u) {
        flag->bits &= ~bits;
    }
    publishSnapshot();
    return KE_OK;
}

void EeScheduler::waitEventFlag(int id, uint32_t bits, uint32_t mode, uint32_t resultAddress) {
    assertExecutor();
    EeEventFlag* flag = eventFlag(id);
    GuestThread* self = currentThread();
    assert(self != nullptr);
    if (!flag) {
        setReturnS32(&self->activeContext(), KE_UNKNOWN_EVFID);
        return;
    }
    if (eventCondition(flag->bits, bits, mode)) {
        const uint32_t observed = flag->bits;
        writeGuestU32(resultAddress, observed);
        if ((mode & WEF_CLEAR_ALL) != 0u) {
            flag->bits = 0;
        } else if ((mode & WEF_CLEAR) != 0u) {
            flag->bits &= ~bits;
        }
        setReturnS32(&self->activeContext(), KE_OK);
        publishSnapshot();
        return;
    }
    flag->waiters.push_back(self->id);
    blockCurrent(EeWaitState{EeWaitReason::EventFlag, EeEventFlagWait{id, bits, mode, resultAddress}});
}

int EeScheduler::setAlarm(uint16_t ticks, uint32_t handler, uint32_t argument, uint32_t gp, uint32_t sp) {
    assertExecutor();
    if (handler == 0u || !m_runtime.hasFunction(handler)) {
        return KE_ERROR;
    }
    const int id = allocatePositiveId(m_nextAlarmId, m_alarms);
    if (id == 0) {
        return KE_ERROR;
    }
    m_alarms.emplace(id, EeAlarm{id, ticks, handler, argument, gp, sp});
    const uint64_t tickCount = ticks == 0u ? 1u : static_cast<uint64_t>(ticks);
    scheduleEvent(m_eeCycle + tickCount * kAlarmTickCycles,
                  std::chrono::steady_clock::now() + std::chrono::microseconds(tickCount * kAlarmTickMicroseconds),
                  EeEvent{EeEventType::Alarm, static_cast<uint32_t>(id), 0});
    return id;
}

int EeScheduler::cancelAlarm(int id) {
    assertExecutor();
    if (m_alarms.erase(id) == 0u) {
        return KE_ERROR;
    }
    {
        std::lock_guard lock(m_eventMutex);
        std::erase_if(m_deadlines, [id](const ScheduledEvent& scheduled) {
            return scheduled.event.type == EeEventType::Alarm && scheduled.event.id == static_cast<uint32_t>(id);
        });
        updateNextDeadline();
    }
    return KE_OK;
}

void EeScheduler::queueInvocation(GuestInvocation invocation) {
    assertExecutor();
    invocation.sequence = ++m_invocationSequence;
    if (invocation.kind == GuestInvocationKind::HleCall) {
        // HLE-posted IOP->EE RPC handlers run like the game's own RPC server threads: on a dedicated high-priority
        // dispatcher thread, so a handler that blocks (WaitSema) cannot deadlock the thread it interrupted.
        GuestThread& dispatcher = acquireInvocationThread();
        const int hlePriority = (invocation.tag >= 1u && invocation.tag < static_cast<uint64_t>(kPriorityCount))
                                    ? static_cast<int>(invocation.tag)
                                    : 1;
        dispatcher.initialPriority = hlePriority;
        dispatcher.currentPriority = hlePriority;
        if (getRegU32(&invocation.context, 29) == 0u) {
            const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(dispatcher.id)) << 32u) |
                                 static_cast<uint32_t>(dispatcher.invocations.size());
            auto existing = m_invocationStackTops.find(key);
            uint32_t top = 0;
            if (existing != m_invocationStackTops.end()) {
                top = existing->second;
            } else {
                top = m_runtime.reserveAsyncCallbackStack(0x4000u, 16u);
                if (top == 0u) {
                    throw std::runtime_error("EE invocation stack space exhausted");
                }
                m_invocationStackTops.emplace(key, top);
            }
            SET_GPR_U32(&invocation.context, 29, top);
        }
        if (g_traceSched)
            std::fprintf(stderr,
                         "[sched] cyc=%llu HleCall queued on tid=%d prio=%d pc=0x%x a0=0x%x a1=0x%x (cur tid=%d)\n",
                         (unsigned long long)m_eeCycle, dispatcher.id, hlePriority, invocation.context.pc,
                         getRegU32(&invocation.context, 4), getRegU32(&invocation.context, 5), m_currentThreadId);
        dispatcher.invocations.push_back(std::move(invocation));
        enqueueReady(dispatcher);
        requestPreemptionIfHigher(dispatcher, false);
        m_checkpointPending.store(true, std::memory_order_release);
        return;
    }
    m_pendingInvocations.push_back(std::move(invocation));
    m_checkpointPending.store(true, std::memory_order_release);
}

void EeScheduler::postHostTask(std::function<void()> task) {
    {
        std::lock_guard lock(m_eventMutex);
        m_hostTasks.push_back(std::move(task));
        m_events.push_back(EeEvent{EeEventType::HostTask, 0, 0});
        m_checkpointPending.store(true, std::memory_order_release);
    }
    m_eventCv.notify_one();
}

void EeScheduler::postAfterGuestCycles(uint64_t cycles, std::function<void()> fn) {
    postHostTask([this, cycles, fn = std::move(fn)]() mutable {
        const uint32_t id = ++m_delayedCallSeq;
        m_delayedCalls.emplace(id, std::move(fn));
        // no host pacing (hostDeadline = now): when the guest is idle the wait ends immediately and the cycles are
        // skipped
        scheduleEvent(m_eeCycle + cycles, std::chrono::steady_clock::now(), EeEvent{EeEventType::DelayedCall, id, 0});
    });
}

void EeScheduler::postGuestCall(uint32_t pc, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                std::function<void(uint32_t)> done, int priority) {
    postHostTask([this, pc, a0, a1, a2, a3, priority, done = std::move(done)]() {
        GuestInvocation invocation{};
        invocation.kind = GuestInvocationKind::HleCall;
        invocation.tag = static_cast<uint64_t>(priority);
        invocation.context = R5900Context{};
        invocation.context.pc = pc;
        uint32_t gp = 0;
        if (const GuestThread* mainThread = thread(kMainThreadId)) {
            gp = getRegU32(&mainThread->context, 28);
        }
        SET_GPR_U32(&invocation.context, 28, gp);
        SET_GPR_U32(&invocation.context, 4, a0);
        SET_GPR_U32(&invocation.context, 5, a1);
        SET_GPR_U32(&invocation.context, 6, a2);
        SET_GPR_U32(&invocation.context, 7, a3);
        SET_GPR_U32(&invocation.context, 29, 0u);
        SET_GPR_U32(&invocation.context, 31, 0u);
        invocation.onComplete = [done](const R5900Context& completed, R5900Context&) {
            if (done) {
                done(getRegU32(&completed, 2));
            }
        };
        queueInvocation(std::move(invocation));
    });
}

[[noreturn]] void EeScheduler::invokeCurrent(GuestInvocation invocation) {
    assertExecutor();
    GuestThread* owner = currentThread();
    assert(owner != nullptr);
    if (getRegU32(&invocation.context, 29) == 0u) {
        SET_GPR_U32(&invocation.context, 29, invocationStackTop());
    }
    invocation.sequence = ++m_invocationSequence;
    owner->invocations.push_back(std::move(invocation));
    publishSnapshot();
    throw EeDispatcherTransfer{};
}

[[noreturn]] void EeScheduler::invokeCurrentSequence(std::vector<GuestInvocation> invocations) {
    assertExecutor();
    GuestThread* owner = currentThread();
    assert(owner != nullptr);
    assert(!invocations.empty());
    for (auto it = invocations.rbegin(); it != invocations.rend(); ++it) {
        if (getRegU32(&it->context, 29) == 0u) {
            SET_GPR_U32(&it->context, 29, invocationStackTop());
        }
        it->sequence = ++m_invocationSequence;
        owner->invocations.push_back(std::move(*it));
    }
    publishSnapshot();
    throw EeDispatcherTransfer{};
}

bool EeScheduler::hasInvocation(GuestInvocationKind kind, uint64_t tag) const {
    const GuestThread* owner = currentThread();
    if (!owner) {
        return false;
    }
    return std::any_of(
        owner->invocations.begin(), owner->invocations.end(),
        [kind, tag](const GuestInvocation& invocation) { return invocation.kind == kind && invocation.tag == tag; });
}

uint32_t EeScheduler::invocationStackTop() {
    assertExecutor();
    const GuestThread* owner = currentThread();
    if (!owner) {
        throw std::logic_error("EE invocation stack requested without a current guest context");
    }
    const size_t depth = owner ? owner->invocations.size() : 0u;
    const uint64_t key =
        (static_cast<uint64_t>(static_cast<uint32_t>(owner->id)) << 32u) | static_cast<uint32_t>(depth);
    const auto existing = m_invocationStackTops.find(key);
    if (existing != m_invocationStackTops.end()) {
        return existing->second;
    }
    constexpr uint32_t kInvocationStackSize = 0x4000u;
    const uint32_t top = m_runtime.reserveAsyncCallbackStack(kInvocationStackSize, 16u);
    if (top == 0u) {
        throw std::runtime_error("EE invocation stack space exhausted");
    }
    m_invocationStackTops.emplace(key, top);
    return top;
}

int EeScheduler::addIrqHandler(bool dmac, uint32_t cause, uint32_t handler, bool append, uint32_t argument, uint32_t gp,
                               uint32_t sp) {
    assertExecutor();
    auto& handlers = dmac ? m_dmacHandlers : m_intcHandlers;
    int& nextId = dmac ? m_nextDmacHandlerId : m_nextIntcHandlerId;
    const int id = allocatePositiveId(nextId, handlers);
    if (id == 0) {
        return KE_ERROR;
    }
    int& head = dmac ? m_dmacHeadOrder : m_intcHeadOrder;
    int& tail = dmac ? m_dmacTailOrder : m_intcTailOrder;
    handlers.emplace(id, EeIrqHandler{id, cause, handler, argument, gp, sp, true, append ? ++tail : --head});
    return id;
}

int EeScheduler::removeIrqHandler(bool dmac, uint32_t cause, int id) {
    assertExecutor();
    auto& handlers = dmac ? m_dmacHandlers : m_intcHandlers;
    auto it = handlers.find(id);
    if (it != handlers.end() && it->second.cause == cause) {
        handlers.erase(it);
    }
    return KE_OK;
}

int EeScheduler::setIrqHandlerEnabled(bool dmac, int id, bool enabled) {
    assertExecutor();
    auto& handlers = dmac ? m_dmacHandlers : m_intcHandlers;
    auto it = handlers.find(id);
    if (it != handlers.end()) {
        it->second.enabled = enabled;
    }
    return KE_OK;
}

int EeScheduler::setIrqCauseEnabled(bool dmac, uint32_t cause, bool enabled) {
    assertExecutor();
    if (cause < 32u) {
        uint32_t& mask = dmac ? m_enabledDmacMask : m_enabledIntcMask;
        if (enabled) {
            mask |= 1u << cause;
        } else {
            mask &= ~(1u << cause);
        }
    }
    return KE_OK;
}

void EeScheduler::dispatchIrq(bool dmac, uint32_t cause) {
    assertExecutor();
    const uint32_t mask = dmac ? m_enabledDmacMask : m_enabledIntcMask;
    if (cause < 32u && (mask & (1u << cause)) == 0u) {
        return;
    }
    const auto& handlers = dmac ? m_dmacHandlers : m_intcHandlers;
    std::vector<EeIrqHandler> matching;
    for (const auto& [id, handler] : handlers) {
        (void)id;
        if (handler.enabled && handler.cause == cause && handler.handler != 0u &&
            m_runtime.hasFunction(handler.handler)) {
            matching.push_back(handler);
        }
    }
    std::sort(matching.begin(), matching.end(),
              [](const EeIrqHandler& left, const EeIrqHandler& right) { return left.order < right.order; });
    for (const EeIrqHandler& handler : matching) {
        GuestInvocation invocation{};
        invocation.kind = GuestInvocationKind::Interrupt;
        invocation.context.pc = handler.handler;
        SET_GPR_U32(&invocation.context, 4, cause);
        SET_GPR_U32(&invocation.context, 5, handler.argument);
        SET_GPR_U32(&invocation.context, 28, handler.gp);
        SET_GPR_U32(&invocation.context, 29, 0u);
        SET_GPR_U32(&invocation.context, 31, 0u);
        queueInvocation(std::move(invocation));
    }
}

void EeScheduler::setVSyncFlag(uint32_t flagAddress, uint32_t tickAddress) {
    assertExecutor();
    m_vsyncFlagAddress = flagAddress;
    m_vsyncTickAddress = tickAddress;
    writeGuestU32(flagAddress, 0u);
    if (tickAddress != 0u) {
        const uint32_t physical = tickAddress & 0x1FFFFFFFu;
        if (m_rdram && physical <= PS2_RAM_SIZE - sizeof(uint64_t)) {
            const uint64_t zero = 0u;
            std::memcpy(m_rdram + physical, &zero, sizeof(zero));
        }
    }
}

uint64_t EeScheduler::currentVSyncTick() const noexcept {
    return m_vsyncTick;
}

uint32_t EeScheduler::setGsVSyncCallback(uint32_t callback, uint32_t gp, uint32_t sp) {
    assertExecutor();
    (void)sp;
    const uint32_t previous = m_gsVSyncCallback;
    m_gsVSyncCallback = callback;
    m_gsVSyncCallbackGp = gp;
    m_gsVSyncCallbackSp = 0u;
    return previous;
}

[[noreturn]] void EeScheduler::waitVSync(uint64_t afterTick, int fixedResult,
                                         std::function<void(R5900Context&)> completion) {
    blockCurrent(EeWaitState{EeWaitReason::VSync, EeVSyncWait{afterTick, fixedResult}, std::move(completion)});
}

void EeScheduler::completeVSync(uint64_t tick) {
    assertExecutor();
    std::vector<int> completed;
    for (const auto& [id, candidate] : m_threads) {
        if ((candidate.status == EeThreadStatus::Waiting || candidate.status == EeThreadStatus::WaitingSuspended) &&
            candidate.wait.reason == EeWaitReason::VSync &&
            std::get<EeVSyncWait>(candidate.wait.payload).afterTick < tick) {
            completed.push_back(id);
        }
    }
    std::sort(completed.begin(), completed.end());
    for (const int id : completed) {
        GuestThread* waiter = thread(id);
        assert(waiter != nullptr);
        const EeVSyncWait wait = std::get<EeVSyncWait>(waiter->wait.payload);
        const int result = wait.fixedResult >= 0 ? wait.fixedResult : static_cast<int>((tick - 1u) & 1u);
        makeReady(*waiter, result, false);
    }
    publishSnapshot();
}

void EeScheduler::completeExternalWait(uint32_t type, uint64_t token, int result) {
    assertExecutor();
    std::vector<int> completed;
    for (const auto& [id, candidate] : m_threads) {
        if ((candidate.status != EeThreadStatus::Waiting && candidate.status != EeThreadStatus::WaitingSuspended) ||
            (candidate.wait.reason != EeWaitReason::External && candidate.wait.reason != EeWaitReason::Mpeg)) {
            continue;
        }
        const auto& external = std::get<EeExternalWait>(candidate.wait.payload);
        if (external.type == type && external.token == token) {
            completed.push_back(id);
        }
    }
    std::sort(completed.begin(), completed.end());
    for (const int id : completed) {
        GuestThread* waiter = thread(id);
        assert(waiter != nullptr);
        makeReady(*waiter, result, false);
    }
    publishSnapshot();
}

[[noreturn]] void EeScheduler::waitExternal(EeWaitReason reason, uint32_t type, uint64_t token,
                                            std::function<void(R5900Context&)> completion) {
    EeWaitState wait{reason, EeExternalWait{type, token}, std::move(completion)};
    blockCurrent(std::move(wait));
}

GuestThread* EeScheduler::thread(int id) {
    auto it = m_threads.find(id);
    return it == m_threads.end() ? nullptr : &it->second;
}

const GuestThread* EeScheduler::thread(int id) const {
    auto it = m_threads.find(id);
    return it == m_threads.end() ? nullptr : &it->second;
}

EeSemaphore* EeScheduler::semaphore(int id) {
    auto it = m_semaphores.find(id);
    return it == m_semaphores.end() ? nullptr : &it->second;
}

const EeSemaphore* EeScheduler::semaphore(int id) const {
    auto it = m_semaphores.find(id);
    return it == m_semaphores.end() ? nullptr : &it->second;
}

EeEventFlag* EeScheduler::eventFlag(int id) {
    auto it = m_eventFlags.find(id);
    return it == m_eventFlags.end() ? nullptr : &it->second;
}

const EeEventFlag* EeScheduler::eventFlag(int id) const {
    auto it = m_eventFlags.find(id);
    return it == m_eventFlags.end() ? nullptr : &it->second;
}

GuestThread* EeScheduler::currentThread() {
    return thread(m_currentThreadId);
}

const GuestThread* EeScheduler::currentThread() const {
    return thread(m_currentThreadId);
}

int EeScheduler::currentThreadId() const noexcept {
    return m_currentThreadId;
}

R5900Context* EeScheduler::currentContext() {
    GuestThread* self = currentThread();
    return self ? &self->activeContext() : nullptr;
}

uint8_t* EeScheduler::rdram() const noexcept {
    return m_rdram;
}

void EeScheduler::bindMainContextForSyscall(R5900Context& ctx, uint8_t* rdram) {
    if (m_executorThread == std::thread::id{}) {
        reset(rdram, ctx);
        GuestThread* main = selectReady();
        assert(main != nullptr);
        makeRunning(*main);
        return;
    }
    assertExecutor();
    m_rdram = rdram;
    if (m_currentThreadId == 0) {
        GuestThread* main = thread(kMainThreadId);
        assert(main != nullptr);
        assert(main->status == EeThreadStatus::Ready);
        removeReady(*main);
        makeRunning(*main);
    }
}

EeKernelSnapshot EeScheduler::snapshot() const {
    std::lock_guard lock(m_snapshotMutex);
    return m_snapshot;
}

void EeScheduler::publishSnapshot() {
    EeKernelSnapshot next{};
    next.sequence = ++m_snapshotSequence;
    next.eeCycle = m_eeCycle;
    next.sliceEndCycle = m_sliceEndCycle;
    next.nextEventCycle = m_nextDeadlineCycle.load(std::memory_order_acquire);
    next.runningThreadId = m_currentThreadId;
    next.threads.reserve(m_threads.size());
    for (const auto& [id, item] : m_threads) {
        if (id < 0 && item.status == EeThreadStatus::Dormant) {
            continue;
        }
        EeThreadSnapshot snapshot{};
        snapshot.id = id;
        const R5900Context& context = item.activeContext();
        snapshot.pc = context.pc;
        snapshot.ra = getRegU32(&context, 31);
        snapshot.sp = getRegU32(&context, 29);
        snapshot.contextGp = getRegU32(&context, 28);
        snapshot.entry = item.entry;
        snapshot.stack = item.stack;
        snapshot.stackSize = item.stackSize;
        snapshot.gp = item.gp;
        snapshot.initialPriority = item.initialPriority;
        snapshot.currentPriority = item.currentPriority;
        snapshot.status = item.status;
        snapshot.waitReason = item.wait.reason;
        snapshot.waitId = waitObjectId(item.wait);
        snapshot.suspendCount = item.suspendCount;
        snapshot.wakeupCount = item.wakeupCount;
        snapshot.invocationDepth = static_cast<uint32_t>(item.invocations.size());
        next.threads.push_back(snapshot);
    }
    std::sort(next.threads.begin(), next.threads.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });
    next.semaphores.reserve(m_semaphores.size());
    for (const auto& [id, item] : m_semaphores) {
        next.semaphores.push_back(
            EeSemaphoreSnapshot{id, item.count, item.maxCount, static_cast<uint32_t>(item.waiters.size())});
    }
    std::sort(next.semaphores.begin(), next.semaphores.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });
    next.eventFlags.reserve(m_eventFlags.size());
    for (const auto& [id, item] : m_eventFlags) {
        next.eventFlags.push_back(
            EeEventFlagSnapshot{id, item.bits, item.initBits, item.attr, static_cast<uint32_t>(item.waiters.size())});
    }
    std::sort(next.eventFlags.begin(), next.eventFlags.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });
    {
        std::lock_guard lock(m_snapshotMutex);
        m_snapshot = std::move(next);
    }
}

void EeScheduler::assertExecutor() const {
    assert(m_executorThread == std::this_thread::get_id());
}

int EeScheduler::allocateThreadId() {
    for (int attempts = 0; attempts <= kLastThreadId - kFirstThreadId; ++attempts) {
        const int candidate = m_nextThreadId;
        m_nextThreadId = candidate == kLastThreadId ? kFirstThreadId : candidate + 1;
        if (!m_threads.contains(candidate)) {
            return candidate;
        }
    }
    return 0;
}

GuestThread& EeScheduler::acquireInvocationThread() {
    for (auto& [id, candidate] : m_threads) {
        if (id < 0 && candidate.status == EeThreadStatus::Dormant && candidate.invocations.empty()) {
            return candidate;
        }
    }

    GuestThread dispatcher{};
    dispatcher.id = m_nextInvocationThreadId--;
    dispatcher.initialPriority = 0;
    dispatcher.currentPriority = 0;
    dispatcher.status = EeThreadStatus::Dormant;
    return m_threads.emplace(dispatcher.id, std::move(dispatcher)).first->second;
}

void EeScheduler::enqueueReady(GuestThread& item, bool front) {
    assert(item.currentPriority >= 0 && item.currentPriority < kPriorityCount);
    item.status = EeThreadStatus::Ready;
    auto& queue = m_readyQueues[item.currentPriority];
    if (front) {
        queue.push_front(item.id);
    } else {
        queue.push_back(item.id);
    }
}

void EeScheduler::removeReady(GuestThread& item) {
    if (item.status != EeThreadStatus::Ready) {
        return;
    }
    auto& queue = m_readyQueues[item.currentPriority];
    auto it = std::find(queue.begin(), queue.end(), item.id);
    assert(it != queue.end());
    queue.erase(it);
}

GuestThread* EeScheduler::selectReady() {
    for (auto& queue : m_readyQueues) {
        if (queue.empty()) {
            continue;
        }
        const int id = queue.front();
        queue.pop_front();
        GuestThread* selected = thread(id);
        assert(selected != nullptr);
        assert(selected->status == EeThreadStatus::Ready);
        return selected;
    }
    return nullptr;
}

void EeScheduler::makeRunning(GuestThread& item) {
    assert(m_currentThreadId == 0);
    assert(item.status == EeThreadStatus::Ready);
    item.status = EeThreadStatus::Running;
    m_currentThreadId = item.id;
    renewTimeSlice();
    if (g_traceSched)
        std::fprintf(stderr, "[sched] cyc=%llu run tid=%d prio=%d pc=0x%x ra=0x%x sp=0x%x inv=%zu\n",
                     (unsigned long long)m_eeCycle, item.id, item.currentPriority, item.activeContext().pc,
                     getRegU32(&item.activeContext(), 31), getRegU32(&item.activeContext(), 29),
                     item.invocations.size());
}

void EeScheduler::makeDormant(GuestThread& item) {
    removeReady(item);
    removeFromWaitObject(item);
    item.status = EeThreadStatus::Dormant;
    item.wait = {};
    item.resumeCompletion = {};
    item.suspendCount = 0;
    item.wakeupCount = 0;
    item.invocations.clear();
}

void EeScheduler::removeFromWaitObject(GuestThread& item) {
    const int id = item.id;
    if (item.wait.reason == EeWaitReason::Semaphore) {
        const int objectId = std::get<EeSemaphoreWait>(item.wait.payload).id;
        if (EeSemaphore* object = semaphore(objectId)) {
            auto it = std::find(object->waiters.begin(), object->waiters.end(), id);
            if (it != object->waiters.end()) {
                object->waiters.erase(it);
            }
        }
    } else if (item.wait.reason == EeWaitReason::EventFlag) {
        const int objectId = std::get<EeEventFlagWait>(item.wait.payload).id;
        if (EeEventFlag* object = eventFlag(objectId)) {
            auto it = std::find(object->waiters.begin(), object->waiters.end(), id);
            if (it != object->waiters.end()) {
                object->waiters.erase(it);
            }
        }
    }
    item.wait = {};
}

void EeScheduler::blockCurrent(EeWaitState wait) {
    GuestThread* self = currentThread();
    assert(self != nullptr);
    self->wait = std::move(wait);
    self->status = self->suspendCount == 0 ? EeThreadStatus::Waiting : EeThreadStatus::WaitingSuspended;
    m_currentThreadId = 0;
    publishSnapshot();
    throw EeDispatcherTransfer{};
}

void EeScheduler::makeReady(GuestThread& item, int result, bool interruptSafe) {
    auto completion = std::move(item.wait.completion);
    item.wait = {};
    setReturnS32(&item.activeContext(), result);
    item.resumeCompletion = std::move(completion);
    if (item.suspendCount != 0) {
        item.status = EeThreadStatus::Suspended;
        return;
    }
    enqueueReady(item);
    requestPreemptionIfHigher(item, interruptSafe);
}

void EeScheduler::requestPreemptionIfHigher(const GuestThread& readyThread, bool interruptSafe) {
    const GuestThread* running = currentThread();
    if (!running || readyThread.currentPriority >= running->currentPriority) {
        return;
    }
    m_rescheduleRequested = true;
    if (interruptSafe || m_insideInterrupt) {
        m_checkpointPending.store(true, std::memory_order_release);
    }
}

void EeScheduler::applyPendingPreemption() {
    if (!m_rescheduleRequested) {
        return;
    }
    if (m_currentThreadId == 0) {
        m_rescheduleRequested = false;
        m_timeSliceExpired = false;
        return;
    }
    GuestThread* self = currentThread();
    assert(self != nullptr);
    enqueueReady(*self, !m_timeSliceExpired);
    m_currentThreadId = 0;
    m_rescheduleRequested = false;
    m_timeSliceExpired = false;
}

void EeScheduler::processPendingEvents() {
    assertExecutor();
    processDueDeadlines();
    reportVblankAudit(m_eeCycle);
    const uint32_t timerInterrupts = m_pendingEeTimerInterrupts;
    m_pendingEeTimerInterrupts = 0u;
    for (uint32_t timer = 0u; timer < 4u; ++timer) {
        if ((timerInterrupts & (1u << timer)) != 0u) {
            dispatchIrq(false, 9u + timer);
        }
    }
    std::deque<EeEvent> pending;
    {
        std::lock_guard lock(m_eventMutex);
        pending.swap(m_events);
    }
    for (const EeEvent& event : pending) {
        processEvent(event);
    }

    {
        std::lock_guard lock(m_eventMutex);
        const uint64_t nextEventCycle = m_nextDeadlineCycle.load(std::memory_order_acquire);
        const bool cycleEventDue = nextEventCycle != 0u && m_eeCycle >= nextEventCycle;
        const bool pendingWork =
            !m_events.empty() || cycleEventDue || hostVblankDue() || m_stopRequested.load(std::memory_order_acquire);
        m_checkpointPending.store(pendingWork, std::memory_order_release);
    }
    applyPendingPreemption();
}

void EeScheduler::processDueDeadlines() {
    for (;;) {
        std::vector<ScheduledEvent> due;
        std::chrono::steady_clock::time_point pacingDeadline{};
        {
            std::unique_lock lock(m_eventMutex);
            const auto now = std::chrono::steady_clock::now();
            for (const ScheduledEvent& item : m_deadlines) {
                // An uncapped vblank may fire with no cycle budget, but an
                // early cycle deadline must never pace the guest to a future
                // vblank. Other event types retain their original AND gate.
                if ((hostVblank(item.event.type) ? item.hostDeadline <= now : item.deadlineCycle <= m_eeCycle) &&
                    (pacingDeadline == std::chrono::steady_clock::time_point{} || item.hostDeadline < pacingDeadline)) {
                    pacingDeadline = item.hostDeadline;
                }
            }

            if (pacingDeadline == std::chrono::steady_clock::time_point{}) {
                updateNextDeadline();
                return;
            }

            if (now < pacingDeadline) {
                m_eventCv.wait_until(lock, pacingDeadline, [this]() {
                    return !m_events.empty() || m_stopRequested.load(std::memory_order_acquire);
                });
                if (!m_events.empty() || m_stopRequested.load(std::memory_order_acquire)) {
                    updateNextDeadline();
                    return;
                }
            }

            const auto pacedNow = std::chrono::steady_clock::now();
            auto firstFuture =
                std::partition(m_deadlines.begin(), m_deadlines.end(), [this, pacedNow](const ScheduledEvent& item) {
                    return (hostVblank(item.event.type) || item.deadlineCycle <= m_eeCycle) &&
                           item.hostDeadline <= pacedNow;
                });
            due.insert(due.end(), std::make_move_iterator(m_deadlines.begin()), std::make_move_iterator(firstFuture));
            m_deadlines.erase(m_deadlines.begin(), firstFuture);
            updateNextDeadline();
        }

        std::sort(due.begin(), due.end(), [](const ScheduledEvent& left, const ScheduledEvent& right) {
            if (left.deadlineCycle != right.deadlineCycle) {
                return left.deadlineCycle < right.deadlineCycle;
            }
            if (left.event.type != right.event.type) {
                return left.event.type < right.event.type;
            }
            if (left.event.id != right.event.id) {
                return left.event.id < right.event.id;
            }
            return left.sequence < right.sequence;
        });

        if (due.empty()) {
            return;
        }

        for (ScheduledEvent& scheduled : due) {
            if (g_vblankAudit.enabled &&
                (scheduled.event.type == EeEventType::VBlankStart || scheduled.event.type == EeEventType::VBlankEnd)) {
                if (scheduled.event.type == EeEventType::VBlankStart)
                    ++g_vblankAudit.starts;
                else
                    ++g_vblankAudit.ends;
                const auto now = std::chrono::steady_clock::now();
                g_vblankAudit.maxDelay = std::max(g_vblankAudit.maxDelay, now - scheduled.hostDeadline);
                if (g_vblankAudit.trace && scheduled.event.type == EeEventType::VBlankStart)
                    std::fprintf(
                        stderr, "[ft] V %lld %lld\n",
                        (long long)std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch())
                            .count(),
                        (long long)std::chrono::duration_cast<std::chrono::microseconds>(now - scheduled.hostDeadline)
                            .count());
            }
            if (scheduled.event.type == EeEventType::VBlankStart) {
                scheduleEvent(scheduled.deadlineCycle + kVBlankDurationCycles, scheduled.hostDeadline + kVBlankDuration,
                              EeEvent{EeEventType::VBlankEnd, 0, m_vsyncTick + 1u});
                // Never let the host deadline fall more than two frames behind the wall clock: after the game thread
                // was held (settings overlay, window drag, debugger) the vblanks would otherwise be delivered in a
                // burst that jumps the game clock.
                const auto nextVBlankHost = std::max(scheduled.hostDeadline + kVBlankPeriod,
                                                     std::chrono::steady_clock::now() - 2 * kVBlankPeriod);
                scheduleEvent(scheduled.deadlineCycle + kVBlankPeriodCycles, nextVBlankHost,
                              EeEvent{EeEventType::VBlankStart, 0, 0});
            }
            processEvent(scheduled.event);
        }
    }
}

void EeScheduler::processEvent(const EeEvent& event) {
    switch (event.type) {
        case EeEventType::Stop:
            requestStop();
            break;
        case EeEventType::VBlankStart:
            ++m_vsyncTick;
            GsCapture::instance().onVsync(m_vsyncTick);
            m_runtime.gs().vsyncHost(m_vsyncTick);
            m_runtime.memory().gs().vsyncTick.store(m_vsyncTick, std::memory_order_release);
            if ((m_vsyncTick & 1u) != 0u) {
                m_runtime.memory().gs().csr.fetch_or(0x2000ull, std::memory_order_acq_rel);
            } else {
                m_runtime.memory().gs().csr.fetch_and(~0x2000ull, std::memory_order_acq_rel);
            }
            writeGuestU32(m_vsyncFlagAddress, 1u);
            if (m_vsyncTickAddress != 0u) {
                const uint32_t physical = m_vsyncTickAddress & 0x1FFFFFFFu;
                if (m_rdram && physical <= PS2_RAM_SIZE - sizeof(uint64_t)) {
                    std::memcpy(m_rdram + physical, &m_vsyncTick, sizeof(m_vsyncTick));
                }
            }
            m_vsyncFlagAddress = 0u;
            m_vsyncTickAddress = 0u;
            completeVSync(m_vsyncTick);
            if (m_gsVSyncCallback != 0u && m_runtime.hasFunction(m_gsVSyncCallback)) {
                GuestInvocation invocation{};
                invocation.kind = GuestInvocationKind::GsCallback;
                invocation.context.pc = m_gsVSyncCallback;
                SET_GPR_U32(&invocation.context, 4, static_cast<uint32_t>(m_vsyncTick));
                SET_GPR_U32(&invocation.context, 28, m_gsVSyncCallbackGp);
                SET_GPR_U32(&invocation.context, 29, m_gsVSyncCallbackSp);
                SET_GPR_U32(&invocation.context, 31, 0u);
                queueInvocation(std::move(invocation));
            }
            dispatchIrq(false, 2u);
            break;
        case EeEventType::ExternalWake:
            completeExternalWait(event.id, event.value, KE_OK);
            break;
        case EeEventType::HostTask: {
            std::function<void()> task;
            {
                std::lock_guard lock(m_eventMutex);
                if (!m_hostTasks.empty()) {
                    task = std::move(m_hostTasks.front());
                    m_hostTasks.pop_front();
                }
            }
            if (task) {
                task();
            }
            break;
        }
        case EeEventType::DelayedCall: {
            auto it = m_delayedCalls.find(event.id);
            if (it != m_delayedCalls.end()) {
                std::function<void()> fn = std::move(it->second);
                m_delayedCalls.erase(it);
                if (fn) {
                    fn();
                }
            }
            break;
        }
        case EeEventType::VBlankEnd:
            dispatchIrq(false, 3u);
            break;
        case EeEventType::Dmac:
            break;
        case EeEventType::Alarm: {
            auto it = m_alarms.find(static_cast<int>(event.id));
            if (it == m_alarms.end()) {
                break;
            }
            const EeAlarm alarm = it->second;
            m_alarms.erase(it);
            GuestInvocation invocation{};
            invocation.kind = GuestInvocationKind::Alarm;
            invocation.context.pc = alarm.handler;
            SET_GPR_U32(&invocation.context, 4, static_cast<uint32_t>(alarm.id));
            SET_GPR_U32(&invocation.context, 5, static_cast<uint32_t>(alarm.ticks));
            SET_GPR_U32(&invocation.context, 6, alarm.argument);
            SET_GPR_U32(&invocation.context, 28, alarm.gp);
            SET_GPR_U32(&invocation.context, 29, 0u);
            SET_GPR_U32(&invocation.context, 31, 0u);
            queueInvocation(std::move(invocation));
            break;
        }
    }
}

void EeScheduler::finishEventWaiters(EeEventFlag& flag, bool interruptSafe) {
    for (auto it = flag.waiters.begin(); it != flag.waiters.end();) {
        GuestThread* waiter = thread(*it);
        assert(waiter != nullptr);
        const EeEventFlagWait wait = std::get<EeEventFlagWait>(waiter->wait.payload);
        if (!eventCondition(flag.bits, wait.bits, wait.mode)) {
            ++it;
            continue;
        }
        const uint32_t observed = flag.bits;
        writeGuestU32(wait.resultAddress, observed);
        if ((wait.mode & WEF_CLEAR_ALL) != 0u) {
            flag.bits = 0;
        } else if ((wait.mode & WEF_CLEAR) != 0u) {
            flag.bits &= ~wait.bits;
        }
        it = flag.waiters.erase(it);
        makeReady(*waiter, KE_OK, interruptSafe);
    }
}

bool EeScheduler::eventCondition(uint32_t current, uint32_t requested, uint32_t mode) {
    return (mode & WEF_OR) != 0u ? (current & requested) != 0u : (current & requested) == requested;
}

int EeScheduler::waitObjectId(const EeWaitState& wait) {
    switch (wait.reason) {
        case EeWaitReason::Semaphore:
            return std::get<EeSemaphoreWait>(wait.payload).id;
        case EeWaitReason::EventFlag:
            return std::get<EeEventFlagWait>(wait.payload).id;
        default:
            return 0;
    }
}

void EeScheduler::writeGuestU32(uint32_t address, uint32_t value) {
    if (address == 0u) {
        return;
    }
    const uint32_t physical = address & 0x1FFFFFFFu;
    if (!m_rdram || physical > PS2_RAM_SIZE - sizeof(value)) {
        return;
    }
    std::memcpy(m_rdram + physical, &value, sizeof(value));
}

void EeScheduler::waitForEvent() {
    std::unique_lock lock(m_eventMutex);
    if (!m_events.empty() || m_stopRequested.load(std::memory_order_acquire)) {
        return;
    }
    const uint64_t timerCycles = m_runtime.memory().cyclesUntilNextEeTimerInterrupt();
    const bool hasTimerDeadline = timerCycles != std::numeric_limits<uint64_t>::max();
    if (m_deadlines.empty() && !hasTimerDeadline) {
        m_eventCv.wait(lock, [this]() { return !m_events.empty() || m_stopRequested.load(std::memory_order_acquire); });
        return;
    }

    uint64_t deadlineCycle = 0u;
    uint64_t idleCycleLimit = std::numeric_limits<uint64_t>::max();
    auto hostDeadline = std::chrono::steady_clock::time_point::max();
    if (!m_deadlines.empty()) {
        const ScheduledEvent* next = nullptr;
        for (const auto& item : m_deadlines) {
            if (hostVblank(item.event.type))
                continue;
            if (!next || item.deadlineCycle < next->deadlineCycle ||
                (item.deadlineCycle == next->deadlineCycle && item.sequence < next->sequence))
                next = &item;
        }
        if (next) {
            deadlineCycle = next->deadlineCycle;
            idleCycleLimit = next->deadlineCycle;
            hostDeadline = next->hostDeadline;
        }
    }
    if (hasTimerDeadline) {
        idleCycleLimit = std::min(idleCycleLimit, m_eeCycle + timerCycles);
        const auto timerHostDeadline = std::chrono::steady_clock::now() + eeCyclesToHostDuration(timerCycles);
        if (timerHostDeadline < hostDeadline) {
            deadlineCycle = m_eeCycle + timerCycles;
            hostDeadline = timerHostDeadline;
        }
    }

    const bool hostOnlyWake = g_hostVblank && g_nextVblankHost <= hostDeadline;
    if (hostOnlyWake)
        hostDeadline = g_nextVblankHost;
    const auto idleStart = std::chrono::steady_clock::now();

    const bool signaled = m_eventCv.wait_until(
        lock, hostDeadline, [this]() { return !m_events.empty() || m_stopRequested.load(std::memory_order_acquire); });
    if (!signaled) {
        uint64_t elapsed = deadlineCycle > m_eeCycle ? deadlineCycle - m_eeCycle : 0u;
        if (hostOnlyWake) {
            // A host edge can interrupt a longer idle timer. Account the real
            // idle interval at the unchanged EE frequency, so repeated vblanks
            // cannot restart that timer forever. Never skip a cycle event.
            const auto idleNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - idleStart)
                    .count();
            const uint64_t ns = static_cast<uint64_t>(std::max<int64_t>(0, idleNs));
            elapsed = (ns / 1000000000ull) * kEeClockHz + ((ns % 1000000000ull) * kEeClockHz) / 1000000000ull;
            if (idleCycleLimit != std::numeric_limits<uint64_t>::max())
                elapsed = std::min(elapsed, idleCycleLimit > m_eeCycle ? idleCycleLimit - m_eeCycle : 0u);
        }
        lock.unlock();
        uint64_t remaining = elapsed;
        while (remaining > 0u) {
            const uint32_t step =
                static_cast<uint32_t>(std::min<uint64_t>(remaining, std::numeric_limits<uint32_t>::max()));
            accountCycles(step);
            remaining -= step;
        }
        m_checkpointPending.store(true, std::memory_order_release);
    }
}

void EeScheduler::scheduleEvent(uint64_t deadlineCycle, std::chrono::steady_clock::time_point hostDeadline,
                                EeEvent event) {
    {
        std::lock_guard lock(m_eventMutex);
        m_deadlines.push_back(ScheduledEvent{deadlineCycle, hostDeadline, event, ++m_eventSequence});
        updateNextDeadline();
    }
    m_eventCv.notify_one();
}

void EeScheduler::updateNextDeadline() {
    const ScheduledEvent* next = nullptr;
    g_nextVblankHost = std::chrono::steady_clock::time_point::max();
    for (const auto& item : m_deadlines) {
        if (hostVblank(item.event.type)) {
            g_nextVblankHost = std::min(g_nextVblankHost, item.hostDeadline);
        } else if (!next || item.deadlineCycle < next->deadlineCycle) {
            next = &item;
        }
    }
    m_nextDeadlineCycle.store(next ? next->deadlineCycle : 0u, std::memory_order_release);
}

bool EeScheduler::hasReadyAtOrAbovePriority(int priority) const {
    const int last = std::clamp(priority, 0, kPriorityCount - 1);
    for (int p = 0; p <= last; ++p) {
        if (!m_readyQueues[static_cast<size_t>(p)].empty()) {
            return true;
        }
    }
    return false;
}

void EeScheduler::renewTimeSlice() {
    m_sliceEndCycle = m_eeCycle + kDefaultTimeSliceCycles;
    m_timeSliceExpired = false;
}

void EeScheduler::copyMainContextToRuntime() {
    const GuestThread* main = thread(kMainThreadId);
    if (main) {
        m_runtime.m_cpuContext = main->context;
    }
}

void EeScheduler::publishDebugContext(const R5900Context& context) {
    m_runtime.m_debugPc.store(context.pc, std::memory_order_relaxed);
    m_runtime.m_debugRa.store(getRegU32(&context, 31), std::memory_order_relaxed);
    m_runtime.m_debugSp.store(getRegU32(&context, 29), std::memory_order_relaxed);
    m_runtime.m_debugGp.store(getRegU32(&context, 28), std::memory_order_relaxed);
}

void EeScheduler::publishIdleDebugContext() {
    // Temporary IRQ/RPC/alarm invocations deliberately return to PC=0. Once
    // the scheduler is idle, show a real EE thread context instead of leaving
    // the debugger pinned to that completed dispatcher frame.
    const GuestThread* selected = thread(kMainThreadId);
    if (!selected) {
        for (const auto& [id, candidate] : m_threads) {
            if (id > 0 && candidate.status != EeThreadStatus::Dormant) {
                selected = &candidate;
                break;
            }
        }
    }

    if (selected) {
        publishDebugContext(selected->activeContext());
    }
}
