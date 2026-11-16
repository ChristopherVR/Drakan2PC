// preciseSleepUntil: never early, and close to the deadline (the frame limiter's cadence depends on it). limiterPeriod:
// the interval frame_limit asks for (auto = the display refresh only while vsync is off).
#include "gs/gs_frame_limit.h"
#include "gs/gs_precise_sleep.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int checkLimiterPeriod() {
    using drakan_pacing::kFrameLimitAuto;
    using drakan_pacing::limiterPeriod;
    using std::chrono::nanoseconds;
    const double panel = 1.0 / 59.997;
    int bad = 0;
    auto expect = [&](const char* what, nanoseconds got, long long want) {
        if (got.count() != want) {
            std::fprintf(stderr, "limiterPeriod %s: %lld ns, want %lld ns\n", what, (long long)got.count(), want);
            ++bad;
        }
    };
    // Explicit values keep their old meaning with and without vsync.
    expect("off", limiterPeriod(0, false, panel), 0);
    expect("off, vsync", limiterPeriod(0, true, panel), 0);
    expect("60", limiterPeriod(60, false, panel), 16666666);
    expect("60, vsync", limiterPeriod(60, true, panel), 16666666);
    expect("144", limiterPeriod(144, false, 0.0), 6944444);
    // Auto: nothing while vsync paces, the exact refresh period otherwise, 60 Hz while the refresh is unknown.
    expect("auto, vsync", limiterPeriod(kFrameLimitAuto, true, panel), 0);
    expect("auto, 59.997 Hz", limiterPeriod(kFrameLimitAuto, false, panel), std::llround(panel * 1e9));
    expect("auto, 144 Hz", limiterPeriod(kFrameLimitAuto, false, 1.0 / 144.0), std::llround(1e9 / 144.0));
    expect("auto, unknown", limiterPeriod(kFrameLimitAuto, false, 0.0), std::llround(1e9 / 60.0));
    expect("auto, implausible", limiterPeriod(kFrameLimitAuto, false, 2.0), std::llround(1e9 / 60.0));
    // Other negative values are not auto: off, as max(0, value) made them before.
    expect("-5", limiterPeriod(-5, false, panel), 0);
    return bad;
}

int main() {
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono;
    if (checkLimiterPeriod())
        return 1;
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
