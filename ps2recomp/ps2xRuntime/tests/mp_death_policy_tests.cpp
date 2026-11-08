// When a multiplayer session turns an HP write into a respawn instead of the engine's game over (drakan_mp_death.h).
#include "../src/lib/drakan_mp_death.h"
#include <cstdio>
#include <cstdlib>

using namespace drakan_mp_death;
static void check(bool b, const char* message) {
    if (!b) {
        std::fprintf(stderr, "FAIL %s\n", message);
        std::exit(1);
    }
}

int main() {
    // A fall, lava or drowning takes the last HP in a session: intercepted.
    check(interceptsDeath(true, true, 0, 12), "lethal write in a session is a respawn");
    check(interceptsDeath(true, true, -40, 12), "overkill (negative HP) is a respawn too");
    // Single-player is untouched: the engine's own death and game over stay.
    check(!interceptsDeath(false, true, 0, 12), "single-player death is the engine's");
    check(!interceptsDeath(false, true, -1, 1), "single-player overkill is the engine's");
    // Non-lethal writes, other stats blocks and an already dead player pass through.
    check(!interceptsDeath(true, true, 1, 12), "HP 1 is alive");
    check(!interceptsDeath(true, true, 50, 12), "a heal passes");
    check(!interceptsDeath(true, false, 0, 12), "not the local player's stats");
    check(!interceptsDeath(true, true, 0, 0), "already at 0: nothing new to intercept");
    check(kSurviveHp > 0, "the engine is given a positive HP");
    // The self respawn waits for the death to be seen first.
    check(!selfRespawnDue(0.0f) && !selfRespawnDue(kSelfRespawnDelaySec - 0.1f), "no instant respawn");
    check(selfRespawnDue(kSelfRespawnDelaySec) && selfRespawnDue(10.0f), "respawn after the delay");
    std::puts("mp_death_policy_test passed");
    return 0;
}
