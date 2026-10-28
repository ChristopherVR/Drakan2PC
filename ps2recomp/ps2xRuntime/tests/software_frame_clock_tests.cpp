#include "drakan_software_frame_clock.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <cmath>
int main() {
    drakan_presentation::FrameMetrics metrics;
    metrics.frame(1000000000);
    metrics.gameDelta(0.5f);
    assert(metrics.gameSeconds() == 0 && metrics.updates() == 0);
    metrics.enable(true);
    int64_t time = 1000000000;
    metrics.frame(time);
    for (int64_t interval : {16000000, 24000000, 29000000, 32000000, 39000000, 49000000, 99000000, 120000000}) {
        time += interval;
        metrics.frame(time);
    }
    for (unsigned bin = 0; bin < 8; ++bin)
        assert(metrics.bucket(bin) == 1);
    metrics.frame(time - 1);
    metrics.uploaded(1, 1000000000);
    metrics.uploaded(1, 1100000000); // A repeated host paint is not another game image.
    metrics.uploaded(3, 1032000000);
    metrics.uploadError();
    assert(metrics.uploads() == 2 && metrics.uploadErrors() == 1);
    assert(metrics.uploadBucket(3) == 1 && metrics.uploadBucket(7) == 0);
    metrics.gameDelta(0.02f);
    metrics.gameDelta(0.03f);
    assert(std::fabs(metrics.gameSeconds() - 0.05) < 0.00000001 && metrics.updates() == 2);
    metrics.gameDelta(-1);
    metrics.gameDelta(std::numeric_limits<float>::infinity());
    metrics.gameDelta(std::numeric_limits<float>::quiet_NaN());
    assert(metrics.invalid() == 4 && metrics.updates() == 2);
    metrics.enable(false);
    assert(metrics.invalid() == 0 && metrics.gameSeconds() == 0 && metrics.bucket(7) == 0);

    drakan_presentation::FrameClock clock;
    assert(!clock.select(10, 1000000000).completed);
    clock.reset(true);
    assert(!clock.select(11, 1000000000).completed);
    clock.commit(1000000000);
    const auto first = clock.select(12, 1100000000);
    assert(first.completed && first.value == 1 && clock.completedFrames() == first.value);
    const auto repaint = clock.select(13, 1200000000);
    assert(repaint.completed && repaint.value == first.value);
    clock.commit(1300000000);
    const auto next = clock.select(14, 1400000000);
    assert(next.completed && next.value == 2 && clock.completedFrames() == next.value);
    const auto stale = clock.select(20, 1900000000);
    assert(!stale.completed && stale.value == 20);
    clock.commit(2000000000);
    assert(clock.select(21, 2100000000).completed);
    clock.reset(false);
    clock.commit(2200000000);
    const auto disabled = clock.select(22, 2300000000);
    assert(!disabled.completed && disabled.value == 22 && clock.completedFrames() == 0);
    clock.reset(true);
    assert(!clock.select(23, 2400000000).completed);
    std::puts("Completed-frame latching and stale-progress fallback passed");
}
