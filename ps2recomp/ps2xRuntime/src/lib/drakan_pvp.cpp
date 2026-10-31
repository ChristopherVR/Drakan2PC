// Drakan: PvP combat glue (drakan_pvp.h). PAL addresses are canonical, translated through the disc profile.
// Evidence and the design: docs/journal/2026-10-31/pvp.md, docs/reference/netcode.md "PvP".
#include "drakan_pvp.h"
#include "drakan_ghost.h"
#include "drakan_lifecycle.h"
#include "drakan_netplay.h"
#include "drakan_region.h"
#include "input/GuestInterface.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace drakan_pvp {
namespace {

using drakan_region::guestAddress;

constexpr uint32_t kContextPtr = 0x5565d4u, kBladeHit = 0x498020u, kStatsBlock = 0x4fbac8u, kHpKey = 0x4fe86cu;
constexpr uint32_t kDamageMethod = 0x314ab0u; // Rynn data object: (data, int* damage, attacker, 0, 0, kind)
constexpr uint32_t kDeathClip = 0x20577u;     // Rynn's death clip (lower channel when the engine kills her)
constexpr float kDeathClipHold = 4.0f;        // seconds of the clip shown; the last pose is held after that
constexpr uint32_t kScratch = 0x5b8480u;      // tail of the speech controller's text buffer (idle), 0xa0 bytes
constexpr uint32_t kRightHolder = 0xeb4u, kLeftHolder = 0xe90u, kHolderRef = 0x1cu;
constexpr uint32_t kBladeHitCount = 0x1a0u;
constexpr float kCrumbSpacing = 500.0f; // units of travel between two remembered walkable positions
constexpr size_t kMaxCrumbs = 24;

enum class Life : int { Alive, Dying };

std::atomic<bool> g_enabled{false};
bool g_probe = false;
std::atomic<uint8_t> g_local{0};
std::atomic<int> g_life{static_cast<int>(Life::Alive)};
PS2Runtime::RecompiledFunction g_origBladeHit = nullptr;
uint32_t g_deadHandlerSlot = 0;          // function table slot of deadHandler
std::atomic<uint32_t> g_savedHandler{0}; // the player's behaviour handler while a death is shown
uint32_t g_deadActor = 0;                // the player actor the handler was swapped on

struct Event {
    enum Kind { Hit, Death, Respawn, Phase } kind = Hit;
    uint8_t
        a = 0,
        b = 0,
        w = 0; // Hit: attacker, victim, weapon; Death: victim (a), killer (b), weapon; Respawn: player (a), spawn (b)
    uint16_t damage = 0, health = 0;
};
std::mutex g_m;
std::deque<Event> g_events;

// EE thread state.
struct Crumb {
    float p[3];
};
std::vector<Crumb> g_crumbs;
std::vector<Crumb> g_enemies; // positions of the ghosts the last time any existed (they are gone after a reload)
std::string g_level;          // last loaded level stem
unsigned g_lifeTicks = 0, g_ready = 0;
float g_deathTime = 0.0f;
int g_pendingHp = -1; // engine HP to write once no posted engine call is in flight
struct RespawnReq {
    uint8_t spawn = 0;
    uint16_t health = 100;
} g_resp;

uint32_t rd32(const uint8_t* ram, uint32_t a) {
    a &= 0x1ffffffu;
    uint32_t v = 0;
    if (a + 4 <= PS2_RAM_SIZE)
        std::memcpy(&v, ram + a, 4);
    return v;
}
uint16_t rd16(const uint8_t* ram, uint32_t a) {
    a &= 0x1ffffffu;
    uint16_t v = 0;
    if (a + 2 <= PS2_RAM_SIZE)
        std::memcpy(&v, ram + a, 2);
    return v;
}
float rdf(const uint8_t* ram, uint32_t a) {
    const uint32_t v = rd32(ram, a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
bool ramPtr(uint32_t p) {
    return p >= 0x100000u && p < 0x2000000u;
}

uint32_t rotr8(uint32_t x) {
    return (x >> 8) | (x << 24);
}
uint32_t rotl8(uint32_t x) {
    return (x << 8) | (x >> 24);
}

// Local player's HP block: dec(S+0xc) / dec(S+0x10) (max), dec(x) = rotr32(x ^ key, 8), enc(v) = rotl32(v, 8) ^ key.
int32_t playerHp(const uint8_t* rdram) {
    return static_cast<int32_t>(
        rotr8(rd32(rdram, guestAddress(kStatsBlock) + 0xc) ^ rd32(rdram, guestAddress(kHpKey))));
}
int32_t playerHpMax(const uint8_t* rdram) {
    return static_cast<int32_t>(
        rotr8(rd32(rdram, guestAddress(kStatsBlock) + 0x10) ^ rd32(rdram, guestAddress(kHpKey))));
}
void setPlayerHp(uint8_t* rdram, int32_t hp) {
    const uint32_t raw = rotl8(static_cast<uint32_t>(hp)) ^ rd32(rdram, guestAddress(kHpKey));
    std::memcpy(rdram + ((guestAddress(kStatsBlock) + 0xc) & 0x1ffffffu), &raw, 4);
}
// The server counts health on a 100 point bar (MatchConfig.startHealth); Rynn's own maximum is whatever her level
// gives.
int engineUnits(const uint8_t* rdram, int serverPoints) {
    const int max = std::max(1, playerHpMax(rdram));
    return std::max(0, static_cast<int>(std::lround(static_cast<double>(serverPoints) * max / 100.0)));
}

uint32_t registryObject(const uint8_t* rdram, uint32_t ref) {
    const uint32_t ctx = rd32(rdram, guestAddress(kContextPtr));
    if (!ref || ref > rd32(rdram, ctx + 0x84) + 1)
        return 0;
    return rd32(rdram, rd32(rdram, ctx + 0x7c) + (ref - 1) * 8);
}
// An actor's data object (Cam_GetObjFromHandle): *(*(h+0xfc)+0x10).
uint32_t dataOf(const uint8_t* rdram, uint32_t h) {
    const uint32_t p = ramPtr(h) ? rd32(rdram, h + 0xfc) : 0;
    return ramPtr(p) ? rd32(rdram, p + 0x10) : 0;
}
uint32_t controlledActor(const uint8_t* rdram) {
    const uint32_t ctx = rd32(rdram, guestAddress(kContextPtr));
    return ramPtr(ctx) ? rd32(rdram, ctx + 0x4b0) : 0;
}

bool isGhost(uint32_t handle, int* id = nullptr) {
    bool found = false;
    drakan_ghost::forEach([&](const drakan_ghost::Info& i) {
        if (i.handle && i.handle == handle) {
            found = true;
            if (id)
                *id = i.id;
        }
    });
    return found;
}

// Which hand a blade (weapon data object) belongs to: 0 right holder, 1 left holder, 2 unknown.
uint8_t handOfBlade(const uint8_t* rdram, uint32_t blade) {
    const uint32_t player = dataOf(rdram, controlledActor(rdram));
    if (!ramPtr(player))
        return 2;
    const uint32_t right = dataOf(rdram, registryObject(rdram, rd16(rdram, player + kRightHolder + kHolderRef)));
    const uint32_t left = dataOf(rdram, registryObject(rdram, rd16(rdram, player + kLeftHolder + kHolderRef)));
    if (blade && blade == right)
        return 0;
    if (blade && blade == left)
        return 1;
    return 2;
}

// ---------------------------------------------------------------------------------------------- attacker side

// The engine's blade sweep finds a ghost once drakan_ghost files it in the collision cells (refile), and the hit
// application runs the engine's normal path: effects at the contact, the message to the victim, which the ghost's
// swallowing handler drops (the local HP block is untouched; checked by the guard below). A hit that the blade's own
// hit list accepted (its count grows: one per victim and swing) becomes a claim for the server.
void hookBladeHit(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t blade = getRegU32(c, 4), contact = getRegU32(c, 5);
    const uint32_t victim = ramPtr(contact) ? rd32(rdram, contact + 0x20) : 0;
    int gid = 0;
    if (!ramPtr(victim) || !isGhost(victim, &gid)) {
        g_origBladeHit(rdram, c, rt);
        return;
    }
    const int32_t hpBefore = playerHp(rdram);
    const uint32_t hitsBefore = rd32(rdram, blade + kBladeHitCount);
    const int32_t damage = static_cast<int32_t>(rd32(rdram, contact));
    g_origBladeHit(rdram, c, rt);
    const int32_t hpAfter = playerHp(rdram);
    if (hpAfter != hpBefore) { // the swallowed handler should make this impossible: undo, never hurt ourselves
        std::fprintf(stderr, "[pvp] WARNING: a hit on ghost %d changed the local HP %d -> %d, restored\n", gid,
                     hpBefore, hpAfter);
        setPlayerHp(rdram, hpBefore);
    }
    const bool isNew = rd32(rdram, blade + kBladeHitCount) > hitsBefore;
    const int player = drakan_netplay::playerOfGhost(gid);
    const uint8_t hand = handOfBlade(rdram, blade);
    if (g_probe || isNew)
        std::fprintf(stderr, "[pvp] blade %x hit ghost %d (player %d) hand %u damage %d %s\n", blade, gid, player,
                     static_cast<unsigned>(hand), damage, isNew ? "NEW -> claim" : "(same swing)");
    if (isNew && player > 0 && damage > 0 && hand < 2 && g_life.load() == static_cast<int>(Life::Alive))
        drakan_netplay::sendHitClaim(static_cast<uint8_t>(player), hand,
                                     static_cast<uint16_t>(std::min(damage, 65535)));
}

// ---------------------------------------------------------------------------------------------- victim side

// Engine hit reaction for the local player: Rynn's own damage method (flinch, sound, HUD message) with a damage that
// can never kill her. Posted, not called: the method plays sounds and effects (allocations behind the heap semaphore);
// a nested call that has to wait hangs the guest (seen). The HP block is set afterwards (g_pendingHp).
void hurtLocal(drakan_input::IGuest& guest, uint8_t* rdram, int engineDamage) {
    const uint32_t data = dataOf(rdram, controlledActor(rdram));
    if (!ramPtr(data))
        return;
    const int32_t hp = playerHp(rdram);
    const int d = std::clamp(engineDamage, 0, std::max(0, hp - 1));
    if (d <= 0)
        return;
    guest.write32(kScratch, static_cast<uint32_t>(d));
    guest.post(
        drakan_input::GuestCall(guestAddress(kDamageMethod)).arg(data).arg(kScratch).arg(0).arg(0).arg(0).arg(1));
}

// The behaviour handler the player has while a death is shown: it swallows every event but the destroy (2), so the
// engine's tick (input, physics, the state machine) does not touch her and the death clip written into her skeleton
// stays (the same technique as a ghost). Every other player sees the clip through the replicated pose.
void deadHandler(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (getRegU32(c, 5) == 2u && g_savedHandler.load()) { // destroy: the real handler releases her sub-objects
        c->pc = g_savedHandler.load();
        rt->lookupFunction(c->pc)(rdram, c, rt);
        return;
    }
    SET_GPR_U32(c, 2, 0u);
    c->pc = getRegU32(c, 31);
}

// A match death. The engine's own death (lethal damage) hides the body and raises the single-player game over prompt
// ("Press Enter to load the last saved game"), seen in run pvp14 / pvp15, and the level reload that follows hangs the
// guest when it is started from outside the game's own flow. So HP never reaches 0 here: the player is frozen with
// the death clip until the server's Respawn.
void startDeath(uint8_t* rdram) {
    const uint32_t actor = controlledActor(rdram);
    const uint32_t inner = ramPtr(actor) ? rd32(rdram, actor + 0xfc) : 0;
    if (!ramPtr(inner) || !g_deadHandlerSlot)
        return;
    g_deadActor = actor;
    g_savedHandler.store(rd32(rdram, inner + 0x14));
    const uint32_t slot = g_deadHandlerSlot;
    std::memcpy(rdram + ((inner + 0x14) & 0x1ffffffu), &slot, 4);
    g_deathTime = 0.0f;
    setPlayerHp(rdram, 1);
    g_pendingHp = -1;
}

void endDeath(uint8_t* rdram) {
    const uint32_t actor = controlledActor(rdram);
    const uint32_t inner = ramPtr(actor) && actor == g_deadActor ? rd32(rdram, actor + 0xfc) : 0;
    if (ramPtr(inner) && g_savedHandler.load()) {
        const uint32_t h = g_savedHandler.load();
        std::memcpy(rdram + ((inner + 0x14) & 0x1ffffffu), &h, 4);
    }
    g_savedHandler.store(0);
    g_deadActor = 0;
}

// Writes the death clip into every channel of the player's skeleton, once per boundary (before the engine draws).
void showDeath(drakan_input::IGuest& guest, const uint8_t* rdram, float dt) {
    const uint32_t actor = controlledActor(rdram);
    if (actor != g_deadActor)
        return;
    g_deathTime += dt;
    const uint32_t sk = rd32(rdram, actor + 0x50);
    const uint32_t ch = ramPtr(sk) ? rd32(rdram, sk + 0xe8) : 0;
    const uint32_t n = ramPtr(sk) ? rd32(rdram, sk + 0xf0) : 0;
    if (!ramPtr(ch) || n == 0 || n > 32)
        return;
    const float t = std::min(g_deathTime, kDeathClipHold);
    for (uint32_t i = 0; i < n; ++i) {
        guest.write32(ch + i * 0x80u + 0x50u, kDeathClip);
        guest.writeF(ch + i * 0x80u + 0x58u, t);
    }
}

// A walkable position far from every enemy; the spawn index breaks ties and spreads successive respawns.
bool pickSpawn(uint8_t index, float out[3]) {
    if (g_crumbs.empty())
        return false;
    if (g_enemies.empty()) {
        std::memcpy(out, g_crumbs[index % g_crumbs.size()].p, sizeof(float) * 3);
        return true;
    }
    float best = -1.0f;
    size_t pick = 0;
    for (size_t i = 0; i < g_crumbs.size(); ++i) {
        float nearest = 1e30f;
        for (const Crumb& e : g_enemies) {
            const float dx = g_crumbs[i].p[0] - e.p[0], dz = g_crumbs[i].p[2] - e.p[2];
            nearest = std::min(nearest, dx * dx + dz * dz);
        }
        // Ties (and the first few picks) rotate with the index so two respawns in a row do not stack.
        const float score = nearest + static_cast<float>((i + index) % g_crumbs.size()) * 1.0f;
        if (score > best) {
            best = score;
            pick = i;
        }
    }
    std::memcpy(out, g_crumbs[pick].p, sizeof(float) * 3);
    return true;
}

void reviveLocal(drakan_input::IGuest& guest, uint8_t* rdram) {
    endDeath(rdram);
    const uint32_t player = controlledActor(rdram);
    float spawn[3];
    if (ramPtr(player) && pickSpawn(g_resp.spawn, spawn)) {
        for (int k = 0; k < 3; ++k) {
            const float v = k == 1 ? spawn[k] + 20.0f : spawn[k];
            guest.writeF(player + 0x14 + 4u * static_cast<uint32_t>(k), v);
        }
        drakan_ghost::refileActor(guest, player);
        std::fprintf(stderr, "[pvp] respawned at spawn %u: %.0f %.0f %.0f\n", g_resp.spawn,
                     static_cast<double>(spawn[0]), static_cast<double>(spawn[1]), static_cast<double>(spawn[2]));
    }
    g_pendingHp = std::max(1, engineUnits(rdram, g_resp.health));
    g_life.store(static_cast<int>(Life::Alive));
}

void recordCrumb(const uint8_t* rdram, uint32_t player) {
    float p[3];
    for (int k = 0; k < 3; ++k)
        p[k] = rdf(rdram, player + 0x14 + 4u * static_cast<uint32_t>(k));
    if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]))
        return;
    if (!g_crumbs.empty()) {
        const Crumb& last = g_crumbs.back();
        const float dx = p[0] - last.p[0], dy = p[1] - last.p[1], dz = p[2] - last.p[2];
        if (dx * dx + dz * dz < kCrumbSpacing * kCrumbSpacing || std::fabs(dy) > 150.0f)
            return; // not far enough, or a jump / fall: not a position to come back to
    }
    g_crumbs.push_back({{p[0], p[1], p[2]}});
    if (g_crumbs.size() > kMaxCrumbs)
        g_crumbs.erase(g_crumbs.begin() + 1); // keep the first (the level's start)
}

void handleEvent(drakan_input::IGuest& guest, uint8_t* rdram, const Event& e) {
    const uint8_t me = g_local.load();
    if (!me)
        return;
    const Life life = static_cast<Life>(g_life.load());
    switch (e.kind) {
        case Event::Hit:
            if (e.b == me && e.health > 0 && life == Life::Alive) {
                std::fprintf(stderr, "[pvp] hit by player %u: %u damage, server health %u (HP %d)\n", e.a, e.damage,
                             e.health, playerHp(rdram));
                hurtLocal(guest, rdram, engineUnits(rdram, e.damage));
                g_pendingHp = std::max(1, engineUnits(rdram, e.health));
            }
            break;
        case Event::Death:
            if (e.a == me && life == Life::Alive) {
                std::fprintf(stderr, "[pvp] killed by player %u (weapon %u): frozen with the death clip\n", e.b, e.w);
                g_life.store(static_cast<int>(Life::Dying));
                g_lifeTicks = 0;
                startDeath(rdram);
            }
            break;
        case Event::Phase: // the match ended (or went back to the lobby) while we are down: get up where we are
            if (e.a != 2 && life == Life::Dying) {
                g_resp.spawn = 0;
                g_resp.health = 100;
                endDeath(rdram);
                g_pendingHp = std::max(1, engineUnits(rdram, 100));
                g_life.store(static_cast<int>(Life::Alive));
            }
            break;
        case Event::Respawn:
            if (e.a != me)
                break;
            g_resp.spawn = e.b;
            g_resp.health = e.health;
            if (life == Life::Dying) {
                reviveLocal(guest, rdram);
            } else if (life == Life::Alive) { // a match start / mid-match join: just the health
                g_pendingHp = std::max(1, engineUnits(rdram, e.health));
            }
            break;
    }
}

struct SelfTest {
    int hit = -1, kill = -1, respawn = -1;
} g_self;

void parseSelfTest(const char* s) {
    int v = 0;
    for (const char* p = s; *p;) {
        if (std::sscanf(p, "hit:%d", &v) == 1)
            g_self.hit = v;
        else if (std::sscanf(p, "kill:%d", &v) == 1)
            g_self.kill = v;
        else if (std::sscanf(p, "respawn:%d", &v) == 1)
            g_self.respawn = v;
        while (*p && *p != ',')
            ++p;
        if (*p == ',')
            ++p;
    }
    std::fprintf(stderr, "[pvp] self test: hit@%d kill@%d respawn@%d (ticks after the player is ready)\n", g_self.hit,
                 g_self.kill, g_self.respawn);
}

// Feeds the victim side the events a server would send (for a single instance without an opponent).
void selfTestTick() {
    const int t = static_cast<int>(g_ready);
    const uint8_t me = g_local.load() ? g_local.load() : 1;
    g_local.store(me);
    if (t == g_self.hit)
        onServerHit(2, me, 0, 20, 70);
    if (t == g_self.kill) {
        onServerHit(2, me, 0, 70, 0);
        onServerDeath(me, 2, 0);
    }
    if (t == g_self.respawn)
        onServerRespawn(me, 1, 100);
}

// Test aid (DRAKAN_PVP_TEST_PLACE=<dist>): the ghosts are held <dist> units from the player, rotating through the four
// directions every 240 boundaries, so a scripted attack reaches them wherever the other instance's player stands.
void testPlaceGhosts(uint8_t* rdram, uint32_t player, unsigned tick) {
    static const char* place = std::getenv("DRAKAN_PVP_TEST_PLACE");
    if (!place)
        return;
    // DRAKAN_PVP_TEST_ANCHOR=1: the player is held where she stood at the first call (solid ghosts pushed her away).
    static const bool anchor = std::getenv("DRAKAN_PVP_TEST_ANCHOR") != nullptr;
    static bool haveAnchor = false;
    static float anchorPos[3];
    if (anchor) {
        if (!haveAnchor) {
            std::memcpy(anchorPos, rdram + ((player + 0x14) & 0x1ffffffu), sizeof anchorPos);
            haveAnchor = true;
        }
        std::memcpy(rdram + ((player + 0x14) & 0x1ffffffu), anchorPos, sizeof anchorPos);
    }
    float m[12];
    std::memcpy(m, rdram + ((player + 0x14) & 0x1ffffffu), sizeof m);
    const float dist = static_cast<float>(std::atof(place));
    const unsigned phase = (tick / 240) % 4;
    const float* axis = phase < 2 ? m + 9 : m + 3;
    const float sign = (phase & 1) ? -1.0f : 1.0f;
    float pos[3] = {m[0] + sign * dist * axis[0], m[1], m[2] + sign * dist * axis[2]};
    drakan_ghost::forEach([&](const drakan_ghost::Info& i) {
        if (i.handle)
            drakan_ghost::setTransform(i.id, pos, m + 3);
    });
}

} // namespace

bool enabled() {
    return g_enabled.load();
}

void install(PS2Runtime& rt) {
    const char* v = std::getenv("DRAKAN_PVP");
    const bool want = v && *v ? v[0] != '0' : drakan_netplay::wanted();
    if (!want)
        return;
    const uint32_t a = guestAddress(kBladeHit);
    if (!a || !guestAddress(kContextPtr) || !guestAddress(kDamageMethod) || !(g_origBladeHit = rt.lookupFunction(a))) {
        std::fprintf(stderr, "[pvp] disabled: no address translation / function for this disc profile\n");
        return;
    }
    g_probe = std::getenv("DRAKAN_PVP_PROBE") != nullptr;
    if (const char* st = std::getenv("DRAKAN_PVP_SELFTEST"))
        parseSelfTest(st);
    for (uint32_t slot = 0x100300u; slot < 0x100400u && !g_deadHandlerSlot; slot += 4)
        if (!rt.hasFunction(slot) && rt.registerFunction(slot, &deadHandler))
            g_deadHandlerSlot = slot;
    if (!g_deadHandlerSlot) {
        std::fprintf(stderr, "[pvp] disabled: no free function table slot\n");
        return;
    }
    rt.replaceFunction(a, &hookBladeHit);
    using drakan_lifecycle::Kind;
    drakan_lifecycle::subscribe(Kind::AfterLevelLoad, [](const drakan_lifecycle::Event& e) {
        if (!e.ok || e.level.empty())
            return;
        if (e.level != g_level) { // a different level: the remembered positions mean nothing there
            g_crumbs.clear();
            g_enemies.clear();
        }
        g_level = e.level;
    });
    // A level load destroys the actors (the swapped handler with them): a death shown is over, the new life starts
    // alive.
    drakan_lifecycle::subscribe(Kind::BeforeLevelLoad, [](const drakan_lifecycle::Event&) {
        g_savedHandler.store(0);
        g_deadActor = 0;
        g_life.store(static_cast<int>(Life::Alive));
        g_pendingHp = -1;
    });
    g_enabled.store(true);
    std::fprintf(stderr, "[pvp] enabled\n");
}

void beforePadMgr(drakan_input::IGuest& guest, uint8_t* rdram) {
    if (!g_enabled.load())
        return;
    const uint32_t player = controlledActor(rdram);
    if (!ramPtr(player) || !ramPtr(dataOf(rdram, player)))
        return;
    if (!drakan_ghost::ready() &&
        g_life.load() != static_cast<int>(Life::Dying)) // cut scene level / a load in progress
        return;
    ++g_ready;
    selfTestTick();

    // Remember where enemies are (for spawn selection) and where we walked.
    std::vector<Crumb> enemies;
    drakan_ghost::forEach([&](const drakan_ghost::Info& i) {
        if (i.handle)
            enemies.push_back(
                {{rdf(rdram, i.handle + 0x14), rdf(rdram, i.handle + 0x18), rdf(rdram, i.handle + 0x1c)}});
    });
    testPlaceGhosts(rdram, player, g_ready);
    if (!enemies.empty())
        g_enemies = std::move(enemies);

    switch (static_cast<Life>(g_life.load())) {
        case Life::Alive:
            recordCrumb(rdram, player);
            break;
        case Life::Dying: {
            ++g_lifeTicks;
            float dt = guest.readF(rd32(rdram, guestAddress(kContextPtr)) + 0x24);
            if (!(dt > 0.0f && dt < 0.25f))
                dt = 1.0f / 60.0f;
            showDeath(guest, rdram, dt);
            break;
        }
    }

    std::deque<Event> events;
    {
        std::lock_guard<std::mutex> l(g_m);
        events.swap(g_events);
    }
    for (const Event& e : events)
        handleEvent(guest, rdram, e);

    // The HP block after the engine's own (posted) damage has run.
    if (g_pendingHp >= 0 && !guest.postBusy() && g_life.load() != static_cast<int>(Life::Dying)) {
        setPlayerHp(rdram, g_pendingHp);
        g_pendingHp = -1;
    }
}

void onServerHit(uint8_t attacker, uint8_t victim, uint8_t weapon, uint16_t damage, uint16_t healthAfter) {
    std::lock_guard<std::mutex> l(g_m);
    g_events.push_back({Event::Hit, attacker, victim, weapon, damage, healthAfter});
}
void onServerDeath(uint8_t victim, uint8_t killer, uint8_t weapon) {
    std::lock_guard<std::mutex> l(g_m);
    g_events.push_back({Event::Death, victim, killer, weapon, 0, 0});
}
void onServerRespawn(uint8_t player, uint8_t spawnIndex, uint16_t health) {
    std::lock_guard<std::mutex> l(g_m);
    g_events.push_back({Event::Respawn, player, spawnIndex, 0, 0, health});
}
void onServerPhase(uint8_t phase) {
    std::lock_guard<std::mutex> l(g_m);
    g_events.push_back({Event::Phase, phase, 0, 0, 0, 0});
}
void setLocalPlayer(uint8_t id) {
    g_local.store(id);
}

} // namespace drakan_pvp
