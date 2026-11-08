// drakan_arena_power_policy.h: every arena character starts at the maximum level with every spell unlocked.
#include "../src/lib/drakan_arena_power_policy.h"

#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    using namespace drakan_arena_power;
    // Stat words round trip through the obfuscation, for any key.
    for (uint32_t key : {0u, 0x1234abcdu, 0xffffffffu})
        for (int32_t v : {0, 1, 12, 219623, -1}) {
            assert(decodeStat(encodeStat(v, key), key) == v);
        }
    // rotl8: value 1 with key 0 is stored as 0x100.
    assert(encodeStat(1, 0) == 0x100u);
    // A campaign character (level 1, no experience) needs the boost; a maxed one does not.
    assert(needsLevel(1, 0));
    assert(needsLevel(kMaxLevel, 0));
    assert(needsLevel(1, kMaxXp));
    assert(!needsLevel(kMaxLevel, kMaxXp));
    assert(!needsLevel(kMaxLevel + 1, kMaxXp + 5));
    // Skills rise to their record maximum, clamped to 8 (a zero or absurd maximum means 8).
    assert(skillTarget(8) == 8 && skillTarget(5) == 5 && skillTarget(0) == 8 && skillTarget(200) == 8);
    assert(needsSkill(1, 8));
    assert(!needsSkill(8, 8));
    assert(!needsSkill(5, 5));
    // Spell unlocking: always at least one call; repeat while the sum rises; stop at the cap.
    assert(callShamanAgain(0, 0, 0));
    assert(callShamanAgain(0, 10, 1));
    assert(!callShamanAgain(10, 10, 1));
    assert(!callShamanAgain(10, 20, kMaxShamanCalls));
    std::puts("arena_power_policy_test ok");
    return 0;
}
