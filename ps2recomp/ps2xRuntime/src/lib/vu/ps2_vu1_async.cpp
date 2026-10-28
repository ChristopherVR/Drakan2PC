#include "runtime/ps2_vu1_async.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_render_queue.h"
#include "../ps2_vu1_batch.h"
#include "../gs/gs_gsr_batch.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>
#include <functional>
#if defined(_M_X64) || defined(_M_IX86) || defined(__i386__) || defined(__x86_64__)
#include <emmintrin.h>
#endif

namespace vu1async {
std::atomic<uint32_t> g_pending{0};

namespace {
uint32_t spinCount() {
    static const uint32_t count = [] {
        const char* setting = std::getenv("PS2X_VU_SPIN");
        const unsigned long value = setting ? std::strtoul(setting, nullptr, 10) : 4096ul;
        return static_cast<uint32_t>(value > 4096ul ? 4096ul : value);
    }();
    return count;
}

inline void spinPause() {
#if defined(_M_X64) || defined(_M_IX86) || defined(__i386__) || defined(__x86_64__)
    _mm_pause();
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

struct Job {
    PS2Memory* mem;
    std::vector<uint8_t> data;
    Meta meta;
    bool path3 = false;
    std::function<void()> task;
};

thread_local bool t_worker = false;
thread_local const Meta* t_meta = nullptr;
constexpr size_t kMaxQueued = 256; // bounded EE/VU pipeline

struct Worker {
    std::mutex m;
    std::condition_variable cvJob;  // worker waits for jobs
    std::condition_variable cvDone; // EE waits for completion / queue space
    std::deque<Job> q;
    bool stop = false;
    bool started = false;
    std::thread th;
    // statistics
    std::atomic<uint64_t> jobs{0}, syncs{0}, syncWaits{0}, syncNs{0}, maxQueued{0};

    void start() {
        started = true;
        th = std::thread([this]() { run(); });
    }

    void run() {
        t_worker = true;
        for (;;) {
            Job job;
            std::vector<vu1batch::Input> batch;
            {
                std::unique_lock<std::mutex> lock(m);
                if (q.empty() && !stop && spinCount() != 0) {
                    // Give a nearby producer a short chance to publish
                    // without parking. Never hold its queue mutex here.
                    lock.unlock();
                    for (uint32_t n = 0, limit = spinCount(); n < limit; ++n) {
                        if (g_pending.load(std::memory_order_acquire) != 0)
                            break;
                        spinPause();
                    }
                    lock.lock();
                }
                cvJob.wait(lock, [&] { return !q.empty() || stop; });
                if (q.empty() && stop)
                    return;
                const bool wasFull = q.size() == kMaxQueued;
                job = std::move(q.front());
                q.pop_front();
                if (vu1batch::enabled() && !job.path3 && !job.task) {
                    batch.push_back({job.mem, std::move(job.data), std::move(job.meta), false, {}});
                    // PATH3 and immutable renderer records are replayed
                    // by the collector, never by geometry workers.
                    while (batch.size() < 64 && !q.empty() && (q.front().task || q.front().mem == job.mem)) {
                        auto& next = q.front();
                        batch.push_back(
                            {next.mem, std::move(next.data), std::move(next.meta), next.path3, std::move(next.task)});
                        q.pop_front();
                    }
                }
                // A completion waiter needs the whole stream, not each
                // intermediate job. Only a full queue has a space waiter.
                if (wasFull)
                    cvDone.notify_all();
            }
            const uint32_t completed = batch.empty() ? 1u : uint32_t(batch.size());
            t_meta = (job.path3 || job.task) ? nullptr : &job.meta;
            {
                // The job's GIF packets reach the renderer queue in one append, before the job counts as completed.
                gsr_batch::Scope gifBatch;
                if (!batch.empty()) {
                    const auto serial = [](const vu1batch::Input& input) {
                        t_meta = input.vif() ? &input.meta : nullptr;
                        if (input.task)
                            input.task();
                        else if (input.path3)
                            input.memory->submitGifPacket(GifPathId::Path3, input.data.data(),
                                                          uint32_t(input.data.size()), true);
                        else
                            input.memory->runVif1Slice(input.data.data(), uint32_t(input.data.size()), input.offset,
                                                       input.meta.objectMotion.get(), input.begin, input.end);
                        t_meta = nullptr;
                    };
                    if (!vu1batch::process(batch, serial))
                        for (const auto& input : batch)
                            serial(input);
                } else if (job.task)
                    job.task();
                else if (job.path3)
                    job.mem->submitGifPacket(GifPathId::Path3, job.data.data(), static_cast<uint32_t>(job.data.size()),
                                             true);
                else
                    job.mem->runVif1Job(job.data.data(), static_cast<uint32_t>(job.data.size()),
                                        job.meta.objectMotion.get());
            }
            t_meta = nullptr;
            if (g_pending.fetch_sub(completed, std::memory_order_release) == completed) {
                // Publish every job through g_pending, but take the
                // mutex only at the completion barrier. Holding it for
                // the final notification prevents a lost waiter wakeup.
                std::lock_guard<std::mutex> lock(m);
                cvDone.notify_all();
            }
        }
    }
};

Worker& worker() {
    static Worker* w = new Worker; // never destroyed: the process may end while the worker is still parked
    return *w;
}

} // namespace

bool enabled() {
    static const bool on = []() {
        const char* e = std::getenv("PS2X_VU1_THREAD");
        return !(e && e[0] == '0');
    }();
    return on;
}

bool onWorker() {
    return t_worker;
}
const Meta* currentMeta() {
    return t_meta;
}

static void enqueue(Job&& job) {
    Worker& w = worker();
    std::unique_lock<std::mutex> lock(w.m);
    if (!w.started) {
        w.start();
        static const bool reg = []() {
            std::atexit(shutdown);
            return true;
        }();
        (void)reg;
    }
    if (w.q.size() >= kMaxQueued)
        w.cvDone.wait(lock, [&] { return w.q.size() < kMaxQueued; });
    w.q.push_back(std::move(job));
    g_pending.fetch_add(1, std::memory_order_release);
    w.jobs.fetch_add(1, std::memory_order_relaxed);
    if (w.q.size() > w.maxQueued.load(std::memory_order_relaxed))
        w.maxQueued.store(w.q.size(), std::memory_order_relaxed);
    lock.unlock();
    w.cvJob.notify_one();
}

void submit(PS2Memory* mem, std::vector<uint8_t>&& data, const Meta& meta) {
    enqueue(Job{mem, std::move(data), meta, false});
}

bool renderQueueEnabled() {
    static const bool on = [] {
        const char* setting = std::getenv("DRAKAN_RENDER_QUEUE");
        return !(setting && setting[0] == '0');
    }();
    return on && enabled();
}

void submitPath3(PS2Memory* mem, std::vector<uint8_t>&& data) {
    enqueue(Job{mem, std::move(data), {}, true});
}

void submitRenderTask(std::function<void()> task) {
    if (task)
        enqueue(Job{nullptr, {}, {}, false, std::move(task)});
}

void syncSlow() {
    if (t_worker)
        return;
    Worker& w = worker();
    const auto t0 = std::chrono::steady_clock::now();
    w.syncs.fetch_add(1, std::memory_order_relaxed);
    if (g_pending.load(std::memory_order_acquire) == 0)
        return;
    w.syncWaits.fetch_add(1, std::memory_order_relaxed);
    for (uint32_t n = 0, limit = spinCount(); n < limit; ++n) {
        if (g_pending.load(std::memory_order_acquire) == 0)
            break;
        spinPause();
    }
    if (g_pending.load(std::memory_order_acquire) != 0) {
        std::unique_lock<std::mutex> lock(w.m);
        w.cvDone.wait(lock, [&] { return g_pending.load(std::memory_order_acquire) == 0; });
    }
    const auto ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
    w.syncNs.fetch_add(ns, std::memory_order_relaxed);
}

void shutdown() {
    Worker& w = worker();
    if (!w.started || t_worker)
        return;
    {
        std::unique_lock<std::mutex> lock(w.m);
        w.cvDone.wait(lock, [&] { return g_pending.load(std::memory_order_acquire) == 0; });
        w.stop = true;
    }
    w.cvJob.notify_all();
    if (w.th.joinable())
        w.th.join();
    w.started = false;
}

void printStats() {
    Worker& w = worker();
    std::fprintf(stderr, "[vu1async] %llu jobs, %llu syncs (%llu waited, %.1f ms total), max queued %llu\n",
                 static_cast<unsigned long long>(w.jobs.load()), static_cast<unsigned long long>(w.syncs.load()),
                 static_cast<unsigned long long>(w.syncWaits.load()), w.syncNs.load() * 1e-6,
                 static_cast<unsigned long long>(w.maxQueued.load()));
    vu1batch::printStats();
}
} // namespace vu1async
