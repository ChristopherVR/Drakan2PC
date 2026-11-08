// The background ray pass build (src/ray_pass_loader.h) with a fake pass whose "compile" finishes only after N render
// frames. Regression: with a cold driver cache the DXR pipelines took 18 s and the first world frame that wanted a ray
// effect waited for them on the GS worker (the guest stalled); the title crashed reading the pass that was not built.
#include "ray_pass_loader.h"
#include "ray_status.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

using gsr::raystatus::acquirePass;
using gsr::raystatus::kStarting;
using gsr::raystatus::PassLoader;
using gsr::raystatus::PassView;
using gsr::raystatus::waterStatus;
using Clock = std::chrono::steady_clock;

namespace {

struct FakePass {
    bool ok = true;
    bool ready() const { return ok; }
    std::string status() const { return ok ? "fake ray tracing ready" : "fake ray tracing unavailable"; }
};

// The compile: blocks its (background) thread until the test releases it.
struct Gate {
    std::mutex m;
    std::condition_variable cv;
    bool open = false;
    void release() {
        {
            std::lock_guard lock(m);
            open = true;
        }
        cv.notify_all();
    }
    void wait() {
        std::unique_lock lock(m);
        cv.wait(lock, [&] { return open; });
    }
};

// The backends' pattern: a pass slot, the loader, the status line and one world frame.
struct FakeBackend {
    std::unique_ptr<FakePass> pass;
    PassLoader<FakePass> loader;
    Gate* gate = nullptr;
    bool passOk = true;
    std::atomic<int> builds{0}, adopted{0};
    bool modernWater = true;

    void start() {
        loader.start([this] {
            ++builds;
            gate->wait();
            auto p = std::make_unique<FakePass>();
            p->ok = passOk;
            return p;
        });
    }
    FakePass* acquire() {
        return acquirePass(
            pass, loader, [&] { start(); },
            [&](std::unique_ptr<FakePass> built) {
                ++adopted;
                return built;
            });
    }
    bool starting() const { return !pass && loader.pending(); }
    std::string waterText() const {
        PassView view;
        if (modernWater && pass)
            view = {true, pass->ready(), pass->status()};
        else if (modernWater && starting())
            return kStarting;
        return waterStatus(modernWater, view, false, true, "DXR reflections");
    }
    // One world frame: true when it traced (effects on), false when it drew the plain raster.
    bool frame() {
        FakePass* p = acquire();
        return p && p->ready();
    }
};

double ms(Clock::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
}

void completesAfterFrames() {
    constexpr int kReleaseFrame = 30;
    Gate gate;
    FakeBackend b;
    b.gate = &gate;
    assert(!b.loader.started() && !b.starting());
    assert(b.waterText().find("no eligible water world") != std::string::npos); // nothing building: no pass text
    double worst = 0;
    int firstTraced = -1;
    for (int f = 0; f < 100000; ++f) {
        if (f == kReleaseFrame)
            gate.release();
        const auto t0 = Clock::now();
        const bool traced = b.frame();
        const std::string text = b.waterText();
        worst = std::max(worst, ms(Clock::now() - t0));
        if (f < kReleaseFrame) {
            // Still compiling: the frame draws without ray effects and the settings say so.
            assert(!traced);
            assert(b.starting());
            assert(text == kStarting);
        }
        if (traced) {
            if (firstTraced < 0)
                firstTraced = f;
            assert(text.find("fake ray tracing ready") != std::string::npos);
        } else {
            assert(firstTraced < 0); // once on, the effects never switch off again (no flicker)
        }
        if (firstTraced >= 0 && f > firstTraced + 10)
            break;
        if (f >= kReleaseFrame && firstTraced < 0)
            std::this_thread::sleep_for(std::chrono::microseconds(200)); // a frame's worth of other work
    }
    assert(firstTraced >= kReleaseFrame);
    assert(b.builds == 1 && b.adopted == 1); // one build, adopted once, whatever the number of frames
    assert(!b.starting());
    // A frame never waits for the compile: generous bound (the compile itself was held for 30 frames).
    assert(worst < 50.0);
    std::cout << "ray pass adopted at frame " << firstTraced << ", slowest frame " << worst << " ms\n";
}

void unavailablePass() {
    Gate gate;
    gate.release();
    FakeBackend b;
    b.gate = &gate;
    b.passOk = false; // no ray tracing hardware: the pass exists but is not ready
    const auto until = Clock::now() + std::chrono::seconds(10);
    while (!b.acquire() && Clock::now() < until)
        std::this_thread::yield();
    assert(b.pass && !b.frame());
    assert(b.waterText() == "fake ray tracing unavailable");
}

void nullBuild() {
    PassLoader<FakePass> loader;
    std::unique_ptr<FakePass> pass;
    assert(!loader.take()); // before start
    int starts = 0;
    auto start = [&] {
        ++starts;
        loader.start([] { return std::unique_ptr<FakePass>(); });
    };
    auto adopt = [](std::unique_ptr<FakePass> p) { return p; };
    const auto until = Clock::now() + std::chrono::seconds(10);
    while (loader.pending() || !loader.started()) {
        assert(!acquirePass(pass, loader, start, adopt));
        if (Clock::now() > until)
            break;
    }
    for (int i = 0; i < 10; ++i)
        assert(!acquirePass(pass, loader, start, adopt)); // nothing to adopt, nothing dereferenced
    assert(starts == 1 && !loader.pending());
}

void shutdownDuringDelay() {
    std::atomic<bool> built{false};
    const auto t0 = Clock::now();
    {
        PassLoader<FakePass> loader;
        loader.start(
            [&] {
                built = true;
                return std::make_unique<FakePass>();
            },
            std::chrono::seconds(30)); // GSR_RAY_PREWARM_DELAY_MS
        assert(loader.pending() && !loader.take());
    } // the destructor cuts the delay short and skips the build
    assert(!built);
    assert(ms(Clock::now() - t0) < 5000.0);
}

} // namespace

int main() {
    completesAfterFrames();
    unavailablePass();
    nullBuild();
    shutdownDuringDelay();
    std::cout << "ray pass loader passed\n";
    return 0;
}
