// Drakan: PvP combat glue (drakan_pvp.h). PAL addresses are canonical, translated through the disc profile.
// Evidence and the design: docs/journal/2026-10-31/pvp.md, docs/reference/netcode.md "PvP".
#include "drakan_pvp.h"
#include "drakan_arena.h"
#include "drakan_ghost.h"
#include "drakan_lifecycle.h"
#include "drakan_netplay.h"
#include "drakan_region.h"
#include "input/GuestInterface.h"
#include "net/WeaponTable.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
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
    // Co-op: the partner's ghost is not a target (drakan_coop owns the hits of that mode).
    if (drakan_netplay::coopMatch() || !ramPtr(victim) || !isGhost(victim, &gid)) {
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
    // A blade neither holder owns (a two-handed weapon is carried by one holder, but the lookup can miss it mid-swing):
    // the right hand's record limits the claim on the server.
    uint8_t hand = handOfBlade(rdram, blade);
    if (hand >= 2)
        hand = 0;
    if (g_probe || isNew)
        std::fprintf(stderr, "[pvp] blade %x hit ghost %d (player %d) hand %u damage %d %s\n", blade, gid, player,
                     static_cast<unsigned>(hand), damage, isNew ? "NEW -> claim" : "(same swing)");
    if (isNew && player > 0 && damage > 0 && hand < 2 && g_life.load() == static_cast<int>(Life::Alive))
        drakan_netplay::sendHitClaim(static_cast<uint8_t>(player), hand,
                                     static_cast<uint16_t>(std::min(damage, 65535)));
}

// ---------------------------------------------------------------------------------------------- projectiles

// Arrows and spells are actors of their own (arrow: type 9, class 0x1005f, found in the bow5 / audio77 RAM dumps; spell
// projectile: class 0x10008, combat-encounters journal). The engine's projectile code was not traced; instead every
// boundary the registry is scanned for projectile actors that first appear next to the local player (the shot), and
// the path each one flew since the last boundary is tested against the vertical capsule of every ghost. A crossing is
// one claim (weapon = 2) per projectile: arrow damage 5 (the starting arrows' inventory "Damage"), spells
// kSpellDamage (their damage is not decoded; the server caps projectile claims at kMaxProjectileDamage).
constexpr uint32_t kArrowClass = 0x1005fu, kSpellClass = 0x10008u;
constexpr uint16_t kSpellDamage = 10;
constexpr float kProjSpawnRadius = 900.0f; // a projectile first seen farther from the player is somebody else's
constexpr float kGhostRadius = 150.0f;     // horizontal reach of a ghost
constexpr float kGhostBottom = -60.0f, kGhostTop = 460.0f; // vertical window around the ghost's origin
constexpr unsigned kProjMaxAgeTicks = 600;

struct Proj {
    float last[3];
    unsigned born = 0;
    bool claimed = false;
    bool seen = false;
    uint16_t damage = 0;
};
std::map<uint32_t, Proj> g_proj;
unsigned g_projTick = 0;

bool segmentHitsGhost(const float a[3], const float b[3], const float g[3]) {
    const float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    const int steps = std::clamp(static_cast<int>(len / 50.0f) + 1, 1, 64);
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float x = a[0] + dx * t - g[0], z = a[2] + dz * t - g[2], y = a[1] + dy * t - g[1];
        if (x * x + z * z <= kGhostRadius * kGhostRadius && y >= kGhostBottom && y <= kGhostTop)
            return true;
    }
    return false;
}

// Item property `index` of actor `h` (the property block's accessor: vtable +0x1c, this adjust at +0x18), 0 when
// absent. The accessor only indexes a table: safe to call nested.
uint32_t actorProperty(drakan_input::IGuest& guest, const uint8_t* rdram, uint32_t h, uint32_t index) {
    const uint32_t block = rd32(rdram, h + 0x54), vt = ramPtr(block) ? rd32(rdram, block + 8) : 0;
    if (!ramPtr(vt))
        return 0;
    const uint32_t fn = rd32(rdram, vt + 0x1c);
    if (!ramPtr(fn))
        return 0;
    const auto off = static_cast<int16_t>(rd32(rdram, vt + 0x18) & 0xffffu);
    const uint32_t pp = guest.call(drakan_input::GuestCall(fn)
                                       .arg(block + static_cast<uint32_t>(static_cast<int32_t>(off)))
                                       .arg(index)
                                       .arg(0xffffffffu));
    return ramPtr(pp) ? rd32(rdram, pp) : 0;
}

void scanProjectiles(drakan_input::IGuest& guest, const uint8_t* rdram, uint32_t player) {
    const uint32_t ctx = rd32(rdram, guestAddress(kContextPtr));
    if (!ramPtr(ctx))
        return;
    const uint32_t table = rd32(rdram, ctx + 0x7c), res = rd32(rdram, ctx + 0x320);
    const uint32_t n = std::min<uint32_t>(rd32(rdram, ctx + 0x84), 20000u),
                   recs = ramPtr(res) ? rd32(rdram, res + 8) : 0;
    if (!ramPtr(table) || !ramPtr(recs))
        return;
    struct Target {
        int id;
        float pos[3];
    };
    std::vector<Target> targets;
    drakan_ghost::forEach([&](const drakan_ghost::Info& i) {
        if (i.handle)
            targets.push_back(
                {i.id, {rdf(rdram, i.handle + 0x14), rdf(rdram, i.handle + 0x18), rdf(rdram, i.handle + 0x1c)}});
    });
    float me[3];
    for (int k = 0; k < 3; ++k)
        me[k] = rdf(rdram, player + 0x14 + 4u * static_cast<uint32_t>(k));
    ++g_projTick;
    std::vector<uint32_t> alive;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t h = rd32(rdram, table + i * 8);
        if (!ramPtr(h) || h == player || rd32(rdram, h) != guestAddress(0x51b8b0u))
            continue;
        const uint32_t type = rd32(rdram, h + 0xc);
        if (type != 9 && type != 12 && type != 13)
            continue;
        const uint16_t ix = rd16(rdram, h + 0x10);
        if (ix == 0xffff)
            continue;
        const uint32_t cls = rd32(rdram, recs + ix * 0x24u + 8);
        if (cls != kArrowClass && cls != kSpellClass)
            continue;
        alive.push_back(h);
        float p[3];
        for (int k = 0; k < 3; ++k)
            p[k] = rdf(rdram, h + 0x14 + 4u * static_cast<uint32_t>(k));
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]))
            continue;
        auto it = g_proj.find(h);
        if (it == g_proj.end()) {
            const float dx = p[0] - me[0], dy = p[1] - me[1], dz = p[2] - me[2];
            Proj pr;
            std::memcpy(pr.last, p, sizeof p);
            pr.born = g_projTick;
            pr.damage = cls == kArrowClass ? drakan::net::kArrowDamage : kSpellDamage;
            // The projectile's own "Damage" (item property 0x1a, like a blade's): other arrow types and the spells
            // carry their value there; the table / flat numbers stay the fallback for records without a plausible one.
            const uint32_t own = actorProperty(guest, rdram, h, 0x1a);
            if (own >= 1 && own <= drakan::net::kMaxProjectileDamage)
                pr.damage = static_cast<uint16_t>(own);
            std::fprintf(stderr, "[pvp] projectile %x class %x damage property %u -> claim damage %u\n", h, cls, own,
                         static_cast<unsigned>(pr.damage));
            pr.claimed = std::sqrt(dx * dx + dy * dy + dz * dz) > kProjSpawnRadius; // not ours: never claim
            if (g_probe)
                std::fprintf(stderr, "[pvp] projectile %x class %x at %.0f %.0f %.0f (%s)\n", h, cls,
                             static_cast<double>(p[0]), static_cast<double>(p[1]), static_cast<double>(p[2]),
                             pr.claimed ? "foreign" : "ours");
            it = g_proj.emplace(h, pr).first;
        }
        Proj& pr = it->second;
        if (!pr.claimed && g_projTick - pr.born <= kProjMaxAgeTicks) {
            for (const Target& t : targets) {
                if (!segmentHitsGhost(pr.last, p, t.pos))
                    continue;
                const int player = drakan_netplay::playerOfGhost(t.id);
                pr.claimed = true;
                std::fprintf(stderr, "[pvp] %s %x hit ghost %d (player %d) damage %u -> claim\n",
                             cls == kArrowClass ? "arrow" : "spell", h, t.id, player, static_cast<unsigned>(pr.damage));
                if (player > 0 && g_life.load() == static_cast<int>(Life::Alive))
                    drakan_netplay::sendHitClaim(static_cast<uint8_t>(player), 2, pr.damage);
                break;
            }
        }
        std::memcpy(pr.last, p, sizeof p);
    }
    for (auto it = g_proj.begin(); it != g_proj.end();) { // forget gone projectiles (and reused slots after a while)
        if (std::find(alive.begin(), alive.end(), it->first) == alive.end() || g_projTick - it->second.born > 3000)
            it = g_proj.erase(it);
        else
            ++it;
    }
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
    if (drakan_arena::placeAtSpawn(guest, rdram, g_resp.spawn)) {
        // the arena's spawn point (the server's choice, farthest from the other players): placed and refiled
    } else if (ramPtr(player) && pickSpawn(g_resp.spawn, spawn)) {
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
            } else if (life == Life::Alive) { // a match start / mid-match join: the health, and the arena's spawn point
                drakan_arena::placeAtSpawn(guest, rdram, e.b);
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
        g_proj.clear();
        g_savedHandler.store(0);
        g_deadActor = 0;
        g_life.store(static_cast<int>(Life::Alive));
        g_pendingHp = -1;
    });
    g_enabled.store(true);
    std::fprintf(stderr, "[pvp] enabled\n");
}

void beforePadMgr(drakan_input::IGuest& guest, uint8_t* rdram) {
    if (!g_enabled.load() || drakan_netplay::coopMatch())
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
            if (g_local.load() && !g_enemies.empty())
                scanProjectiles(guest, rdram, player);
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
    if (drakan_netplay::coopMatch())
        return; // co-op has no PvP rounds: no deaths, spawns or phases for this module
    std::lock_guard<std::mutex> l(g_m);
    g_events.push_back({Event::Hit, attacker, victim, weapon, damage, healthAfter});
}
void onServerDeath(uint8_t victim, uint8_t killer, uint8_t weapon) {
    if (drakan_netplay::coopMatch())
        return; // co-op has no PvP rounds: no deaths, spawns or phases for this module
    std::lock_guard<std::mutex> l(g_m);
    g_events.push_back({Event::Death, victim, killer, weapon, 0, 0});
}
void onServerRespawn(uint8_t player, uint8_t spawnIndex, uint16_t health) {
    if (drakan_netplay::coopMatch())
        return; // co-op has no PvP rounds: no deaths, spawns or phases for this module
    std::lock_guard<std::mutex> l(g_m);
    g_events.push_back({Event::Respawn, player, spawnIndex, 0, 0, health});
}
void onServerPhase(uint8_t phase) {
    if (drakan_netplay::coopMatch())
        return; // co-op has no PvP rounds: no deaths, spawns or phases for this module
    std::lock_guard<std::mutex> l(g_m);
    g_events.push_back({Event::Phase, phase, 0, 0, 0, 0});
}
void setLocalPlayer(uint8_t id) {
    g_local.store(id);
}

} // namespace drakan_pvp
