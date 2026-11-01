// preciseSleepUntil: never early, and close to the deadline (the frame limiter's cadence depends on it).
#include "gs/gs_precise_sleep.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main() {
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono;
    std::vector<double> late;
    auto next = Clock::now();
    for (int i = 0; i < 120; ++i) {
        next += microseconds(6944); // 144 Hz
        drakan_pacing::preciseSleepUntil(next);
        const auto now = Clock::now();
        if (now < next) {
            std::fprintf(stderr, "returned %.3f ms early\n", duration<double, std::milli>(next - now).count());
            return 1;
        }
        late.push_back(duration<double, std::milli>(now - next).count());
    }
    std::sort(late.begin(), late.end());
    const double median = late[late.size() / 2];
    std::printf("lateness median %.3f ms, p95 %.3f ms, max %.3f ms\n", median, late[late.size() * 95 / 100],
                late.back());
    // A loaded CI runner may deschedule the thread now and then; the median stays tight when the wait is precise.
    if (median > 0.5) {
        std::fprintf(stderr, "median lateness %.3f ms is above 0.5 ms\n", median);
        return 1;
    }
    // A deadline in the past returns at once.
    const auto t0 = Clock::now();
    drakan_pacing::preciseSleepUntil(t0 - milliseconds(5));
    if (Clock::now() - t0 > milliseconds(5))
        return 1;
    std::puts("PASS");
    return 0;
}
