// Drakan: ghost players (drakan_ghost.h). PAL addresses are canonical, translated through the disc profile.
#include "drakan_ghost.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "gameui/GuestMemory.h"
#include "imgui.h"
#include "input/DrakanInput.h"
#include "input/GuestInterface.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "world/Pose.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

void ps2xNoPreemptEnter() noexcept;
void ps2xNoPreemptLeave() noexcept;

namespace drakan_ghost {
namespace {

using drakan_region::guestAddress;

constexpr uint32_t kContextPtr = 0x5565d4u, kFactory = 0x181ad8u, kDestroy = 0x1413c0u, kRynnHandler = 0x30ea30u,
                   kFreeCamHandler = 0x2a3378u;
constexpr uint32_t kHandleVtable = 0x51b8b0u, kRynnClass = 0x10009u;
constexpr uint32_t kDikF9 = 0x43u, kDikF10 = 0x44u;
constexpr float kOffsetX = 300.0f;
float g_offX = kOffsetX, g_offZ = 0.0f; // DRAKAN_GHOST_OFFSET=x,z (mirror mode test aid)
constexpr uint32_t kFrame = 0x60;       // stack bytes the spawn trampoline borrows below the dispatcher thread's sp

struct Ghost {
    Info info;
    bool destroyRequested = false;
    bool hasTransform = false; // target is set (a spawn at a given transform, or a pending setTransform)
    bool hold = false;         // after the spawn, rewrite `target` into the actor every tick
    Transform target;
    // Clip state written into the skeleton every tick (channels 0..31; bit set in animSet = channel is driven).
    uint32_t animSet = 0;
    uint32_t animClip[world::skel::kMaxChannels] = {};
    float animTime[world::skel::kMaxChannels] = {};
};

std::atomic<bool> g_enabled{false};
std::mutex g_m;
std::vector<Ghost> g_ghosts;
int g_nextId = 1;
std::atomic<bool> g_busy{false};
bool g_log = false, g_hold = true;
uint32_t g_forwardMask =
    0; // bit n = event 1<<n forwarded to Rynn's real handler (event 2 always goes to the free-cam handler)
bool g_mirror = false;            // DRAKAN_GHOST_MIRROR=1: the ghost replays the local player 1 s late
bool g_autoPending = false;       // mirror mode: (re)spawn the ghost once the player is ready
unsigned g_autoDelay = 0;         // ticks to wait before the auto spawn
world::PoseBuffer g_buf;          // EE thread only
world::PoseReadState g_readState; // EE thread only
unsigned g_tick = 0;
uint32_t g_t1 = 0, g_t2 = 0, g_t3 = 0, g_handlerSlot = 0;
PS2Runtime::RecompiledFunction g_rynn = nullptr;

// In-flight spawn (one at a time; written on the EE thread before the post, read by the trampolines on the dispatcher)
struct Flight {
    int ghostId = 0;
    float xform[12] = {};
    uint32_t actor = 0, player = 0, camera = 0, resIndex = 0, camModeAddr = 0, savedSp = 0, savedRa = 0, oldMode = 0;
} g_flight;

uint32_t rd32(const uint8_t* ram, uint32_t a) {
    a &= 0x1ffffffu;
    uint32_t v = 0;
    if (a + 4 <= PS2_RAM_SIZE)
        std::memcpy(&v, ram + a, 4);
    return v;
}
void wr32(uint8_t* ram, uint32_t a, uint32_t v) {
    a &= 0x1ffffffu;
    if (a + 4 <= PS2_RAM_SIZE)
        std::memcpy(ram + a, &v, 4);
}
bool ramPtr(uint32_t p) {
    return p >= 0x100000u && p < 0x2000000u;
}

// Spawn trampoline, stage 1 (a postGuestCall target on a guest dispatcher thread): put the transform on the stack, set
// the camera mode word to 7 (Rynn's init takes its early-out for a second body), call 0x181ad8 returning to stage 2.
void spawnStage1(uint8_t* rdram, R5900Context* c, PS2Runtime*) {
    ps2xNoPreemptEnter(); // the camera mode word must stay 7 until Rynn's init has read it: no thread switch
    g_flight.savedSp = getRegU32(c, 29);
    g_flight.savedRa = getRegU32(c, 31);
    const uint32_t sp = g_flight.savedSp - kFrame, xf = sp + 0x10;
    std::memcpy(rdram + (xf & 0x1ffffffu), g_flight.xform, sizeof g_flight.xform);
    if (g_flight.camModeAddr) {
        g_flight.oldMode = rd32(rdram, g_flight.camModeAddr);
        wr32(rdram, g_flight.camModeAddr, 7u);
    }
    const uint32_t ctx = rd32(rdram, guestAddress(kContextPtr));
    g_flight.player = rd32(rdram, ctx + 0x4b0);
    g_flight.camera = rd32(rdram, ctx + 0x4b4);
    SET_GPR_U32(c, 29, sp);
    SET_GPR_U32(c, 4, ctx);
    SET_GPR_U32(c, 5, 0u);
    SET_GPR_U32(c, 6, xf);
    SET_GPR_U32(c, 7, 0u);
    SET_GPR_U32(c, 8, 0u);
    SET_GPR_U32(c, 9, g_flight.resIndex);
    SET_GPR_U32(c, 31, g_t2);
    c->pc = guestAddress(kFactory);
}

// Stage 2: the factory returned the actor in v0. Restore the camera mode and the stack, swap the handler (what 0x283ae0
// does: *(actor+0xfc)+0x14), record the ghost.
void spawnStage2(uint8_t* rdram, R5900Context* c, PS2Runtime*) {
    const uint32_t actor = getRegU32(c, 2);
    if (g_flight.camModeAddr)
        wr32(rdram, g_flight.camModeAddr, g_flight.oldMode);
    {
        const uint32_t ctx = rd32(rdram, guestAddress(kContextPtr));
        const uint32_t p = rd32(rdram, ctx + 0x4b0), cam = rd32(rdram, ctx + 0x4b4);
        std::fprintf(stderr, "[ghost] after factory: ctx+4b0 %x -> %x, ctx+4b4 %x -> %x (mode word now %x)\n",
                     g_flight.player, p, g_flight.camera, cam, rd32(rdram, g_flight.camModeAddr));
        wr32(rdram, ctx + 0x4b0, g_flight.player);
        wr32(rdram, ctx + 0x4b4, g_flight.camera);
    }
    std::lock_guard<std::mutex> l(g_m);
    auto it =
        std::find_if(g_ghosts.begin(), g_ghosts.end(), [](const Ghost& g) { return g.info.id == g_flight.ghostId; });
    if (ramPtr(actor)) {
        const uint32_t inner = rd32(rdram, actor + 0xfc);
        if (ramPtr(inner))
            wr32(rdram, inner + 0x14, g_handlerSlot);
        if (it != g_ghosts.end()) {
            uint16_t ref = 0, res = 0;
            std::memcpy(&ref, rdram + ((actor + 0x12) & 0x1ffffffu), 2);
            std::memcpy(&res, rdram + ((actor + 0x10) & 0x1ffffffu), 2);
            it->info.handle = actor;
            std::memcpy(it->target.pos, g_flight.xform, sizeof it->target.pos);
            std::memcpy(it->target.rot, g_flight.xform + 3, sizeof it->target.rot);
            it->hold = g_hold;
            float pos[3];
            std::memcpy(pos, rdram + ((actor + 0x14) & 0x1ffffffu), sizeof pos);
            std::fprintf(stderr, "[ghost] pos right after the factory: %g %g %g (asked %g %g %g)\n",
                         static_cast<double>(pos[0]), static_cast<double>(pos[1]), static_cast<double>(pos[2]),
                         static_cast<double>(g_flight.xform[0]), static_cast<double>(g_flight.xform[1]),
                         static_cast<double>(g_flight.xform[2]));
            it->info.ref = ref;
            it->info.resIndex = res;
        }
        std::fprintf(stderr, "[ghost] spawned id %d handle %x (res %u)\n", g_flight.ghostId, actor, g_flight.resIndex);
    } else {
        std::fprintf(stderr, "[ghost] spawn id %d failed (v0=%x)\n", g_flight.ghostId, actor);
        if (it != g_ghosts.end())
            g_ghosts.erase(it);
    }
    // Rynn's init took the camera-mode-7 early out, which skips the world insert (the engine's free-cam body is
    // inserted by its own handler): do it here the way event 1 does, through the level object's vtable +0x74 (this
    // adjusted by the +0x70 offset).
    const uint32_t world = rd32(rdram, rd32(rdram, guestAddress(kContextPtr)) + 0x31c);
    const uint32_t vt = ramPtr(world) ? rd32(rdram, world + 0x24b4) : 0;
    const uint32_t fn = ramPtr(vt) ? rd32(rdram, vt + 0x74) : 0;
    if (ramPtr(actor) && fn) {
        uint16_t adj = 0;
        std::memcpy(&adj, rdram + ((vt + 0x70) & 0x1ffffffu), 2);
        SET_GPR_U32(c, 4, world + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(adj))));
        SET_GPR_U32(c, 5, actor);
        SET_GPR_U32(c, 6, 0xffffffffu);
        SET_GPR_U32(c, 31, g_t3);
        g_flight.actor = actor;
        c->pc = fn;
        return;
    }
    SET_GPR_U32(c, 29, g_flight.savedSp);
    SET_GPR_U32(c, 31, g_flight.savedRa);
    c->pc = g_flight.savedRa;
    ps2xNoPreemptLeave();
    g_busy.store(false);
}

// Stage 3: the world insert returned; hand the actor back as the result.
void spawnStage3(uint8_t*, R5900Context* c, PS2Runtime*) {
    std::fprintf(stderr, "[ghost] world insert of %x returned %x\n", g_flight.actor, getRegU32(c, 2));
    SET_GPR_U32(c, 2, g_flight.actor);
    SET_GPR_U32(c, 29, g_flight.savedSp);
    SET_GPR_U32(c, 31, g_flight.savedRa);
    c->pc = g_flight.savedRa;
    ps2xNoPreemptLeave();
    g_busy.store(false);
}

// The ghost's behaviour handler: handler(actor, event). Forwarded events go to Rynn's real handler, the rest return 0.
void ghostHandler(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t event = getRegU32(c, 5);
    unsigned bit = 0;
    while (bit < 32 && !(event >> bit & 1u))
        ++bit;
    const bool forward = event && (event & (event - 1)) == 0 && (g_forwardMask >> bit & 1u);
    if (g_log)
        std::fprintf(stderr, "[ghost] handler actor=%x event=%x %s\n", getRegU32(c, 4), event,
                     forward ? "forward" : "swallow");
    if (event == 2) { // destroy: the free-cam body's handler removes the actor from the world and frees what it set up
        c->pc = guestAddress(kFreeCamHandler);
        rt->lookupFunction(c->pc)(rdram, c, rt);
        return;
    }
    if (forward && g_rynn) {
        c->pc = guestAddress(kRynnHandler); // the handler resumes by pc: it was entered through our slot
        g_rynn(rdram, c, rt);
        return;
    }
    SET_GPR_U32(c, 2, 0u);
    c->pc = getRegU32(c, 31);
}

uint32_t freeSlot(PS2Runtime& rt, PS2Runtime::RecompiledFunction fn) {
    for (uint32_t a = 0x100100u; a < 0x100400u; a += 4)
        if (!rt.hasFunction(a) && rt.registerFunction(a, fn))
            return a;
    return 0;
}

// world::IGuestMemory over the input glue's IGuest (EE thread).
class GuestMem final : public gameui::IGuestMemory {
public:
    explicit GuestMem(drakan_input::IGuest& g) : g_(g) {}
    uint8_t read8(uint32_t a) const override { return g_.read8(a); }
    uint32_t read32(uint32_t a) const override { return g_.read32(a); }

private:
    drakan_input::IGuest& g_;
};

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Writes the driven clips into the ghost skeleton's channels (sk = *(handle+0x50), channels at sk+0xe8, stride 0x80,
// clip +0x50, time +0x58). Returns the number of channels written.
int writeAnim(drakan_input::IGuest& guest, const Ghost& g) {
    if (!g.animSet)
        return 0;
    const uint32_t sk = guest.read32(g.info.handle + world::skel::kHandleSkeleton);
    if (!ramPtr(sk))
        return 0;
    const uint32_t ch = guest.read32(sk + world::skel::kChannels);
    const uint32_t n = guest.read32(sk + world::skel::kChannelCount);
    if (!ramPtr(ch) || n == 0 || n > world::skel::kMaxChannels)
        return 0;
    int written = 0;
    for (uint32_t i = 0; i < n; ++i)
        if (g.animSet >> i & 1u) {
            const uint32_t a = ch + i * world::skel::kChannelStride;
            guest.write32(a + world::skel::kChannelClip, g.animClip[i]);
            guest.writeF(a + world::skel::kChannelTime, g.animTime[i]);
            ++written;
        }
    return written;
}

// Caller holds g_m. Position, rotation and the clips of every channel (lower on the mask, upper on the rest).
void applyPose(Ghost& g, const world::PlayerPose& p) {
    const float pos[3] = {p.position.x, p.position.y, p.position.z};
    std::memcpy(g.target.pos, pos, sizeof pos);
    std::memcpy(g.target.rot, p.rotation, sizeof g.target.rot);
    g.hasTransform = true;
    for (uint32_t c = 0; c < world::skel::kMaxChannels; ++c) {
        const world::ChannelPose& cp = (p.lowerMask >> c & 1u) ? p.lower : p.upper;
        g.animSet |= 1u << c;
        g.animClip[c] = cp.clip;
        g.animTime[c] = cp.time;
    }
}

// Level load and save write (drakan_lifecycle, EE thread at the hooked call): ghosts must not survive either (the level
// loader does not flush the kill list; the serialisers iterate every class 0x10009 actor).
void destroyAll(const drakan_lifecycle::Event& e) {
    std::lock_guard<std::mutex> l(g_m);
    for (const Ghost& g : g_ghosts)
        if (g.info.handle && drakan_lifecycle::destroyActor(e, g.info.handle, true))
            std::fprintf(stderr, "[ghost] destroyed id %d before %s\n", g.info.id, drakan_lifecycle::kindName(e.kind));
    g_ghosts.clear();
    g_buf.clear(); // EE thread
    if (g_mirror) {
        g_autoPending = true;
        g_autoDelay = 120;
    }
}

int newestId() {
    std::lock_guard<std::mutex> l(g_m);
    int newest = 0;
    for (const Ghost& g : g_ghosts)
        if (!g.destroyRequested && g.info.id > newest)
            newest = g.info.id;
    return newest;
}

} // namespace

bool enabled() {
    return g_enabled.load();
}

void install(PS2Runtime& rt) {
    const char* v = std::getenv("DRAKAN_GHOST");
    const char* mir = std::getenv("DRAKAN_GHOST_MIRROR");
    g_mirror = mir && *mir && mir[0] != '0';
    if (!g_mirror && (!v || !*v || v[0] == '0'))
        return;
    if (!guestAddress(kFactory) || !guestAddress(kDestroy) || !guestAddress(kRynnHandler) ||
        !guestAddress(kContextPtr) || !guestAddress(kHandleVtable)) {
        std::fprintf(stderr, "[ghost] disabled: no address translation for this disc profile\n");
        return;
    }
    g_rynn = rt.lookupFunction(guestAddress(kRynnHandler));
    g_t1 = freeSlot(rt, &spawnStage1);
    g_t2 = freeSlot(rt, &spawnStage2);
    g_t3 = freeSlot(rt, &spawnStage3);
    g_handlerSlot = freeSlot(rt, &ghostHandler);
    if (!g_rynn || !g_t1 || !g_t2 || !g_t3 || !g_handlerSlot) {
        std::fprintf(stderr, "[ghost] disabled: Rynn's handler or a free function slot is missing\n");
        return;
    }
    if (const char* m = std::getenv("DRAKAN_GHOST_FWD"))
        g_forwardMask = static_cast<uint32_t>(std::strtoul(m, nullptr, 16));
    const char* l = std::getenv("DRAKAN_GHOST_LOG");
    g_log = l && l[0] && l[0] != '0';
    if (const char* h = std::getenv("DRAKAN_GHOST_HOLD"))
        g_hold = h[0] != '0';
    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeLevelLoad, destroyAll);
    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeSaveWrite, destroyAll);
    if (const char* o = std::getenv("DRAKAN_GHOST_OFFSET"))
        std::sscanf(o, "%f,%f", &g_offX, &g_offZ);
    if (g_mirror) {
        world::PoseBufferConfig cfg;
        cfg.delaySec = 1.0;
        cfg.maxSamples = 512;
        g_buf = world::PoseBuffer(cfg);
        g_autoPending = true;
        g_autoDelay = 240;
        std::fprintf(stderr, "[ghost] mirror mode: the ghost replays the player's pose from 1.0 s ago (+%g units x)\n",
                     static_cast<double>(kOffsetX));
    }
    g_enabled.store(true);
    std::fprintf(stderr, "[ghost] enabled (F9 spawn, F10 destroy; slots %x %x %x; forward mask %x)\n", g_t1, g_t2,
                 g_handlerSlot, g_forwardMask);
}

int spawn(const Transform* t) {
    if (!g_enabled.load())
        return 0;
    std::lock_guard<std::mutex> l(g_m);
    Ghost g;
    g.info.id = g_nextId++;
    if (t) {
        g.hasTransform = true;
        g.target = *t;
    }
    g_ghosts.push_back(g);
    return g.info.id;
}

void destroy(int id) {
    std::lock_guard<std::mutex> l(g_m);
    for (Ghost& g : g_ghosts)
        if (g.info.id == id)
            g.destroyRequested = true;
}

void setTransform(int id, const float pos[3], const float rot[9]) {
    std::lock_guard<std::mutex> l(g_m);
    for (Ghost& g : g_ghosts)
        if (g.info.id == id) {
            std::memcpy(g.target.pos, pos, sizeof g.target.pos);
            std::memcpy(g.target.rot, rot, sizeof g.target.rot);
            g.hasTransform = true;
        }
}

void setAnim(int id, int channel, int clip, float time) {
    std::lock_guard<std::mutex> l(g_m);
    for (Ghost& g : g_ghosts)
        if (g.info.id == id) {
            const int lo = channel < 0 ? 0 : channel;
            const int hi = channel < 0 ? static_cast<int>(world::skel::kMaxChannels) : channel + 1;
            for (int c = lo; c < hi && c < static_cast<int>(world::skel::kMaxChannels); ++c) {
                g.animSet |= 1u << c;
                g.animClip[c] = static_cast<uint32_t>(clip);
                g.animTime[c] = time;
            }
        }
}

void setPose(int id, const world::PlayerPose& p) {
    std::lock_guard<std::mutex> l(g_m);
    for (Ghost& g : g_ghosts)
        if (g.info.id == id)
            applyPose(g, p);
}

void forEach(const std::function<void(const Info&)>& fn) {
    std::vector<Info> copy;
    {
        std::lock_guard<std::mutex> l(g_m);
        for (const Ghost& g : g_ghosts)
            copy.push_back(g.info);
    }
    for (const Info& i : copy)
        fn(i);
}

void beforePadMgr(drakan_input::IGuest& guest, drakan_input::InputModule& input) {
    if (!g_enabled.load())
        return;
    const drakan_input::FrameInput& f = input.guestMuted() ? input.hostFrame() : input.frame();
    if (f.keyPressed[kDikF9])
        spawn();
    if (f.keyPressed[kDikF10])
        if (const int id = newestId())
            destroy(id);
    const uint32_t ctx = guest.read32(guestAddress(kContextPtr));
    if (!ramPtr(ctx))
        return;
    std::lock_guard<std::mutex> l(g_m);

    // Liveness: a level change rebuilds the pools; a stale ghost is forgotten, never destroyed through a dead pointer.
    const uint32_t table = guest.read32(ctx + 0x7c);
    for (size_t i = 0; i < g_ghosts.size();) {
        const Ghost& g = g_ghosts[i];
        const uint32_t h = g.info.handle;
        const bool live = !h || (ramPtr(h) && guest.read32(h) == guestAddress(kHandleVtable) && ramPtr(table) &&
                                 g.info.ref && guest.read32(table + (g.info.ref - 1u) * 8u) == h);
        if (!live) {
            std::fprintf(stderr, "[ghost] id %d (%x) is gone (destroyed or level change)\n", g.info.id, h);
            g_ghosts.erase(g_ghosts.begin() + static_cast<long>(i));
            continue;
        }
        ++i;
    }

    // Destroy: 0x1413c0(engine, actor) queues the kill, the engine frees it at its next flush.
    for (size_t i = 0; i < g_ghosts.size();) {
        const Ghost& g = g_ghosts[i];
        if (g.destroyRequested && g.info.handle) {
            guest.post(drakan_input::GuestCall(guestAddress(kDestroy)).arg(ctx).arg(g.info.handle));
            std::fprintf(stderr, "[ghost] destroy id %d handle %x queued\n", g.info.id, g.info.handle);
            g_ghosts.erase(g_ghosts.begin() + static_cast<long>(i));
            continue;
        }
        ++i;
    }

    ++g_tick;
    if (g_mirror) {
        // Record the local player and replay the pose from 1.0 s ago beside it (EE thread).
        world::PlayerPose cur, past;
        GuestMem mem(guest);
        world::Globals gl;
        gl.contextPtr = guestAddress(kContextPtr);
        gl.hpKey = guestAddress(gl.hpKey);
        gl.playerStats = guestAddress(gl.playerStats);
        gl.handleVtable = guestAddress(kHandleVtable);
        const double now = nowSeconds();
        const bool ok = world::readPlayerPose(mem, gl, g_readState, now, g_tick, cur) == world::PoseStatus::Ok;
        if (ok && !cur.mounted)
            g_buf.push(cur);
        if (g_buf.sample(now, past) && past.lower.clip) {
            past.position.x += g_offX;
            past.position.z += g_offZ;
            for (Ghost& g : g_ghosts)
                if (g.info.handle && !g.destroyRequested)
                    applyPose(g, past);
        }
        if (g_autoPending && g_ghosts.empty() && !drakan_lifecycle::transitionActive() && ok && !cur.mounted) {
            if (g_autoDelay)
                --g_autoDelay;
            else {
                g_autoPending = false;
                g_ghosts.push_back(Ghost{});
                g_ghosts.back().info.id = g_nextId++;
                std::fprintf(stderr, "[ghost] mirror: auto spawn id %d\n", g_ghosts.back().info.id);
            }
        }
    }

    // Anim readback (log only): what the engine left in channel 0 since our last write.
    if (g_log && g_tick % 30 == 0)
        for (const Ghost& g : g_ghosts)
            if (g.info.handle && g.animSet) {
                const uint32_t sk = guest.read32(g.info.handle + world::skel::kHandleSkeleton);
                const uint32_t ch = ramPtr(sk) ? guest.read32(sk + world::skel::kChannels) : 0;
                if (ramPtr(ch))
                    std::fprintf(stderr, "[ghost] id %d ch0 clip %x time %g (we wrote %x %g)\n", g.info.id,
                                 guest.read32(ch + world::skel::kChannelClip),
                                 static_cast<double>(guest.readF(ch + world::skel::kChannelTime)), g.animClip[0],
                                 static_cast<double>(g.animTime[0]));
            }

    for (Ghost& g : g_ghosts)
        if (g.info.handle && g.animSet)
            writeAnim(guest, g);
    // Transforms (the spawn's own transform is applied by the factory, so only live ghosts here).
    for (Ghost& g : g_ghosts)
        if ((g.hasTransform || g.hold) && g.info.handle) {
            for (int k = 0; k < 3; ++k)
                guest.writeF(g.info.handle + 0x14 + 4u * static_cast<uint32_t>(k), g.target.pos[k]);
            for (int k = 0; k < 9; ++k)
                guest.writeF(g.info.handle + 0x20 + 4u * static_cast<uint32_t>(k), g.target.rot[k]);
            g.hasTransform = false;
        }

    if (g_log) {
        static unsigned tick = 0;
        if (++tick % 120 == 0)
            for (const Ghost& g : g_ghosts)
                if (g.info.handle)
                    std::fprintf(stderr, "[ghost] id %d pos %g %g %g  player %x camera %x\n", g.info.id,
                                 static_cast<double>(guest.readF(g.info.handle + 0x14)),
                                 static_cast<double>(guest.readF(g.info.handle + 0x18)),
                                 static_cast<double>(guest.readF(g.info.handle + 0x1c)), guest.read32(ctx + 0x4b0),
                                 guest.read32(ctx + 0x4b4));
    }

    // One spawn at a time.
    if (g_busy.load())
        return;
    Ghost* want = nullptr;
    for (Ghost& g : g_ghosts)
        if (!g.info.handle && !g.destroyRequested) {
            want = &g;
            break;
        }
    if (!want)
        return;
    const uint32_t player = guest.read32(ctx + 0x4b0);
    const uint32_t res = ramPtr(player) ? guest.read16(player + 0x10) : 0xffffu;
    const uint32_t db = guest.read32(ctx + 0x320);
    const uint32_t records = ramPtr(db) ? guest.read32(db + 8) : 0;
    if (!ramPtr(player) || !ramPtr(records) || res == 0xffffu ||
        guest.read32(records + res * 0x24u + 8) != kRynnClass) {
        std::fprintf(stderr, "[ghost] spawn refused: the controlled actor is not Rynn (class 0x10009)\n");
        g_ghosts.erase(g_ghosts.begin() + (want - g_ghosts.data()));
        return;
    }
    Transform t = want->target;
    if (!want->hasTransform) {
        for (int k = 0; k < 3; ++k)
            t.pos[k] = guest.readF(player + 0x14 + 4u * static_cast<uint32_t>(k));
        for (int k = 0; k < 9; ++k)
            t.rot[k] = guest.readF(player + 0x20 + 4u * static_cast<uint32_t>(k));
        t.pos[0] += kOffsetX;
    }
    want->hasTransform = false;
    std::memcpy(g_flight.xform, t.pos, sizeof t.pos);
    std::memcpy(g_flight.xform + 3, t.rot, sizeof t.rot);
    g_flight.ghostId = want->info.id;
    g_flight.resIndex = res;
    // Camera mode word: *(*(ctx+0x4b4)+0xfc)+0x10 = camera instance data, +0x14c
    const uint32_t cam = guest.read32(ctx + 0x4b4);
    const uint32_t inner = ramPtr(cam) ? guest.read32(cam + 0xfc) : 0;
    const uint32_t data = ramPtr(inner) ? guest.read32(inner + 0x10) : 0;
    g_flight.camModeAddr = ramPtr(data) ? data + 0x14c : 0;
    g_busy.store(true);
    std::fprintf(stderr, "[ghost] spawning id %d: res %u at %.0f %.0f %.0f (camera mode word %x)\n", want->info.id, res,
                 static_cast<double>(t.pos[0]), static_cast<double>(t.pos[1]), static_cast<double>(t.pos[2]),
                 g_flight.camModeAddr);
    guest.post(drakan_input::GuestCall(g_t1));
}

void drawControls() {
    if (!g_enabled.load())
        return;
    ImGui::Separator();
    ImGui::TextUnformatted("Ghost (F9 spawn, F10 destroy)");
    ImGui::SameLine();
    if (ImGui::Button("Spawn ghost"))
        spawn();
    ImGui::SameLine();
    if (ImGui::Button("Destroy newest"))
        if (const int id = newestId())
            destroy(id);
    forEach(
        [](const Info& i) { ImGui::Text("ghost %d  handle %08x  ref %u  res %u", i.id, i.handle, i.ref, i.resIndex); });
}

} // namespace drakan_ghost
