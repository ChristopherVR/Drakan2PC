// Level change on a mount into a level without a dragon: Rynn arrives on foot.
//
// The transition 0x362d38 carries Rynn and her dragon into the new level. It notes before the load whether the
// controlled actor is Rynn (the word "rider controlled"; the dragon gets control otherwise), then after the load
// respawns Rynn over the new level's own Rynn and, when the new level gives a dragon (the level's Rynn links one, or
// the level starts on one), the dragon over that. The new level's Rynn and dragon are killed once their carried copies
// stand in. Control goes to the copy whose word is set; the engine has no branch for a mounted arrival in a level
// that gives no dragon. A converted Order of the Flame level can be one (Rift Crystal: the PC level holds a Rynn start
// only, and the Islands rift box fires for Rynn on Arokh too). Then nothing takes control, the controlled-actor slot
// keeps the killed level Rynn, and the next input tick calls through her freed controller (null method at 0x23d924
// in 0x23ce18, docs/journal/2026-11-06/ootf-rift-mount-arrival.md). The PC engine has no dragon to put her on there
// either: she arrives on foot. This policy hands control to Rynn for that case only. Pure logic:
// drakan_levelchange.cpp reads the inputs at the Rynn respawn and writes the result back.
#pragma once

namespace drakan_mount_arrival {

struct Control {
    bool rider = false;  // the transition's "Rynn is controlled" word
    bool dragon = false; // its "the dragon is controlled" word (the inverse, kept by the transition in its own slot)
    bool changed = false;
};

// `riderControlled`: the word as the transition computed it before the load; `dragonFollows`: the new level gives a
// dragon to respawn after Rynn.
inline Control decide(bool riderControlled, bool dragonFollows) {
    Control c;
    c.rider = riderControlled;
    c.dragon = !riderControlled;
    if (!riderControlled && !dragonFollows) {
        c.rider = true;
        c.dragon = false;
        c.changed = true;
    }
    return c;
}

} // namespace drakan_mount_arrival
