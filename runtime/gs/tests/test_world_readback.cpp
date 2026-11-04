// Hardware regression: a closed world read by the guest before HUD/display must contain the modern resolve.
#include "d3d11_backend_impl.h"
#include <cassert>
#include <cstdio>

using namespace gsr;

int main() {
    LocalMemory memory;
    Config config;
    config.scale = 1;
    config.allowWarp = false;
    config.textureUpscale = 1;
    Stats stats;
    D3D11Backend::Impl renderer(memory, config, stats);
    if (!renderer.init())
        return 77;
    renderer.waterRenderer = std::make_unique<surface::RayWater>(renderer.dev.Get(), renderer.ctx.Get());
    if (!renderer.waterRenderer->ready()) {
        std::printf("SKIP: %s\n", renderer.waterRenderer->status().c_str());
        return 77;
    }
    renderer.upMode = 0;
    renderer.modernWater = renderer.rayReflections = renderer.rayShadows = false;
    renderer.rayGlobalIllumination = true;
    auto models = std::make_shared<SceneModels>();
    models->lighting.ambient[0] = models->lighting.ambient[1] = models->lighting.ambient[2] = 1;
    renderer.nativeModels = models;

    SceneGuideCamera camera{};
    camera.flags = SceneGuideCamera::Valid | SceneGuideCamera::World | SceneGuideCamera::HaveViewport |
                   SceneGuideCamera::InfiniteFar | SceneGuideCamera::Reset;
    camera.frameWidth = camera.frameHeight = 64;
    camera.worldToView[0] = camera.worldToView[5] = camera.worldToView[10] = camera.worldToView[15] = 1;
    camera.worldToClipVU[0] = camera.worldToClipVU[5] = 1;
    camera.worldToClipVU[11] = -1;
    camera.worldToClipVU[14] = 32;
    camera.viewportScale[0] = 32;
    camera.viewportScale[1] = -32;
    camera.viewportScale[2] = 4294967296.0f / 16;
    camera.viewportOffset[0] = camera.viewportOffset[1] = 2048;
    camera.cameraRight[0] = camera.cameraUp[1] = 1;
    camera.cameraForward[2] = -1;
    const uint64_t xy = uint64_t(2016 * 16) | (uint64_t(2016 * 16) << 32);
    auto* target = renderer.getRT(0, 1, PSM_CT32, 64);
    auto* depth = renderer.getDepth(140, target->w, target->h);
    assert(target && depth && depth->dsv);
    auto prepare = [&](bool closed, bool enabled) {
        const float color[4]{.2f, .2f, .2f, .5f};
        renderer.ctx->ClearRenderTargetView(target->rtv.Get(), color);
        renderer.ctx->ClearDepthStencilView(depth->dsv.Get(), D3D11_CLEAR_DEPTH, .1f, 0);
        renderer.sceneCamera = camera;
        renderer.rayGlobalIllumination = enabled;
        renderer.motionEpoch++;
        renderer.neuralReset = true;
        renderer.enterTemporalWorld(*target, 140, 4294967296.0f, xy);
        target->worldEpoch = renderer.motionEpoch;
        target->surface.epoch = renderer.motionEpoch;
        target->surface.triangles.clear();
        auto quad = [&](float z, std::array<float, 3> color) {
            surface::Triangle a{{{-10000, -10000, z}, {10000, -10000, z}, {10000, 10000, z}},
                                {color[0], color[1], color[2]}};
            auto b = a;
            b.p[1] = a.p[2];
            b.p[2] = {-10000, 10000, z};
            target->surface.triangles = {a, b};
        };
        quad(-320, {.5f, .5f, .5f});
        auto receiver = target->surface.triangles;
        quad(400, {1, 0, 0});
        target->surface.triangles.insert(target->surface.triangles.begin(), receiver.begin(), receiver.end());
        target->dirty = true;
        target->dy0 = 0;
        target->dy1 = 64;
        ++target->writes;
        target->wstate = 0; // combined modern world owns neuralState, not legacy SR's wstate
        renderer.engineWorldOpen = !closed;
        renderer.pendingTemporalEnd = closed;
        renderer.neuralWorldClosed = closed;
    };
    auto read = [&] {
        renderer.flushToMemory(0, 1023); // readVram/guest screenshot funnel
        std::vector<uint32_t> pixels(64 * 64);
        memory.readRectCT32(0, 1, 0, 0, 64, 64, pixels.data());
        return pixels;
    };
    prepare(true, true);
    DisplayState display{};
    display.c[0].enable = true;
    display.c[0].fbw = 1;
    display.c[0].dw = display.c[0].dh = 63;
    renderer.latchDisplay(display, 0);
    const auto resolved = read();
    prepare(false, true);
    const auto active = read();
    assert(target->neuralState == 1); // an active-world read must not finalize partial geometry
    assert(active != resolved);       // the fixture actually produces visible indirect illumination
    prepare(true, true);
    renderer.nativeWaterPass = true;
    assert(read() == active);
    assert(target->neuralState == 1); // a post-EndWorld native-water stream still owns its pending geometry
    renderer.nativeWaterPass = false;
    prepare(true, false);
    assert(read() == active); // with effects disabled the same closed-world read preserves the native image
    prepare(true, true);
    const auto closed = read();
    assert(closed == resolved);
    assert(target->rayOutputEpoch == renderer.motionEpoch);
    std::puts("Closed-world guest readback matches latch-first resolve; active-world and disabled controls passed");
}
