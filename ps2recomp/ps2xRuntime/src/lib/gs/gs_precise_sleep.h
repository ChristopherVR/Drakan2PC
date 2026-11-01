#pragma once
#include <algorithm>
#include <chrono>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace drakan_pacing {
// Sleeps until `deadline` and never returns early. std::this_thread::sleep_until wakes up to a scheduler tick late
// (about a millisecond), which turns a frame limiter into a jittery one: the bulk of the wait uses a high-resolution
// waitable timer (Windows) or sleep_until, the last `spin` is spent polling the clock.
inline void preciseSleepUntil(std::chrono::steady_clock::time_point deadline,
                              std::chrono::microseconds spin = std::chrono::microseconds(800)) {
    using Clock = std::chrono::steady_clock;
    const auto coarse = deadline - spin;
    const auto now = Clock::now();
    if (coarse > now) {
#if defined(_WIN32)
        static thread_local HANDLE timer =
            CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        LARGE_INTEGER due;
        due.QuadPart =
            -std::max<long long>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(coarse - now).count() / 100);
        if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
            WaitForSingleObject(timer, INFINITE);
        else
            std::this_thread::sleep_until(coarse);
#else
        std::this_thread::sleep_until(coarse);
#endif
    }
    while (Clock::now() < deadline) {
#if defined(__x86_64__) || defined(_M_X64)
        _mm_pause();
#else
        std::this_thread::yield();
#endif
    }
}
} // namespace drakan_pacing
