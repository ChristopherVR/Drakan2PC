// Level exits of converted Order of the Flame levels: no exit touch until the player has walked off her start spot.
//
// A PC level often has its own exit inside the detector box that fires it (Grotto's way back to Grimstone, Ruined
// Village, the Islands): the arrival marker and the Rynn start stand in that box. The PC engine does not take an exit
// for a player who is already inside the box when the level starts; the PS2 engine sees her "enter" it on the first
// frame (or when a cut scene puts her back on her spot), sends ON to the exit and opens the level-change save prompt at
// once, so the player is offered the level she just left (docs/journal/2026-11-06/ootf-progression.md). The guard
// swallows exit touches until the player has moved `kWalkOff` units (horizontal) away from where she stood when she
// got control: the position is re-taken while she is not in gameplay (loading, cut scenes, menus) and for
// `kSettleTicks` gameplay ticks after that (the respawn and a cut scene's put-back land a few frames late). Touches
// that arrive outside gameplay are let through (a cut-scene level's own timer, the Ruined Village Book, takes its exit
// while the player never gets control), except under the loading screen: an exit touched there is queued but its save
// prompt never opens, and a queued exit ignores every later touch, so the exit would be dead for good.
// Pure logic, no guest memory: drakan_levelchange.cpp feeds it once per engine tick.
#pragma once

namespace drakan_exit_guard {

class ArrivalGuard {
public:
    static constexpr float kWalkOff = 1500.0f;
    static constexpr int kSettleTicks = 60;

    // A level became live; `guarded` = it is a campaign pack level (disc levels place their detectors beside the
    // exits).
    void levelStart(bool guarded, float x, float z) {
        armed_ = guarded;
        gameplay_ = false;
        loading_ = true;
        settle_ = kSettleTicks;
        x_ = x;
        z_ = z;
    }

    // One engine tick with the controlled actor at (x, z); `loading` = the loading screen is up. Returns true on the
    // tick the guard lets go.
    bool tick(bool gameplay, bool loading, float x, float z) {
        gameplay_ = gameplay;
        loading_ = loading;
        if (!armed_)
            return false;
        if (!gameplay || settle_ > 0) {
            if (!gameplay)
                settle_ = kSettleTicks;
            else
                --settle_;
            x_ = x;
            z_ = z;
            return false;
        }
        const float dx = x - x_, dz = z - z_;
        if (dx * dx + dz * dz <= kWalkOff * kWalkOff)
            return false;
        armed_ = false;
        return true;
    }

    // An exit touch (event 0x1001) arrived: true = ignore it.
    bool blocks() const { return armed_ && (gameplay_ || loading_); }

private:
    bool armed_ = false, gameplay_ = false, loading_ = false;
    int settle_ = 0;
    float x_ = 0, z_ = 0;
};

} // namespace drakan_exit_guard
