#include "../src/lib/drakan_mount_arrival.h"
#include <cassert>
#include <iostream>

using drakan_mount_arrival::decide;

int main() {
    // On foot: Rynn keeps control, with or without a dragon in the new level.
    for (bool follows : {false, true}) {
        const auto c = decide(true, follows);
        assert(c.rider && !c.dragon && !c.changed);
    }
    // On a mount into a level that gives a dragon (the disc's flown levels, Grotto to Islands): the dragon keeps
    // control.
    {
        const auto c = decide(false, true);
        assert(!c.rider && c.dragon && !c.changed);
    }
    // On a mount into a level without a dragon (Islands to Rift Crystal): Rynn takes control, the dragon word is
    // cleared.
    {
        const auto c = decide(false, false);
        assert(c.rider && !c.dragon && c.changed);
    }
    std::cout << "mount arrival policy: ok\n";
    return 0;
}
