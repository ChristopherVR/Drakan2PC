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
//
// One-way exits (a pack manifest's `one_way_exits`, "<level title> @ x, y, z"): an exit a converted level relays
// through its exit scene (the Ruined Village church stair to the Book, formats ootfexitseq.ts). It is a point nothing
// can walk into; only its scene and the pack's re-offer box send it ON, so the arrival guard is not applied to it. When
// its level-change save is written (the player said yes) the exit is closed in that save (instance flag 0x4, what
// message 0x1002 sets), so the level that comes back from the next one never offers it again. A player who backs out of
// the save screen writes no such save and keeps the exit (docs/journal/2026-11-13/ootf-book-skip-and-reload.md).
#pragma once

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace drakan_exit_guard {

struct OneWayExit {
    std::string level; // level title, compared without case and outer blanks
    float x = 0, y = 0, z = 0;
};

inline std::string trimLower(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    std::string out = s.substr(a, b - a);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// "<level title> @ x, y, z" -> true and `out`; anything else -> false.
inline bool parseOneWayExit(const std::string& entry, OneWayExit& out) {
    const size_t at = entry.rfind('@');
    if (at == std::string::npos)
        return false;
    out.level = trimLower(entry.substr(0, at));
    if (out.level.empty())
        return false;
    const std::string nums = entry.substr(at + 1);
    float v[3];
    const char* p = nums.c_str();
    for (int i = 0; i < 3; ++i) {
        char* end = nullptr;
        v[i] = std::strtof(p, &end);
        if (end == p || !std::isfinite(v[i]))
            return false;
        p = end;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (i < 2) {
            if (*p != ',')
                return false;
            ++p;
        }
    }
    while (*p == ' ' || *p == '\t')
        ++p;
    if (*p)
        return false;
    out.x = v[0];
    out.y = v[1];
    out.z = v[2];
    return true;
}

// The exit object of `level` at (x, y, z) is that one-way exit (positions within 64 units: the LVL stores floats).
inline bool isOneWayExit(const OneWayExit& w, const std::string& level, float x, float y, float z) {
    constexpr float kNear = 64.0f;
    return trimLower(level) == w.level && std::fabs(x - w.x) <= kNear && std::fabs(y - w.y) <= kNear &&
           std::fabs(z - w.z) <= kNear;
}

// Exit instance flag that makes it ignore the touch event (set by message 0x1002, the exit's own handler 0x361440).
constexpr uint32_t kExitClosedFlag = 0x4;

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
