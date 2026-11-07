#include "drakan_playtest.h"

#include "drakan_autoaim.h"
#include "drakan_ghost.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "input/GuestInterface.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace drakan_playtest {

namespace {
constexpr const char* kIntroLevel = "Level 0 - Intro";

bool equalNoCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(x) == std::tolower(y); });
}
} // namespace

bool takeTitleNewGame() {
    static std::atomic<bool> taken{false};
    static const bool wanted = [] {
        const char* f = std::getenv("DRAKAN_FAST_START");
        const char* l = std::getenv("DRAKAN_START_LEVEL");
        return f && *f && *f != '0' && l && *l;
    }();
    return wanted && !taken.exchange(true);
}

std::string introToLevel(const std::string& path, const std::string& level) {
    if (level.empty() || equalNoCase(level, kIntroLevel))
        return {};
    const size_t slash = path.find_last_of("/\\");
    const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = file.rfind('.');
    if (!equalNoCase(dot == std::string::npos ? file : file.substr(0, dot), kIntroLevel))
        return {}; // another level, or another file of the intro folder (Out.lvl, the outro)
    std::string out;
    const std::string intro = kIntroLevel;
    for (size_t i = 0; i < path.size();) {
        if (equalNoCase(path.substr(i, intro.size()), intro)) {
            out += level;
            i += intro.size();
        } else
            out += path[i++];
    }
    return out;
}

bool parsePose(const char* text, float pos[3], float& yaw, bool& hasYaw) {
    if (!text)
        return false;
    float v[4] = {};
    int n = 0;
    const char* p = text;
    while (*p && n < 4) {
        char* end = nullptr;
        const float f = std::strtof(p, &end);
        if (end == p)
            break;
        v[n++] = f;
        p = end;
        while (*p == ' ' || *p == ',' || *p == '\t')
            ++p;
    }
    if (n < 3)
        return false;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(v[i]))
            return false;
    std::memcpy(pos, v, 3 * sizeof(float));
    hasYaw = n == 4 && std::isfinite(v[3]);
    yaw = hasYaw ? v[3] : 0.0f;
    return true;
}

namespace {

constexpr uint32_t kContextPtr = 0x5565d4u; // engine object pointer (PAL)
constexpr uint32_t kControlled = 0x4b0u;    // engine + 0x4b0: the controlled actor
constexpr unsigned kWaitTicks = 240;        // boundaries after the player appears before the first move
// DRAKAN_FAST_START (the editor's Play): 60 (about 1 s); a move onto floor that is not resident yet is retried anyway.
unsigned waitTicks() {
    static const unsigned v = [] {
        const char* e = std::getenv("DRAKAN_FAST_START");
        return e && *e && *e != '0' ? 60u : kWaitTicks;
    }();
    return v;
}
constexpr unsigned kRetryTicks = 30, kTries = 40; // the terrain around a far target streams in over a few seconds
constexpr float kTolerance = 3500.0f;
constexpr unsigned kHoldTicks = 240; // keep the player on the spot while the terrain under her streams in
// DRAKAN_PLAYTEST_HOLD=<boundaries> overrides the hold (test aid: a scripted E to call / mount Arokh needs her to stay
// put).
unsigned holdTicks() {
    const char* e = std::getenv("DRAKAN_PLAYTEST_HOLD");
    const long v = e ? std::strtol(e, nullptr, 10) : 0;
    return v > 0 ? static_cast<unsigned>(v) : kHoldTicks;
}

struct State {
    bool init = false, enabled = false, hasYaw = false, done = false;
    float pos[3] = {}, yaw = 0;
    uint32_t player = 0;
    unsigned seen = 0, tries = 0, nextAt = 0, watch = 0, hold = 0;
    bool request = false; // an input-script `place` waits for the next boundary
};
State g;

void writeYaw(drakan_input::IGuest& guest, uint32_t player) {
    if (!g.hasYaw)
        return;
    // A pure rotation about y; yaw = atan2(m[0][2], m[0][0]) (world-model.md section 1).
    const float c = std::cos(g.yaw), s = std::sin(g.yaw);
    const float m[9] = {c, 0, s, 0, 1, 0, -s, 0, c};
    for (int k = 0; k < 9; ++k)
        guest.writeF(player + 0x20 + 4u * static_cast<uint32_t>(k), m[k]);
}

void writePose(drakan_input::IGuest& guest, uint32_t player) {
    for (int k = 0; k < 3; ++k)
        guest.writeF(player + 0x14 + 4u * static_cast<uint32_t>(k), g.pos[k]);
    writeYaw(guest, player);
    drakan_ghost::refileActor(guest, player);
}

} // namespace

// DRAKAN_PLAYTEST_GOD=1: the player's hit points are kept at their maximum (test aid: a harness run through a fight
// needs her to live). Same obfuscation as the world model: word = rotl8(value) ^ key (docs/reference/world-model.md).
static void keepAlive(drakan_input::IGuest& guest) {
    static const bool god = std::getenv("DRAKAN_PLAYTEST_GOD") != nullptr;
    if (!god)
        return;
    const uint32_t key = guest.read32(drakan_region::guestAddress(0x4fe86cu));
    const uint32_t stats = drakan_region::guestAddress(0x4fbac8u);
    const uint32_t raw = guest.read32(stats + 0x10) ^ key;
    const uint32_t max = (raw >> 8) | (raw << 24);
    if (max > 0 && max < 100000u)
        guest.write32(stats + 0xc, ((max << 8) | (max >> 24)) ^ key);
}

// DRAKAN_PLAYTEST_AUTOAIM=<runtime class hex> (test aid, closed loop): every boundary the controlled actor is turned to
// face the nearest living object of that engine class (hit points at data + 0x24c above 0), within
// DRAKAN_PLAYTEST_AUTOAIM_RANGE units (default 20000). Class 0x1001e = Ebon Dragon, 0x1003b = war giant (see
// docs/reference/env-flags.md). DRAKAN_PLAYTEST_ALT=<y>: the controlled actor's height is held at y (Arokh through a
// low tunnel). Both write the actor directly, like `place`, and never move it sideways.
static void autoSteer(drakan_input::IGuest& guest) {
    static const uint32_t cls = [] {
        const char* e = std::getenv("DRAKAN_PLAYTEST_AUTOAIM");
        return e && *e ? static_cast<uint32_t>(std::strtoul(e, nullptr, 16)) : 0u;
    }();
    static const float range = [] {
        const char* e = std::getenv("DRAKAN_PLAYTEST_AUTOAIM_RANGE");
        const float v = e && *e ? std::strtof(e, nullptr) : 0.0f;
        return v > 0.0f ? v : 20000.0f;
    }();
    static const bool hasAlt = std::getenv("DRAKAN_PLAYTEST_ALT") != nullptr;
    static const float alt = hasAlt ? std::strtof(std::getenv("DRAKAN_PLAYTEST_ALT"), nullptr) : 0.0f;
    if (!cls && !hasAlt && !std::getenv("DRAKAN_PLAYTEST_WAYPOINTS"))
        return;
    const uint32_t eng = guest.read32(drakan_region::guestAddress(kContextPtr));
    const uint32_t pl = eng ? guest.read32(eng + kControlled) : 0;
    if (!pl)
        return;
    if (hasAlt && std::isfinite(alt))
        guest.writeF(pl + 0x18, alt);
    // DRAKAN_PLAYTEST_WAYPOINTS="x y z; x y z; ...": an autopilot for flights a time-based script cannot steer. The
    // actor is turned toward the current point (yaw only), its height moves toward the point's at DRAKAN_PLAYTEST_CLIMB
    // units per boundary (default 25), and the next point is taken within DRAKAN_PLAYTEST_WP_RADIUS units (xz, default
    // 2500). The recipe only holds W.
    static const std::vector<drakan_autoaim::Waypoint> wps =
        drakan_autoaim::parseWaypoints(std::getenv("DRAKAN_PLAYTEST_WAYPOINTS"));
    static const float climb = [] {
        const char* e = std::getenv("DRAKAN_PLAYTEST_CLIMB");
        const float v = e && *e ? std::strtof(e, nullptr) : 0.0f;
        return v > 0.0f ? v : 25.0f;
    }();
    static const float wpRadius = [] {
        const char* e = std::getenv("DRAKAN_PLAYTEST_WP_RADIUS");
        const float v = e && *e ? std::strtof(e, nullptr) : 0.0f;
        return v > 0.0f ? v : 2500.0f;
    }();
    static size_t wpIndex = 0;
    // DRAKAN_PLAYTEST_WP_HOLD=<boundaries>: after the last point the actor's x / z are held where it arrived (height
    // free), so a recipe that still holds W stays over the point (the altar) while it descends.
    static const unsigned wpHold = [] {
        const char* e = std::getenv("DRAKAN_PLAYTEST_WP_HOLD");
        const long v = e ? std::strtol(e, nullptr, 10) : 0;
        return v > 0 ? static_cast<unsigned>(v) : 0u;
    }();
    static unsigned holdLeft = 0;
    static float pinX = 0, pinZ = 0;
    if (holdLeft > 0) {
        guest.writeF(pl + 0x14, pinX);
        guest.writeF(pl + 0x1c, pinZ);
        --holdLeft;
    }
    if (!wps.empty() && wpIndex < wps.size()) {
        const auto& w = wps[wpIndex].p;
        const float dx = w[0] - guest.readF(pl + 0x14), dz = w[2] - guest.readF(pl + 0x1c);
        if (dx * dx + dz * dz < wpRadius * wpRadius) {
            std::fprintf(stderr, "[playtest] waypoint %zu reached (%.0f %.0f %.0f)\n", wpIndex,
                         static_cast<double>(w[0]), static_cast<double>(w[1]), static_cast<double>(w[2]));
            ++wpIndex;
            if (wpIndex == wps.size() && wpHold > 0) {
                holdLeft = wpHold;
                pinX = guest.readF(pl + 0x14);
                pinZ = guest.readF(pl + 0x1c);
            }
        } else {
            guest.writeF(pl + 0x18, drakan_autoaim::stepToward(guest.readF(pl + 0x18), w[1], climb));
            float m[9];
            drakan_autoaim::yawMatrix(drakan_autoaim::yawToward(dx, dz), m);
            for (int k = 0; k < 9; ++k)
                guest.writeF(pl + 0x20 + 4u * static_cast<uint32_t>(k), m[k]);
        }
    }
    if (!cls)
        return;
    const uint32_t table = guest.read32(eng + 0x7c), top = guest.read32(eng + 0x84), res = guest.read32(eng + 0x320);
    if (!table || !res || top > 20000)
        return;
    const uint32_t vt = drakan_region::guestAddress(0x51b8b0u);
    const uint32_t classes = guest.read32(res + 8), nClasses = guest.read32(res + 0x10);
    const float px = guest.readF(pl + 0x14), pz = guest.readF(pl + 0x1c);
    float best = range * range, bdx = 0, bdz = 0;
    bool found = false;
    for (uint32_t s = 0; s <= top; ++s) {
        const uint32_t h = guest.read32(table + s * 8);
        if (!h || h == pl || guest.read32(h) != vt)
            continue;
        const uint32_t ix = guest.read32(h + 0x10) & 0xffffu;
        if (ix == 0xffffu || ix > nClasses || guest.read32(classes + ix * 0x24 + 8) != cls)
            continue;
        const uint32_t beh = guest.read32(h + 0xfc);
        const uint32_t q = beh ? guest.read32(beh + 0x10) : 0;
        if (!q || guest.read32(q + 0x23c) != h || static_cast<int32_t>(guest.read32(q + 0x24c)) <= 0)
            continue;
        const float dx = guest.readF(h + 0x14) - px, dz = guest.readF(h + 0x1c) - pz;
        if (dx * dx + dz * dz < best) {
            best = dx * dx + dz * dz;
            bdx = dx;
            bdz = dz;
            found = true;
        }
    }
    if (!found)
        return;
    float m[9];
    drakan_autoaim::yawMatrix(drakan_autoaim::yawToward(bdx, bdz), m);
    for (int k = 0; k < 9; ++k)
        guest.writeF(pl + 0x20 + 4u * static_cast<uint32_t>(k), m[k]);
}

void requestPlace(const float pos[3], float yaw, bool hasYaw) {
    for (int k = 0; k < 3; ++k)
        g.pos[k] = pos[k];
    g.yaw = yaw;
    g.hasYaw = hasYaw;
    g.request = true;
    std::fprintf(stderr, "[playtest] script place: target %.0f %.0f %.0f yaw %s %.3f\n", static_cast<double>(pos[0]),
                 static_cast<double>(pos[1]), static_cast<double>(pos[2]), hasYaw ? "set" : "unchanged",
                 static_cast<double>(yaw));
}

void beforePadMgr(drakan_input::IGuest& guest, uint8_t* /*rdram*/) {
    keepAlive(guest);
    autoSteer(guest);
    if (!g.init) {
        g.init = true;
        // DRAKAN_START_LEVEL with DRAKAN_START_LEVEL_FROM="Level 0 - Intro" (the editor's Play on a disc level): New
        // Game's first level load is pointed at the tested level itself, so the engine knows it by its real name. The
        // file redirect alone (drakan_overrides) loaded it as "Level 0 - Intro": an exit taken there arrived at the
        // wrong spot in the next level or stalled (docs/journal/2026-11-02/editor-play-direct.md).
        const char* to = std::getenv("DRAKAN_START_LEVEL");
        const char* from = std::getenv("DRAKAN_START_LEVEL_FROM");
        if (to && *to && from && equalNoCase(from, kIntroLevel)) {
            const std::string level = to;
            drakan_lifecycle::setLevelPathRewriter([level](const std::string& path, bool fromSave) {
                return fromSave ? std::string() : introToLevel(path, level);
            });
            std::fprintf(stderr, "[playtest] New Game opens '%s' in place of '%s'\n", to, kIntroLevel);
        }
        const char* e = std::getenv("DRAKAN_PLAYTEST_POS");
        g.enabled = e && *e && parsePose(e, g.pos, g.yaw, g.hasYaw);
        if (e && *e)
            std::fprintf(stderr, "[playtest] %s: target %.0f %.0f %.0f yaw %s %.3f\n",
                         g.enabled ? "armed" : "bad value", static_cast<double>(g.pos[0]),
                         static_cast<double>(g.pos[1]), static_cast<double>(g.pos[2]), g.hasYaw ? "set" : "unchanged",
                         static_cast<double>(g.yaw));
    }
    if (g.request && g.init && drakan_ghost::ready()) {
        // A later placement: start over on the current controlled actor (Rynn, or Arokh while she rides him) and move
        // her at once (the level is already up, so the first-placement wait does not apply).
        const uint32_t eng = guest.read32(drakan_region::guestAddress(kContextPtr));
        const uint32_t pl = eng ? guest.read32(eng + kControlled) : 0;
        if (pl) {
            g.request = false;
            g.enabled = true;
            g.done = false;
            g.player = pl;
            g.hold = 0;
            g.tries = 0;
            g.watch = 0;
            g.seen = waitTicks();
        }
    }
    if (!g.enabled || !drakan_ghost::ready())
        return;
    if (g.done) { // after the placement: say where the player is every 2 s for a while (diagnosis aid)
        if (g.watch > 0 && ++g.watch % 120 == 0 && g.watch <= 1800) {
            const uint32_t eng = guest.read32(drakan_region::guestAddress(kContextPtr));
            const uint32_t pl = eng ? guest.read32(eng + kControlled) : 0;
            if (pl)
                std::fprintf(stderr, "[playtest] +%u boundaries: player at %.0f %.0f %.0f yaw %.3f\n", g.watch,
                             static_cast<double>(guest.readF(pl + 0x14)), static_cast<double>(guest.readF(pl + 0x18)),
                             static_cast<double>(guest.readF(pl + 0x1c)),
                             static_cast<double>(std::atan2(guest.readF(pl + 0x28), guest.readF(pl + 0x20))));
        }
        return;
    }
    const uint32_t engine = guest.read32(drakan_region::guestAddress(kContextPtr));
    const uint32_t player = engine ? guest.read32(engine + kControlled) : 0;
    if (!player)
        return;
    if (player != g.player) {         // a new level came up: start counting again
        if (g.hold > 0 && g.player) { // the controlled actor changed while she was held (mounted Arokh): let go
            std::fprintf(stderr, "[playtest] controlled actor changed during the hold (%x -> %x): released\n", g.player,
                         player);
            g.hold = 0;
            g.done = true;
            g.watch = 1;
            g.player = player;
            return;
        }
        g.player = player;
        g.seen = 0;
        g.tries = 0;
        return;
    }
    if (g.hold > 0) { // placed: a player over terrain that is not resident yet falls through it, so hold her
        // DRAKAN_PLAYTEST_SOFT=1: only pull her back when she is more than 400 units off the point, so the engine's own
        // movement of a mount (Rynn climbing onto Arokh) is not overwritten.
        static const bool soft = std::getenv("DRAKAN_PLAYTEST_SOFT") != nullptr;
        if (soft) {
            const float sdx = guest.readF(player + 0x14) - g.pos[0], sdz = guest.readF(player + 0x1c) - g.pos[2];
            if (sdx * sdx + sdz * sdz < 400.0f * 400.0f) {
                if (--g.hold == 0) {
                    std::fprintf(stderr, "[playtest] released\n");
                    g.done = true;
                    g.watch = 1;
                }
                return;
            }
        }
        for (int k = 0; k < 3; ++k)
            guest.writeF(player + 0x14 + 4u * static_cast<uint32_t>(k), g.pos[k]);
        writeYaw(guest, player);
        if (--g.hold % 20 == 0)
            drakan_ghost::refileActor(guest, player);
        if (g.hold == 0) {
            std::fprintf(stderr, "[playtest] released\n");
            g.done = true;
            g.watch = 1;
        }
        return;
    }
    if (++g.seen < waitTicks())
        return;
    if (g.tries == 0 || g.seen >= g.nextAt) {
        if (g.tries > 0) {
            const float dx = guest.readF(player + 0x14) - g.pos[0], dz = guest.readF(player + 0x1c) - g.pos[2];
            const float d = std::sqrt(dx * dx + dz * dz);
            if (g.tries % 5 == 1)
                std::fprintf(stderr, "[playtest] try %u: player %.0f units from the point (%.0f %.0f %.0f)\n", g.tries,
                             static_cast<double>(d), static_cast<double>(guest.readF(player + 0x14)),
                             static_cast<double>(guest.readF(player + 0x18)),
                             static_cast<double>(guest.readF(player + 0x1c)));
            if (d <= kTolerance) {
                std::fprintf(stderr, "[playtest] placed after %u %s (%.0f units from the point)\n", g.tries,
                             g.tries == 1 ? "try" : "tries", static_cast<double>(d));
                g.hold = holdTicks();
                return;
            }
            if (g.tries >= kTries) {
                std::fprintf(stderr, "[playtest] gave up: the player is %.0f units from the point after %u tries\n",
                             static_cast<double>(d), g.tries);
                g.done = true;
                g.watch = 1;
                return;
            }
        }
        writePose(guest, player);
        ++g.tries;
        g.nextAt = g.seen + kRetryTicks;
    }
}

} // namespace drakan_playtest
