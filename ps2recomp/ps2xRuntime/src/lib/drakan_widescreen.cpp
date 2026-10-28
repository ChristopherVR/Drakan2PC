// Drakan: widescreen (anamorphic Hor+) - see drakan_widescreen.h and docs/reference/widescreen.md.
#include "drakan_widescreen.h"
#include "drakan_widescreen_policy.h"
#include "drakan_inventory_viewport_policy.h"
#include "drakan_widescreen_scope.h"
#include "drakan_config.h"
#include "drakan_host.h"
#include "drakan_settings_host.h"

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_ipu.h"
#ifdef PS2X_HAVE_GSR
#include "drakan_scene_camera.h"
#endif

#include <gsr/gsr.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace drakan_ws {
namespace {
// Guest addresses (SCES_500.06, see docs/reference/widescreen.md section 2)
constexpr uint32_t kCameraUpdateMat = 0x1480e0u; // FUN_001480e0: copies the 2x3 view template, multiplies row 0 / 1 by
                                                 // obj+0x48 / +0x4c; once per camera update
constexpr uint32_t kSetViewport = 0x132d68u;     // FUN_00132d68: computes obj+0x48 / +0x4c
constexpr uint32_t kCropViewport = 0x132f60u;    // later screen-manager crop, after full viewport setup
constexpr uint32_t kRenderWorld = 0x139010u;
constexpr uint32_t kEndWorld = 0x147690u; // only RA 0x139344 ends the world before native HUD/GUI callbacks
constexpr uint32_t kBuildFrustum = 0x199ff0u;
constexpr uint32_t kRenderObjPtr = 0x5565c8u;        // *(u32*)0x5565c8 = render / viewport object
constexpr uint32_t kScreenManagerActive = 0x4fb60cu; // active GUI screen object, its id at +0x18
constexpr float k43 = 4.0f / 3.0f;

PS2Runtime::RecompiledFunction g_origCameraUpdateMat = nullptr;
PS2Runtime::RecompiledFunction g_origSetViewport = nullptr;
PS2Runtime::RecompiledFunction g_origCropViewport = nullptr;
PS2Runtime::RecompiledFunction g_origRenderWorld = nullptr, g_origEndWorld = nullptr, g_origBuildFrustum = nullptr;
detail::ProjectionScope g_projectionScope; // EE executor only; survives guest yields
uint8_t* g_rdram = nullptr;
const std::atomic<uint64_t>* g_vsync = nullptr;

std::mutex g_mu; // guards the configuration below
Mode g_mode = Mode::Fixed;
float g_fixedAspect = k43; // Mode::Fixed
bool g_hudCorrect = true;
bool g_smoothHL = true;
float g_hudSizeCfg = 0.0f; // hud_scale: 0 = auto
constexpr float kMaxAspect =
    32.0f / 9.0f; // wider windows are pillarboxed at 32:9 instead of stretching the picture further
gsr::Renderer* g_gs = nullptr;
void* g_hwnd = nullptr;

std::atomic<float> g_projAspect{k43}; // read by the EE hooks: aspect the 3D world is rendered for
std::atomic<float> g_fovScale{1.0f};
std::atomic<float> g_worldProjX{0.0f};        // sc+0x40 * sc+0x48 at the last world camera update (worldProjectionX)
std::atomic<float> g_inventoryHudScale{1.0f}; // exact foreground compression sent to GSR
std::atomic<uint64_t> g_worldTick{0};         // vblank counter at the last 3D camera update
std::atomic<bool> g_wide{false};
std::atomic<int> g_wsLog{0};

// state of hostTick (window thread only)
bool g_pushedMenu = false;
int g_pushedMenuScreen = -1;
bool g_pushedValid = false, g_pushedWide = false, g_pushedStretch = false;
float g_pushedAspect = 0.0f, g_pushedHud = 0.0f, g_pushedSize = 0.0f;
detail::WorldPolicy g_worldPolicy;

float parseAspectValue(const std::string& str, Mode& mode, float def) {
    const char* s = str.c_str();
    mode = Mode::Fixed;
    if (!*s)
        return def;
    if (drakan_host::asciiEqual(s, "auto") || drakan_host::asciiEqual(s, "window")) {
        mode = Mode::Auto;
        return def;
    }
    if (drakan_host::asciiEqual(s, "native")) {
        mode = Mode::Native;
        return k43;
    }
    if (drakan_host::asciiEqual(s, "stretch") || drakan_host::asciiEqual(s, "fill")) {
        mode = Mode::Stretch;
        return def;
    }
    float a = 0, b = 0;
    if (std::sscanf(s, "%f:%f", &a, &b) == 2 && a > 0 && b > 0)
        return a / b;
    if (std::sscanf(s, "%f", &a) == 1 && a > 0.5f)
        return a;
    return def;
}

int activeScreenId() {
    if (!g_rdram)
        return -1;
    auto rd = [&](uint32_t a) {
        uint32_t v;
        std::memcpy(&v, g_rdram + (a & 0x1FFFFFFFu), 4);
        return v;
    };
    const uint32_t s = rd(kScreenManagerActive);
    if (s < 0x100000u || s + 0x20u >= PS2_RAM_SIZE)
        return -1;
    return static_cast<int>(rd(s + 0x18u));
}

// Id of the screen below the active one (screen+0x34, the screen stack), -1 = none.
int parentScreenId() {
    if (!g_rdram)
        return -1;
    auto rd = [&](uint32_t a) {
        uint32_t v;
        std::memcpy(&v, g_rdram + (a & 0x1FFFFFFFu), 4);
        return v;
    };
    const uint32_t s = rd(kScreenManagerActive);
    if (s < 0x100000u || s + 0x40u >= PS2_RAM_SIZE)
        return -1;
    const uint32_t p = rd(s + 0x34u) & 0x1FFFFFFFu;
    if (p < 0x100000u || p + 0x20u >= PS2_RAM_SIZE)
        return -1;
    return static_cast<int>(rd(p + 0x18u));
}

uint32_t renderObject() {
    uint32_t obj = 0;
    std::memcpy(&obj, g_rdram + kRenderObjPtr, 4);
    obj &= 0x1FFFFFFFu;
    if (obj < 0x100000u || obj + 0x120u >= PS2_RAM_SIZE)
        return 0;
    return obj;
}

void applyProjection(float aspect, bool forceDirty = false, float fovScale = 1.0f) {
    const uint32_t obj = renderObject();
    if (!obj)
        return;
    float f10c = 0, fEc = 0;
    std::memcpy(&f10c, g_rdram + obj + 0x10c, 4);
    std::memcpy(&fEc, g_rdram + obj + 0xec, 4);
    if (!(fEc > 0.0f) || !std::isfinite(fEc) || !std::isfinite(f10c))
        return;
    float height;
    std::memcpy(&height, g_rdram + obj + 0xf0, 4);
    if (!(height > 0) || !std::isfinite(height))
        return;
    const float x = detail::projectionX(f10c, fEc, aspect) * fovScale;
    const float y = -f10c / height * fovScale;
    float previous;
    float previousY;
    std::memcpy(&previous, g_rdram + obj + 0x48, 4);
    std::memcpy(&previousY, g_rdram + obj + 0x4c, 4);
    std::memcpy(g_rdram + obj + 0x48, &x, 4);
    std::memcpy(g_rdram + obj + 0x4c, &y, 4);
    if (forceDirty || previous != x || previousY != y) {
        // 0x134450 must rebuild both the camera and CPU planes after a scope
        // transition, including a stationary world following an inventory view.
        const uint32_t dirty = 1;
        std::memcpy(g_rdram + obj + 0xd4, &dirty, 4);
    }
}

void closeWorldProjection(int thread) {
    if (g_projectionScope.end(thread))
        applyProjection(k43);
}

void hookRenderWorld(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const int thread = rt->eeScheduler().currentThreadId();
    if (c->pc == kRenderWorld) {
        // Latch for one engine world: a window-thread mode change cannot split
        // the matrix and CPU frustum between two different aspect factors.
        // Preserve authored cinematics; latch both axes together for a world.
        uint32_t scripted = 0, ctx = 0, handle = 0, inner = 0, camera = 0, mode = 0;
        auto read = [&](uint32_t a) {
            uint32_t v = 0;
            if (a >= 0x100000u && a <= PS2_RAM_SIZE - 4)
                std::memcpy(&v, rdram + a, 4);
            return v;
        };
        scripted = read(0x4fbafc);
        ctx = read(0x5565d4);
        handle = ctx ? read(ctx + 0x4b4) : 0;
        inner = handle ? read(handle + 0xfc) : 0;
        camera = inner ? read(inner + 0x10) : 0;
        mode = camera ? read(camera + 0x14c) : 0;
        const float fov = !scripted && mode == 6 ? g_fovScale.load(std::memory_order_relaxed) : 1.0f;
        g_projectionScope.begin(thread, GPR_U32(c, 31), g_projAspect.load(std::memory_order_relaxed), fov);
        applyProjection(g_projectionScope.aspect, true, g_projectionScope.fovScale);
        if (g_wsLog.load() > 0) {
            static float last = -1;
            if (last != fov) {
                last = fov;
                const uint32_t obj = renderObject();
                float sx = 0, sy = 0;
                if (obj) {
                    std::memcpy(&sx, rdram + obj + 0x48, 4);
                    std::memcpy(&sy, rdram + obj + 0x4c, 4);
                }
                std::fprintf(stderr, "[fov] scale=%.7f sx=%.7f sy=%.7f mode=%u scripted=%u\n", fov, sx, sy, mode,
                             scripted);
            }
        }
#ifdef PS2X_HAVE_GSR
        // Unlike a validated camera packet, this covers sky/background draws
        // emitted before the first world MVP has been recognized.
        drakan_scene_camera::beginWorldPhase(*rt);
#endif
    }
    g_origRenderWorld(rdram, c, rt);
    // Also close on early/failed world returns that never reach EndWorld.
    // A yielded nested guest call has a different PC and keeps its scope.
    if (g_projectionScope.world(thread) && c->pc == g_projectionScope.returnPc) {
#ifdef PS2X_HAVE_GSR
        drakan_scene_camera::endWorldPhase(*rt);
#endif
        closeWorldProjection(thread);
    }
}

void hookEndWorld(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    // Installed before scene_camera: its outer sink drains prior VIF/GS work,
    // then this chained hook restores native projection before GUI callbacks.
    if (c->pc == kEndWorld && GPR_U32(c, 31) == 0x139344u)
        closeWorldProjection(rt->eeScheduler().currentThreadId());
    g_origEndWorld(rdram, c, rt);
}

void hookBuildFrustum(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t obj = renderObject();
    const uint32_t input = GPR_U32(c, 5) & 0x1fffffffu;
    if (obj && input >= 0x100000u && input < PS2_RAM_SIZE - 12) {
        float width, focal, sx, sy;
        std::memcpy(&width, rdram + input, 4);
        std::memcpy(&focal, rdram + input + 8, 4);
        std::memcpy(&sx, rdram + obj + 0x48, 4);
        std::memcpy(&sy, rdram + obj + 0x4c, 4);
        if (detail::correctFrustumWidth(g_projectionScope, rt->eeScheduler().currentThreadId(), c->pc, GPR_U32(c, 31),
                                        GPR_U32(c, 4) & 0x1fffffffu, obj, input, GPR_U32(c, 29) & 0x1fffffffu, focal,
                                        sx, width)) {
            std::memcpy(rdram + input, &width, 4);
            // FOV changes both axes; CPU culling must match the GPU projection.
            if (g_projectionScope.fovScale != 1.0f && std::isfinite(sy) && sy < -1e-6f) {
                const float height = focal / -sy;
                std::memcpy(rdram + input + 4, &height, 4);
            }
        }
    }
    g_origBuildFrustum(rdram, c, rt);
}

void hookCameraUpdateMat(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == kCameraUpdateMat) {
        const int thread = rt->eeScheduler().currentThreadId();
        if (g_projectionScope.world(thread))
            noteWorldFrame();
        // UI reuses the same camera pointer: always write the correct scale,
        // including native, before the original reads +0x48.
        applyProjection(g_projectionScope.effectiveAspect(thread), false, g_projectionScope.effectiveFovScale(thread));
        const uint32_t obj = renderObject();
        if (obj && g_projectionScope.world(thread)) {
            float sx = 0, extent = 0;
            std::memcpy(&sx, rdram + obj + 0x48, 4);
            std::memcpy(&extent, rdram + obj + 0x40, 4);
            const float product = sx * extent;
            if (std::isfinite(product) && product > 0)
                g_worldProjX.store(product, std::memory_order_relaxed);
        }
    }
    g_origCameraUpdateMat(rdram, c, rt);
}

void hookSetViewport(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    static const bool logInventory = [] {
        const char* p = std::getenv("DRAKAN_INVENTORY_VIEWPORT_LOG");
        return p && std::atoi(p) > 0;
    }();
    static unsigned inventoryViewportLogs = 0;
    if (logInventory && c->pc == kSetViewport && activeScreenId() == 21 && inventoryViewportLogs++ < 80)
        std::fprintf(stderr, "[inventory-viewport] ra=%x obj=%x x=%d y=%d w=%u h=%u world=%d\n", GPR_U32(c, 31),
                     GPR_U32(c, 4), (int)GPR_U32(c, 5), (int)GPR_U32(c, 6), GPR_U32(c, 7), GPR_U32(c, 8),
                     (int)g_projectionScope.world(rt->eeScheduler().currentThreadId()));
    g_origSetViewport(rdram, c, rt);
    const int thread = rt->eeScheduler().currentThreadId();
    applyProjection(g_projectionScope.effectiveAspect(thread), false, g_projectionScope.effectiveFovScale(thread));
}

void hookCropViewport(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    static const bool logInventory = [] {
        const char* p = std::getenv("DRAKAN_INVENTORY_VIEWPORT_LOG");
        return p && std::atoi(p) > 0;
    }();
    if (c->pc == kCropViewport && GPR_U32(c, 31) == 0x2c4444u &&
        detail::inventoryCropScreen(activeScreenId(), parentScreenId()) == 21) {
        const uint32_t obj = renderObject();
        if (obj) {
            uint32_t frameWidth, viewportWidth;
            std::memcpy(&frameWidth, rdram + obj + 0xf8, 4);
            std::memcpy(&viewportWidth, rdram + obj + 0x118, 4);
            float expanded = c->f[12];
            if (detail::inventoryWorldCrop(c->pc, GPR_U32(c, 31), 21, GPR_U32(c, 4) & 0x1fffffffu, obj, frameWidth,
                                           viewportWidth, static_cast<int32_t>(GPR_U32(c, 5)),
                                           g_inventoryHudScale.load(std::memory_order_relaxed), expanded)) {
                static unsigned cropLogs = 0;
                if (logInventory && cropLogs++ < 24)
                    std::fprintf(
                        stderr,
                        "[inventory-world-crop] obj=%x width=%u offset=%d oldScale=%g newScale=%g hudScale=%g\n", obj,
                        frameWidth, static_cast<int32_t>(GPR_U32(c, 5)), c->f[12], expanded,
                        g_inventoryHudScale.load(std::memory_order_relaxed));
                c->f[12] = expanded;
                const uint32_t dirty = 1;
                std::memcpy(rdram + obj + 0xd4, &dirty, 4);
            }
        }
    }
    g_origCropViewport(rdram, c, rt);
}

float windowHeight() {
#if defined(_WIN32)
    if (g_hwnd) {
        RECT rc{};
        if (GetClientRect(static_cast<HWND>(g_hwnd), &rc) && rc.bottom > 0)
            return static_cast<float>(rc.bottom);
    }
#endif
    return 1080.0f;
}

float windowAspect() {
#if defined(_WIN32)
    if (g_hwnd) {
        RECT rc{};
        if (GetClientRect(static_cast<HWND>(g_hwnd), &rc) && rc.bottom > 0 && rc.right > 0)
            return static_cast<float>(rc.right) / static_cast<float>(rc.bottom);
    }
#endif
    return 16.0f / 9.0f;
}
} // namespace

bool renderingWorld(int thread) {
    return g_projectionScope.world(thread);
}

void configure() {
    g_fovScale.store(
        detail::fieldOfViewScale(static_cast<float>(drakan_cfg::getFloat("video", "fov", "DRAKAN_FOV", 80.0))));
    std::string a = drakan_cfg::getString("video", "aspect", "DRAKAN_WS_ASPECT", "4:3");
    if (!std::getenv("DRAKAN_WS_ASPECT") && std::getenv("PS2X_ASPECT"))
        a = std::getenv("PS2X_ASPECT");
    Mode m;
    const float v = parseAspectValue(a, m, k43);
    std::lock_guard<std::mutex> l(g_mu);
    g_mode = m;
    g_fixedAspect = v;
    g_hudCorrect = drakan_cfg::getBool("video", "hud_aspect_correct", "DRAKAN_HUD_ASPECT", true);
    {
        const std::string hs = drakan_cfg::getString("video", "hud_scale", "DRAKAN_HUD_SCALE", "auto");
        float x = 0.0f;
        g_hudSizeCfg = (hs.empty() || drakan_host::asciiEqual(hs, "auto") || std::sscanf(hs.c_str(), "%f", &x) != 1)
                           ? 0.0f
                           : std::min(1.5f, std::max(0.5f, x));
    }
    g_smoothHL = drakan_cfg::getBool("video", "smooth_menu_highlight", "DRAKAN_SMOOTH_HIGHLIGHT", true);
}

void install(PS2Runtime& rt, uint8_t* rdram, const std::atomic<uint64_t>* vsyncTick) {
    g_rdram = rdram;
    g_vsync = vsyncTick;
    if (rt.hasFunction(kCameraUpdateMat) && rt.hasFunction(kSetViewport) && rt.hasFunction(kRenderWorld) &&
        rt.hasFunction(kEndWorld) && rt.hasFunction(kBuildFrustum)) {
        g_origCameraUpdateMat = rt.lookupFunction(kCameraUpdateMat);
        rt.replaceFunction(kCameraUpdateMat, &hookCameraUpdateMat);
        g_origSetViewport = rt.lookupFunction(kSetViewport);
        rt.replaceFunction(kSetViewport, &hookSetViewport);
        g_origRenderWorld = rt.lookupFunction(kRenderWorld);
        rt.replaceFunction(kRenderWorld, &hookRenderWorld);
        g_origEndWorld = rt.lookupFunction(kEndWorld);
        rt.replaceFunction(kEndWorld, &hookEndWorld);
        g_origBuildFrustum = rt.lookupFunction(kBuildFrustum);
        rt.replaceFunction(kBuildFrustum, &hookBuildFrustum);
    } else
        std::fprintf(stderr, "[ws] projection functions not found: widescreen unavailable\n");
    if (rt.hasFunction(kCropViewport)) {
        g_origCropViewport = rt.lookupFunction(kCropViewport);
        rt.replaceFunction(kCropViewport, &hookCropViewport);
    }
    const char* l = std::getenv("DRAKAN_WS_LOG");
    g_wsLog = l ? std::atoi(l) : 0;
}

void setAspectText(const std::string& text) {
    Mode m;
    {
        std::lock_guard<std::mutex> l(g_mu);
        const float v = parseAspectValue(text, m, g_fixedAspect);
        g_mode = m;
        g_fixedAspect = v;
    }
    std::fprintf(stderr, "[ws] aspect set to '%s'\n", text.c_str());
}

void setHudCorrect(bool on) {
    std::lock_guard<std::mutex> l(g_mu);
    g_hudCorrect = on;
}

void setFieldOfView(float degrees) {
    g_fovScale.store(detail::fieldOfViewScale(degrees), std::memory_order_relaxed);
    std::fprintf(stderr, "[ws] field of view set to %.0f degrees\n", degrees);
}

void setHudScale(float v) {
    std::lock_guard<std::mutex> l(g_mu);
    g_hudSizeCfg = v <= 0.0f ? 0.0f : std::min(1.5f, std::max(0.5f, v));
}

void setSmoothHighlight(bool on) {
    gsr::Renderer* gs;
    {
        std::lock_guard<std::mutex> l(g_mu);
        g_smoothHL = on;
        gs = g_gs;
    }
#ifdef PS2X_HAVE_GSR
    if (gs)
        gs->setSmoothMenuHighlight(on);
#endif
}

void attachRenderer(gsr::Renderer* gs, void* hwnd) {
    std::lock_guard<std::mutex> l(g_mu);
    g_gs = gs;
    g_hwnd = hwnd;
    g_pushedValid = false;
}

void noteWorldFrame() {
    if (g_vsync)
        g_worldTick.store(g_vsync->load(std::memory_order_relaxed), std::memory_order_relaxed);
}

bool wideActive() {
    return g_wide.load();
}

float worldProjectionX() {
    return g_worldProjX.load(std::memory_order_relaxed);
}

// The game opens on the logo screens, which are rendered as a world (wide) page, so start in the layout hostTick will
// pick for them. Starting in 4:3 showed a pillarboxed picture for the first frames before the first tick widened it.
void initialPresenter(float& forcedAspect, bool& stretch, float windowAspect) {
    std::lock_guard<std::mutex> l(g_mu);
    stretch = false;
    forcedAspect = k43;
    if (g_mode == Mode::Native) {
        forcedAspect = 0.0f;
        return;
    }
    const float target = g_mode == Mode::Fixed ? g_fixedAspect : windowAspect;
    if (target <= 1.4f)
        return;
    g_worldPolicy.wide = true; // hostTick keeps it while the page stays the gameplay page (-1) or a camera runs
    if (g_mode == Mode::Fixed || windowAspect > kMaxAspect + 0.01f)
        forcedAspect = std::min(target, kMaxAspect);
    else {
        forcedAspect = 0.0f;
        stretch = true;
    }
}

bool smoothHighlightInitial() {
    std::lock_guard<std::mutex> l(g_mu);
    return g_smoothHL;
}

// Test aid: DRAKAN_WS_TEST_SEQ="<vsync>:<aspect>,<vsync>:<aspect>,..." calls setAspectText (the function the F1 menu
// uses) when the vblank counter passes each entry.
void testSequenceTick() {
    static const char* env = std::getenv("DRAKAN_WS_TEST_SEQ");
    if (!env || !g_vsync)
        return;
    static std::vector<std::pair<uint64_t, std::string>> seq;
    static size_t next = 0;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        std::string s = env;
        size_t pos = 0;
        while (pos < s.size()) {
            size_t comma = s.find(',', pos);
            std::string item = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            const size_t colon = item.find(':');
            if (colon != std::string::npos)
                seq.push_back({std::strtoull(item.c_str(), nullptr, 10), item.substr(colon + 1)});
            if (comma == std::string::npos)
                break;
            pos = comma + 1;
        }
    }
    if (next < seq.size() && g_vsync->load(std::memory_order_relaxed) >= seq[next].first)
        setAspectText(seq[next++].second);
}

void hostTick() {
    testSequenceTick();
    Mode mode;
    float fixed;
    bool hud;
    float hudSizeCfg;
    gsr::Renderer* gs;
    {
        std::lock_guard<std::mutex> l(g_mu);
        mode = g_mode;
        fixed = g_fixedAspect;
        hud = g_hudCorrect;
        hudSizeCfg = g_hudSizeCfg;
        gs = g_gs;
    }
    if (!gs)
        return;
    // --- is a 3D world being presented? Native pause/inventory can hold the
    // camera just like F1; camera silence there does not mean the world ended.
    const uint64_t now = g_vsync ? g_vsync->load(std::memory_order_relaxed) : 0;
    const uint64_t wt = g_worldTick.load(std::memory_order_relaxed);
    const int scr = activeScreenId();
    static uint64_t movieFrames = 0, movieTick = 0; // vblank of the newest IPU movie frame
    if (const uint64_t frames = PS2Ipu::decodedFrameCount(); frames != movieFrames) {
        movieFrames = frames;
        movieTick = now ? now : 1;
    }
    g_worldPolicy.update(now, wt, scr, drakan_settings_host::isOpen(), movieTick);
    const bool g_stateWide = g_worldPolicy.wide;
    if (!drakan_settings_host::isOpen()) {
        if (g_wsLog.load() > 0) {
            static bool lastWide = false;
            static int lastScr = -9;
            if (g_stateWide != lastWide || scr != lastScr) {
                std::fprintf(stderr, "[ws] vsync=%llu world=%d screen=%d worldAge=%llu\n", (unsigned long long)now,
                             g_stateWide ? 1 : 0, scr, (unsigned long long)(wt ? now - wt : 0));
                lastWide = g_stateWide;
                lastScr = scr;
            }
        }
    }

    // --- what the projection and the presenter should be
    float target = k43; // aspect the world is rendered for
    if (mode == Mode::Fixed)
        target = fixed;
    else if (mode == Mode::Auto || mode == Mode::Stretch)
        target = windowAspect();
    const float winAspect = (mode == Mode::Auto || mode == Mode::Stretch) ? target : 0.0f;
    target = std::min(target, kMaxAspect); // sane bounds: beyond 32:9 the picture is pillarboxed instead of stretched
    const auto layout = detail::layoutPolicy(scr, g_stateWide, mode != Mode::Native, target, hud);
    const bool wide = layout.presentWide || layout.stretchImage;
    float present = k43; // forced presenter aspect (0 = native display aspect)
    bool stretch = false;
    if (mode == Mode::Native)
        present = 0.0f;
    else if (wide) {
        if (mode == Mode::Fixed)
            present = target;
        else if (winAspect > kMaxAspect + 0.01f)
            present = target; // window wider than 32:9: pillarbox at 32:9
        else {
            present = 0.0f; // Auto / Stretch: fill the window
            stretch = true;
        }
    }
    const float proj = layout.projection;
    const float hudScale = layout.foregroundScale;
    g_inventoryHudScale.store(hudScale, std::memory_order_relaxed);
    // HUD size: only while a world page is shown (title menu / FMV / loading stay as they are). auto: follows the
    // output height (the HUD is laid out for a 512 line picture; at 1080 lines or more it would otherwise look zoomed
    // in), 1.0 = original proportions.
    float hudSize = 1.0f;
    if (detail::gameplayHudSize(scr, g_stateWide) && mode != Mode::Native) {
        if (hudSizeCfg > 0.0f)
            hudSize = hudSizeCfg;
        else {
            const float outH = std::min(windowHeight(), windowHeight() * windowAspect() /
                                                            (wide ? target : k43)); // height of the presented picture
            hudSize = std::min(1.0f, std::max(0.55f, 0.85f * std::sqrt(1080.0f / std::max(outH, 200.0f))));
        }
    }

    g_projAspect.store(proj, std::memory_order_relaxed);
    g_wide.store(proj > 1.4f);
    const int menuScreen = layout.menu ? scr : -1; // picks the wide backdrop before the screen's own draw packet
    if (!g_pushedValid || layout.menu != g_pushedMenu || menuScreen != g_pushedMenuScreen || wide != g_pushedWide ||
        present != g_pushedAspect || stretch != g_pushedStretch || std::fabs(hudScale - g_pushedHud) > 1e-4f ||
        std::fabs(hudSize - g_pushedSize) > 1e-3f) {
#ifdef PS2X_HAVE_GSR
        gs->setNativeMenu(layout.menu, menuScreen);
#endif
        g_pushedMenu = layout.menu;
        g_pushedMenuScreen = menuScreen;
#ifdef PS2X_HAVE_GSR
        gs->setAspect(present, stretch);
        gs->setHudScaleX(hudScale);
        gs->setHudSize(hudSize);
#endif
        g_pushedSize = hudSize;
        g_pushedValid = true;
        g_pushedWide = wide;
        g_pushedAspect = present;
        g_pushedStretch = stretch;
        g_pushedHud = hudScale;
        if (g_wsLog.load() > 0)
            std::fprintf(stderr,
                         "[ws] push: wide=%d projection aspect %.3f, present aspect %.3f%s, hud scale %.3f size %.3f\n",
                         wide ? 1 : 0, proj, present, stretch ? " (stretch)" : "", hudScale, hudSize);
    }
}
} // namespace drakan_ws
