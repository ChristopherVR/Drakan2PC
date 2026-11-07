#include "../src/lib/drakan_dragon_arrival.h"
#include <cassert>
#include <cmath>
#include <iostream>

using drakan_dragon_arrival::decide;

int main() {
    const float rynn[3] = {735232, 154397, 772096};
    // Surdana: the parked dragon on the stage, far from the exit to Kragmor.
    const float stage[3] = {737879, 202773, 198858};
    {
        const auto p = decide(true, rynn, stage);
        assert(p.moved);
        assert(std::fabs(p.pos[0] - 739232) < 1 && std::fabs(p.pos[1] - 157397) < 1 && p.pos[2] == 772096);
    }
    // A dragon that is already near her (Tundra: 2,500 units) stays where he is.
    const float near[3] = {735232, 154397, 774600};
    assert(!decide(true, rynn, near).moved);
    // Mounted: the dragon is the controlled actor, nothing to move.
    assert(!decide(false, rynn, stage).moved);
    // Not a number: left alone.
    const float bad[3] = {NAN, 0, 0};
    assert(!decide(true, rynn, bad).moved);
    std::cout << "dragon arrival policy: ok\n";
    return 0;
}
