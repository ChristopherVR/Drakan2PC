#include "../src/lib/drakan_dragon_flight_policy.h"
#include <cassert>
#include <iostream>

using drakan_dragon_flight::dropLookAheadHit;
using drakan_dragon_flight::insideBox;

int main() {
    // The PC "Bounding Box" slab of the Islands arrival tunnel as the engine holds it (object + 0xb8): turned a quarter
    // about y, 14,336 x 3,072 x 8,192 units.
    const float corner[3] = {337920.0f, 19488.0f, 818048.0f};
    const float axes[9] = {0.0f, 0.0f, 8192.0f, 0.0f, 3072.0f, 0.0f, -14336.0f, 0.0f, 0.0f};
    const float arrival[3] = {332800.0f, 21017.0f, 819018.0f}; // Arokh after the Grotto exit
    const float above[3] = {332800.0f, 26624.0f, 819018.0f};   // over the slab
    const float beside[3] = {320000.0f, 21017.0f, 819018.0f};  // west of it
    assert(insideBox(corner, axes, arrival));
    assert(!insideBox(corner, axes, above));
    assert(!insideBox(corner, axes, beside));
    // A box with a zero edge holds nothing.
    const float flat[9] = {0.0f, 0.0f, 8192.0f, 0.0f, 0.0f, 0.0f, -14336.0f, 0.0f, 0.0f};
    assert(!insideBox(corner, flat, arrival));

    // Only an object hit that starts inside the object's box is dropped: a miss stays a miss, terrain hits and objects
    // ahead of him still slow him down.
    assert(dropLookAheadHit(true, true, corner, axes, arrival));
    assert(!dropLookAheadHit(true, true, corner, axes, above));
    assert(!dropLookAheadHit(true, false, corner, axes, arrival));
    assert(!dropLookAheadHit(false, true, corner, axes, arrival));
    std::cout << "dragon flight policy: ok\n";
    return 0;
}
