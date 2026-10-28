#include "drakan_scene_camera.h"
#include "drakan_scene_camera_policy.h"
#include "drakan_widescreen_scope.h"
#include "drakan_menus.h"
#include "drakan_inventory_views.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>
#include <cstdio>
#include <cstdlib>

namespace drakan_scene_camera {
namespace {
using Packet = gsr::SceneGuideCamera;
PS2Runtime::RecompiledFunction origBegin = nullptr, origViewport = nullptr, origPreview = nullptr;
PS2Runtime::RecompiledFunction origEndWorld = nullptr;
PS2Runtime::RecompiledFunction origInventoryDraw = nullptr, origLoadingDraw = nullptr;
PS2Runtime::RecompiledFunction origTitleDraw = nullptr;
PS2Runtime::RecompiledFunction origSubtitleDraw = nullptr;
PS2Runtime::RecompiledFunction origWaterDraw = nullptr;
std::atomic<bool> waterCaptureEnabled{false};
struct SubtitleScope {
    uint32_t returnPc = 0;
    bool active = false;
};
std::map<int, SubtitleScope> subtitleScopes;
std::map<int, SubtitleScope> waterScopes;
const std::atomic<uint64_t>* ticks = nullptr;
Sink sink = nullptr;
void* sinkUser = nullptr;
uint64_t nextSerial = 0, lastWorldTick = 0;
int lastGui = -999;
bool hadWorld = false;
Packet lastWorld;
Packet lastPublished;
bool havePublished = false;
PS2Runtime* installed = nullptr;
std::atomic<bool> captureEnabled{false}, captureReset{true};
struct Pending {
    Packet p;
    bool sourceMatchesWorld = false;
};
// EE executor only. Keep guest-thread/renderer contexts separate across yields.
std::map<std::pair<int, uint32_t>, Pending> pending;
// EE executor only. Diagnostic counters never affect camera/reset decisions.
struct ResetDiagnostics {
    uint64_t worlds = 0, resets = 0, previews = 0, nonWorlds = 0, modeChanges = 0, worldEnds = 0, reason[9]{};
    unsigned resetSamples = 0, stableSamples = 0, previewSamples = 0, summaries = 0;
    bool on() const {
        static const bool value = [] {
            const char* s = std::getenv("DRAKAN_CAMERA_RESET_DIAG");
            return s && *s && std::strcmp(s, "0");
        }();
        return value;
    }
    void note(const Packet& p, bool preview, uint32_t why, bool previousWorld, uint64_t previousTick, int previousGui,
              float distance2, float forwardDot, const Packet& previous) {
        if (!on())
            return;
        const bool world = (p.flags & Packet::World) != 0, reset = (p.flags & Packet::Reset) != 0;
        if (world) {
            ++worlds;
            resets += reset;
            for (unsigned i = 0; i < 9; ++i)
                reason[i] += (why >> i) & 1u;
        } else {
            ++nonWorlds;
            previews += preview;
        }
        bool sample = false;
        if (world && reset && resetSamples < 16) {
            ++resetSamples;
            sample = true;
        }
        if (world && !reset && stableSamples < 4) {
            ++stableSamples;
            sample = true;
        }
        if (preview && previewSamples < 4) {
            ++previewSamples;
            sample = true;
        }
        if (sample)
            std::fprintf(stderr,
                         "[camera-reset-diag] kind=%s serial=%llu flags=%x reason=%x tick=%llu previousTick=%llu "
                         "liveTick=%llu deltaUnsigned=%llu hadWorld=%u gui=%d previousGui=%d distance2=%g "
                         "forwardDot=%g projection=%g,%g previousProjection=%g,%g size=%ux%u previousSize=%ux%u\n",
                         preview ? "preview"
                         : world ? "world"
                                 : "nonworld",
                         (unsigned long long)p.serial, p.flags, why, (unsigned long long)p.captureTick,
                         (unsigned long long)previousTick,
                         (unsigned long long)(ticks ? ticks->load(std::memory_order_relaxed) : 0),
                         (unsigned long long)(p.captureTick - previousTick), unsigned(previousWorld), p.guiScreen,
                         previousGui, distance2, forwardDot, p.projectionScale[0], p.projectionScale[1],
                         previous.projectionScale[0], previous.projectionScale[1], p.frameWidth, p.frameHeight,
                         previous.frameWidth, previous.frameHeight);
        if (world && summaries < 8 && (worlds <= 4 || worlds % 64 == 0)) {
            ++summaries;
            std::fprintf(stderr,
                         "[camera-reset-summary] worlds=%llu resets=%llu previews=%llu nonworld=%llu modeChanges=%llu "
                         "endWorld=%llu nonWorldReason=%llu firstWorld=%llu guiChange=%llu tickGap=%llu backwards=%llu "
                         "jump=%llu turn=%llu sizeChange=%llu projectionChange=%llu\n",
                         (unsigned long long)worlds, (unsigned long long)resets, (unsigned long long)previews,
                         (unsigned long long)nonWorlds, (unsigned long long)modeChanges, (unsigned long long)worldEnds,
                         (unsigned long long)reason[0], (unsigned long long)reason[1], (unsigned long long)reason[2],
                         (unsigned long long)reason[3], (unsigned long long)reason[4], (unsigned long long)reason[5],
                         (unsigned long long)reason[6], (unsigned long long)reason[7], (unsigned long long)reason[8]);
        }
    }
} resetDiagnostics;
bool enabled() {
    if (!captureEnabled.load(std::memory_order_acquire))
        return false;
    if (captureReset.exchange(false)) {
        pending.clear();
        hadWorld = false;
        havePublished = false;
        if (resetDiagnostics.on())
            ++resetDiagnostics.modeChanges;
    }
    return true;
}
uint32_t rd(const uint8_t* ram, uint32_t a) {
    uint32_t v;
    std::memcpy(&v, ram + (a & 0x1fffffffu), 4);
    return v;
}
float rf(const uint8_t* ram, uint32_t a) {
    float v;
    std::memcpy(&v, ram + (a & 0x1fffffffu), 4);
    return v;
}
bool range(uint32_t a, uint32_t bytes) {
    return a >= 0x100000u && bytes <= PS2_RAM_SIZE && a <= PS2_RAM_SIZE - bytes;
}
int screen(const uint8_t* ram) {
    uint32_t p = rd(ram, 0x4fb60cu) & 0x1fffffffu;
    return range(p, 0x1c) ? (int)rd(ram, p + 0x18) : -1;
}
// Deny known front-end/loading/movie screens; retain gameplay/pause/subtitles.
// Unknown IDs remain observable; source validation is the primary world gate.
bool guiAllowsWorld(int id) {
    return id != 8 && id != 0xe && id != 0x13 && id != 0x1d && id != 0x1e;
}
std::pair<int, uint32_t> key(PS2Runtime* rt, R5900Context* c) {
    return {rt->eeScheduler().currentThreadId(), GPR_U32(c, 4) & 0x1fffffffu};
}
void begin(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (!enabled()) {
        origBegin(ram, c, rt);
        return;
    }
    // Interior resume PCs must not reinterpret overwritten argument registers.
    if (c->pc == 0x1e8c78u) {
        Pending item;
        Packet& p = item.p;
        p.serial = ++nextSerial;
        p.captureTick = ticks ? ticks->load(std::memory_order_relaxed) : 0;
        p.sourcePc = 0x1e8c78;
        p.rendererAddress = GPR_U32(c, 4) & 0x1fffffffu;
        p.sourceAddress = GPR_U32(c, 5) & 0x1fffffffu;
        p.guiScreen = screen(ram);
        const uint32_t obj = rd(ram, 0x5565c8u) & 0x1fffffffu;
        if (range(p.sourceAddress, 48) && range(obj, 0x110)) {
            float s[12], raw[12], expected[12];
            std::memcpy(s, ram + p.sourceAddress, 48);
            std::memcpy(raw, ram + 0x4dad58u, 48);
            std::memcpy(expected, raw, 48);
            const float sx = rf(ram, obj + 0x48), sy = rf(ram, obj + 0x4c);
            p.projectionScale[0] = sx;
            p.projectionScale[1] = sy;
            p.frameWidth = rd(ram, obj + 0xf8);
            p.frameHeight = rd(ram, obj + 0xfc);
            p.viewportOffset[0] = rf(ram, obj + 0x100);
            p.viewportOffset[1] = rf(ram, obj + 0x104);
            p.viewportScale[0] = rf(ram, obj + 0x40);
            p.viewportScale[1] = -rf(ram, obj + 0x44);
            const uint32_t scaleZ = 0x4d7fff00u;
            std::memcpy(&p.viewportScale[2], &scaleZ, 4);
            expected[0] *= sx;
            expected[1] *= sy;
            for (int i = 3; i < 6; ++i)
                expected[i] *= sx;
            for (int i = 6; i < 9; ++i)
                expected[i] *= sy;
            bool good = std::isfinite(sx) && std::isfinite(sy) && std::fabs(sx) > 1e-6f && std::fabs(sy) > 1e-6f;
            bool matches = good;
            for (int i = 0; i < 12; ++i) {
                good = good && std::isfinite(s[i]) && std::isfinite(raw[i]);
                const float tolerance = i < 3 ? std::max(0.125f, std::fabs(expected[i]) * 1e-6f) : 1e-4f;
                matches = matches && std::fabs(s[i] - expected[i]) <= tolerance;
            }
            if (good) {
                // The exact columns emitted at EE 0x1e922c..0x1e92b4.
                const float vp[16] = {s[3], s[6], 0, -s[9],  s[4], s[7], 0,  -s[10],
                                      s[5], s[8], 0, -s[11], s[0], s[1], 32, -s[2]};
                std::memcpy(p.worldToClipVU, vp, sizeof vp);
                const float r[3][3] = {
                    {s[3] / sx, s[4] / sx, s[5] / sx}, {s[6] / sy, s[7] / sy, s[8] / sy}, {s[9], s[10], s[11]}};
                const float t[3] = {s[0] / sx, s[1] / sy, s[2]};
                bool orthonormal = true;
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) {
                        float d = 0;
                        for (int k = 0; k < 3; ++k)
                            d += r[i][k] * r[j][k];
                        orthonormal = orthonormal && std::fabs(d - (i == j ? 1.0f : 0.0f)) < 0.01f;
                    }
                for (int i = 0; i < 3; ++i) {
                    for (int j = 0; j < 3; ++j)
                        p.worldToView[i * 4 + j] = r[j][i];
                    p.worldToView[12 + i] = t[i];
                    p.cameraRight[i] = r[0][i];
                    p.cameraUp[i] = r[1][i];
                    p.cameraForward[i] = -r[2][i];
                    p.cameraPosition[i] = -(r[0][i] * t[0] + r[1][i] * t[1] + r[2][i] * t[2]);
                }
                p.worldToView[15] = 1;
                p.viewToClipVU[0] = sx;
                p.viewToClipVU[5] = sy;
                p.viewToClipVU[11] = -1;
                p.viewToClipVU[14] = 32;
                if (orthonormal)
                    p.flags = Packet::Valid | Packet::InfiniteFar;
                item.sourceMatchesWorld = matches && orthonormal;
            }
        }
        pending[key(rt, c)] = item;
    }
    origBegin(ram, c, rt);
}
void emitViewport(uint8_t* ram, R5900Context* c, PS2Runtime* rt, bool preview) {
    const auto it = pending.find(key(rt, c));
    // Nonmatching VU scene matrices are commonly object-local MVPs. They are
    // NOT HUD boundaries: leave the true world camera in force through them.
    if (!preview && (it == pending.end() || !it->second.sourceMatchesWorld))
        return;
    Packet p;
    if (it != pending.end()) {
        p = it->second.p;
    } else {
        p.serial = ++nextSerial;
        p.guiScreen = screen(ram);
        p.captureTick = ticks ? ticks->load(std::memory_order_relaxed) : 0;
    }
    p.sourcePc = preview ? 0x1e97a0u : 0x1e93b8u;
    const uint32_t params = GPR_U32(c, 5) & 0x1fffffffu;
    if (range(params, 32)) {
        p.fogClamp[1] = rf(ram, params + 0x1c);
        p.fogClamp[2] = rf(ram, params + 0x10);
        p.fogClamp[3] = 255 * rf(ram, params + 0x18);
        p.viewportOffset[3] = 255 + p.fogClamp[3] * p.fogClamp[1];
        if (std::isfinite(p.fogClamp[1]) && std::isfinite(p.fogClamp[2]) && std::isfinite(p.fogClamp[3]) &&
            std::isfinite(p.viewportOffset[3]))
            p.flags |= Packet::HaveViewport;
    }
    // Native inventory reuses the camera/context and can match the template.
    // Only the authoritative engine phase may reopen a world after GUI-begin.
    if (policy::worldPacket(drakan_ws::renderingWorld(rt->eeScheduler().currentThreadId()), preview,
                            it != pending.end() && it->second.sourceMatchesWorld, guiAllowsWorld(p.guiScreen),
                            (p.flags & Packet::HaveViewport) != 0))
        p.flags |= Packet::World;
    if (preview) {
        p.flags &= ~(Packet::World | Packet::Valid);
    }
    const bool world = (p.flags & Packet::World) != 0;
    const bool previousWorld = hadWorld;
    const uint64_t previousTick = lastWorldTick;
    const int previousGui = lastGui;
    const Packet previous = lastWorld;
    float distance2 = 0, forwardDot = 0;
    if (world && hadWorld) {
        for (int i = 0; i < 3; ++i) {
            const float d = p.cameraPosition[i] - lastWorld.cameraPosition[i];
            distance2 += d * d;
            forwardDot += p.cameraForward[i] * lastWorld.cameraForward[i];
        }
        // Conservative capture policy, not a decoded game cutscene flag: a
        // >4096-unit jump, >60-degree turn, or projection/viewport change resets.
    }
    const policy::View currentView{p.captureTick, p.guiScreen,          p.frameWidth,
                                   p.frameHeight, p.projectionScale[0], p.projectionScale[1]};
    const policy::View previousView{lastWorldTick,
                                    lastGui,
                                    lastWorld.frameWidth,
                                    lastWorld.frameHeight,
                                    lastWorld.projectionScale[0],
                                    lastWorld.projectionScale[1]};
    const uint32_t resetReason =
        policy::reasons(world, hadWorld, ticks != nullptr, currentView, previousView, distance2, forwardDot);
    if (policy::reset(resetReason))
        p.flags |= Packet::Reset;
    if (world) {
        lastWorldTick = p.captureTick;
        lastWorld = p;
    }
    policy::updateAnchorContext(world, preview, p.guiScreen, hadWorld, lastGui);
    // Identical camera/viewport metadata can be emitted for many objects in one
    // engine tick. Do not force a VIF drain or an NR resolve per object.
    const uint32_t stateMask = Packet::Valid | Packet::World | Packet::HaveViewport;
    if (havePublished && p.captureTick == lastPublished.captureTick && p.guiScreen == lastPublished.guiScreen &&
        (p.flags & stateMask) == (lastPublished.flags & stateMask) &&
        std::memcmp(p.worldToClipVU, lastPublished.worldToClipVU, sizeof p.worldToClipVU) == 0 &&
        std::memcmp(p.viewportScale, lastPublished.viewportScale, sizeof p.viewportScale) == 0 &&
        std::memcmp(p.viewportOffset, lastPublished.viewportOffset, sizeof p.viewportOffset) == 0)
        return;
    lastPublished = p;
    havePublished = true;
    // Count published packets only, excluding duplicate object-local setup.
    resetDiagnostics.note(p, preview, resetReason, previousWorld, previousTick, previousGui, distance2, forwardDot,
                          previous);
    // The sink must order this after prior VIF/GS work and before this scene.
    if (sink)
        sink(*rt, p, sinkUser);
}
void viewport(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (!enabled()) {
        origViewport(ram, c, rt);
        return;
    }
    if (c->pc == 0x1e93b8u)
        emitViewport(ram, c, rt, false);
    origViewport(ram, c, rt);
}
void preview(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (!enabled()) {
        origPreview(ram, c, rt);
        return;
    }
    if (c->pc == 0x1e97a0u)
        emitViewport(ram, c, rt, true);
    origPreview(ram, c, rt);
}
void inventoryDraw(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    // Handler(this, widget, message): decimal 10 enters 0x42fd18, before
    // any inventory background/section geometry. The active screen global
    // can name a help popup while the inventory is drawn behind it; use this.
    const uint32_t self = GPR_U32(c, 4) & 0x1fffffffu;
    if (c->pc == 0x430d68u && GPR_U32(c, 6) == 10 && range(self, 0x1c) && rd(ram, self + 0x18) == 0x15 && enabled() &&
        sink) {
        Packet p;
        p.serial = ++nextSerial;
        p.captureTick = ticks ? ticks->load(std::memory_order_relaxed) : 0;
        p.sourcePc = 0x430d68u;
        p.sourceAddress = self;
        p.guiScreen = 0x15;
        // An Order-of-the-Flame style view (full-screen map, bottom strip) is centred instead of left-anchored.
        p.flags = drakan_views::g_centredScreen.load(std::memory_order_relaxed) == self ? Packet::CentredPage : 0;
        // The existing sink drains pending transfers and synchronizes VU1,
        // ordering this GUI boundary after world work and before original draw.
        sink(*rt, p, sinkUser);
        havePublished = false;
        // This is a render boundary, not a camera cut: leave the real-world
        // temporal anchor intact, as for an ordinary HUD/preview boundary.
    }
    origInventoryDraw(ram, c, rt);
}
void titleDraw(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t self = GPR_U32(c, 4) & 0x1fffffffu;
    if (c->pc == 0x43e4c8u && GPR_U32(c, 6) == 10 && range(self, 0x1c) && rd(ram, self + 0x18) == 8 && enabled() &&
        sink) {
        Packet p;
        p.serial = ++nextSerial;
        p.captureTick = ticks ? ticks->load(std::memory_order_relaxed) : 0;
        p.sourcePc = 0x43e4c8u;
        p.sourceAddress = self;
        p.guiScreen = 8;
        p.flags = 0;
        sink(*rt, p, sinkUser);
        havePublished = false;
    }
    origTitleDraw(ram, c, rt);
}
void loadingDraw(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    // The loading renderer can run before the input/settings tick observes
    // screen 14. Order its GUI canvas before its first bar/text submission,
    // so it cannot inherit gameplay HUD size/anchors or the previous camera.
    if (c->pc == 0x43d340u && enabled() && sink) {
        Packet p;
        p.serial = ++nextSerial;
        p.captureTick = ticks ? ticks->load(std::memory_order_relaxed) : 0;
        p.sourcePc = 0x43d340u;
        p.sourceAddress = GPR_U32(c, 4) & 0x1fffffffu;
        p.guiScreen = 14;
        p.flags = 0;
        const float offset = drakan_menus::loadingGroupOffset(ram, c, rt);
        std::memcpy(&p.reserved, &offset, sizeof(offset));
        sink(*rt, p, sinkUser);
        havePublished = false;
    }
    origLoadingDraw(ram, c, rt);
}
void subtitleDraw(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const int thread = rt->eeScheduler().currentThreadId();
    auto& scope = subtitleScopes[thread];
    auto marker = [&](bool active) {
        Packet p;
        p.sourcePc = 0x3afd18u;
        p.reserved = active ? 1u : 0u;
        p.captureTick = ticks ? ticks->load(std::memory_order_relaxed) : 0;
        // The sink drains earlier VIF/GIF work before publishing the marker.
        if (sink)
            sink(*rt, p, sinkUser);
    };
    if (c->pc == 0x3afd18u) {
        const uint32_t object = GPR_U32(c, 4) & 0x1fffffffu;
        scope.returnPc = GPR_U32(c, 31);
        scope.active =
            enabled() && sink && range(object, 0x24) && rd(ram, object + 0x20) > 0 && rd(ram, object + 0x10) != 3;
        if (scope.active)
            marker(true);
    }
    origSubtitleDraw(ram, c, rt);
    // Interior resumes are hooked too: a guest yield must not end the scope
    // before its remaining text and background packets have been submitted.
    if (scope.active && c->pc == scope.returnPc) {
        marker(false);
        scope.active = false;
    }
}
void waterDraw(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const int thread = rt->eeScheduler().currentThreadId();
    auto& scope = waterScopes[thread];
    auto marker = [&](bool active) {
        Packet p;
        p.sourcePc = 0x161a78u;
        p.reserved = active ? 1u : 0u;
        if (sink)
            sink(*rt, p, sinkUser);
    };
    if (c->pc == 0x161a78u) {
        scope.returnPc = GPR_U32(c, 31);
        scope.active = waterCaptureEnabled.load(std::memory_order_relaxed) && enabled() && sink &&
                       drakan_ws::renderingWorld(thread);
        if (scope.active)
            marker(true);
    }
    origWaterDraw(ram, c, rt);
    // Registered interior resumes preserve this scope through guest yields.
    // Draining the sink on exit includes the CPU-clipped 1626a8 path and the
    // final asynchronous VIF job before another material can start.
    if (scope.active && c->pc == scope.returnPc) {
        marker(false);
        scope.active = false;
    }
}
void endWorld(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (!enabled()) {
        origEndWorld(ram, c, rt);
        return;
    }
    if (c->pc == 0x147690u && GPR_U32(c, 31) == 0x139344u && sink) {
        if (resetDiagnostics.on())
            ++resetDiagnostics.worldEnds;
        Packet p;
        p.flags = Packet::EndWorld;
        p.sourcePc = 0x147690u;
        p.captureTick = ticks ? ticks->load(std::memory_order_relaxed) : 0;
        sink(*rt, p, sinkUser);
        havePublished = false; // the next engine frame can reuse an unchanged camera
    }
    origEndWorld(ram, c, rt);
}
} // namespace
void setEnabled(bool on) {
    if (captureEnabled.exchange(on) != on)
        captureReset.store(true);
}
void setWaterCaptureEnabled(bool on) {
    waterCaptureEnabled.store(on, std::memory_order_release);
}
void beginWorldPhase(PS2Runtime& rt) {
    if (installed != &rt || !enabled() || !sink)
        return;
    Packet p;
    p.serial = ++nextSerial;
    p.captureTick = ticks ? ticks->load(std::memory_order_relaxed) : 0;
    p.sourcePc = 0x139010u;
    p.flags = 0;
    sink(rt, p, sinkUser);
    havePublished = false; // the following validated camera is a separate event
}
void endWorldPhase(PS2Runtime& rt) {
    if (installed != &rt || !enabled() || !sink)
        return;
    Packet p;
    p.captureTick = ticks ? ticks->load(std::memory_order_relaxed) : 0;
    p.sourcePc = 0x139010u;
    p.flags = Packet::EndWorld;
    sink(rt, p, sinkUser);
    havePublished = false;
}
bool gridCapture(PS2Runtime& rt, bool begin, float x0, float y0, float x1, float y1) {
    if (installed != &rt || !enabled() || !sink)
        return false;
    Packet p;
    p.captureTick = ticks ? ticks->load(std::memory_order_relaxed) : 0;
    p.sourcePc = 0x437170u;
    p.reserved = begin ? 1u : 0u;
    p.viewportOffset[0] = x0;
    p.viewportOffset[1] = y0;
    p.viewportOffset[2] = x1;
    p.viewportOffset[3] = y1;
    // The sink drains earlier VIF/GIF work first, so the scope holds exactly the grid's draws.
    sink(rt, p, sinkUser);
    return true;
}
bool install(PS2Runtime& rt, const std::atomic<uint64_t>* tick, Sink cb, void* opaque) {
    if (installed)
        return installed == &rt;
    if (!cb || !rt.hasFunction(0x1e8c78u) || !rt.hasFunction(0x1e93b8u) || !rt.hasFunction(0x1e97a0u) ||
        !rt.hasFunction(0x147690u) || !rt.hasFunction(0x430d68u) || !rt.hasFunction(0x43d340u))
        return false;
    ticks = tick;
    sink = cb;
    sinkUser = opaque;
    origBegin = rt.lookupFunction(0x1e8c78u);
    origViewport = rt.lookupFunction(0x1e93b8u);
    origPreview = rt.lookupFunction(0x1e97a0u);
    rt.replaceFunction(0x1e8c78u, &begin);
    rt.replaceFunction(0x1e93b8u, &viewport);
    rt.replaceFunction(0x1e97a0u, &preview);
    origEndWorld = rt.lookupFunction(0x147690u);
    rt.replaceFunction(0x147690u, &endWorld);
    origInventoryDraw = rt.lookupFunction(0x430d68u);
    rt.replaceFunction(0x430d68u, &inventoryDraw);
    origLoadingDraw = rt.lookupFunction(0x43d340u);
    rt.replaceFunction(0x43d340u, &loadingDraw);
    origTitleDraw = rt.lookupFunction(0x43e4c8u);
    if (origTitleDraw)
        rt.replaceFunction(0x43e4c8u, &titleDraw);
    origSubtitleDraw = rt.lookupFunction(0x3afd18u);
    if (!origSubtitleDraw)
        return false;
    for (uint32_t pc :
         {0x3afd18u, 0x3afdccu, 0x3afe20u, 0x3afe44u, 0x3afe64u, 0x3afe88u, 0x3afeb0u, 0x3afeccu, 0x3aff00u, 0x3aff08u})
        rt.replaceFunction(pc, &subtitleDraw);
    origWaterDraw = rt.lookupFunction(0x161a78u);
    if (origWaterDraw)
        for (uint32_t pc = 0x161a78u; pc < 0x1626a8u; pc += 4)
            if (rt.hasFunction(pc) && rt.lookupFunction(pc) == origWaterDraw)
                rt.replaceFunction(pc, &waterDraw);
    installed = &rt;
    return true;
}
} // namespace drakan_scene_camera
