#pragma once
#include <atomic>
#include <cstdint>
#include <gsr/scene_guide_camera.h>

namespace drakan_pacing {
inline bool wakeOnNeuralLatch(bool neural, bool overlay, int limit) {
    return neural && !overlay && limit <= 0;
}
// Publish under qmtx; capture while holding rmtx. Acknowledging a capture must
// never consume a newer latch published after the renderer lock was released.
class LatchProgress {
    std::atomic<uint64_t> produced{0}, presented{0};

public:
    uint64_t latest() const { return produced.load(std::memory_order_acquire); }
    uint64_t shown() const { return presented.load(std::memory_order_acquire); }
    void publish() { produced.fetch_add(1, std::memory_order_release); }
    void acknowledge(uint64_t captured) { presented.store(captured, std::memory_order_release); }
    bool pending() const { return latest() != shown(); }
};

// Context-lock owned telemetry, independent of neural duplicate suppression.
// Count completed positive world scenes with draws; never infer worlds from
// vblank, display-buffer flips, camera serials or host timer ticks.
class WorldActivity {
    bool open = false;
    uint64_t beginDraws = 0, completed = 0;

public:
    void scene(const gsr::SceneGuideCamera& c, uint64_t draws) {
        using C = gsr::SceneGuideCamera;
        if (c.flags & C::EndWorld) {
            if (open && draws > beginDraws)
                ++completed;
            open = false;
            return;
        }
        // Phase begin is emitted before camera validation; do not call it a
        // world update until a positive camera and real drawing follow it.
        if (c.sourcePc == 0x139010u && c.flags == 0) {
            open = false;
            return;
        }
        constexpr auto required = C::Valid | C::World | C::HaveViewport;
        if ((c.flags & required) == required && c.guiScreen < 0 && c.frameWidth && c.frameHeight) {
            if (!open)
                beginDraws = draws;
            open = true;
        } else if (c.sourcePc != 0x1e97a0u || c.guiScreen >= 0)
            open = false;
    }
    uint64_t worlds() const { return completed; }
};
} // namespace drakan_pacing
