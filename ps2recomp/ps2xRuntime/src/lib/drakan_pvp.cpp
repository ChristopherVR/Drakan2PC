// Drakan: PvP combat glue (drakan_pvp.h). PAL addresses are canonical, translated through the disc profile.
// Evidence and the design: docs/journal/2026-10-31/pvp.md, docs/reference/netcode.md "PvP".
#include "drakan_pvp.h"
#include "drakan_pvp_targets.h"
#include "drakan_arena.h"
#include "drakan_ghost.h"
#include "drakan_lifecycle.h"
#include "drakan_mp_death.h"
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
PS2Runtime::RecompiledFunction g_origClassIndex = nullptr;
PS2Runtime::RecompiledFunction g_origBladeSweep = nullptr;
uint64_t g_bladeMaskCalls = 0, g_bladeGhostCandidates = 0;
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
int g_pendingHp = -1;    // engine HP to write once no posted engine call is in flight
bool g_envDeath = false; // the death shown came from the environment: the server does not know, the host respawns
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
        if (i.handle && i.handle == handle && drakan_netplay::playerOfGhost(i.id) > 0) {
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

// The native player blade sweep excludes the whole NPC RFL class. Keep that exclusion for ordinary NPCs, but
// classify a registered opponent's avatar as a player for this one collision-mask lookup. Geometry, actor identity,
// native hit lists and damage messages remain native; no global faction / mask table is changed.
void hookBladeSweepProbe(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t blade = getRegU32(c, 4);
    const bool active =
        g_probe && c->pc == guestAddress(0x4979a8u) && ramPtr(blade) && (rd32(rdram, blade + 0x1cu) & 8u);
    static unsigned logged = 0;
    const bool report = active && logged < 16;
    const uint64_t calls = g_bladeMaskCalls, ghosts = g_bladeGhostCandidates;
    g_origBladeSweep(rdram, c, rt);
    if (report) {
        ++logged;
        std::fprintf(stderr,
                     "[pvp] blade query probe: blade=%x source=%x owner=%x controlled=%x maskcalls=%llu ghosts=%llu\n",
                     blade, rd32(rdram, blade + 0xcu), registryObject(rdram, rd16(rdram, blade + 0x60u)),
                     controlledActor(rdram), static_cast<unsigned long long>(g_bladeMaskCalls - calls),
                     static_cast<unsigned long long>(g_bladeGhostCandidates - ghosts));
    }
}

void hookMeleeCandidateClass(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (g_probe && c->pc == guestAddress(0x1cca68u)) {
        const uint32_t ra = getRegU32(c, 31);
        if (ra >= guestAddress(0x124400u) && ra < guestAddress(0x124900u)) {
            const uint32_t query = getRegU32(c, 20), candidate = getRegU32(c, 18);
            const uint32_t source = ramPtr(query) ? rd32(rdram, query + 0x564u) : 0;
            const uint32_t blade = dataOf(rdram, source);
            const uint32_t owner = ramPtr(blade) ? registryObject(rdram, rd16(rdram, blade + 0x60u)) : 0;
            const uint32_t context = rd32(rdram, guestAddress(kContextPtr));
            const uint32_t bladeQuery = ramPtr(context) ? rd32(rdram, context + 0x348u) : 0;
            int ghost = 0;
            static unsigned logged = 0;
            const bool bladeCall = query == bladeQuery;
            const bool ghostCandidate = bladeCall && isGhost(candidate, &ghost);
            if (bladeCall)
                ++g_bladeMaskCalls;
            if (ghostCandidate)
                ++g_bladeGhostCandidates;
            if (logged < 64 && ghostCandidate && owner && owner == controlledActor(rdram)) {
                ++logged;
                std::fprintf(stderr,
                             "[pvp] mask probe: ra=%x class=%x ghost=%d player=%d query=%x source=%x type=%x "
                             "blade=%x owner=%x controlled=%x\n",
                             ra, getRegU32(c, 5), ghost, drakan_netplay::playerOfGhost(ghost), query, source,
                             ramPtr(source) ? rd32(rdram, source + 0xcu) : 0, blade, owner, controlledActor(rdram));
            }
        }
    }
    const uint32_t returnAddress = getRegU32(c, 31);
    if (c->pc == guestAddress(0x1cca68u) &&
        (returnAddress == guestAddress(0x124498u) || returnAddress == guestAddress(0x1244fcu)) &&
        getRegU32(c, 5) == 0x1009du && !drakan_netplay::coopMatch()) {
        // PAL FUN_1242d8: s2 is the candidate actor, s4 the query object; +564 is the source weapon actor.
        const uint32_t query = getRegU32(c, 20), candidate = getRegU32(c, 18);
        const uint32_t source = ramPtr(query) ? rd32(rdram, query + 0x564u) : 0;
        const uint32_t blade = dataOf(rdram, source);
        const uint32_t owner = ramPtr(blade) ? registryObject(rdram, rd16(rdram, blade + 0x60u)) : 0;
        const uint32_t controlled = controlledActor(rdram);
        int ghost = 0;
        const int remote =
            owner && owner == controlled && isGhost(candidate, &ghost) ? drakan_netplay::playerOfGhost(ghost) : 0;
        if (detail::usePlayerMeleeMask(false, owner, controlled, remote, getRegU32(c, 5))) {
            SET_GPR_U32(c, 5, 0x10009u); // lookup player mask only; the NPC's real class / data stay unchanged
            if (g_probe) {
                static unsigned logged = 0;
                if (logged++ < 32)
                    std::fprintf(stderr, "[pvp] native melee candidate: player %d NPC %x accepted for local blade %x\n",
                                 remote, candidate, blade);
            }
        }
    }
    g_origClassIndex(rdram, c, rt);
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
constexpr uint32_t kArrowClass = 0x1005fu, kSpellClass = 0x10008u, kDragonClass = 0x1001cu;
constexpr uint16_t kSpellDamage = 10;
constexpr float kProjSpawnRadius = 900.0f; // a projectile first seen farther from the player is somebody else's
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
    std::vector<detail::ProjectileTarget> targets;
    drakan_ghost::forEach([&](const drakan_ghost::Info& i) {
        if (i.handle)
            targets.push_back(
                {i.id,
                 drakan_netplay::playerOfGhost(i.id),
                 {rdf(rdram, i.handle + 0x14), rdf(rdram, i.handle + 0x18), rdf(rdram, i.handle + 0x1c)}});
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
            // Projectile class 0x1005f: property 0 is Damage; property 0x1a is Armor Piercing, not damage.
            const uint32_t data = dataOf(rdram, h);
            const uint32_t owner = ramPtr(data) ? registryObject(rdram, rd16(rdram, data + 0x130u)) : 0;
            const bool ours = owner ? owner == player : std::sqrt(dx * dx + dy * dy + dz * dz) <= kProjSpawnRadius;
            const uint32_t own = actorProperty(guest, rdram, h, 0u);
            if (own >= 1u && own <= drakan::net::kMaxProjectileDamage)
                pr.damage = static_cast<uint16_t>(own);
            std::fprintf(stderr, "[pvp] projectile %x class %x damage property %u -> claim damage %u\n", h, cls, own,
                         static_cast<unsigned>(pr.damage));
            pr.claimed = !ours; // never claim another actor's projectile
            if (g_probe)
                std::fprintf(stderr, "[pvp] projectile %x class %x at %.0f %.0f %.0f (%s)\n", h, cls,
                             static_cast<double>(p[0]), static_cast<double>(p[1]), static_cast<double>(p[2]),
                             pr.claimed ? "foreign" : "ours");
            it = g_proj.emplace(h, pr).first;
        }
        Proj& pr = it->second;
        if (!pr.claimed && g_projTick - pr.born <= kProjMaxAgeTicks) {
            if (const auto* target = detail::firstProjectileTarget(targets, pr.last, p)) {
                pr.claimed = true;
                std::fprintf(stderr, "[pvp] %s %x hit ghost %d (player %d) damage %u -> claim\n",
                             cls == kArrowClass ? "arrow" : "spell", h, target->ghost, target->player,
                             static_cast<unsigned>(pr.damage));
                if (g_life.load() == static_cast<int>(Life::Alive))
                    drakan_netplay::sendHitClaim(static_cast<uint8_t>(target->player), 2, pr.damage);
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

// ---------------------------------------------------------------------------------------------- dragon breath

// Class id of an actor (registry record +8), 0 when unknown.
uint32_t classOfActor(const uint8_t* rdram, uint32_t h) {
    const uint32_t ctx = rd32(rdram, guestAddress(kContextPtr));
    const uint32_t res = ramPtr(ctx) ? rd32(rdram, ctx + 0x320) : 0, recs = ramPtr(res) ? rd32(rdram, res + 8) : 0;
    const uint16_t ix = ramPtr(h) ? rd16(rdram, h + 0x10) : 0xffff;
    return ramPtr(recs) && ix != 0xffff ? rd32(rdram, recs + ix * 0x24u + 8) : 0;
}
bool controlledIsDragon(const uint8_t* rdram) {
    return classOfActor(rdram, controlledActor(rdram)) == kDragonClass;
}

// Dragon breath. The engine's own breath (attack command 0x4006 on Arokh: the dragon controller's slot +0x164 only sets
// bit 1 of data+0x90 while the button is held) produced no actor and no visible effect in the converted air arenas
// (docs/journal/2026-11-01/ootf-dragon-pvp.md), so the PvP breath is the host's: while the attack bit is held the
// dragon claims a hit on every ghost inside a cone in front of him every kBreathTicks boundaries, as a ranged claim
// (weapon 2, the server's cap and range rules apply). A remote player is two ghosts (rider and dragon) and gets one
// claim per breath.
constexpr unsigned kBreathTicks = 20;   // boundaries between two breath claims while the attack is held (~0.4 s)
constexpr uint16_t kBreathDamage = 4;   // server points before its damage scale (x3: 12 of the 100 point bar)
constexpr float kBreathRange = 5000.0f; // reach of the breath along the dragon's heading
constexpr float kBreathWidth = 350.0f;  // radius of the cone at the dragon, growing by kBreathSpread per unit
constexpr float kBreathSpread = 0.2f;
constexpr uint32_t kDragonHp = 0x6b4u; // Arokh's health, float, 100.0 when full (coop-dragons-health-mount.md)

struct BreathState {
    bool held = false;
    unsigned lastTick = 0;
} g_breath;

// The breath's targets: ghosts inside the cone. Returns the player ids hit (each once).
std::vector<int> breathTargets(const uint8_t* rdram, uint32_t dragon) {
    float m[12];
    std::memcpy(m, rdram + ((dragon + 0x14) & 0x1ffffffu), sizeof m);
    const float* fwd = m + 9; // row 2 of the 3x3 at +0x20: where the actor looks (testPlaceGhosts' "front")
    std::vector<int> out;
    drakan_ghost::forEach([&](const drakan_ghost::Info& i) {
        if (!i.handle)
            return;
        const float d[3] = {rdf(rdram, i.handle + 0x14) - m[0], rdf(rdram, i.handle + 0x18) - m[1],
                            rdf(rdram, i.handle + 0x1c) - m[2]};
        const float along = d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2];
        if (!(along > 0.0f && along < kBreathRange))
            return;
        const float len2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        const float perp = std::sqrt(std::max(0.0f, len2 - along * along));
        if (perp > kBreathWidth + kBreathSpread * along)
            return;
        const int player = drakan_netplay::playerOfGhost(i.id);
        if (player > 0 && std::find(out.begin(), out.end(), player) == out.end())
            out.push_back(player);
    });
    return out;
}

// The breath's flame: a short-lived flame emitter actor made through the engine's factory at the dragon's mouth every
// kFlameTicks boundaries while the attack is held (drakan_arena::spawnEffect kills it after kFlameLife). The emitter
// resources are the FX database's own particle classes (resource id 0x4xxxx = FX class id; `Part: BaseFireBlend [2]`
// 0x418a5, the orange flame, and `Part: FireWisp [2]` 0x418a4, the sparks; the BaseFire classes carry a camp-fire log
// model and are not used). DRAKAN_BREATH_RES=<id,id,...> overrides them (experiments).
constexpr unsigned kFlameTicks = 6, kFlameLife = 40;
constexpr float kMouthAhead = 700.0f;
// DRAKAN_BREATH_RES=<resource id> picks the emitter; DRAKAN_BREATH_SWEEP=<id,id,...> (experiment) spawns each in turn
// side by side to see which resource is the flame.
std::vector<uint32_t> breathResources() {
    std::vector<uint32_t> out;
    for (const char* name : {"DRAKAN_BREATH_SWEEP", "DRAKAN_BREATH_RES"}) {
        const char* e = std::getenv(name);
        for (; e && *e;) {
            char* end = nullptr;
            const uint32_t v = static_cast<uint32_t>(std::strtoul(e, &end, 0));
            if (end == e)
                break;
            out.push_back(v);
            e = *end == ',' ? end + 1 : end;
        }
        if (!out.empty())
            break;
    }
    if (out.empty())
        out = {0x418a5u, 0x418a4u};
    return out;
}
unsigned g_flameCalls = 0, g_flameTick = 0, g_flameNext = 0;
void spawnFlame(uint8_t* rdram, uint32_t dragon) {
    static const std::vector<uint32_t> res = breathResources();
    ++g_flameCalls;
    if (res.empty() || g_flameCalls - g_flameTick < kFlameTicks)
        return;
    float m[12];
    std::memcpy(m, rdram + ((dragon + 0x14) & 0x1ffffffu), sizeof m);
    const float* fwd = m + 9;
    const float* right = m + 3;
    static const bool once = std::getenv("DRAKAN_BREATH_SWEEP_ONCE") != nullptr; // experiment: one long-lived pass
    if (once && g_flameNext >= res.size())
        return;
    const size_t k = g_flameNext % res.size();
    static const bool sweep = std::getenv("DRAKAN_BREATH_SWEEP") != nullptr;
    const float side = sweep && res.size() > 1 ? (static_cast<float>(k) - 0.5f * static_cast<float>(res.size() - 1)) *
                                                     (once ? 350.0f : 450.0f)
                                               : 0.0f;
    // A trail: successive puffs sit 450 units further along the breath (700, 1150, 1600, 2050).
    const float ahead = once ? 1500.0f : kMouthAhead + 450.0f * static_cast<float>((g_flameNext / res.size()) % 4);
    const float pos[3] = {m[0] + fwd[0] * ahead + right[0] * side, m[1] + fwd[1] * ahead + right[1] * side,
                          m[2] + fwd[2] * ahead + right[2] * side};
    if (drakan_arena::spawnEffect(rdram, res[k], pos, m + 3, once ? 900u : kFlameLife)) {
        g_flameTick = g_flameCalls;
        ++g_flameNext;
        std::fprintf(stderr, "[pvp] breath flame resource %x at %.0f %.0f %.0f\n", res[k], static_cast<double>(pos[0]),
                     static_cast<double>(pos[1]), static_cast<double>(pos[2]));
    }
}

// Called every boundary while the local player is alive and rides a dragon.
void scanBreath(uint8_t* rdram, uint32_t dragon) {
    const uint32_t data = dataOf(rdram, dragon);
    const bool attack = ramPtr(data) && (rd32(rdram, data + 0x90) & 2u) != 0;
    const bool edge = attack && !g_breath.held;
    g_breath.held = attack;
    if (attack)
        spawnFlame(rdram, dragon);
    if (!attack || (!edge && g_projTick - g_breath.lastTick < kBreathTicks))
        return;
    g_breath.lastTick = g_projTick;
    for (const int player : breathTargets(rdram, dragon)) {
        std::fprintf(stderr, "[pvp] breath hit player %d damage %u -> claim\n", player,
                     static_cast<unsigned>(kBreathDamage));
        drakan_netplay::sendHitClaim(static_cast<uint8_t>(player), 2, kBreathDamage);
    }
}

// Arokh's health bar follows the server's: the float at data+0x6b4 (the engine regenerates it between hits).
void setDragonHp(drakan_input::IGuest& guest, const uint8_t* rdram, int serverPoints) {
    const uint32_t data = dataOf(rdram, controlledActor(rdram));
    if (ramPtr(data))
        guest.writeF(data + kDragonHp, static_cast<float>(std::clamp(serverPoints, 1, 100)));
}

// DRAKAN_PVP_DRAGON_PROBE=1: while the controlled actor is a dragon, logs every object that appears in the registry
// (any kind) with its offset from the dragon, to find what an attack spawns.
void probeNewActors(const uint8_t* rdram, uint32_t player) {
    static std::map<uint32_t, uint32_t> seen; // handle -> class
    static bool seeded = false;
    const uint32_t ctx = rd32(rdram, guestAddress(kContextPtr));
    const uint32_t table = rd32(rdram, ctx + 0x7c);
    const uint32_t n = std::min<uint32_t>(rd32(rdram, ctx + 0x84), 20000u);
    if (!ramPtr(table))
        return;
    std::map<uint32_t, uint32_t> now;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t h = rd32(rdram, table + i * 8);
        if (!ramPtr(h))
            continue;
        const bool isActor = rd32(rdram, h) == guestAddress(0x51b8b0u);
        const uint32_t cls = isActor ? classOfActor(rdram, h) : 0xdead0000u + (rd32(rdram, h) & 0xffffu);
        now[h] = cls;
        auto it = seen.find(h);
        if (seeded && (it == seen.end() || it->second != cls))
            std::fprintf(stderr, "[pvp] dragon probe: new object %x type %u class %x\n", h, rd32(rdram, h + 0xc), cls);
    }
    seen.swap(now);
    seeded = true;
    static uint32_t lastFlags = 0xffffffffu;
    const uint32_t fl = rd32(rdram, dataOf(rdram, player) + 0x90);
    if (fl != lastFlags) {
        std::fprintf(stderr, "[pvp] dragon probe: flags90 %x -> %x\n", lastFlags, fl);
        lastFlags = fl;
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

constexpr float kDragonGravity = 2600.0f; // units / s^2
constexpr float kDragonFallMax = 7000.0f; // the longest drop when the arena has no floor hint
constexpr float kDragonRoll = 2.2f;       // rad / s about his heading while he falls
struct DragonFall {
    bool active = false;
    float vy = 0.0f, startY = 0.0f, floorY = -1e30f;
    uint32_t actor = 0;
} g_fall;

void fallDragon(drakan_input::IGuest& guest, const uint8_t* rdram, uint32_t actor, float dt) {
    float m[12];
    std::memcpy(m, rdram + ((actor + 0x14) & 0x1ffffffu), sizeof m);
    if (!g_fall.active || g_fall.actor != actor || g_deathTime <= dt * 1.5f) { // first tick of this death
        g_fall = DragonFall();
        g_fall.active = true;
        g_fall.actor = actor;
        g_fall.startY = m[1];
        float hint = 0;
        g_fall.floorY = g_fall.startY - kDragonFallMax;
        if (drakan_arena::floorHint(hint) && hint < g_fall.startY)
            g_fall.floorY = std::max(g_fall.floorY, hint);
        std::fprintf(stderr, "[pvp] dragon shot down at height %.0f: falls to %.0f\n", static_cast<double>(m[1]),
                     static_cast<double>(g_fall.floorY));
    }
    if (m[1] <= g_fall.floorY)
        return; // landed
    g_fall.vy += kDragonGravity * dt;
    m[1] = std::max(g_fall.floorY, m[1] - g_fall.vy * dt);
    // roll: rows 0 and 1 of the 3x3 (m[3..8]) turn about row 2 (m[9..11], his heading)
    const float c = std::cos(kDragonRoll * dt), s = std::sin(kDragonRoll * dt);
    for (int k = 0; k < 3; ++k) {
        const float r0 = m[3 + k], r1 = m[6 + k];
        m[3 + k] = r0 * c + r1 * s;
        m[6 + k] = -r0 * s + r1 * c;
    }
    for (int k = 0; k < 12; ++k)
        guest.writeF(actor + 0x14 + 4u * static_cast<uint32_t>(k), m[k]);
}

// Writes the death clip into every channel of the player's skeleton, once per boundary (before the engine draws).
void showDeath(drakan_input::IGuest& guest, const uint8_t* rdram, float dt) {
    const uint32_t actor = controlledActor(rdram);
    if (actor != g_deadActor)
        return;
    g_deathTime += dt;
    if (controlledIsDragon(rdram)) {
        // Arokh has no clip of Rynn's: the swallowing handler holds him, so the host drops him: gravity, a roll about
        // his heading, down to the arena's lowest spawn height (the host has no terrain query) or kDragonFallMax,
        // whichever is higher; he stays there until the Respawn.
        fallDragon(guest, rdram, actor, dt);
        return;
    }
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
    g_envDeath = false;
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
    if (controlledIsDragon(rdram))
        setDragonHp(guest, rdram, g_resp.health);
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
                if (controlledIsDragon(rdram))
                    setDragonHp(guest, rdram,
                                e.health); // Rynn's damage method is not Arokh's: his bar follows the server
                else
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
                g_envDeath = false;
                endDeath(rdram);
                if (controlledIsDragon(rdram))
                    setDragonHp(guest, rdram, 100);
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
                if (controlledIsDragon(rdram))
                    setDragonHp(guest, rdram, e.health);
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
    // DRAKAN_PVP_TEST_FRONT=1: the ghosts stay in front of the player (aimed shots: a bow or a spell every few
    // seconds).
    static const bool front = std::getenv("DRAKAN_PVP_TEST_FRONT") != nullptr;
    const unsigned phase = front ? 0u : (tick / 240) % 4;
    const float* axis = phase < 2 ? m + 9 : m + 3;
    const float sign = (phase & 1) ? -1.0f : 1.0f;
    float pos[3] = {m[0] + sign * dist * axis[0], m[1], m[2] + sign * dist * axis[2]};
    // DRAKAN_PVP_TEST_ONE=<ticks>: one opponent at a time (three or more players): the target moves to the next
    // opponent by player id every <ticks> boundaries and its ghost is held there unless it is out of the match (then
    // nobody is, for that turn: the players still in fight on a while after an elimination); the other ghosts are held
    // far above (out of the blade's reach).
    static const unsigned oneEvery = [] {
        const char* e = std::getenv("DRAKAN_PVP_TEST_ONE");
        return e ? static_cast<unsigned>(std::max(1, std::atoi(e))) : 0u;
    }();
    static std::vector<std::pair<int, bool>> opponents; // {player id, eliminated}, refreshed every 30 boundaries
    if (oneEvery && tick % 30 == 0) {
        opponents.clear();
        for (const drakan_netplay::ScoreRow& r : drakan_netplay::scoreboard().rows)
            if (!r.self)
                opponents.emplace_back(r.id, r.eliminated);
        std::sort(opponents.begin(), opponents.end());
    }
    int target = 0;
    if (oneEvery && !opponents.empty()) {
        const auto& t = opponents[(tick / oneEvery) % opponents.size()];
        target = t.second ? -1 : t.first;
    }
    const float away[3] = {m[0], m[1] + 4000.0f, m[2]};
    drakan_ghost::forEach([&](const drakan_ghost::Info& i) {
        if (!i.handle || drakan_netplay::playerOfGhost(i.id) <= 0)
            return; // local cosmetic body must never be pinned or used as a projectile target
        const bool held = !oneEvery || drakan_netplay::playerOfGhost(i.id) == target;
        drakan_ghost::pinTransform(i.id, held ? pos : away, m + 3);
    });
    // DRAKAN_PVP_TEST_CLAIM=<ticks>: a hit claim on every ghost each <ticks> boundaries, as if the blade had landed
    // (the engine's blade sweep only finds ghosts that sit in a collision cell; some maps' floors give none: cell -1).
    static const unsigned claimEvery = [] {
        const char* e = std::getenv("DRAKAN_PVP_TEST_CLAIM");
        return e ? static_cast<unsigned>(std::max(1, std::atoi(e))) : 0u;
    }();
    if (claimEvery && tick % claimEvery == 0 && g_life.load() == static_cast<int>(Life::Alive))
        drakan_ghost::forEach([&](const drakan_ghost::Info& i) {
            if (const int pl = drakan_netplay::playerOfGhost(i.id); i.handle && pl > 0)
                drakan_netplay::sendHitClaim(static_cast<uint8_t>(pl), 0, 4);
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
    drakan_mp_death::install(rt); // environmental deaths (falls, lava, drowning) never reach the engine's game over
    const uint32_t a = guestAddress(kBladeHit);
    if (!a || !guestAddress(kContextPtr) || !guestAddress(kDamageMethod) || !(g_origBladeHit = rt.lookupFunction(a))) {
        std::fprintf(stderr, "[pvp] disabled: no address translation / function for this disc profile\n");
        return;
    }
    g_probe = std::getenv("DRAKAN_PVP_PROBE") != nullptr;
    if (const uint32_t index = guestAddress(0x1cca68u); index && guestAddress(0x1244fcu) && rt.hasFunction(index)) {
        g_origClassIndex = rt.lookupFunction(index);
        rt.replaceFunction(index, &hookMeleeCandidateClass);
        if (g_probe)
            std::fprintf(stderr, "[pvp] native mask probe installed at %x\n", index);
    }
    if (g_probe) {
        const uint32_t sweep = guestAddress(0x4979a8u);
        if (sweep && rt.hasFunction(sweep)) {
            g_origBladeSweep = rt.lookupFunction(sweep);
            rt.replaceFunction(sweep, &hookBladeSweepProbe);
        }
    }
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
        g_envDeath = false;
        drakan_mp_death::takeDeath(); // a death queued before the load belongs to the old world
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
        if (i.handle && drakan_netplay::playerOfGhost(i.id) > 0)
            enemies.push_back(
                {{rdf(rdram, i.handle + 0x14), rdf(rdram, i.handle + 0x18), rdf(rdram, i.handle + 0x1c)}});
    });
    testPlaceGhosts(rdram, player, g_ready);
    static const bool dragonProbe = std::getenv("DRAKAN_PVP_DRAGON_PROBE") != nullptr;
    const bool mounted = controlledIsDragon(rdram);
    if (mounted && dragonProbe)
        probeNewActors(rdram, player);
    if (!enemies.empty())
        g_enemies = std::move(enemies);

    // An environmental death (a fall, lava, drowning): the hook kept the engine's HP at 1; show the death and get her
    // up at an arena spawn after a moment (the server never hears of it, so no Respawn comes).
    if (drakan_mp_death::takeDeath() && g_life.load() == static_cast<int>(Life::Alive) && g_local.load()) {
        std::fprintf(stderr, "[pvp] environmental death: frozen with the death clip, respawn follows\n");
        g_life.store(static_cast<int>(Life::Dying));
        g_lifeTicks = 0;
        g_envDeath = true;
        startDeath(rdram);
    }

    switch (static_cast<Life>(g_life.load())) {
        case Life::Alive:
            recordCrumb(rdram, player);
            if (g_local.load()) {
                if (!g_enemies.empty())
                    scanProjectiles(guest, rdram, player);
                if (mounted)
                    scanBreath(rdram, player);
            }
            break;
        case Life::Dying: {
            ++g_lifeTicks;
            float dt = guest.readF(rd32(rdram, guestAddress(kContextPtr)) + 0x24);
            if (!(dt > 0.0f && dt < 0.25f))
                dt = 1.0f / 60.0f;
            showDeath(guest, rdram, dt);
            if (g_envDeath && drakan_mp_death::selfRespawnDue(g_deathTime)) {
                g_resp.spawn = static_cast<uint8_t>(std::max(0, drakan_arena::pickRespawnSpawn(rdram)));
                g_resp.health = 100;
                reviveLocal(guest, rdram);
            }
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
