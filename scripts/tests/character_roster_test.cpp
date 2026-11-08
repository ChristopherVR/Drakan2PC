// Selection and animation mapping for the complete multiplayer catalogue. No guest memory or disc required.
#include "drakan_coop_character.h"
#include "drakan_pvp_targets.h"
#include "drakan_local_character_view.h"
#include "drakan_arena_filter.h"
#include "drakan_arena_placement.h"

#include <cassert>
#include <cmath>
#include <set>
#include <limits>
#include <cstring>
#include <map>

namespace {
struct PlacementMemory {
    std::map<uint32_t, uint32_t> words;
    uint32_t read32(uint32_t address) const {
        const auto found = words.find(address);
        return found == words.end() ? 0 : found->second;
    }
    void writeF(uint32_t address, float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, 4);
        words[address] = bits;
    }
    float readF(uint32_t address) const {
        const uint32_t bits = read32(address);
        float value;
        std::memcpy(&value, &bits, 4);
        return value;
    }
};

void testArenaPlacementMotion() {
    constexpr uint32_t actor = 0x110000, skeleton = 0x120000, inner = 0x130000, player = 0x140000;
    PlacementMemory memory;
    memory.words[actor + 0xc] = 3;
    memory.words[actor + 0x50] = skeleton;
    memory.words[skeleton + 0xd4] = actor;
    memory.words[actor + 0xfc] = inner;
    memory.words[inner + 0x10] = player;
    memory.words[player + 0x80] = actor;
    memory.words[player + 0x90] = 0x62000;
    memory.words[player + 0x94] = 0;
    memory.words[0x4fbad4] = 0x12345678; // encoded native health must not be repaired or suppressed
    memory.writeF(player + 8, 20094.0f);
    memory.writeF(player + 0xb9c, 20094.0f);
    memory.writeF(player + 0xb74, 0.28f);    // native airborne time remains owned by the game
    memory.writeF(player + 0xe8, 150008.0f); // unproven fields are deliberately not reset
    const float target[3] = {625645.0f, 129000.0f, 442713.0f};
    for (uint32_t i = 0; i < 3; ++i) {
        memory.writeF(skeleton + 0x174 + i * 4, 150008.0f);
        memory.writeF(skeleton + 0xc8 + i * 4, 20000.0f);
    }
    assert(drakan_arena::detail::resetPlacementMotion(memory, actor, target, true));
    for (uint32_t i = 0; i < 3; ++i) {
        assert(memory.readF(skeleton + 0x174 + i * 4) == target[i]);
        assert(memory.readF(skeleton + 0xc8 + i * 4) == 0.0f);
    }
    assert(memory.readF(player + 8) == 0.0f && memory.readF(player + 0xb9c) == 0.0f);
    assert(memory.readF(player + 0xb74) == 0.28f && memory.readF(player + 0xe8) == 150008.0f);
    assert(memory.read32(player + 0x90) == 0x62000 && memory.read32(player + 0x94) == 0);
    assert(memory.read32(0x4fbad4) == 0x12345678); // normal native landing damage is not bypassed

    // A different disc layout or a non-player actor cannot receive PAL player-field writes.
    memory.writeF(player + 0xb9c, 6000.0f);
    assert(drakan_arena::detail::resetPlacementMotion(memory, actor, target, false));
    assert(memory.readF(player + 0xb9c) == 6000.0f);
    // A mount can share actor type 3 with Rynn; the caller must use resource class, not type.
    memory.words[actor + 0xc] = 3;
    assert(drakan_arena::detail::resetPlacementMotion(memory, actor, target, false));
    assert(memory.readF(player + 0xb9c) == 6000.0f);
    memory.words[player + 0x80] = actor + 16; // stale inner/data ownership cannot touch unrelated player state
    assert(drakan_arena::detail::resetPlacementMotion(memory, actor, target, true));
    assert(memory.readF(player + 0xb9c) == 6000.0f);

    memory.words[skeleton + 0xd4] = actor + 16;
    const auto staleModel = memory.words;
    assert(!drakan_arena::detail::resetPlacementMotion(memory, actor, target, true));
    assert(memory.words == staleModel);
    memory.words[actor + 0x50] = 0x1fffff0;
    const auto before = memory.words;
    assert(!drakan_arena::detail::resetPlacementMotion(memory, actor, target, true));
    assert(memory.words == before);
    assert(!drakan_arena::detail::resetPlacementMotion(memory, 0, target, true));
    const float invalid[3] = {0, std::numeric_limits<float>::quiet_NaN(), 0};
    assert(!drakan_arena::detail::resetPlacementMotion(memory, actor, invalid, true));
    assert(memory.words == before);
}
} // namespace

int main() {
    testArenaPlacementMotion();
    using namespace drakan_coop_character;
    const auto& all = roster();
    assert(all.size() == 15 && all.front().id == 0);
    assert(!known(0xfbc));  // Mage has no combat animation set
    assert(!known(0x1667)); // Empress is not a combat player avatar
    assert(known(0x16bf));  // Monk
    assert(known(0x16c0));  // Monk Boss
    assert(!known(0xc19));  // creatures are outside the initial 15-character roster
    assert(!known(0x7bd));
    assert(!known(0xa60));
    assert(entry(0xee7).humanoid);
    std::set<uint16_t> ids;
    for (const Entry& e : all) {
        assert(ids.insert(e.id).second);
        assert(!e.name.empty() && e.modelId);
        assert(e.humanoid); // all main-player options use the known equippable NPC hand-joint layout
        setSelected(e.id);
        assert(selected() == e.id && known(e.id));
        if (!e.id)
            continue;
        for (int a = 0; a < static_cast<int>(Act::Count); ++a) {
            const Mapped clip = mapClip(e.id, sampleClip(static_cast<Act>(a)), 1000.0f);
            assert((clip.clip & 0xffff0000u) == (e.recordId & 0xffff0000u));
            assert(std::isfinite(clip.time) && clip.time >= 0);
        }
        const Mapped run = mapClip(e.id, sampleClip(Act::Run), 0.1f);
        for (Act a : {Act::Jump, Act::InAir, Act::Roll}) {
            const uint32_t source = sampleClip(a);
            const Mapped first = mapClip(e.id, source, 0.01f);
            const Mapped later = mapClip(e.id, source, 0.8f);
            assert(first.clip != run.clip); // missing jump must not cycle the run animation in midair
            assert(first.clip == later.clip && first.time == later.time);
        }
        const Mapped invalid = mapClip(e.id, sampleClip(Act::Run), std::numeric_limits<float>::quiet_NaN());
        assert(std::isfinite(invalid.time) && invalid.time == 0);
    }
    assert(nodeUsesLower(0xee7, 0));   // root retains locomotion
    assert(!nodeUsesLower(0xee7, 2));  // native torso split owns upper-body attacks
    assert(!nodeUsesLower(0xee7, 30)); // right weapon follows upper-body pose
    assert(nodeUsesLower(0xee7, 44));  // villager lowerbody
    assert(!nodeUsesLower(0xe88, 44)); // warrior accessory is above lowerbody
    assert(nodeUsesLower(0xe88, 45));  // warrior lowerbody
    setRetargetedMotion(true);
    for (const Entry& e : all) {
        if (!e.id)
            continue;
        const Mapped left = mapClip(e.id, 0x205b5, 0.2f);
        const Mapped right = mapClip(e.id, 0x205b7, 0.2f);
        const Mapped later = mapClip(e.id, 0x205b5, 0.6f);
        assert(left.clip >= 0x58000 && left.clip < 0x58e00);
        assert(left.clip != right.clip && left.clip == later.clip);
        assert(left.time == 0.2f && later.time == 0.6f);
        assert(mapClip(e.id, 0x205b5, std::numeric_limits<float>::infinity()).time == 0);
    }
    setRetargetedMotion(false);
    // Original voice resource ids from Rynn's voice fields / SDB; weapon impacts and unrelated sounds are unaffected.
    assert(mapVoiceSound(0, 0x201a1) == 0x201a1); // Rynn keeps her own voice
    assert(mapVoiceSound(0, 0x202ea) == 0x202ea);
    assert(mapVoiceSound(0xffff, 0x201a1) == 0x201a1);
    assert(mapVoiceSound(0x16bf, 0x201a1) == 0x50804); // shared original male damage voice
    assert(mapVoiceSound(0x177f, 0x201a1) == 0x50804); // Nichols uses his own original voice
    assert(mapVoiceSound(0x177f, 0x201a2) == 0x50805);
    assert(mapVoiceSound(0x177f, 0x2024c) == 0x50804);
    assert(mapVoiceSound(0x16bf, 0x2019c) != 0 && (mapVoiceSound(0x16bf, 0x2019c) >> 16) == 5u); // death cry
    assert(mapVoiceSound(0x16bf, 0x20191) == 0);       // "Rynn Attack Long 01": attack grunt, silent
    assert(mapVoiceSound(0x16bf, 0x202ea) == 0);       // "Rynn Jump 1"
    assert(mapVoiceSound(0x16bf, 0x202e7) == 0);       // "Hoist Rynn 3" (climbing)
    assert(mapVoiceSound(0x177f, 0x201cc) == 0);       // "Rynn_HardLanding_1"
    assert(mapVoiceSound(0x16bf, 0x201fd) == 0);       // "I don't have any room"
    assert(mapVoiceSound(0x177f, 0x201d9) == 0x201d9); // footstep, not a voice
    assert(mapVoiceSound(0x177f, 0x301c2) == 0x301c2); // weapon swish, other bank
    assert(mapVoiceSound(0x177f, 0x501a1) == 0x501a1); // same low id in another sound bank
    using drakan_pvp::detail::usePlayerMeleeMask;
    assert(usePlayerMeleeMask(false, 100, 100, 2, 0x1009d));  // local blade versus a registered NPC opponent
    assert(!usePlayerMeleeMask(true, 100, 100, 2, 0x1009d));  // co-op friendly fire remains native
    assert(!usePlayerMeleeMask(false, 200, 100, 2, 0x1009d)); // NPC / remote attacks keep their native mask
    assert(!usePlayerMeleeMask(false, 100, 100, 0, 0x1009d)); // ordinary NPCs and local cosmetics stay excluded
    assert(!usePlayerMeleeMask(false, 100, 100, 2, 0x10009)); // original player class needs no bypass
    // A projectile starts inside its own visual body. That body must not consume it before the remote player.
    using drakan_pvp::detail::firstProjectileTarget;
    using drakan_pvp::detail::ProjectileTarget;
    const float shotFrom[] = {0, 100, 0}, shotTo[] = {900, 100, 0};
    std::vector<ProjectileTarget> targets = {{1, 0, {0, 0, 0}}, {2, 2, {700, 0, 0}}};
    assert(firstProjectileTarget(targets, shotFrom, shotTo)->player == 2);
    targets.pop_back();
    assert(!firstProjectileTarget(targets, shotFrom, shotTo)); // no self / player-zero claim
    targets.push_back({3, 3, {700, 1000, 0}});
    assert(!firstProjectileTarget(targets, shotFrom, shotTo)); // above the actual trajectory
    setSelected(0xffff);
    assert(selected() == 0 && entry(0xffff).id == 0);
    drakan_local_character::View view{100, 200, {300, 400}};
    view.bodySkeleton = 0x130000;
    view.bodyResource = 0x51323;
    // Stand-in actor+0x12 can be zero in normal gameplay; render eligibility deliberately requires no registry ref.
    assert(view.matchesBody(0x51b8b0, 1, 0x130000, 0x51323, 0x51b8b0));
    // Title teardown can retire a pooled body before a new player pose or level-load callback exists. A reused
    // address with a different controller or model must not receive the old player's transform or substitution.
    assert(!view.matchesBody(0x51b8b0, 0x80000001, 0x130000, 0x51323, 0x51b8b0));
    assert(!view.matchesBody(0x51b8b0, 1, 0x140000, 0x51323, 0x51b8b0));
    assert(!view.matchesBody(0x51b8b0, 1, 0x130000, 0x51262, 0x51b8b0));
    assert(!view.matchesBody(0, 1, 0x130000, 0x51323, 0x51b8b0));
    assert(view.nativeObject(100) == 0);
    assert(view.nativeObject(200) == 200);
    assert(view.nativeObject(300) == 0 && view.nativeObject(400) == 0);
    assert(view.nativeObject(500) == 500);
    assert(view.hiddenModel(100) && view.hiddenModel(300) && view.hiddenModel(400));
    assert(!view.hiddenModel(200) && !view.hiddenModel(500));
    assert(view.shadowObject(100) == 200);
    assert(view.shadowObject(200) == 0 && view.shadowObject(300) == 0);
    assert(view.shadowObject(500) == 500);
    view = {}; // save, restore, leave, selection = Rynn: the native models are restored without changing guest flags
    assert(view.nativeObject(100) == 100 && !view.hiddenModel(100));
    assert(view.shadowObject(100) == 100);
    view.controller = 100; // body still loading: never hide the original player
    assert(view.nativeObject(100) == 100 && !view.hiddenModel(100));
    const uint32_t owned[] = {200, 250}; // local and remote NPC-class bodies, not native-player types
    using drakan_arena::detail::protectedPlayerActor;
    assert(protectedPlayerActor(200, 100, 150, false, owned));
    assert(protectedPlayerActor(250, 100, 150, false, owned));
    assert(protectedPlayerActor(100, 100, 150, false, owned));
    assert(protectedPlayerActor(150, 100, 150, false, owned));
    assert(protectedPlayerActor(300, 100, 150, true, owned));
    assert(!protectedPlayerActor(350, 100, 150, false, owned)); // an actual campaign creature remains removable
}
