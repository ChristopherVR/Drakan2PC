// CPU-only policy tests. No game, renderer, SDK or device is instantiated.
#include "gs_frame_activity.h"
#include "gs_scene_sync_profile.h"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <cstdio>

int main() {
    using namespace drakan_pacing;
    assert(wakeOnNeuralLatch(true, false, 0));
    assert(!wakeOnNeuralLatch(false, false, 0));
    assert(!wakeOnNeuralLatch(true, true, 0));
    assert(!wakeOnNeuralLatch(true, false, 30));
    LatchProgress latch;
    assert(!latch.pending());
    latch.publish();
    const auto first = latch.latest();
    latch.publish(); // worker publishes after the host released rmtx
    latch.acknowledge(first);
    assert(latch.pending() && latch.shown() == 1 && latch.latest() == 2);
    latch.acknowledge(latch.latest());
    assert(!latch.pending());

    // Match production's mutex discipline: predicate and publication under
    // qmtx; the wait releases it. A publication must wake the idle host.
    std::mutex m;
    std::condition_variable cv;
    std::promise<void> ready;
    auto waiter = std::async(std::launch::async, [&] {
        std::unique_lock<std::mutex> lock(m);
        ready.set_value();
        return cv.wait_for(lock, std::chrono::seconds(2), [&] { return latch.pending(); });
    });
    ready.get_future().wait();
    {
        std::lock_guard<std::mutex> lock(m);
        latch.publish();
    }
    cv.notify_all();
    assert(waiter.get());
    latch.acknowledge(latch.latest());

    WorldActivity w;
    gsr::SceneGuideCamera c;
    c.flags = c.Valid | c.World | c.HaveViewport;
    c.frameWidth = c.frameHeight = 512;
    gsr::SceneGuideCamera end;
    end.flags = end.EndWorld;
    end.sourcePc = 0x147690;
    w.scene(c, 10);
    w.scene(c, 11); // duplicate camera keeps original draw anchor
    w.scene(end, 12);
    assert(w.worlds() == 1);
    w.scene(end, 12);
    assert(w.worlds() == 1); // phase-end duplicate
    w.scene(c, 12);
    w.scene(end, 12);
    assert(w.worlds() == 1); // no draws
    c.guiScreen = 0x15;
    w.scene(c, 12);
    w.scene(end, 20);
    assert(w.worlds() == 1);
    c.guiScreen = -1;
    c.flags = 0;
    w.scene(c, 20);
    w.scene(end, 25);
    assert(w.worlds() == 1);
    c.flags = c.Valid | c.World | c.HaveViewport;
    w.scene(c, 25);
    gsr::SceneGuideCamera preview;
    preview.sourcePc = 0x1e97a0;
    preview.guiScreen = -1;
    w.scene(preview, 26);
    w.scene(end, 27);
    assert(w.worlds() == 2);
    c.flags = c.Valid | c.World;
    w.scene(c, 27);
    w.scene(end, 30);
    assert(w.worlds() == 2);
    SceneSyncProfile profile; // disabled by default; no output or GPU work
    profile.begin();
    profile.transfersDone();
    profile.finish(0x139010, false, false, false);
    std::puts("PASS: captured latch acknowledgement, host wake, duplicate/no-draw/GUI/invalid world guards");
}
