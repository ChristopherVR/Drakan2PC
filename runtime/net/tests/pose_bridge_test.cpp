// world::PlayerPose <-> drakan::net::PlayerState through the duck-typed bridge in world/Pose.h (no game, no guest
// memory).
#include <cmath>
#include <cstdio>

#include "net/Protocol.h"
#include "world/Pose.h"

static int g_failed = 0;
static void check(bool ok, int line, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, what);
        ++g_failed;
    }
}
#define CHECK(c) check((c), __LINE__, #c)

int main() {
    world::PlayerPose p;
    p.tick = 99;
    p.position = {675499.5f, 159734.0f, 522732.5f};
    p.velocity = {1, 2, 3};
    world::setYaw(p, 0.75f);
    p.lower = p.upper = {0x2047b, 0.4f};
    p.weaponRight = 0x33cbc;
    p.weaponLeft = 0x4d1;
    p.mounted = true;

    drakan::net::PlayerState s;
    world::toPlayerState(p, s);
    drakan::net::ByteWriter w;
    s.write(w);
    CHECK(w.bytes().size() == drakan::net::kPlayerStateWireSize);
    drakan::net::ByteReader r(w.bytes().data(), w.bytes().size());
    drakan::net::PlayerState back;
    CHECK(back.read(r) && r.atEnd());

    const world::PlayerPose q = world::fromPlayerState(back, 5.0);
    CHECK(std::fabs(q.yaw - 0.75f) < 1e-4f);
    CHECK(q.lower.clip == 0x2047b && q.upper.clip == 0x2047b && q.mounted);
    CHECK(q.position.x == 675499.5f && q.velocity.z == 3.0f && q.tick == 99);
    CHECK(back.weaponRight == 0x33cbc && back.weaponLeft == 0x4d1);
    CHECK(q.weaponRight == 0x33cbc && q.weaponLeft == 0x4d1);

    // An attack: the upper body plays its own clip on the channels outside lowerMask.
    p.upper = {0x20512, 0.1f};
    p.lowerMask = (1u << 0) | (1u << 3) | (1u << 4);
    world::toPlayerState(p, s);
    const world::PlayerPose a = world::fromPlayerState(s, 6.0);
    CHECK(a.lower.clip == 0x2047b && a.upper.clip == 0x20512 && std::fabs(a.upper.time - 0.1f) < 1e-6f);
    CHECK(a.lowerMask == p.lowerMask);
    std::puts(g_failed ? "net_pose_bridge_test: FAILED" : "net_pose_bridge_test: ok");
    return g_failed ? 1 : 0;
}
