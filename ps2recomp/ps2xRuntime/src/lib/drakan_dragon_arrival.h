// Arokh on a level change taken on foot: bring a stray dragon along.
//
// The transition 0x362d38 carries Rynn's linked dragon into the next level and keeps its offset from the exit
// ("Maintain Player Position"). Every level places one Dragon object; Surdana parks its own on the end-of-level stage
// (y 200000, about 570,000 units from the exit to Kragmor). Carried over, he arrives that far from Rynn, outside the
// world, with his take-off state frozen (his tick does not run that far from the player), so Rynn on foot in Kragmor
// has no Arokh to mount and the only way back (a detector that fires for a mounted Rynn) never opens
// (docs/journal/2026-11-07/kragmor-arrival.md). A dragon farther than kStrayDistance from Rynn at the moment she takes
// an exit on foot is put next to her instead; he keeps his state (airborne, taking off) and lands by himself, which is
// what a level that starts with Rynn on foot does for its dragon (Initial Mode 1). Pure logic: drakan_levelchange.cpp
// reads the positions and writes the result.
#pragma once

#include <cmath>

namespace drakan_dragon_arrival {

constexpr float kStrayDistance = 100000.0f; // a dragon this far from Rynn is not where a player left him
constexpr float kBesideDistance = 4000.0f;  // where he is put: beside Rynn
constexpr float kAbove = 3000.0f;           // and above her, so he comes down onto the ground

struct Placement {
    bool moved = false;
    float pos[3] = {0, 0, 0};
};

// `rynn` / `dragon`: world positions (x, y up, z). `onFoot`: Rynn is the controlled actor.
inline Placement decide(bool onFoot, const float* rynn, const float* dragon) {
    Placement p;
    if (!onFoot)
        return p;
    const float dx = dragon[0] - rynn[0], dy = dragon[1] - rynn[1], dz = dragon[2] - rynn[2];
    if (!(std::sqrt(dx * dx + dy * dy + dz * dz) > kStrayDistance)) // also false for NaN
        return p;
    p.moved = true;
    p.pos[0] = rynn[0] + kBesideDistance;
    p.pos[1] = rynn[1] + kAbove;
    p.pos[2] = rynn[2];
    return p;
}

} // namespace drakan_dragon_arrival
