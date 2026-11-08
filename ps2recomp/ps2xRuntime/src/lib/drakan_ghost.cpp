// Drakan: ghost players (drakan_ghost.h). PAL addresses are canonical, translated through the disc profile.
#include "drakan_ghost.h"
#include "drakan_dlss.h"
#include "drakan_arena.h"
#include "drakan_coop_character.h"
#include "drakan_local_character_view.h"
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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

void ps2xNoPreemptEnter() noexcept;
void ps2xNoPreemptLeave() noexcept;

namespace drakan_ghost {
namespace {

using drakan_region::guestAddress;

constexpr uint32_t kContextPtr = 0x5565d4u, kFactory = 0x181ad8u, kDestroy = 0x1413c0u, kRynnHandler = 0x30ea30u,
                   kFreeCamHandler = 0x2a3378u;
constexpr uint32_t kHandleVtable = 0x51b8b0u, kRynnClass = 0x10009u, kDragonClass = 0x1001cu, kRynnRecord = 0x20a50u;
constexpr uint32_t kDikF9 = 0x43u, kDikF10 = 0x44u;
constexpr uint32_t kBoneAttach = 0x13b878u, kBoneDetach = 0x13b8c8u; // (model, item, bone, 0, 0) / (model, bone)
constexpr uint32_t kPropHand[2] = {0x45u, 0x46u};
// NPC bodies (stand-in characters) have no hand bone properties; every humanoid roster model (30 of 30, disc 0x0208
// node lists) has the same layout: joint 30 is the node "Rweapon", joint 13 "Lweapon" (Rynn: 37 / 19).
constexpr uint32_t kNpcWeaponJoint[2] = {30u, 13u};
constexpr uint32_t kWeaponOwner = 0x60u;
constexpr float kOffsetX = 300.0f;
float g_offX = kOffsetX, g_offZ = 0.0f; // DRAKAN_GHOST_OFFSET=x,z (mirror mode test aid)
constexpr uint32_t kFrame = 0x60;       // stack bytes the spawn trampoline borrows below the dispatcher thread's sp

struct Ghost {
    Info info;
    bool destroyRequested = false;
    bool hasTransform = false; // target is set (a spawn at a given transform, or a pending setTransform)
    bool hold = false;         // after the spawn, rewrite `target` into the actor every tick
    Transform target;
    bool pinned = false; // test aid (pinTransform): `pin` wins over every pose the net layer sets
    Transform pin;
    // Clip state written into the skeleton every tick (channels 0..31; bit set in animSet = channel is driven).
    uint32_t animSet = 0;
    uint32_t animClip[world::skel::kMaxChannels] = {};
    float animTime[world::skel::kMaxChannels] = {};
    bool animFree = false; // setAnimFree: the clip is written once and the engine's own playback loops it
    // Weapons (hand 0 = right / bone property 0x45, 1 = left / 0x46): wanted record id, record id of the live item.
    uint32_t wantRec[2] = {};
    uint32_t haveRec[2] = {};
    uint32_t weapon[2] = {}; // item actor handle, 0 none
    float sectorPos[3] = {}; // position of the last sector update (collision cells), see updateSector
    bool sectorValid = false;
    // Stand-in character (drakan_coop_character): 0 = Rynn's own body. Otherwise the actor is an NPC-class body and the
    // pose is kept in Rynn's terms (raw*), mapped to the character's own clips when written (writeAnim).
    uint16_t character = 0;
    bool dragon = false; // an Arokh-class body (the second dragon of a mounted partner): Rynn's clips are not mapped,
                         // the dragon's own are replayed as received (clip prefix g_dragonClipHi)
    bool rawSet = false;
    bool standInLogged = false;
    uint32_t loggedLod = ~0u; // DRAKAN_GHOST_LOG: initial native LOD and subsequent changes only
    bool ownsDetailRequest = false;
    world::ChannelPose rawLower, rawUpper;
    bool localVisual = false; // render-only local body: cell membership is scoped to world rendering
};

std::atomic<bool> g_enabled{false};
bool g_debugKeys = false; // DRAKAN_GHOST=1 / mirror: F9, F10 and the F1 controls
std::mutex g_m;
std::vector<Ghost> g_ghosts;
int g_nextId = 1;
std::atomic<bool> g_busy{false};
bool g_log = false, g_hold = true;
bool g_sectorUpdate = true; // DRAKAN_GHOST_SECTOR=0 disables the sector update (test aid)
bool g_trace = false;       // DRAKAN_GHOST_TRACE=1: one CSV line per PadMgr boundary (smoothness measurements)
uint32_t g_forwardMask =
    0; // bit n = event 1<<n forwarded to Rynn's real handler (event 2 always goes to the free-cam handler)
bool g_mirror = false;      // DRAKAN_GHOST_MIRROR=1: the ghost replays the local player 1 s late
bool g_autoPending = false; // mirror mode: (re)spawn the ghost once the player is ready
unsigned g_autoDelay = 0;   // ticks to wait before the auto spawn
// Mirror replay is indexed by PadMgr boundary (world tick), not by time: the engine's dt follows the host's frame time
// (capped at 0.1 s) while the player's movement and clips advance in world steps, so a time-indexed replay of a
// bursty source (a long frame is followed by several world steps) re-times those bursts into stalls and jumps. The
// ghost shows the sample recorded g_delayTicks boundaries ago, i.e. exactly the cadence the player's own screen had.
std::deque<world::PlayerPose> g_hist; // EE thread only
uint32_t g_forceRec[2] = {};      // DRAKAN_GHOST_WEAPONS=<right>,<left> (hex record ids) overrides the mirrored weapons
unsigned g_delayTicks = 60;       // DRAKAN_GHOST_DELAY_TICKS
world::PoseReadState g_readState; // EE thread only
unsigned g_tick = 0;
// Readiness: a ghost spawned in a cut scene level ("Level 0 - Intro": no world / no controllable Rynn; the init takes a
// null JALR) or in the first moments after a load hangs the guest. Refused until the level is a playable one and
// kReadyTicks boundaries have passed since the last load / restore.
std::atomic<bool> g_cutsceneLevel{false};
std::atomic<unsigned> g_sinceLoad{0};
std::atomic<unsigned> g_spawnFailures{0};
constexpr unsigned kReadyTicks = 90;
double g_gameTime = 0; // seconds of game time (sum of the engine's per-tick dt, ctx+0x24), EE thread only
uint32_t g_t1 = 0, g_t2 = 0, g_t3 = 0, g_channelDone = 0, g_handlerSlot = 0, g_w1 = 0, g_w2 = 0, g_w3 = 0;
PS2Runtime::RecompiledFunction g_rynn = nullptr;
PS2Runtime::RecompiledFunction g_renderObject = nullptr;
PS2Runtime::RecompiledFunction g_characterProjector = nullptr;
PS2Runtime::RecompiledFunction g_playSound = nullptr;
uint32_t g_shadowedBody = 0;
PS2Runtime::RecompiledFunction g_renderWorld = nullptr;
PS2Runtime::RecompiledFunction g_endWorld = nullptr;
uint32_t g_localWorldBody = 0, g_localWorldReturn = 0;
bool g_localEnabled = false;
int g_localId = 0;
unsigned g_localRetry = 0;
uint32_t g_localSource = 0;
uint32_t g_localDrawn = 0;
world::PoseReadState g_localRead;

void hookPlaySound(uint8_t* ram, R5900Context* c, PS2Runtime* rt);
void clearLocalCharacterView();
void resetLocalCharacter();

void hookRenderObject(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == guestAddress(0x189b38u)) {
        const uint32_t object = getRegU32(c, 4);
        uint32_t draw = drakan_local_character::g_view.nativeObject(object);
        const auto& view = drakan_local_character::g_view;
        if (object == view.body && object && g_localDrawn != object) {
            g_localDrawn = object;
            std::fprintf(stderr, "[local-character] native draw ready: body %x\n", object);
        }
        {
            std::lock_guard<std::mutex> lock(g_m);
            for (const Ghost& g : g_ghosts)
                if (g.localVisual && g.info.handle == object && object != drakan_local_character::g_view.body)
                    draw = 0; // includes bodies queued for removal / not ready to substitute yet
        }
        if (!draw) {
            SET_GPR_U32(c, 2, 0u);
            c->pc = getRegU32(c, 31);
            return;
        }
    }
    g_renderObject(ram, c, rt);
}

uint32_t resourceOf(const uint8_t* ram, uint32_t object) { // actor+8, for the shadow trace
    uint32_t v = 0;
    object &= 0x1ffffffu;
    if (object + 12 <= PS2_RAM_SIZE)
        std::memcpy(&v, ram + object + 8, 4);
    return v;
}

void hookCharacterProjector(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    // This is the character projector, not the generic model-shadow renderer. a0 is its manager; a1 is the model.
    // Redirect before silhouette bounds / pose / projected geometry are prepared, retaining the manager and light.
    if (c->pc == guestAddress(0x1ab890u)) {
        // Diagnostic isolation of native projection from the later modern ray composite. Keep the default native
        // path intact: replacement eligibility cannot be decided here, before the renderer finishes this frame.
        static const bool disabled = [] {
            const char* value = std::getenv("DRAKAN_NATIVE_CHARACTER_SHADOWS");
            const bool off = value && std::strcmp(value, "0") == 0;
            if (off)
                std::fprintf(stderr, "[shadow-diagnostic] native character projection disabled\n");
            return off;
        }();
        if (disabled) {
            SET_GPR_U32(c, 2, 0u);
            c->pc = getRegU32(c, 31);
            return;
        }
        const uint32_t source = getRegU32(c, 5);
        const uint32_t caster = drakan_local_character::g_view.shadowObject(source);
        // DRAKAN_SHADOW_TRACE=<n>: log the first n projector calls (which objects cast a character shadow, and what
        // the local view made of each: the duplicate-shadow harness reads these lines).
        static long traceLeft = [] {
            const char* v = std::getenv("DRAKAN_SHADOW_TRACE");
            return v ? std::strtol(v, nullptr, 10) : 0L;
        }();
        if (traceLeft > 0) {
            --traceLeft;
            const auto& view = drakan_local_character::g_view;
            const char* role = source == view.controller                                        ? "controller"
                               : source == view.body                                            ? "body"
                               : source == view.weapons[0] || source == view.weapons[1]         ? "original-item"
                               : source == view.bodyWeapons[0] || source == view.bodyWeapons[1] ? "body-item"
                                                                                                : "other";
            int ghostId = 0;
            const char* ghostPart = "";
            {
                std::lock_guard<std::mutex> lock(g_m); // the render path already takes it (hookRenderObject)
                for (const Ghost& g : g_ghosts) {
                    if (g.info.handle == source)
                        ghostId = g.info.id, ghostPart = g.localVisual ? "local-body" : "remote-body";
                    else if (source && (g.weapon[0] == source || g.weapon[1] == source))
                        ghostId = g.info.id, ghostPart = g.localVisual ? "local-item" : "remote-item";
                }
            }
            std::fprintf(stderr, "[shadow-trace] tick %u source %x res %x role %s ghost %d %s caster %x\n", g_tick,
                         source, resourceOf(ram, source), role, ghostId, ghostPart, caster);
        }
        if (!caster) {
            SET_GPR_U32(c, 2, 0u);
            c->pc = getRegU32(c, 31);
            return;
        }
        if (caster != source) {
            SET_GPR_U32(c, 5, caster);
            if (g_shadowedBody != caster) {
                g_shadowedBody = caster;
                std::fprintf(stderr, "[local-character] character shadow: %x -> selected body %x\n", source, caster);
            }
        }
        drakan_dlss::noteNativeCharacterShadow(caster);
    }
    g_characterProjector(ram, c, rt);
}

// In-flight spawn (one at a time; written on the EE thread before the post, read by the trampolines on the dispatcher)
struct Flight {
    int ghostId = 0;
    float xform[12] = {};
    uint32_t actor = 0, player = 0, camera = 0, resIndex = 0, camModeAddr = 0, savedSp = 0, savedRa = 0, oldMode = 0;
    bool noPreempt = false;
    bool npc =
        false; // a stand-in character: the NPC class's own init runs (no camera mode trick, no extra world insert)
    bool dragon =
        false; // an Arokh body: its init inserts itself and, with camera mode 7, returns before the rider link
    uint32_t linkA = 0, linkB = 0,
             partner = 0; // the controlled actor's rider / dragon link (data+0x4d8) before the spawn
} g_flight;
constexpr uint32_t kDragonRecord = 0x20a60u;

// Actor -> the handler the class factory installed (stand-in characters only): event 2 (destroy) goes to it, so the NPC
// class tears down what its own init built. Separate lock: the handler runs while destroyAll holds g_m.
std::mutex g_om;
std::vector<std::pair<uint32_t, uint32_t>> g_origHandler;
bool g_npcInsert = false;        // DRAKAN_GHOST_NPC_INSERT=1: also insert the stand-in into the level object (test aid)
uint16_t g_defaultCharacter = 0; // DRAKAN_GHOST_CHAR=<hex class id>: character of F9 / mirror ghosts (test aid)
bool g_defaultDragon = false;    // DRAKAN_GHOST_DRAGON=1: F9 / mirror ghosts are dragons (test aid)
std::vector<drakan_coop_character::Act>
    g_actCycle; // DRAKAN_GHOST_ACTS=attack,hit,death: mirror stand-ins play these in turn
uint32_t g_dragonClipHi =
    0x40000u; // high half of the dragon's clip ids (the wire carries 16 bits); DRAKAN_DRAGON_CLIP_HI
bool g_dragonClipHiForced = false;     // the env override is set: it wins over the level's own SYSTEM index
std::atomic<uint32_t> g_levelSysHi{0}; // the level's SYSTEM database index << 16 (0x20000 Surdana, 0x40000 Ravenshold)

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

// The rider <-> dragon link (data+0x4d8 of the instance data of both ends, coop-characters.md section 3): the
// controlled actor's value, and the value at the other end (the registry slot of that ref).
uint32_t linkOf(const uint8_t* ram, uint32_t actor) {
    const uint32_t inner = ramPtr(actor) ? rd32(ram, actor + 0xfc) : 0;
    const uint32_t data = ramPtr(inner) ? rd32(ram, inner + 0x10) : 0;
    return ramPtr(data) ? rd32(ram, data + 0x4d8) : 0;
}
uint32_t linkedActor(const uint8_t* ram, uint32_t ctx, uint32_t ref) {
    const uint32_t table = rd32(ram, ctx + 0x7c);
    ref &= 0xffffu;
    return ref && ramPtr(table) && ref <= rd32(ram, ctx + 0x84) + 1 ? rd32(ram, table + (ref - 1u) * 8u) : 0;
}

// Spawn trampoline, stage 1 (a postGuestCall target on a guest dispatcher thread): put the transform on the stack, set
// the camera mode word to 7 (Rynn's init takes its early-out for a second body), call 0x181ad8 returning to stage 2.
void spawnStage1(uint8_t* rdram, R5900Context* c, PS2Runtime*) {
    // The dragon init may block (model / resource waits), which must stay possible: no no-preempt section for it.
    g_flight.noPreempt = !g_flight.dragon && !g_flight.npc;
    if (g_flight.noPreempt)
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
    g_flight.linkA = linkOf(rdram, g_flight.player);
    g_flight.partner = linkedActor(rdram, ctx, g_flight.linkA);
    g_flight.linkB = linkOf(rdram, g_flight.partner);
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

void finishSpawn(uint8_t* rdram, R5900Context* c, uint32_t actor) {
    // Rynn's init took the camera-mode-7 early out, which skips the world insert (the engine's free-cam body is
    // inserted by its own handler): do it here the way event 1 does, through the level object's vtable +0x74 (this
    // adjusted by the +0x70 offset).
    const uint32_t world = rd32(rdram, rd32(rdram, guestAddress(kContextPtr)) + 0x31c);
    const uint32_t vt = ramPtr(world) && (!g_flight.npc || g_npcInsert) ? rd32(rdram, world + 0x24b4) : 0;
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
    if (g_flight.noPreempt)
        ps2xNoPreemptLeave();
    g_busy.store(false);
}

void spawnChannelDone(uint8_t* rdram, R5900Context* c, PS2Runtime*) {
    const uint32_t sk = rd32(rdram, g_flight.actor + world::skel::kHandleSkeleton);
    std::fprintf(stderr, "[character-motion] split torso channel: actor %x skeleton %x channels %u\n", g_flight.actor,
                 sk, rd32(rdram, sk + world::skel::kChannelCount));
    finishSpawn(rdram, c, g_flight.actor);
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
        // The local player's own dragon link (rider <-> Arokh, data+0x4d8 at both ends) must survive the spawn.
        const uint32_t a = linkOf(rdram, g_flight.player), pa = linkedActor(rdram, ctx, a), b = linkOf(rdram, pa);
        std::fprintf(
            stderr,
            "[ghost] link check (%s ghost): controlled %x +4d8 %x -> %x, partner %x -> %x, its +4d8 %x -> %x: %s\n",
            g_flight.dragon ? "dragon" : "body", g_flight.player, g_flight.linkA, a, g_flight.partner, pa,
            g_flight.linkB, b,
            a == g_flight.linkA && pa == g_flight.partner && b == g_flight.linkB ? "intact" : "CHANGED");
    }
    std::lock_guard<std::mutex> l(g_m);
    auto it =
        std::find_if(g_ghosts.begin(), g_ghosts.end(), [](const Ghost& g) { return g.info.id == g_flight.ghostId; });
    if (ramPtr(actor)) {
        const uint32_t inner = rd32(rdram, actor + 0xfc);
        if (ramPtr(inner)) {
            if (g_flight.npc) {
                std::lock_guard<std::mutex> lo(g_om);
                g_origHandler.erase(std::remove_if(g_origHandler.begin(), g_origHandler.end(),
                                                   [&](const auto& e) { return e.first == actor; }),
                                    g_origHandler.end());
                g_origHandler.emplace_back(actor, rd32(rdram, inner + 0x14));
            }
            wr32(rdram, inner + 0x14, g_handlerSlot);
        }
        if (it != g_ghosts.end()) {
            uint16_t ref = 0, res = 0;
            std::memcpy(&ref, rdram + ((actor + 0x12) & 0x1ffffffu), 2);
            std::memcpy(&res, rdram + ((actor + 0x10) & 0x1ffffffu), 2);
            it->info.handle = actor;
            it->loggedLod = ~0u;
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
    g_flight.actor = actor;
    const uint32_t sk = ramPtr(actor) ? rd32(rdram, actor + world::skel::kHandleSkeleton) : 0;
    if (g_flight.npc && ramPtr(sk) && g_channelDone) {
        // Channel creation may allocate / yield: enter through the spawn trampoline, never a nested call.
        // All curated rigs share torso node 2; root retains the independent locomotion channel.
        SET_GPR_U32(c, 4, sk);
        SET_GPR_U32(c, 5, 2u);
        SET_GPR_U32(c, 31, g_channelDone);
        c->pc = guestAddress(0x102e70u);
        return;
    }
    finishSpawn(rdram, c, actor);
}

// Stage 3: the world insert returned; hand the actor back as the result.
void spawnStage3(uint8_t*, R5900Context* c, PS2Runtime*) {
    std::fprintf(stderr, "[ghost] world insert of %x returned %x\n", g_flight.actor, getRegU32(c, 2));
    SET_GPR_U32(c, 2, g_flight.actor);
    SET_GPR_U32(c, 29, g_flight.savedSp);
    SET_GPR_U32(c, 31, g_flight.savedRa);
    c->pc = g_flight.savedRa;
    if (g_flight.noPreempt)
        ps2xNoPreemptLeave();
    g_busy.store(false);
}

// Weapon spawn (one item at a time, serialised with the ghost spawn by g_busy): the factory creates the item actor of
// record index `idx` exactly as for the ghost (no camera trick: only the class 0x10009 init needs it), then the host
// sets the item's owner (+0x60 of its data: a registry reference to the ghost, which its blade sweep excludes) and
// attaches it to the ghost's hand bone with 0x13b878(model, item, bone, 0, 0), the call the equip path uses. Nothing
// touches the local player's inventory, slots, hand holders or HP block.
struct WFlight {
    int ghostId = 0, hand = 0;
    float xform[12] = {};
    uint32_t idx = 0, rec = 0, model = 0, bone = 0, actor = 0, savedSp = 0, savedRa = 0;
} g_wf;

void weaponStage1(uint8_t* rdram, R5900Context* c, PS2Runtime*) {
    g_wf.savedSp = getRegU32(c, 29);
    g_wf.savedRa = getRegU32(c, 31);
    const uint32_t sp = g_wf.savedSp - kFrame, xf = sp + 0x10;
    std::memcpy(rdram + (xf & 0x1ffffffu), g_wf.xform, sizeof g_wf.xform);
    SET_GPR_U32(c, 29, sp);
    SET_GPR_U32(c, 4, rd32(rdram, guestAddress(kContextPtr)));
    SET_GPR_U32(c, 5, 0u);
    SET_GPR_U32(c, 6, xf);
    SET_GPR_U32(c, 7, 0u);
    SET_GPR_U32(c, 8, 0u);
    SET_GPR_U32(c, 9, g_wf.idx);
    SET_GPR_U32(c, 31, g_w2);
    c->pc = guestAddress(kFactory);
}

void weaponFinish(R5900Context* c, uint32_t result) {
    SET_GPR_U32(c, 2, result);
    SET_GPR_U32(c, 29, g_wf.savedSp);
    SET_GPR_U32(c, 31, g_wf.savedRa);
    c->pc = g_wf.savedRa;
    g_busy.store(false);
}

void weaponStage2(uint8_t* rdram, R5900Context* c, PS2Runtime*) {
    const uint32_t item = getRegU32(c, 2);
    const uint32_t ctx = rd32(rdram, guestAddress(kContextPtr));
    bool attach = false;
    {
        std::lock_guard<std::mutex> l(g_m);
        auto it =
            std::find_if(g_ghosts.begin(), g_ghosts.end(), [](const Ghost& g) { return g.info.id == g_wf.ghostId; });
        if (ramPtr(item) && it != g_ghosts.end() && it->info.handle == g_wf.model) {
            it->weapon[g_wf.hand] = item;
            it->haveRec[g_wf.hand] = g_wf.rec;
            // Owner: take a reference on the ghost's registry entry (the part of 0x168388 that cannot allocate).
            const uint32_t data = rd32(rdram, rd32(rdram, item + 0xfc) + 0x10);
            uint16_t ref = 0;
            std::memcpy(&ref, rdram + ((g_wf.model + 0x12) & 0x1ffffffu), 2);
            const uint32_t table = rd32(rdram, ctx + 0x7c);
            if (ramPtr(data) && ref && ramPtr(table)) {
                const uint32_t e = table + (ref - 1u) * 8u + 4u;
                uint16_t n = 0;
                std::memcpy(&n, rdram + (e & 0x1ffffffu), 2);
                ++n;
                std::memcpy(rdram + (e & 0x1ffffffu), &n, 2);
                std::memcpy(rdram + ((data + kWeaponOwner) & 0x1ffffffu), &ref, 2);
            }
            attach = ramPtr(data);
        } else if (!ramPtr(item) && it != g_ghosts.end() && it->info.handle == g_wf.model) {
            it->haveRec[g_wf.hand] = g_wf.rec; // the factory refused this record: do not retry it every tick
        } else if (ramPtr(item)) {
            // The ghost vanished meanwhile: do not leave an orphan item behind.
            std::fprintf(stderr, "[ghost] weapon %x spawned for a vanished ghost: left to the level teardown\n", item);
        }
    }
    std::fprintf(stderr, "[ghost] weapon record %x -> item %x (ghost %d hand %d bone %u)\n", g_wf.rec, item,
                 g_wf.ghostId, g_wf.hand, g_wf.bone);
    if (attach) {
        g_wf.actor = item;
        SET_GPR_U32(c, 4, g_wf.model);
        SET_GPR_U32(c, 5, item);
        SET_GPR_U32(c, 6, g_wf.bone);
        SET_GPR_U32(c, 7, 0u);
        SET_GPR_U32(c, 8, 0u);
        SET_GPR_U32(c, 31, g_w3);
        c->pc = guestAddress(kBoneAttach);
        return;
    }
    weaponFinish(c, item);
}

void weaponStage3(uint8_t*, R5900Context* c, PS2Runtime*) {
    weaponFinish(c, g_wf.actor);
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
        uint32_t orig = 0;
        {
            std::lock_guard<std::mutex> lo(g_om);
            for (size_t i = 0; i < g_origHandler.size(); ++i)
                if (g_origHandler[i].first == getRegU32(c, 4)) {
                    orig = g_origHandler[i].second;
                    g_origHandler.erase(g_origHandler.begin() + static_cast<long>(i));
                    break;
                }
        }
        if (orig) { // a stand-in character: its own class handler tears it down
            c->pc = orig;
            rt->lookupFunction(orig)(rdram, c, rt);
            return;
        }
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

// Who may speak with Rynn's voice: every player body (local controller / its render-only body, each remote ghost)
// and the character it shows. Refreshed each PadMgr boundary; read by the PlaySound hook, which can run while g_m is
// held by a guest call, hence its own lock.
struct VoiceOwner {
    uint32_t actor = 0;
    uint16_t character = 0;
};
std::mutex g_voiceMutex;
std::vector<VoiceOwner> g_voiceOwners;

// g_m held, EE thread. `player` is the controlled actor (ctx+0x4b0), 0 when unknown.
void refreshVoiceOwners(uint32_t player) {
    std::vector<VoiceOwner> owners;
    if (g_localEnabled && g_localId) {
        const uint16_t mine = drakan_coop_character::selected();
        owners.push_back({g_localSource, mine});
        const auto& view = drakan_local_character::g_view;
        if (view.controller && view.controller != g_localSource)
            owners.push_back({view.controller, mine});
        if (view.body)
            owners.push_back({view.body, mine});
    } else if (player && !g_ghosts.empty()) {
        // Playing Rynn: keep her own lines hers even when an NPC-bodied opponent stands right next to her.
        owners.push_back({player, 0});
    }
    for (const Ghost& g : g_ghosts)
        if (g.info.handle && !g.dragon && !g.localVisual)
            owners.push_back({g.info.handle, g.character});
    std::lock_guard<std::mutex> lock(g_voiceMutex);
    g_voiceOwners.swap(owners);
}

void hookPlaySound(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    // PlaySound(mgr, id, pos*, flags): a1 is a resource id. Only Rynn's vocalizations change (damage / death cries
    // to the character's own male set, every other line silenced); impact / weapon / ambient sounds pass through.
    // The local controller remains Rynn for gameplay, and NPC bodies play clips retargeted from Rynn's, so both would
    // otherwise speak with Rynn's voice. The emitter decides the character: the animation-event caller (0x10d100,
    // returning to 0x10d1c0) holds the actor in s0+8; every other caller passes a copy of the emitter's position.
    if (c->pc == guestAddress(0x1b7f30u)) {
        std::vector<VoiceOwner> owners;
        {
            std::lock_guard<std::mutex> lock(g_voiceMutex);
            owners = g_voiceOwners;
        }
        if (!owners.empty()) {
            const uint32_t sound = getRegU32(c, 5);
            const uint32_t sysHi = g_levelSysHi.load();
            // A relocated SYSTEM bank can leave another resource in bank 2. Do not reinterpret that resource as Rynn.
            const uint32_t canonical =
                sysHi && (sound & 0xffff0000u) != sysHi ? 0u : (sysHi ? 0x20000u | (sound & 0xffffu) : sound);
            // Cheap reject: an unrelated sound keeps the native path untouched.
            if (canonical && drakan_coop_character::mapVoiceSound(0x177fu, canonical) != canonical) {
                const VoiceOwner* who = nullptr;
                uint32_t emitter = 0;
                if (getRegU32(c, 31) == guestAddress(0x10d1c0u)) {
                    const uint32_t slot = getRegU32(c, 16);
                    emitter = ramPtr(slot) ? rd32(ram, slot + 8) : 0;
                }
                for (const VoiceOwner& o : owners)
                    if (emitter && o.actor == emitter)
                        who = &o;
                const uint32_t pos = getRegU32(c, 6);
                if (!who && !emitter && ramPtr(pos)) {
                    float p[3];
                    std::memcpy(p, ram + (pos & 0x1ffffffu), sizeof p);
                    float best = 256.0f * 256.0f;
                    for (const VoiceOwner& o : owners) {
                        if (!ramPtr(o.actor))
                            continue;
                        float q[3];
                        std::memcpy(q, ram + ((o.actor + 0x14u) & 0x1ffffffu), sizeof q);
                        const float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2];
                        const float d = dx * dx + dy * dy + dz * dz;
                        if (d < best) {
                            best = d;
                            who = &o;
                        }
                    }
                }
                // A non-positional Rynn line (or an emitter far from every body) belongs to the local player.
                if (!who && !emitter && g_localEnabled && g_localId)
                    who = &owners.front();
                const uint32_t mapped =
                    who ? drakan_coop_character::mapVoiceSound(who->character, canonical) : canonical;
                if (mapped != canonical) {
                    if (!mapped) {
                        SET_GPR_U32(c, 2, 0u); // no sound instance: the native caller contract for a culled sound
                        c->pc = getRegU32(c, 31);
                        return;
                    }
                    SET_GPR_U32(c, 5, mapped);
                }
            }
        }
    }
    g_playSound(ram, c, rt);
}

// Writes the driven clips into the ghost skeleton's channels (sk = *(handle+0x50), channels at sk+0xe8, stride 0x80,
// clip +0x50, time +0x58). Returns the number of channels written.
int writeAnim(drakan_input::IGuest& guest, const Ghost& g) {
    if (g.character && g.rawSet) { // stand-in character: its own clips; channel 0 (locomotion) lower, the others upper
        const uint32_t sk = guest.read32(g.info.handle + world::skel::kHandleSkeleton);
        const uint32_t ch = ramPtr(sk) ? guest.read32(sk + world::skel::kChannels) : 0;
        const uint32_t n = ramPtr(sk) ? guest.read32(sk + world::skel::kChannelCount) : 0;
        if (!ramPtr(ch) || n == 0 || n > world::skel::kMaxChannels)
            return 0;
        const bool split = g.rawLower.clip != g.rawUpper.clip;
        const auto lo = drakan_coop_character::mapClip(g.character, g.rawLower.clip, g.rawLower.time);
        const auto up = split ? drakan_coop_character::mapClip(g.character, g.rawUpper.clip, g.rawUpper.time) : lo;
        int written = 0;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t a = ch + i * world::skel::kChannelStride;
            const uint32_t node = guest.read32(a + 4u);
            const auto& m = (!split || drakan_coop_character::nodeUsesLower(g.character, node)) ? lo : up;
            if (!m.clip)
                continue;
            const uint32_t sysHi = g_levelSysHi.load();
            const uint32_t clip = (m.clip & 0xffff0000u) == 0x20000u && sysHi ? sysHi | (m.clip & 0xffffu) : m.clip;
            guest.write32(a + world::skel::kChannelClip, clip);
            guest.writeF(a + world::skel::kChannelTime, m.time);
            ++written;
        }
        return written;
    }
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
            if (g.animFree && guest.read32(a + world::skel::kChannelClip) == g.animClip[i])
                continue; // already playing: the engine advances (and loops) it
            guest.write32(a + world::skel::kChannelClip, g.animClip[i]);
            guest.writeF(a + world::skel::kChannelTime, g.animTime[i]);
            ++written;
        }
    return written;
}

// The engine files every actor in 2048-unit cells of the level object; a blade sweep, projectiles and the AI find
// actors only through them. The engine's own movers set actor+0x104 bit 28 ("needs sector update") and call the update
// (vtable +0x84 of *(*(ctx+0x31c)+0x24b4), this = world + (s16)vt[+0x80], arg the actor; 0x1494c8): a host write of
// the position bypasses both, so a ghost that moved stayed in its spawn cell and nothing could hit it. Done only when
// the ghost moved since the last update.
void refileHandle(drakan_input::IGuest& guest, uint32_t ctx, uint32_t h) {
    const uint32_t world = guest.read32(ctx + 0x31c);
    const uint32_t vt = ramPtr(world) ? guest.read32(world + 0x24b4) : 0;
    const uint32_t fn = ramPtr(vt) ? guest.read32(vt + 0x84) : 0;
    if (!ramPtr(fn))
        return;
    const auto adjusted = [&](uint32_t off) {
        const uint32_t a = guest.read32(vt + off) & 0xffffu;
        return world + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(a)));
    };
    // The engine's mover: the actor leaves its cell list (update, vtable +0x84, only while it is listed: flag 0x104
    // bit 28), the bounding sphere is marked stale (0x104 bit 11) and the actor is filed again (refile 0x1496a0,
    // vtable +0x8c: new cell index at +0x7c and the list node a blade sweep walks).
    uint32_t f104 = guest.read32(h + 0x104);
    if (f104 & 0x10000000u)
        guest.call(drakan_input::GuestCall(fn).arg(adjusted(0x80)).arg(h));
    guest.write32(h + 0x104, guest.read32(h + 0x104) & ~0x800u);
    if (const uint32_t refile = guest.read32(vt + 0x8c); ramPtr(refile))
        guest.call(drakan_input::GuestCall(refile).arg(adjusted(0x88)).arg(h));
    if (static_cast<int32_t>(guest.read32(h + 0x7c)) == -1) {
        // The engine's refile returns at once for an actor whose cell is -1, so an actor created where no cell was
        // resident yet (a ghost spawned while the level streams in, a respawn during a load) stays out of every cell
        // list for good and a blade sweep never finds it. File it by hand: the cell lookup the refile uses (vtable
        // +0x6c: old cell, flags 5, the position), then the refile proper, which now links it into that cell.
        const uint32_t lookup = guest.read32(vt + 0x6c);
        if (ramPtr(lookup)) {
            const uint32_t cell = guest.call(drakan_input::GuestCall(lookup)
                                                 .arg(adjusted(0x68))
                                                 .arg(0xffffffffu)
                                                 .arg(5)
                                                 .argf(guest.readF(h + 0x14))
                                                 .argf(guest.readF(h + 0x18))
                                                 .argf(guest.readF(h + 0x1c)));
            if (static_cast<int32_t>(cell) >= 0 && cell < guest.read32(world + 0x10)) {
                guest.write32(h + 0x7c, cell);
                if (const uint32_t refile = guest.read32(vt + 0x8c); ramPtr(refile))
                    guest.call(drakan_input::GuestCall(refile).arg(adjusted(0x88)).arg(h));
                if (g_log)
                    std::fprintf(stderr, "[ghost] actor %x had no collision cell: filed in cell %u (now %d)\n", h, cell,
                                 static_cast<int32_t>(guest.read32(h + 0x7c)));
            }
        }
    }
}

// Collision and visibility share cell membership. Expose the cosmetic body to those lists only while the engine
// renders a world, allowing its normal material / skinning / culling preparation without a gameplay collider.
void localRenderCell(uint8_t* ram, R5900Context* c, PS2Runtime* rt, uint32_t body, bool insert) {
    if (!ramPtr(body) || rd32(ram, body) != guestAddress(kHandleVtable))
        return;
    const uint32_t ctx = rd32(ram, guestAddress(kContextPtr));
    const uint32_t world = ramPtr(ctx) ? rd32(ram, ctx + 0x31c) : 0;
    const uint32_t vt = ramPtr(world) ? rd32(ram, world + 0x24b4) : 0;
    if (!ramPtr(vt))
        return;
    drakan_lifecycle::Event e;
    e.rdram = ram;
    e.ctx = c;
    e.runtime = rt;
    if (insert) {
        const auto& view = drakan_local_character::g_view;
        const uint32_t source = view.poseSource ? view.poseSource : view.controller;
        if (!ramPtr(source))
            return;
        wr32(ram, body + 0x7c, rd32(ram, source + 0x7c));
        wr32(ram, body + 0x104, rd32(ram, body + 0x104) & ~0x800u);
        const int16_t adjust = static_cast<int16_t>(rd32(ram, vt + 0x88) & 0xffffu);
        drakan_lifecycle::callGuest(e, rd32(ram, vt + 0x8c), world + adjust, body);
    } else {
        if (rd32(ram, body + 0x104) & 0x10000000u) {
            const int16_t adjust = static_cast<int16_t>(rd32(ram, vt + 0x80) & 0xffffu);
            drakan_lifecycle::callGuest(e, rd32(ram, vt + 0x84), world + adjust, body);
        }
        wr32(ram, body + 0x7c, 0xffffffffu);
    }
}

void hookRenderWorld(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == guestAddress(0x139010u)) {
        { // EE-only view validation: no ownership lock or ghost mutation inside the native render wrapper.
            const auto& view = drakan_local_character::g_view;
            if (view.body) {
                const uint32_t body = view.body;
                bool live = false;
                // Native stand-in bodies can clear actor+0x12 after spawning while remaining alive and visible.
                // Their controller and resource identify this binding; a registry ref is not a liveness invariant.
                if (ramPtr(body))
                    live = view.matchesBody(rd32(ram, body), rd32(ram, body + 4), rd32(ram, body + 0x50),
                                            rd32(ram, body + 8), guestAddress(kHandleVtable));
                if (!live)
                    clearLocalCharacterView();
            }
        }
        g_localWorldBody = drakan_local_character::g_view.body;
        g_localWorldReturn = getRegU32(c, 31);
        if (g_localWorldBody) {
            const auto& view = drakan_local_character::g_view;
            const uint32_t source = view.poseSource ? view.poseSource : view.controller;
            // Sample the completed gameplay transform, not the preceding PadMgr pose. This keeps the visible avatar
            // on the real movement / collision position even when the game advances several updates per present.
            if (ramPtr(source) && rd32(ram, source) == guestAddress(kHandleVtable)) {
                std::memcpy(ram + g_localWorldBody + 0x14, ram + source + 0x14, 0x30);
            }
            localRenderCell(ram, c, rt, g_localWorldBody, true);
        }
    }
    g_renderWorld(ram, c, rt);
    if (g_localWorldBody && c->pc == g_localWorldReturn) {
        localRenderCell(ram, c, rt, g_localWorldBody, false);
        g_localWorldBody = 0;
    }
}

void hookEndWorld(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == guestAddress(0x147690u) && getRegU32(c, 31) == guestAddress(0x139344u)) {
        if (g_localWorldBody) {
            localRenderCell(ram, c, rt, g_localWorldBody, false);
            g_localWorldBody = 0;
        }
    }
    g_endWorld(ram, c, rt);
}

void updateSector(drakan_input::IGuest& guest, uint32_t ctx, Ghost& g) {
    const uint32_t h = g.info.handle;
    float p[3];
    for (int k = 0; k < 3; ++k)
        p[k] = guest.readF(h + 0x14 + 4u * static_cast<uint32_t>(k));
    const float dx = p[0] - g.sectorPos[0], dy = p[1] - g.sectorPos[1], dz = p[2] - g.sectorPos[2];
    if (g.sectorValid && dx * dx + dy * dy + dz * dz < 1.0f)
        return;
    refileHandle(guest, ctx, h);
    std::memcpy(g.sectorPos, p, sizeof p);
    g.sectorValid = true;
    if (g_log) {
        static unsigned n = 0;
        if (n++ % 60 == 0)
            std::fprintf(stderr, "[ghost] id %d sector update: cell %d flags+4 %x +0x104 %x\n", g.info.id,
                         static_cast<int32_t>(guest.read32(h + 0x7c)), guest.read32(h + 4), guest.read32(h + 0x104));
    }
}

// Caller holds g_m. Position, rotation and the clips of every channel (lower on the mask, upper on the rest).
void applyPose(Ghost& g, const world::PlayerPose& p) {
    g.animFree = false;
    const float pos[3] = {p.position.x, p.position.y, p.position.z};
    std::memcpy(g.target.pos, pos, sizeof pos);
    std::memcpy(g.target.rot, p.rotation, sizeof g.target.rot);
    g.hasTransform = true;
    if (g.dragon) {
        world::PlayerPose q = p;
        // The net layer sends Arokh's own clip ids (PlayerState.clipHi, v8); a pose that still carries Rynn's 0x2
        // prefix (the mirror test aid) gets the guessed one.
        // The level's own SYSTEM index wins (Surdana's dragon clips really are 0x2xxxx: no rewrite there).
        const uint32_t lvlHi = g_levelSysHi.load();
        const uint32_t hi = g_dragonClipHiForced || !lvlHi ? g_dragonClipHi : lvlHi;
        if (q.lower.clip && (q.lower.clip >> 16) == 2u)
            q.lower.clip = (q.lower.clip & 0xffffu) | hi;
        if (q.upper.clip && (q.upper.clip >> 16) == 2u)
            q.upper.clip = (q.upper.clip & 0xffffu) | hi;
        for (uint32_t c = 0; c < world::skel::kMaxChannels; ++c) {
            const world::ChannelPose& cp = (q.lowerMask >> c & 1u) ? q.lower : q.upper;
            g.animSet |= 1u << c;
            g.animClip[c] = cp.clip;
            g.animTime[c] = cp.time;
        }
        return;
    }
    if (g.character) {
        if (g_log && (!g.rawSet || g.rawLower.clip != p.lower.clip || g.rawUpper.clip != p.upper.clip)) {
            const auto lower = drakan_coop_character::mapClip(g.character, p.lower.clip, p.lower.time);
            const auto upper = drakan_coop_character::mapClip(g.character, p.upper.clip, p.upper.time);
            std::fprintf(
                stderr,
                "[character-motion] local=%d character=%x source=%x/%x mapped=%x/%x mask=%x pos=%.1f,%.1f,%.1f\n",
                g.localVisual ? 1 : 0, g.character, p.lower.clip, p.upper.clip, lower.clip, upper.clip, p.lowerMask,
                p.position.x, p.position.y, p.position.z);
        }
        g.rawLower = p.lower;
        g.rawUpper = p.upper;
        g.rawSet = true;
        g.animSet = 1; // writeAnim runs (the mapped clips are computed there)
        return;
    }
    for (uint32_t c = 0; c < world::skel::kMaxChannels; ++c) {
        const world::ChannelPose& cp = (p.lowerMask >> c & 1u) ? p.lower : p.upper;
        g.animSet |= 1u << c;
        g.animClip[c] = cp.clip;
        g.animTime[c] = cp.time;
    }
}

// EE thread only; safe at native render entry, without touching the owned ghost list.
void clearLocalCharacterView() {
    drakan_local_character::g_view = {};
    g_localWorldBody = g_localWorldReturn = 0;
    g_localDrawn = 0;
    g_shadowedBody = 0;
}

// g_m held, EE thread. Clearing this view restores the original body immediately: no player flags, skeleton,
// inventory, camera links or controller pointers were changed.
void resetLocalCharacter() {
    clearLocalCharacterView();
    for (Ghost& g : g_ghosts)
        if (g.info.id == g_localId)
            g.destroyRequested = true;
    g_localId = 0;
    g_localSource = 0;
}

uint32_t localReference(drakan_input::IGuest& guest, uint32_t ctx, uint32_t ref) {
    const uint32_t table = guest.read32(ctx + 0x7c);
    if (!ref || ref > guest.read32(ctx + 0x84) + 1 || !ramPtr(table))
        return 0;
    const uint32_t h = guest.read32(table + (ref - 1) * 8u);
    return ramPtr(h) && guest.read32(h) == guestAddress(kHandleVtable) && guest.read16(h + 0x12) == ref &&
                   !(guest.read32(h + 4) & 0x80000000u)
               ? h
               : 0;
}

// Returns true while a selected character's body is wanted but not visible yet (the original model would show).
bool localCharacterUpdate(drakan_input::IGuest& guest, uint32_t ctx) {
    const uint16_t character = drakan_coop_character::selected();
    if (!g_localEnabled || !character || !g_renderObject || drakan_lifecycle::transitionActive() ||
        g_cutsceneLevel.load()) {
        resetLocalCharacter();
        return false;
    }
    if (g_sinceLoad.load() < kReadyTicks)
        return true;
    world::Globals globals;
    globals.contextPtr = guestAddress(kContextPtr);
    globals.hpKey = guestAddress(globals.hpKey);
    globals.playerStats = guestAddress(globals.playerStats);
    globals.handleVtable = guestAddress(kHandleVtable);
    world::PlayerPose pose;
    if (world::readPlayerPose(GuestMem(guest), globals, g_localRead, g_gameTime, g_tick, pose) != world::PoseStatus::Ok)
        return !drakan_local_character::g_view.body;
    uint32_t source = pose.handle;
    if (pose.mounted) {
        const uint32_t inner = guest.read32(source + 0xfc);
        const uint32_t data = ramPtr(inner) ? guest.read32(inner + 0x10) : 0;
        source = ramPtr(data) ? localReference(guest, ctx, guest.read16(data + 0x4d8)) : 0;
        if (!source) {
            resetLocalCharacter();
            return false;
        }
        // The actual passenger supplies its world transform and Ride clip, not the dragon's animation.
        pose.position = {guest.readF(source + 0x14), guest.readF(source + 0x18), guest.readF(source + 0x1c)};
        for (int i = 0; i < 9; ++i)
            pose.rotation[i] = guest.readF(source + 0x20 + 4u * i);
        const uint32_t sk = guest.read32(source + 0x50);
        const uint32_t ch = ramPtr(sk) ? guest.read32(sk + world::skel::kChannels) : 0;
        if (!ramPtr(ch))
            return !drakan_local_character::g_view.body;
        pose.lower =
            pose.upper = {guest.read32(ch + world::skel::kChannelClip), guest.readF(ch + world::skel::kChannelTime)};
        pose.weaponRight = pose.weaponLeft = 0;
    }
    auto it = std::find_if(g_ghosts.begin(), g_ghosts.end(), [](const Ghost& g) { return g.info.id == g_localId; });
    if (g_localId &&
        (it == g_ghosts.end() || it->destroyRequested || it->character != character || g_localSource != source)) {
        const bool failed = it == g_ghosts.end();
        resetLocalCharacter();
        g_localRetry = failed ? g_tick + 120 : g_tick;
        it = g_ghosts.end();
    }
    if (!g_localId) {
        if (g_tick < g_localRetry)
            return true;
        Ghost body;
        body.info.id = g_nextId++;
        body.character = character;
        body.localVisual = true;
        body.hold = true;
        g_localId = body.info.id;
        g_localSource = source;
        g_ghosts.push_back(body);
        it = g_ghosts.end() - 1;
        std::fprintf(stderr, "[local-character] selected %04x (%s), body id %d, controller %x unchanged\n", character,
                     drakan_coop_character::entry(character).name.c_str(), g_localId, source);
    }
    applyPose(*it, pose);
    it->wantRec[0] = pose.weaponRight;
    it->wantRec[1] = pose.weaponLeft;
    const uint32_t h = it->info.handle;
    if (!h || !ramPtr(guest.read32(h + 0x50)))
        return true; // the selected body is loading (applyLocalHide decides whether the original model shows)
    if (!drakan_local_character::g_view.body)
        std::fprintf(stderr, "[local-character] visible %04x: body %x replaces model %x (render only)\n", character, h,
                     source);
    drakan_local_character::View view;
    view.controller = source;
    view.poseSource = source;
    view.body = h;
    view.bodySkeleton = guest.read32(h + 0x50);
    view.bodyResource = guest.read32(h + 8);
    view.bodyWeapons[0] = it->weapon[0];
    view.bodyWeapons[1] = it->weapon[1];
    const uint32_t inner = guest.read32(source + 0xfc);
    const uint32_t data = ramPtr(inner) ? guest.read32(inner + 0x10) : 0;
    if (ramPtr(data)) {
        const uint32_t visual = guest.read32(data + 0x80);
        if (ramPtr(visual) && guest.read32(visual) == guestAddress(kHandleVtable) &&
            ramPtr(guest.read32(visual + 0x50)))
            view.controller = visual;
        if (!drakan_local_character::g_view.body)
            std::fprintf(stderr, "[local-character] binding: actor %x, native visual %x, suppressed model %x\n", source,
                         visual, view.controller);
        if (!drakan_local_character::g_view.body)
            std::fprintf(stderr, "[local-character] native shadow mode %u\n", guest.read32(guestAddress(0x4df434u)));
    }
    if (ramPtr(data))
        for (unsigned hand = 0; hand < 2; ++hand)
            if (it->weapon[hand]) {
                const uint32_t holder = hand ? world::playerdata::kLeftHolder : world::playerdata::kRightHolder;
                view.weapons[hand] =
                    localReference(guest, ctx, guest.read16(data + holder + world::playerdata::kHolderWeaponRef));
            }
    drakan_local_character::g_view = view;
    return false;
}

// Hide the whole local avatar while she waits for her arena spawn placement (she would be seen at the level start and
// then jump), and while a selected body is still loading (Rynn would flash first). The loading part is bounded: a
// body that never comes shows the original model again after kPendingHideTicks rather than an invisible player.
constexpr unsigned kPendingHideTicks = 300;
bool g_pendingActive = false;
unsigned g_pendingSince = 0;
bool g_hideLogged = false;

uint32_t heldItem(drakan_input::IGuest& guest, uint32_t ctx, uint32_t data, unsigned hand) {
    const uint32_t holder = hand ? world::playerdata::kLeftHolder : world::playerdata::kRightHolder;
    return localReference(guest, ctx, guest.read16(data + holder + world::playerdata::kHolderWeaponRef));
}

void applyLocalHide(drakan_input::IGuest& guest, uint32_t ctx, bool bodyPending) {
    if (bodyPending && !g_pendingActive) {
        g_pendingActive = true;
        g_pendingSince = g_tick;
    } else if (!bodyPending) {
        g_pendingActive = false;
    }
    auto& view = drakan_local_character::g_view;
    const bool awaiting = drakan_arena::awaitingSpawn();
    const bool hide = g_localEnabled && !drakan_lifecycle::transitionActive() &&
                      (awaiting || (bodyPending && g_tick - g_pendingSince < kPendingHideTicks));
    if (!hide) {
        if (view.hideAll) {
            view.hideAll = false;
            if (!view.body)
                view = {};
        }
        if (g_hideLogged)
            std::fprintf(stderr, "[local-character] avatar shown\n");
        g_hideLogged = false;
        return;
    }
    if (!view.body) {
        // No selected body yet (or Rynn is the choice): name the original model and its held items directly.
        const uint32_t player = guest.read32(ctx + 0x4b0);
        if (!ramPtr(player) || guest.read32(player) != guestAddress(kHandleVtable))
            return;
        const uint32_t inner = guest.read32(player + 0xfc);
        const uint32_t data = ramPtr(inner) ? guest.read32(inner + 0x10) : 0;
        const uint32_t db = guest.read32(ctx + 0x320);
        const uint32_t records = ramPtr(db) ? guest.read32(db + 8) : 0;
        const uint32_t res = guest.read16(player + 0x10);
        if (!ramPtr(data) || !ramPtr(records) || res == 0xffffu ||
            guest.read32(records + res * 0x24u + 8) != kRynnClass)
            return; // mounted (the dragon is controlled) or not Rynn: leave everything visible
        drakan_local_character::View v;
        v.poseSource = player;
        v.controller = player;
        const uint32_t visual = guest.read32(data + 0x80);
        if (ramPtr(visual) && guest.read32(visual) == guestAddress(kHandleVtable))
            v.controller = visual;
        for (unsigned hand = 0; hand < 2; ++hand)
            v.weapons[hand] = heldItem(guest, ctx, data, hand);
        view = v;
    } else if (const uint32_t source = view.poseSource ? view.poseSource : view.controller; ramPtr(source)) {
        // The original held items are named only once the selected body holds its own: name them for hiding too.
        const uint32_t inner = guest.read32(source + 0xfc);
        const uint32_t data = ramPtr(inner) ? guest.read32(inner + 0x10) : 0;
        if (ramPtr(data))
            for (unsigned hand = 0; hand < 2; ++hand)
                if (!view.weapons[hand])
                    view.weapons[hand] = heldItem(guest, ctx, data, hand);
    }
    if (!g_hideLogged)
        std::fprintf(stderr, "[local-character] avatar hidden: %s\n",
                     awaiting ? "not at the arena spawn yet" : "selected body still loading");
    g_hideLogged = true;
    view.hideAll = true;
}

void localCharacterTick(drakan_input::IGuest& guest, uint32_t ctx) {
    applyLocalHide(guest, ctx, localCharacterUpdate(guest, ctx));
}

// Level load and save write (drakan_lifecycle, EE thread at the hooked call): ghosts must not survive either (the level
// loader does not flush the kill list; the serialisers iterate every class 0x10009 actor).
void destroyAll(const drakan_lifecycle::Event& e) {
    std::vector<Ghost> retiring;
    {
        std::lock_guard<std::mutex> lock(g_m);
        resetLocalCharacter();
        g_localRetry = 0;
        g_localRead = {};
        retiring.swap(g_ghosts);
        refreshVoiceOwners(0);
        g_hist.clear();
        if (g_mirror) {
            g_autoPending = true;
            g_autoDelay = 120;
        }
    }
    // Destroying an actor runs guest callbacks. Those callbacks may query the ghost registry.
    for (const Ghost& g : retiring)
        for (uint32_t w : g.weapon)
            if (w && drakan_lifecycle::destroyActor(e, w, true))
                std::fprintf(stderr, "[ghost] destroyed weapon %x of id %d before %s\n", w, g.info.id,
                             drakan_lifecycle::kindName(e.kind));
    for (const Ghost& g : retiring)
        if (g.info.handle && drakan_lifecycle::destroyActor(e, g.info.handle, true))
            std::fprintf(stderr, "[ghost] destroyed id %d before %s\n", g.info.id, drakan_lifecycle::kindName(e.kind));
}

void onLoaded(const drakan_lifecycle::Event& e) {
    g_cutsceneLevel.store(e.level.rfind("Level 0", 0) == 0);
    g_sinceLoad.store(0);
}
void onLoadStart(const drakan_lifecycle::Event&) {
    g_sinceLoad.store(0);
}

const char* notReadyReason() {
    if (g_cutsceneLevel.load())
        return "cut scene level";
    if (drakan_lifecycle::transitionActive())
        return "level load / save in progress";
    if (g_sinceLoad.load() < kReadyTicks)
        return "world not ready after a load";
    return nullptr;
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

void setLocalCharacterEnabled(bool active) {
    g_localEnabled = active;
}

void install(PS2Runtime& rt) {
    const char* v = std::getenv("DRAKAN_GHOST");
    const char* mir = std::getenv("DRAKAN_GHOST_MIRROR");
    g_mirror = mir && *mir && mir[0] != '0';
    // The multiplayer menu needs ghosts without DRAKAN_GHOST: they install unless DRAKAN_MULTIPLAYER=0. The F9 / F10 /
    // F1 debug controls stay opt-in with DRAKAN_GHOST=1 (or mirror mode).
    g_debugKeys = g_mirror || (v && *v && v[0] != '0');
    const char* mpEnv = std::getenv("DRAKAN_MULTIPLAYER");
    const bool mpOn = !(mpEnv && mpEnv[0] == '0');
    if (!g_debugKeys && !mpOn)
        return;
    if (!guestAddress(kFactory) || !guestAddress(kDestroy) || !guestAddress(kRynnHandler) ||
        !guestAddress(kContextPtr) || !guestAddress(kHandleVtable)) {
        std::fprintf(stderr, "[ghost] disabled: no address translation for this disc profile\n");
        return;
    }
    g_rynn = rt.lookupFunction(guestAddress(kRynnHandler));
    if (const uint32_t play = guestAddress(0x1b7f30u); play && rt.hasFunction(play)) {
        g_playSound = rt.lookupFunction(play);
        rt.replaceFunction(play, &hookPlaySound);
    }
    if (const uint32_t draw = guestAddress(0x189b38u); draw && rt.hasFunction(draw)) {
        g_renderObject = rt.lookupFunction(draw);
        rt.replaceFunction(draw, &hookRenderObject);
    }
    if (const uint32_t shadow = guestAddress(0x1ab890u); shadow && rt.hasFunction(shadow)) {
        g_characterProjector = rt.lookupFunction(shadow);
        rt.replaceFunction(shadow, &hookCharacterProjector);
    }
    if (const uint32_t draw = guestAddress(0x139010u); draw && rt.hasFunction(draw)) {
        g_renderWorld = rt.lookupFunction(draw);
        rt.replaceFunction(draw, &hookRenderWorld);
    }
    if (const uint32_t end = guestAddress(0x147690u); end && rt.hasFunction(end)) {
        g_endWorld = rt.lookupFunction(end);
        rt.replaceFunction(end, &hookEndWorld);
    }
    g_t1 = freeSlot(rt, &spawnStage1);
    g_t2 = freeSlot(rt, &spawnStage2);
    g_t3 = freeSlot(rt, &spawnStage3);
    g_channelDone = freeSlot(rt, &spawnChannelDone);
    g_handlerSlot = freeSlot(rt, &ghostHandler);
    g_w1 = freeSlot(rt, &weaponStage1);
    g_w2 = freeSlot(rt, &weaponStage2);
    g_w3 = freeSlot(rt, &weaponStage3);
    if (!g_rynn || !g_t1 || !g_t2 || !g_t3 || !g_channelDone || !g_handlerSlot || !g_w1 || !g_w2 || !g_w3) {
        std::fprintf(stderr, "[ghost] disabled: Rynn's handler or a free function slot is missing\n");
        return;
    }
    if (const char* m = std::getenv("DRAKAN_GHOST_FWD"))
        g_forwardMask = static_cast<uint32_t>(std::strtoul(m, nullptr, 16));
    g_trace = std::getenv("DRAKAN_GHOST_TRACE") != nullptr;
    const char* l = std::getenv("DRAKAN_GHOST_LOG");
    g_log = l && l[0] && l[0] != '0';
    if (const char* h = std::getenv("DRAKAN_GHOST_HOLD"))
        g_hold = h[0] != '0';
    if (const char* s = std::getenv("DRAKAN_GHOST_SECTOR"))
        g_sectorUpdate = s[0] != '0';
    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeLevelLoad, destroyAll);
    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeSaveWrite, destroyAll);
    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::AfterLevelLoad, onLoaded);
    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeSaveRestore, onLoadStart);
    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeSaveRestore, destroyAll);
    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::AfterSaveRestore, onLoadStart);
    if (const char* o = std::getenv("DRAKAN_GHOST_OFFSET"))
        std::sscanf(o, "%f,%f", &g_offX, &g_offZ);
    if (const char* c = std::getenv("DRAKAN_GHOST_CHAR"))
        g_defaultCharacter = static_cast<uint16_t>(std::strtoul(c, nullptr, 16));
    g_npcInsert = std::getenv("DRAKAN_GHOST_NPC_INSERT") != nullptr;
    g_defaultDragon = std::getenv("DRAKAN_GHOST_DRAGON") != nullptr;
    if (const char* acts = std::getenv("DRAKAN_GHOST_ACTS")) {
        std::string list = acts;
        size_t pos = 0;
        while (pos <= list.size()) {
            const size_t e = list.find(',', pos);
            drakan_coop_character::Act a;
            if (drakan_coop_character::actByName(list.substr(pos, e == std::string::npos ? e : e - pos), a))
                g_actCycle.push_back(a);
            if (e == std::string::npos)
                break;
            pos = e + 1;
        }
    }
    if (const char* h = std::getenv("DRAKAN_DRAGON_CLIP_HI")) {
        g_dragonClipHi = static_cast<uint32_t>(std::strtoul(h, nullptr, 16));
        g_dragonClipHiForced = true;
    }
    if (const char* w = std::getenv("DRAKAN_GHOST_WEAPONS"))
        std::sscanf(w, "%x,%x", &g_forceRec[0], &g_forceRec[1]);
    if (g_mirror) {
        if (const char* d = std::getenv("DRAKAN_GHOST_DELAY_TICKS"))
            g_delayTicks = static_cast<unsigned>(std::strtoul(d, nullptr, 10));
        g_autoPending = true;
        g_autoDelay = 240;
        std::fprintf(stderr,
                     "[ghost] mirror mode: the ghost replays the player's pose from %u world ticks ago (+%g units x)\n",
                     g_delayTicks, static_cast<double>(g_offX));
    }
    g_enabled.store(true);
    std::fprintf(stderr, "[ghost] enabled (%s; slots %x %x %x; forward mask %x)\n",
                 g_debugKeys ? "F9 spawn, F10 destroy" : "for multiplayer", g_t1, g_t2, g_handlerSlot, g_forwardMask);
}

int spawnBody(const Transform* t, uint16_t character, bool dragon, bool remotePlayer = false) {
    if (!g_enabled.load())
        return 0;
    if (const char* why = notReadyReason()) {
        g_spawnFailures.fetch_add(1);
        std::fprintf(stderr, "[ghost] spawn refused: %s\n", why);
        return 0;
    }
    std::lock_guard<std::mutex> l(g_m);
    Ghost g;
    g.info.id = g_nextId++;
    g.wantRec[0] = g_forceRec[0];
    g.wantRec[1] = g_forceRec[1];
    g.character = !dragon && drakan_coop_character::known(character) ? character : 0;
    g.dragon = dragon;
    g.info.remotePlayer = remotePlayer;
    if (t) {
        g.hasTransform = true;
        g.target = *t;
    }
    g_ghosts.push_back(g);
    return g.info.id;
}

int spawn(const Transform* t, uint16_t character, bool remotePlayer) {
    return spawnBody(t, character, false, remotePlayer);
}

int spawnDragon(const Transform* t) {
    return spawnBody(t, 0, true);
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

void pinTransform(int id, const float pos[3], const float rot[9]) {
    std::lock_guard<std::mutex> l(g_m);
    for (Ghost& g : g_ghosts)
        if (g.info.id == id) {
            std::memcpy(g.pin.pos, pos, sizeof g.pin.pos);
            std::memcpy(g.pin.rot, rot, sizeof g.pin.rot);
            g.pinned = true;
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

void setAnimFree(int id, uint32_t clipLow) {
    std::lock_guard<std::mutex> l(g_m);
    for (Ghost& g : g_ghosts)
        if (g.info.id == id) {
            const uint32_t lvlHi = g_levelSysHi.load();
            const uint32_t hi = g.dragon ? (g_dragonClipHiForced || !lvlHi ? g_dragonClipHi : lvlHi) : 0x20000u;
            for (uint32_t c = 0; c < world::skel::kMaxChannels; ++c) {
                g.animSet |= 1u << c;
                g.animClip[c] = hi | (clipLow & 0xffffu);
                g.animTime[c] = 0.0f;
            }
            g.animFree = true;
        }
}

void setPose(int id, const world::PlayerPose& p) {
    std::lock_guard<std::mutex> l(g_m);
    for (Ghost& g : g_ghosts)
        if (g.info.id == id) {
            applyPose(g, p);
            // weaponRight / weaponLeft are record ids (0 = empty hand); a change swaps the item at the PadMgr boundary.
            g.wantRec[0] = g_forceRec[0] ? g_forceRec[0] : p.weaponRight;
            g.wantRec[1] = g_forceRec[1] ? g_forceRec[1] : p.weaponLeft;
        }
}

unsigned spawnFailures() {
    return g_spawnFailures.load();
}

bool ready() {
    return g_enabled.load() && !notReadyReason();
}

void setWeapons(int id, uint32_t rightRecordId, uint32_t leftRecordId) {
    std::lock_guard<std::mutex> l(g_m);
    for (Ghost& g : g_ghosts)
        if (g.info.id == id) {
            g.wantRec[0] = rightRecordId;
            g.wantRec[1] = leftRecordId;
        }
}

void refileActor(drakan_input::IGuest& guest, uint32_t actor) {
    const uint32_t ctx = guest.read32(guestAddress(kContextPtr));
    if (ramPtr(ctx) && ramPtr(actor))
        refileHandle(guest, ctx, actor);
}

bool isGhostItem(uint32_t handle) {
    if (!handle)
        return false;
    std::lock_guard<std::mutex> l(g_m);
    for (const Ghost& g : g_ghosts)
        if (g.weapon[0] == handle || g.weapon[1] == handle)
            return true;
    return g_wf.actor == handle;
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

// Record id -> index into the resource record table (ctx+0x320: +8 records, +0x10 inclusive count, 0x24 bytes each,
// id at +0). 0xffff when absent.
uint32_t recordIndex(drakan_input::IGuest& guest, uint32_t ctx, uint32_t rec) {
    const uint32_t db = guest.read32(ctx + 0x320);
    const uint32_t records = ramPtr(db) ? guest.read32(db + 8) : 0;
    if (!ramPtr(records))
        return 0xffffu;
    const uint32_t count = std::min<uint32_t>(guest.read32(db + 0x10), 0x2000u);
    for (uint32_t i = 0; i <= count; ++i)
        if (guest.read32(records + i * 0x24u) == rec)
            return i;
    return 0xffffu;
}

// EE thread, g_m held, no spawn in flight. Brings one hand of one live ghost in line with its wanted weapon: drops a
// wrong item first (detach + the engine's kill list), then spawns the wanted one. True when a guest call was posted.
bool startWeaponChange(drakan_input::IGuest& guest, uint32_t ctx) { // g_m held by the caller
    for (Ghost& g : g_ghosts) {
        if (!g.info.handle || g.destroyRequested || g.dragon ||
            (g.character && !drakan_coop_character::entry(g.character).humanoid))
            continue; // (a stand-in character's hand bone is looked up through the same properties: see coop-stand-ins)
        for (int hand = 0; hand < 2; ++hand) {
            if (g.wantRec[hand] == g.haveRec[hand])
                continue;
            const uint32_t bone = [&] {
                if (g.character)
                    return kNpcWeaponJoint[hand];
                const uint32_t pb = guest.read32(g.info.handle + 0x54);
                const uint32_t props = ramPtr(pb) ? guest.read32(pb + 4) : 0;
                return ramPtr(props) ? guest.read32(props + kPropHand[hand] * 4u) : 0xffffffffu;
            }();
            if (g.weapon[hand]) {
                if (bone < 0x100u)
                    guest.post(drakan_input::GuestCall(guestAddress(kBoneDetach)).arg(g.info.handle).arg(bone));
                guest.post(drakan_input::GuestCall(guestAddress(kDestroy)).arg(ctx).arg(g.weapon[hand]));
                std::fprintf(stderr, "[ghost] id %d hand %d: weapon %x dropped\n", g.info.id, hand, g.weapon[hand]);
                g.weapon[hand] = 0;
                g.haveRec[hand] = 0;
                if (g.wantRec[hand] == 0)
                    return true;
            }
            if (g.wantRec[hand] == 0)
                continue;
            const uint32_t idx = recordIndex(guest, ctx, g.wantRec[hand]);
            if (idx == 0xffffu || bone >= 0x100u) {
                std::fprintf(stderr, "[ghost] id %d hand %d: weapon record %x unusable (index %x, bone %x)\n",
                             g.info.id, hand, g.wantRec[hand], idx, bone);
                g.haveRec[hand] = g.wantRec[hand]; // do not retry every tick
                continue;
            }
            g_wf.ghostId = g.info.id;
            g_wf.hand = hand;
            g_wf.idx = idx;
            g_wf.rec = g.wantRec[hand];
            g_wf.model = g.info.handle;
            g_wf.bone = bone;
            for (int k = 0; k < 3; ++k)
                g_wf.xform[k] = guest.readF(g.info.handle + 0x14 + 4u * static_cast<uint32_t>(k));
            for (int k = 0; k < 9; ++k)
                g_wf.xform[3 + k] = guest.readF(g.info.handle + 0x20 + 4u * static_cast<uint32_t>(k));
            g_busy.store(true);
            guest.post(drakan_input::GuestCall(g_w1));
            return true;
        }
    }
    return false;
}

void beforePadMgr(drakan_input::IGuest& guest, drakan_input::InputModule& input) {
    if (!g_enabled.load())
        return;
    const drakan_input::FrameInput& f = input.guestMuted() ? input.hostFrame() : input.frame();
    if (g_debugKeys && f.keyPressed[kDikF9])
        spawnBody(nullptr, g_defaultCharacter, g_defaultDragon);
    if (g_debugKeys && f.keyPressed[kDikF10])
        if (const int id = newestId())
            destroy(id);
    const uint32_t ctx = guest.read32(guestAddress(kContextPtr));
    if (!ramPtr(ctx))
        return;
    std::lock_guard<std::mutex> l(g_m);

    // Liveness: a level change rebuilds the pools; a stale ghost is forgotten, never destroyed through a dead pointer.
    const uint32_t table = guest.read32(ctx + 0x7c);
    for (size_t i = 0; i < g_ghosts.size();) {
        Ghost& g = g_ghosts[i];
        const uint32_t h = g.info.handle;
        const bool standIn = g.character || g.dragon;
        if (h && ramPtr(h) && standIn) {
            // A stand-in body is created by another class's init: report what it looks like once (and pick up a ref
            // that was not set yet at the end of the spawn).
            if (!g.info.ref)
                g.info.ref = guest.read16(h + 0x12);
            if (!g.standInLogged) {
                g.standInLogged = true;
                const uint32_t slot = g.info.ref && ramPtr(table) ? guest.read32(table + (g.info.ref - 1u) * 8u) : 0;
                std::fprintf(stderr,
                             "[ghost] id %d stand-in %x: vtable %x flags+4 %x type %x res %x ref %x table[ref] %x\n",
                             g.info.id, h, guest.read32(h), guest.read32(h + 4), guest.read32(h + 0xc),
                             guest.read16(h + 0x10), g.info.ref, slot);
            }
        }
        // Stand-ins: the vtable and a live (not dying) flag word are the test (a level change destroys them through
        // drakan_lifecycle first); Rynn-class ghosts also need their registry slot.
        const bool live = !h || (ramPtr(h) && guest.read32(h) == guestAddress(kHandleVtable) && ramPtr(table) &&
                                 (standIn ? !(guest.read32(h + 4) & 0x80000000u)
                                          : g.info.ref && guest.read32(table + (g.info.ref - 1u) * 8u) == h));
        if (!live) {
            std::fprintf(stderr, "[ghost] id %d (%x) is gone (destroyed or level change)\n", g.info.id, h);
            if (g.info.id == g_localId)
                resetLocalCharacter(); // pose reading may already be unavailable during Quit to Title
            g_ghosts.erase(g_ghosts.begin() + static_cast<long>(i));
            continue;
        }
        ++i;
    }

    // Destroy: 0x1413c0(engine, actor) queues the kill, the engine frees it at its next flush.
    for (size_t i = 0; i < g_ghosts.size();) {
        const Ghost& g = g_ghosts[i];
        if (g.destroyRequested && !g.info.handle && (!g_busy.load() || g_flight.ghostId != g.info.id)) {
            if (g.info.id == g_localId)
                resetLocalCharacter();
            g_ghosts.erase(g_ghosts.begin() + static_cast<long>(i));
            continue;
        }
        if (g.destroyRequested && g.info.handle) {
            for (uint32_t w : g.weapon)
                if (w)
                    guest.post(drakan_input::GuestCall(guestAddress(kDestroy)).arg(ctx).arg(w));
            guest.post(drakan_input::GuestCall(guestAddress(kDestroy)).arg(ctx).arg(g.info.handle));
            std::fprintf(stderr, "[ghost] destroy id %d handle %x queued\n", g.info.id, g.info.handle);
            if (g.info.id == g_localId)
                resetLocalCharacter();
            g_ghosts.erase(g_ghosts.begin() + static_cast<long>(i));
            continue;
        }
        ++i;
    }

    ++g_tick;
    if (g_sinceLoad.load() < kReadyTicks)
        g_sinceLoad.fetch_add(1);
    // The world steps by the engine's own dt (ctx+0x24); the wall clock between PadMgr boundaries jitters (host load,
    // present pacing) and must not time the recording, or the replay stutters.
    float engineDt = guest.readF(ctx + 0x24);
    if (!(engineDt > 0.0f && engineDt < 0.25f))
        engineDt = 1.0f / 60.0f;
    g_gameTime += static_cast<double>(engineDt);
    localCharacterTick(guest, ctx);
    refreshVoiceOwners(guest.read32(ctx + 0x4b0));
    if (g_mirror) {
        // Record the local player and replay the pose from 1.0 s ago beside it (EE thread).
        world::PlayerPose cur, past;
        GuestMem mem(guest);
        world::Globals gl;
        gl.contextPtr = guestAddress(kContextPtr);
        gl.hpKey = guestAddress(gl.hpKey);
        gl.playerStats = guestAddress(gl.playerStats);
        gl.handleVtable = guestAddress(kHandleVtable);
        const double now = g_gameTime;
        const bool ok = world::readPlayerPose(mem, gl, g_readState, now, g_tick, cur) == world::PoseStatus::Ok;
        if (ok && !cur.mounted)
            g_hist.push_back(cur);
        while (g_hist.size() > g_delayTicks + 1)
            g_hist.pop_front();
        if (g_log && ok && g_tick % 15 == 0)
            std::fprintf(stderr, "[ghost] player lower %x@%g upper %x@%g lowerMask %x\n", cur.lower.clip,
                         static_cast<double>(cur.lower.time), cur.upper.clip, static_cast<double>(cur.upper.time),
                         cur.lowerMask);
        if (g_hist.size() > g_delayTicks && (past = g_hist.front()).lower.clip) {
            past.position.x += g_offX;
            past.position.z += g_offZ;
            if (!g_actCycle.empty()) { // test aid: show each activity's clip on the stand-in for 3 s
                const size_t i = static_cast<size_t>(g_gameTime / 3.0) % g_actCycle.size();
                float dur = 1.0f;
                const uint32_t clip = drakan_coop_character::sampleClip(g_actCycle[i], &dur);
                if (clip) {
                    const float t = static_cast<float>(std::fmod(g_gameTime, 3.0));
                    past.lower = past.upper = {clip, std::fmod(t, std::max(dur, 0.1f))};
                    if (g_actCycle[i] == drakan_coop_character::Act::Death)
                        past.lower.time = past.upper.time = std::min(t, dur * 0.999f);
                    past.lowerMask = ~0u;
                }
            }
            for (Ghost& g : g_ghosts)
                if (g.info.handle && !g.destroyRequested) {
                    if (g.dragon) { // the mirrored pose is Rynn's: only the position follows (test aid)
                        std::memcpy(g.target.pos, &past.position, sizeof g.target.pos);
                        std::memcpy(g.target.rot, past.rotation, sizeof g.target.rot);
                        g.hasTransform = true;
                        continue;
                    }
                    applyPose(g, past);
                    g.wantRec[0] = g_forceRec[0] ? g_forceRec[0] : past.weaponRight;
                    g.wantRec[1] = g_forceRec[1] ? g_forceRec[1] : past.weaponLeft;
                }
        }
        if (g_autoPending && g_ghosts.empty() && !notReadyReason() && ok && !cur.mounted) {
            if (g_autoDelay)
                --g_autoDelay;
            else {
                g_autoPending = false;
                g_ghosts.push_back(Ghost{});
                g_ghosts.back().info.id = g_nextId++;
                g_ghosts.back().character = g_defaultCharacter;
                g_ghosts.back().dragon = g_defaultDragon;
                std::fprintf(stderr, "[ghost] mirror: auto spawn id %d\n", g_ghosts.back().info.id);
            }
        }
    }

    // Native LOD readback, independent of pose writes. Never force the controller's slot: native selection also
    // updates its vertex/skin buffers. These transitions distinguish a coarse source model from a stuck far LOD.
    if (g_log)
        for (Ghost& g : g_ghosts)
            if (g.info.handle) {
                const uint32_t sk = guest.read32(g.info.handle + world::skel::kHandleSkeleton);
                if (ramPtr(sk)) {
                    const uint32_t lod = guest.read32(sk + 0x10);
                    if (lod != g.loggedLod) {
                        g.loggedLod = lod;
                        std::fprintf(stderr, "[ghost] id %d body %x resource %x native lod %u flags+104 %x\n",
                                     g.info.id, g.info.handle, guest.read32(g.info.handle + 8), lod,
                                     guest.read32(g.info.handle + 0x104));
                    }
                }
            }

    // Anim readback (log only): what the engine left in channel 0 since our last write.
    if (g_log && g_tick % 30 == 0)
        for (const Ghost& g : g_ghosts)
            if (g.info.handle && g.animSet) {
                const uint32_t sk = guest.read32(g.info.handle + world::skel::kHandleSkeleton);
                const uint32_t ch = ramPtr(sk) ? guest.read32(sk + world::skel::kChannels) : 0;
                if (ramPtr(ch)) {
                    const uint32_t n = guest.read32(sk + world::skel::kChannelCount);
                    std::fprintf(stderr, "[ghost] id %d channels", g.info.id);
                    for (uint32_t i = 0; i < n && i < world::skel::kMaxChannels; ++i)
                        std::fprintf(stderr, " %x",
                                     guest.read32(ch + i * world::skel::kChannelStride + world::skel::kChannelClip));
                    std::fputc('\n', stderr);
                }
                if (ramPtr(ch))
                    std::fprintf(stderr, "[ghost] id %d ch0 clip %x time %g (we wrote %x %g)\n", g.info.id,
                                 guest.read32(ch + world::skel::kChannelClip),
                                 static_cast<double>(guest.readF(ch + world::skel::kChannelTime)), g.animClip[0],
                                 static_cast<double>(g.animTime[0]));
            }

    if (g_trace)
        for (const Ghost& g : g_ghosts)
            if (g.info.handle && g.animSet) {
                const uint32_t sk = guest.read32(g.info.handle + world::skel::kHandleSkeleton);
                const uint32_t ch = ramPtr(sk) ? guest.read32(sk + world::skel::kChannels) : 0;
                const uint32_t pl = guest.read32(ctx + 0x4b0);
                std::fprintf(
                    stderr,
                    "[gtrace] %u %.4f dt %.4f gpos %.3f %.3f ghostch0t %.4f wrote %.4f clip %x ppos %.3f %.3f\n",
                    g_tick, nowSeconds(), static_cast<double>(engineDt),
                    static_cast<double>(guest.readF(g.info.handle + 0x14)),
                    static_cast<double>(guest.readF(g.info.handle + 0x1c)),
                    ramPtr(ch) ? static_cast<double>(guest.readF(ch + world::skel::kChannelTime)) : -1.0,
                    static_cast<double>(g.animTime[0]), g.animClip[0], static_cast<double>(guest.readF(pl + 0x14)),
                    static_cast<double>(guest.readF(pl + 0x1c)));
            }
    for (Ghost& g : g_ghosts)
        if (g.info.handle && g.animSet)
            writeAnim(guest, g);
    // Transforms (the spawn's own transform is applied by the factory, so only live ghosts here).
    for (Ghost& g : g_ghosts)
        if ((g.hasTransform || g.hold || g.pinned) && g.info.handle) {
            if (g.pinned)
                g.target = g.pin;
            for (int k = 0; k < 3; ++k)
                guest.writeF(g.info.handle + 0x14 + 4u * static_cast<uint32_t>(k), g.target.pos[k]);
            for (int k = 0; k < 9; ++k)
                guest.writeF(g.info.handle + 0x20 + 4u * static_cast<uint32_t>(k), g.target.rot[k]);
            // PAL's native selector honors this actor flag and rebuilds the appropriate skinning buffers.
            // A nearby network Rynn can otherwise drop from 1,628 vertices to 208 at just 2,730 units.
            if (g.info.remotePlayer && drakan_region::build().identityAddresses) {
                constexpr uint32_t kDetailBit = 1u << 22;
                const float dx = g.target.pos[0] - guest.readF(0x4dadc4);
                const float dy = g.target.pos[1] - guest.readF(0x4dadc8);
                const float dz = g.target.pos[2] - guest.readF(0x4dadcc);
                const float distanceSq = dx * dx + dy * dy + dz * dz;
                const uint32_t flags = guest.read32(g.info.handle + 0x104);
                if (distanceSq <= 4000.f * 4000.f && !(flags & kDetailBit)) {
                    guest.write32(g.info.handle + 0x104, flags | kDetailBit);
                    g.ownsDetailRequest = true;
                } else if (g.ownsDetailRequest && distanceSq >= 4500.f * 4500.f) {
                    guest.write32(g.info.handle + 0x104, flags & ~kDetailBit);
                    g.ownsDetailRequest = false;
                }
            }
            g.hasTransform = false;
            if (!g_busy.load()) {
                if (g.localVisual) {
                    // Keep the cosmetic body out of collision / blade-sweep cells during simulation. RenderWorld
                    // temporarily files it for the native visibility / skinning pipeline, then unfiles it again.
                    const uint32_t world = guest.read32(ctx + 0x31c);
                    const uint32_t vt = ramPtr(world) ? guest.read32(world + 0x24b4) : 0;
                    if (ramPtr(vt) && (guest.read32(g.info.handle + 0x104) & 0x10000000u)) {
                        const uint32_t remove = guest.read32(vt + 0x84);
                        const int16_t adjust = static_cast<int16_t>(guest.read16(vt + 0x80));
                        if (ramPtr(remove))
                            guest.call(drakan_input::GuestCall(remove).arg(world + adjust).arg(g.info.handle));
                    }
                    guest.write32(g.info.handle + 0x7c, 0xffffffffu);
                } else if (g_sectorUpdate) {
                    updateSector(guest, ctx, g);
                }
            }
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
    if (startWeaponChange(guest, ctx))
        return;
    Ghost* want = nullptr;
    for (Ghost& g : g_ghosts)
        if (!g.info.handle && !g.destroyRequested) {
            want = &g;
            break;
        }
    if (!want)
        return;
    if (const char* why = notReadyReason()) { // queued before the world was ready (or a load started since)
        g_spawnFailures.fetch_add(1);
        std::fprintf(stderr, "[ghost] spawn of id %d refused: %s\n", want->info.id, why);
        g_ghosts.erase(g_ghosts.begin() + (want - g_ghosts.data()));
        return;
    }
    const uint32_t player = guest.read32(ctx + 0x4b0);
    uint32_t res = ramPtr(player) ? guest.read16(player + 0x10) : 0xffffu;
    const uint32_t db = guest.read32(ctx + 0x320);
    const uint32_t records = ramPtr(db) ? guest.read32(db + 8) : 0;
    const uint32_t playerClass =
        ramPtr(player) && ramPtr(records) && res != 0xffffu ? guest.read32(records + res * 0x24u + 8) : 0;
    // Rynn is the controlled actor on foot; a rider on Arokh controls the dragon (class 0x1001c): the bodies of a
    // partner are still wanted then (a Rynn ghost needs Rynn's record index, not the dragon's).
    if (playerClass != kRynnClass && playerClass != kDragonClass) {
        std::fprintf(stderr, "[ghost] spawn refused: the controlled actor is not Rynn (class 0x10009)\n");
        g_spawnFailures.fetch_add(1);
        g_ghosts.erase(g_ghosts.begin() + (want - g_ghosts.data()));
        return;
    }
    // SYSTEM's database index (the high half of every record / clip id) differs per level (0x2 in Surdana, 0x4 in
    // Ravenshold): Rynn's and Arokh's records are named by the prefix of the controlled actor's own record.
    const uint32_t sysHi = guest.read32(records + res * 0x24u) & 0xffff0000u;
    if (playerClass == kDragonClass) {
        res = recordIndex(guest, ctx, sysHi | (kRynnRecord & 0xffffu));
        if (res == 0xffffu) {
            std::fprintf(stderr, "[ghost] spawn refused: Rynn's record is not in this level's database\n");
            g_spawnFailures.fetch_add(1);
            g_ghosts.erase(g_ghosts.begin() + (want - g_ghosts.data()));
            return;
        }
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
    g_flight.npc = false;
    g_flight.dragon = false;
    const uint32_t sysHiNow = sysHi;
    g_levelSysHi.store(sysHi);
    if (want->character || want->dragon) {
        const uint32_t chosen = drakan_coop_character::entry(want->character).recordId;
        const uint32_t rec = want->dragon                         ? (sysHiNow | (kDragonRecord & 0xffffu))
                             : (chosen & 0xffff0000u) == 0x20000u ? sysHiNow | (chosen & 0xffffu)
                                                                  : chosen;
        const uint32_t idx = recordIndex(guest, ctx, rec);
        if (idx == 0xffffu) {
            std::fprintf(stderr, "[ghost] spawn of id %d: record %x of character %x is not in this level's database\n",
                         want->info.id, rec, want->character);
            g_spawnFailures.fetch_add(1);
            g_ghosts.erase(g_ghosts.begin() + (want - g_ghosts.data()));
            return;
        }
        g_flight.resIndex = idx;
        g_flight.npc = true;
        g_flight.dragon = want->dragon || chosen == kDragonRecord;
    }
    // Camera mode word: *(*(ctx+0x4b4)+0xfc)+0x10 = camera instance data, +0x14c
    const uint32_t cam = guest.read32(ctx + 0x4b4);
    const uint32_t inner = ramPtr(cam) ? guest.read32(cam + 0xfc) : 0;
    const uint32_t data = ramPtr(inner) ? guest.read32(inner + 0x10) : 0;
    g_flight.camModeAddr = ramPtr(data) && (!g_flight.npc || g_flight.dragon) ? data + 0x14c : 0;
    g_busy.store(true);
    std::fprintf(stderr, "[ghost] spawning id %d: res %u at %.0f %.0f %.0f (camera mode word %x)\n", want->info.id, res,
                 static_cast<double>(t.pos[0]), static_cast<double>(t.pos[1]), static_cast<double>(t.pos[2]),
                 g_flight.camModeAddr);
    guest.post(drakan_input::GuestCall(g_t1));
}

void drawControls() {
    if (!g_enabled.load() || !g_debugKeys)
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
