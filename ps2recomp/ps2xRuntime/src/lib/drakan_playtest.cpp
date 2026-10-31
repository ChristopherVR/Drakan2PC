#include "drakan_playtest.h"

#include "drakan_ghost.h"
#include "drakan_region.h"
#include "input/GuestInterface.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace drakan_playtest {

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

constexpr uint32_t kContextPtr = 0x5565d4u;       // engine object pointer (PAL)
constexpr uint32_t kControlled = 0x4b0u;          // engine + 0x4b0: the controlled actor
constexpr unsigned kWaitTicks = 240;              // boundaries after the player appears before the first move
constexpr unsigned kRetryTicks = 30, kTries = 40; // the terrain around a far target streams in over a few seconds
constexpr float kTolerance = 3500.0f;
constexpr unsigned kHoldTicks = 240; // keep the player on the spot while the terrain under her streams in

struct State {
    bool init = false, enabled = false, hasYaw = false, done = false;
    float pos[3] = {}, yaw = 0;
    uint32_t player = 0;
    unsigned seen = 0, tries = 0, nextAt = 0, watch = 0, hold = 0;
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

void beforePadMgr(drakan_input::IGuest& guest, uint8_t* /*rdram*/) {
    if (!g.init) {
        g.init = true;
        const char* e = std::getenv("DRAKAN_PLAYTEST_POS");
        g.enabled = e && *e && parsePose(e, g.pos, g.yaw, g.hasYaw);
        if (e && *e)
            std::fprintf(stderr, "[playtest] %s: target %.0f %.0f %.0f yaw %s %.3f\n",
                         g.enabled ? "armed" : "bad value", static_cast<double>(g.pos[0]),
                         static_cast<double>(g.pos[1]), static_cast<double>(g.pos[2]), g.hasYaw ? "set" : "unchanged",
                         static_cast<double>(g.yaw));
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
    if (player != g.player) { // a new level came up: start counting again
        g.player = player;
        g.seen = 0;
        g.tries = 0;
        return;
    }
    if (g.hold > 0) { // placed: a player over terrain that is not resident yet falls through it, so hold her
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
    if (++g.seen < kWaitTicks)
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
                g.hold = kHoldTicks;
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
