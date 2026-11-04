// Selection and animation mapping for the complete multiplayer catalogue. No guest memory or disc required.
#include "drakan_coop_character.h"
#include "drakan_pvp_targets.h"
#include "drakan_local_character_view.h"
#include "drakan_arena_filter.h"

#include <cassert>
#include <cmath>
#include <set>
#include <limits>

int main() {
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
    // Original voice resource ids from Rynn's HitRxn arrays; weapon impacts and other SYSTEM voices are unaffected.
    assert(mapHitSound(0, 0x201a1) == 0x201a1);
    assert(mapHitSound(0xffff, 0x201a1) == 0x201a1);
    assert(mapHitSound(0x16bf, 0x201a1) == 0x50804); // shared original male damage voice
    assert(mapHitSound(0x177f, 0x201a1) == 0x50804); // Nichols uses his own original voice
    assert(mapHitSound(0x177f, 0x201a2) == 0x50805);
    assert(mapHitSound(0x177f, 0x2024c) == 0x50804);
    assert(mapHitSound(0x177f, 0x201cc) == 0x201cc); // hard landing, outside the HitRxn set
    assert(mapHitSound(0x177f, 0x501a1) == 0x501a1); // same low id in another sound bank
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
