// Drakan: optional DLSS glue, see drakan_dlss.h and docs/reference/dlss.md.
#include "drakan_dlss.h"
#include "gs/gs_scene_sync_profile.h"
#include "drakan_config.h"
#include "drakan_vu1_hook.h"
#include "drakan_scene_camera.h"
#include "drakan_scene_geometry.h"
#include "drakan_scene_models.h"
#include "drakan_scene_assets.h"
#include "drakan_object_motion.h"
#include "ps2_runtime.h"
#include "runtime/ps2_vu1_async.h"
#include "runtime/gs/gs_gsr_bridge.h"

#include <gsr/gsr.h>

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <memory>

#include "runtime/ps2_render_queue.h"

namespace drakan_dlss {
static drakan_scene_models::Assets reflectionAssets;
void setAssetSource(hle::IsoFs* fs) {
    reflectionAssets.source(fs);
}
namespace {
GsrBridge* g_bridge = nullptr;
gsr::Renderer* g_gs = nullptr;
std::atomic<bool> g_tap{false};
std::mutex g_mutex;
bool g_requested = false;
std::atomic<bool> g_neuralRequested{false};
std::atomic<const std::atomic<uint64_t>*> g_ticks{nullptr};
std::condition_variable g_idle;
unsigned g_active = 0;
// Never hold g_mutex across renderer/queue calls: the GS worker can query status from an overlay callback.
struct Lease {
    GsrBridge* bridge;
    gsr::Renderer* gs;
    Lease() {
        std::lock_guard<std::mutex> lock(g_mutex);
        bridge = g_bridge;
        gs = g_gs;
        ++g_active;
    }
    ~Lease() {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (--g_active == 0)
            g_idle.notify_all();
    }
};
void sceneSink(PS2Runtime& runtime, const gsr::SceneGuideCamera& camera, void*) {
    auto lease = std::make_shared<Lease>();
    // World/UI boundaries also drive native rendering (HUD classification).
    // Only the bridge lifetime gates them; SR/NR controls the expensive taps.
    if (!lease->bridge)
        return;
    const bool async = vu1async::renderQueueEnabled();
    const auto send = [lease, async](auto action) {
        if (async)
            vu1async::submitRenderTask([lease, action = std::move(action)] { action(*lease->bridge); });
        else
            action(*lease->bridge);
    };
    static drakan_pacing::SceneSyncProfile profile;
    profile.begin();
    const bool before = profile.enabled() && vu1async::g_pending.load(std::memory_order_acquire) != 0;
    runtime.memory().processPendingTransfers();
    profile.transfersDone();
    const bool after = profile.enabled() && vu1async::g_pending.load(std::memory_order_acquire) != 0;
    if (!async)
        vu1async::sync();
    profile.finish(camera.sourcePc, (camera.flags & gsr::SceneGuideCamera::EndWorld) != 0, before, after);
    send([camera](GsrBridge& bridge) { bridge.noteSceneCamera(camera); });
    // World entry precedes camera culling; export resident level layers, not
    // just the current GIF stream. Hash checks avoid reuploading static data.
    if (camera.sourcePc == 0x139010u && !(camera.flags & gsr::SceneGuideCamera::EndWorld)) {
        static uint64_t identity = 0;
        static unsigned checks = 0;
        static bool wasOn = false;
        const bool on = lease->gs && lease->gs->sceneModelsRequested();
        drakan_scene_camera::setWaterCaptureEnabled(lease->gs && lease->gs->waterRenderingRequested());
        if (on && (!wasOn || (++checks % 32) == 0)) {
            auto geometry = drakan_scene_geometry::capture(runtime.memory().getRDRAM(), PS2_RAM_SIZE, identity);
            if (geometry.identity != identity || !wasOn) {
                if (!wasOn && geometry.identity == identity)
                    geometry = drakan_scene_geometry::capture(runtime.memory().getRDRAM(), PS2_RAM_SIZE);
                identity = geometry.identity;
                std::fprintf(stderr, "[resident-scene] identity=%llx layers=%u waterLayers=%u triangles=%zu\n",
                             (unsigned long long)identity, geometry.layers, geometry.waterLayers,
                             geometry.triangles.size());
                send([geometry = std::move(geometry)](GsrBridge& bridge) { bridge.noteSceneGeometry(geometry); });
            }
        }
        drakan_scene_models::enabled = on;
        if (on) {
            static drakan_scene_models::Capture capture;
            static uint64_t assetLevel = ~0ull;
            if (assetLevel != identity) {
                reflectionAssets.clear();
                assetLevel = identity;
            }
            if (!wasOn)
                capture.reset();
            capture.loadTexture = [](uint32_t id, const std::string& path) { return reflectionAssets.load(id, path); };
            auto models = capture.capture(runtime.memory().getRDRAM(), PS2_RAM_SIZE, identity);
            if (!wasOn || checks % 120 == 0)
                std::fprintf(
                    stderr,
                    "[resident-models] level=%llx instances=%zu meshes=%zu textures=%zu rejected=%u untextured=%u\n",
                    (unsigned long long)identity, models->instances.size(), models->meshes.size(),
                    models->textures.size(), models->rejectedInstances, models->untexturedTriangles);
            send([models = std::move(models)](GsrBridge& bridge) { bridge.noteSceneModels(models); });
        }
        wasOn = on;
    }
}
void objectSink(const gsr::ObjectMotionScope& scope, void*) {
    Lease lease;
    if (g_tap.load(std::memory_order_relaxed) && lease.bridge)
        lease.bridge->noteObjectMotion(scope);
}

uint32_t modeFromText(const std::string& s) {
    const char* c = s.c_str();
    if (!_stricmp(c, "dlaa"))
        return 1;
    if (!_stricmp(c, "quality") || !_stricmp(c, "on"))
        return 2;
    if (!_stricmp(c, "balanced"))
        return 3;
    if (!_stricmp(c, "performance") || !_stricmp(c, "perf"))
        return 4;
    return 0;
}
uint32_t presetFromText(const std::string& s) {
    const char* c = s.c_str();
    if (!_stricmp(c, "j"))
        return 10;
    if (!_stricmp(c, "k"))
        return 11;
    if (!_stricmp(c, "l"))
        return 12;
    if (!_stricmp(c, "m"))
        return 13;
    return 0;
}

void tapHook(uint32_t addr, uint32_t qw, const uint8_t* vu1) {
    Lease lease;
    if (!g_tap.load(std::memory_order_relaxed) || !lease.bridge)
        return;
    float rows[28];
    std::memcpy(rows, vu1, sizeof(rows));
    lease.bridge->noteVuUpload(addr, qw, rows);
}

void installTap(bool on) {
    drakan_object_motion::setEnabled(on);
    g_tap.store(on);
    drakan_vu1::g_uploadHook.store(on ? &tapHook : nullptr, std::memory_order_release);
}
} // namespace
void commitDisplay(PS2Runtime& runtime) {
    Lease lease;
    if (!lease.bridge)
        return;
    runtime.memory().processPendingTransfers();
    vu1async::sync();
    lease.bridge->commitDisplay();
}

void configure(gsr::Config& cfg) {
    const std::string m = drakan_cfg::getString("video", "dlss", "DRAKAN_DLSS", "off");
    const std::string p = drakan_cfg::getString("video", "dlss_preset", "DRAKAN_DLSS_PRESET", "auto");
    cfg.upscaleMode = modeFromText(m);
    // Honor the renderer override too, so the camera tap and selected mode agree.
    if (const char* e = std::getenv("GSR_UPSCALE"))
        cfg.upscaleMode = modeFromText(e);
    g_requested = cfg.upscaleMode != 0;
    cfg.neuralRendering = drakan_cfg::getBool("video", "dlss_neural", "DRAKAN_DLSS_NEURAL", false);
    cfg.neuralRuntimeDir = drakan_cfg::getString("video", "dlss_neural_runtime", "DRAKAN_DLSS_NEURAL_DIR", "");
    cfg.neuralAtRenderResolution = neuralResolutionIsRender(
        drakan_cfg::getString("video", "dlss_neural_resolution", "DRAKAN_DLSS_NEURAL_RESOLUTION", "output"));
    g_neuralRequested = cfg.neuralRendering;
    cfg.upscalePreset = presetFromText(p);
    cfg.upscaleSharpness = (float)drakan_cfg::getFloat("video", "dlss_sharpness", "DRAKAN_DLSS_SHARPNESS", 0.0);
}

void installSceneHooks(PS2Runtime& runtime) {
    g_ticks.store(&runtime.memory().gs().vsyncTick, std::memory_order_release);
    if (!drakan_scene_camera::install(runtime, &runtime.memory().gs().vsyncTick, &sceneSink, nullptr))
        std::fprintf(stderr, "[dlss] engine scene camera hooks unavailable\n");
    if (!drakan_object_motion::install(runtime, &objectSink, nullptr))
        std::fprintf(stderr, "[dlss] engine object motion hooks unavailable\n");
}

void attach(GsrBridge* bridge, gsr::Renderer* gs) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_bridge = bridge;
    g_gs = gs;
    drakan_scene_camera::setEnabled(bridge != nullptr);
    installTap((g_requested || g_neuralRequested) && bridge);
}

void detach() {
    std::unique_lock<std::mutex> lock(g_mutex);
    drakan_scene_camera::setEnabled(false);
    installTap(false);
    g_ticks.store(nullptr, std::memory_order_release);
    g_bridge = nullptr;
    g_gs = nullptr;
    g_idle.wait(lock, [] { return g_active == 0; });
}

void setMode(const std::string& mode, const std::string& preset, float sharpness) {
    Lease lease;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_requested = modeFromText(mode) != 0;
        installTap((g_requested || g_neuralRequested) && g_bridge);
    }
    if (!lease.gs)
        return;
    lease.gs->setUpscale(modeFromText(mode), presetFromText(preset), sharpness);
}

bool available(std::string* why) {
    Lease lease;
    std::string reason = "renderer not attached";
    const bool candidate = lease.gs && lease.gs->upscaleAvailable(reason);
    if (why)
        *why = reason;
    return candidate;
}

std::string status() {
    Lease lease;
    return lease.gs ? lease.gs->upscaleStatus() : "DLSS SR unavailable: renderer not attached";
}
void setNeural(bool on) {
    Lease lease;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_neuralRequested = on;
        installTap((g_requested || g_neuralRequested) && g_bridge);
    }
    if (lease.gs)
        lease.gs->setNeuralRendering(on);
}
bool neuralResolutionIsRender(const std::string& value) {
    return _stricmp(value.c_str(), "render") == 0;
}
void setNeuralResolution(const std::string& value) {
    Lease lease;
    if (lease.gs)
        lease.gs->setNeuralResolution(neuralResolutionIsRender(value));
}
bool neuralRequested() {
    return g_neuralRequested.load(std::memory_order_acquire);
}
void hostTick() {
    const char* sequence = std::getenv("DRAKAN_NR_TEST_SEQ");
    const auto* ticks = g_ticks.load(std::memory_order_acquire);
    if (!sequence || !ticks)
        return;
    static const auto entries = [sequence] {
        std::vector<std::pair<uint64_t, bool>> result;
        const char* cursor = sequence;
        while (*cursor) {
            char* end = nullptr;
            const auto tick = std::strtoull(cursor, &end, 10);
            if (end == cursor || *end != ':' || (end[1] != '0' && end[1] != '1'))
                break;
            result.emplace_back(tick, end[1] == '1');
            cursor = std::strchr(end + 2, ',');
            if (!cursor)
                break;
            ++cursor;
        }
        return result;
    }();
    static size_t next = 0;
    const auto tick = ticks->load(std::memory_order_relaxed);
    if (next < entries.size() && tick >= entries[next].first) {
        const bool on = entries[next++].second;
        std::fprintf(stderr, "[dlss-nr-test] tick=%llu enable=%d\n", (unsigned long long)tick, (int)on);
        setNeural(on);
    }
}
bool neuralAvailable(std::string* why) {
    Lease lease;
    std::string reason = "renderer not attached";
    const bool ok = lease.gs && lease.gs->neuralRenderingAvailable(reason);
    if (why)
        *why = reason;
    return ok;
}
std::string neuralStatus() {
    Lease lease;
    return lease.gs ? lease.gs->neuralRenderingStatus() : "DLSS 5 unavailable: renderer not attached";
}
} // namespace drakan_dlss
