// world::readPlayerPose / PoseBuffer / PlayerState bridge over SYNTHETIC memory (no game bytes).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "world/Pose.h"

namespace {

int g_failed = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                          \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)
#define NEAR(a, b, e) CHECK(std::fabs(double(a) - double(b)) <= (e))

using namespace world;

constexpr uint32_t kCtx = 0x4de5e0, kTable = 0x1741170, kRecs = 0x881c10, kRes = 0x4db4c0;
constexpr uint32_t kHandles = 0xa5b000, kInst = 0xc30000, kSk = 0xbb7000, kChan = 0xbe4000, kNodeTab = 0xbd5000;

struct Image {
    std::vector<uint8_t> ram = std::vector<uint8_t>(0x2000000, 0);
    void w32(uint32_t a, uint32_t v) { std::memcpy(&ram[a], &v, 4); }
    void w16(uint32_t a, uint16_t v) { std::memcpy(&ram[a], &v, 2); }
    void wf(uint32_t a, float f) { std::memcpy(&ram[a], &f, 4); }
    gameui::FlatGuestMemory mem() const { return gameui::FlatGuestMemory(ram.data(), ram.size()); }
};

uint32_t handleAddr(uint32_t slot) {
    return kHandles + slot * 0x120u;
}

void addRecord(Image& im, uint32_t index, uint32_t id, uint32_t type, uint32_t cls) {
    const uint32_t r = kRecs + index * 0x24;
    im.w32(r, id);
    im.w32(r + 4, type);
    im.w32(r + 8, cls);
}

uint32_t addHandle(Image& im, uint32_t slot, uint32_t type, uint32_t resIndex, float x, float y, float z, float yaw) {
    const Globals g;
    const uint32_t h = handleAddr(slot);
    im.w32(kTable + slot * 8, h);
    im.w32(kTable + slot * 8 + 4, 1);
    im.w32(h, g.handleVtable);
    im.w32(h + 0xc, type);
    im.w16(h + 0x10, static_cast<uint16_t>(resIndex));
    im.w16(h + 0x12, static_cast<uint16_t>(slot + 1));
    im.wf(h + 0x14, x);
    im.wf(h + 0x18, y);
    im.wf(h + 0x1c, z);
    const float c = std::cos(yaw), s = std::sin(yaw);
    const float rot[9] = {c, 0, s, 0, 1, 0, -s, 0, c};
    for (int i = 0; i < 9; ++i)
        im.wf(h + 0x20 + 4 * i, rot[i]);
    return h;
}

// Rynn in slot 1 with an 11-channel skeleton like the real one: node 0 -> channel 0, node 2 -> channel 8; channels
// 0, 3, 4 play `lowerClip`, the others `upperClip`. Sword (resource 0x33cbc) in the right holder.
Image makeRynn(uint32_t lowerClip, float lowerTime, uint32_t upperClip, float upperTime, bool attacking, float yaw) {
    Image im;
    const Globals g;
    im.w32(g.contextPtr, kCtx);
    im.w32(kCtx + ctx::kRegistryTable, kTable);
    im.w32(kCtx + ctx::kRegistryCapacity, 64);
    im.w32(kCtx + ctx::kRegistryHighWater, 4);
    im.w32(kCtx + ctx::kResourceDb, kRes);
    im.w32(kRes + 8, kRecs);
    im.w32(kRes + 0x10, 8);
    addRecord(im, 1, 0x20a50, 3, kClassRynn);
    addRecord(im, 2, 0x33cbc, 10, 0x1000e);
    addHandle(im, 0, 8, 2, 0, 0, 0, 0);
    const uint32_t rynn = addHandle(im, 1, 3, 1, 675499.5f, 159734.0f, 522732.5f, yaw);
    addHandle(im, 2, 10, 2, 0, 0, 0, 0); // the sword, ref 3
    im.w32(kCtx + ctx::kPlayerHandle, rynn);

    im.w32(rynn + 0x50, kSk);
    im.w32(kSk + skel::kChannels, kChan);
    im.w32(kSk + skel::kChannelCount, 11);
    im.w32(kSk + skel::kNodeToChannel, kNodeTab);
    for (uint32_t n = 0; n < 64; ++n)
        im.w16(kNodeTab + n * 2, 0);
    im.w16(kNodeTab + 2 * 2, 8); // node 2 (spine) -> channel 8
    for (uint32_t c = 0; c < 11; ++c) {
        const bool lower = c == 0 || c == 3 || c == 4;
        im.w32(kChan + c * 0x80 + 0x50, lower ? lowerClip : upperClip);
        im.wf(kChan + c * 0x80 + 0x58, lower ? lowerTime : upperTime);
    }
    im.w32(rynn + 0xfc, kInst + 0x100);
    im.w32(kInst + 0x100 + 0x10, kInst);
    im.w32(kInst + playerdata::kState, attacking ? playerdata::kStateAttacking : 2);
    im.w16(kInst + playerdata::kRightHolder + 0x1c, 3);
    im.w16(kInst + playerdata::kLeftHolder + 0x1c, 0xff01); // "none" marker seen in the game
    return im;
}

PlayerPose pose(double t, float x, float yaw, uint32_t clip, float clipTime) {
    PlayerPose p;
    p.timeSec = t;
    p.position = {x, 0, 0};
    setYaw(p, yaw);
    p.lower = p.upper = {clip, clipTime};
    return p;
}

void testRead() {
    const Image im = makeRynn(0x2066d, 1.5f, 0x206a5, 0.25f, true, 1.0f);
    const auto mem = im.mem();
    PoseReadState st;
    PlayerPose p;
    CHECK(readPlayerPose(mem, Globals{}, st, 10.0, 7, p) == PoseStatus::Ok);
    CHECK(p.classId == kClassRynn && !p.mounted && p.attacking && p.tick == 7);
    NEAR(p.position.x, 675499.5, 0.01);
    NEAR(p.yaw, 1.0, 1e-4);
    CHECK(p.lower.clip == 0x2066d && p.upper.clip == 0x206a5);
    NEAR(p.lower.time, 1.5, 1e-6);
    NEAR(p.upper.time, 0.25, 1e-6);
    CHECK(p.channelCount == 11);
    CHECK(p.lowerMask == ((1u << 0) | (1u << 3) | (1u << 4)));
    CHECK(p.weaponRight == 0x33cbc);
    CHECK(p.weaponLeft == 0);
    CHECK(p.velocity.x == 0 && p.velocity.z == 0); // first read

    // second read moves 100 units in 0.5 s along x -> 200 u/s
    Image im2 = im;
    im2.wf(handleAddr(1) + 0x14, 675599.5f);
    const auto mem2 = im2.mem();
    CHECK(readPlayerPose(mem2, Globals{}, st, 10.5, 8, p) == PoseStatus::Ok);
    NEAR(p.velocity.x, 200.0, 0.5);
    NEAR(p.velocity.z, 0.0, 1e-3);

    // same clip on both groups: mask is everything
    const Image same = makeRynn(0x20577, 0.5f, 0x20577, 0.5f, false, 0.0f);
    const auto memS = same.mem();
    PoseReadState st2;
    CHECK(readPlayerPose(memS, Globals{}, st2, 1.0, 1, p) == PoseStatus::Ok);
    CHECK(p.lowerMask == ~0u && !p.attacking);
}

void testReadFailures() {
    Image blank;
    PoseReadState st;
    PlayerPose p;
    CHECK(readPlayerPose(blank.mem(), Globals{}, st, 0, 0, p) == PoseStatus::NoContext);

    Image im = makeRynn(1, 0, 1, 0, false, 0);
    im.w32(kCtx + ctx::kPlayerHandle, 0);
    CHECK(readPlayerPose(im.mem(), Globals{}, st, 0, 0, p) == PoseStatus::NoPlayer);

    Image noSk = makeRynn(1, 0, 1, 0, false, 0);
    noSk.w32(handleAddr(1) + 0x50, 0);
    CHECK(readPlayerPose(noSk.mem(), Globals{}, st, 0, 0, p) == PoseStatus::NoSkeleton);

    Image bad = makeRynn(1, 0, 1, 0, false, 0);
    bad.w32(kSk + skel::kChannelCount, 5000); // torn read: absurd count
    CHECK(readPlayerPose(bad.mem(), Globals{}, st, 0, 0, p) == PoseStatus::NoSkeleton);

    // dragon: class from the record, no weapon read
    Image drg = makeRynn(0x40663, 0.6f, 0x40663, 0.6f, false, 0);
    drg.w32(kRecs + 1 * 0x24 + 8, kClassDragon);
    CHECK(readPlayerPose(drg.mem(), Globals{}, st, 0, 0, p) == PoseStatus::Ok);
    CHECK(p.mounted && p.weaponRight == 0);
}

void testInterpolation() {
    const PlayerPose a = pose(1.0, 0, 0.0f, 0x2047b, 0.50f);
    const PlayerPose b = pose(1.1, 100, 1.0f, 0x2047b, 0.60f);
    PlayerPose r = interpolatePose(a, b, 0.5f);
    NEAR(r.position.x, 50, 1e-3);
    NEAR(r.yaw, 0.5, 1e-3);
    NEAR(r.lower.time, 0.55, 1e-4);
    CHECK(r.lower.clip == 0x2047b);
    NEAR(r.rotation[1], 0, 1e-4); // stays a rotation about Y
    NEAR(r.rotation[4], 1, 1e-4);
    NEAR(r.timeSec, 1.05, 1e-9);

    // shortest arc across the +-pi seam
    r = interpolatePose(pose(0, 0, 3.0f, 1, 0), pose(1, 0, -3.0f, 1, 0), 0.5f);
    CHECK(std::fabs(std::fabs(r.yaw) - 3.14159f) < 0.01f); // goes through pi, not through 0

    // clip change: a's clip keeps playing (time advances) until b is reached, then b snaps in
    const PlayerPose c = pose(1.2, 200, 1.0f, 0x2081b, 0.01f);
    r = interpolatePose(b, c, 0.5f);
    CHECK(r.lower.clip == 0x2047b);
    NEAR(r.lower.time, 0.65, 1e-4);
    r = interpolatePose(b, c, 1.0f);
    CHECK(r.lower.clip == 0x2081b);
    NEAR(r.lower.time, 0.01, 1e-6);

    // a clip loop (time wraps back) advances instead of lerping backwards
    r = interpolatePose(pose(0, 0, 0, 5, 0.95f), pose(0.1, 0, 0, 5, 0.05f), 0.5f);
    NEAR(r.lower.time, 1.0, 1e-4);
}

void testBuffer() {
    PoseBuffer buf;
    CHECK(buf.empty());
    PlayerPose out;
    CHECK(!buf.sample(1.0, out));
    for (int i = 0; i <= 10; ++i) { // 20 Hz, moving 10 u per sample (200 u/s)
        PlayerPose p = pose(i * 0.05, i * 10.0f, 0, 0x2047b, 0.1f * i);
        p.velocity.x = 200;
        buf.push(p);
    }
    CHECK(buf.size() == 11);
    buf.push(pose(0.2, 999, 0, 1, 0)); // out of order: dropped
    CHECK(buf.size() == 11);

    // rendering 100 ms behind: now = 0.4 shows t = 0.3, x = 60
    CHECK(buf.sample(0.4, out));
    NEAR(out.position.x, 60, 1e-3);
    NEAR(out.lower.time, 0.6, 1e-4);
    // between samples
    CHECK(buf.sampleAt(0.325, out));
    NEAR(out.position.x, 65, 1e-3);
    // before the oldest: hold the oldest
    CHECK(buf.sampleAt(-5, out));
    NEAR(out.position.x, 0, 1e-6);
    // past the newest: extrapolates 0.1 s (20 u), clip time advances
    CHECK(buf.sampleAt(0.6, out));
    NEAR(out.position.x, 120, 1e-3);
    NEAR(out.lower.time, 1.1, 1e-4);
    // ... but not beyond the cap (0.25 s -> 50 u)
    CHECK(buf.sampleAt(5.0, out));
    NEAR(out.position.x, 150, 1e-3);

    // teleport: no lerp across a big jump
    buf.push(pose(0.55, 100000, 0, 0x2047b, 0));
    CHECK(buf.sampleAt(0.525, out));
    NEAR(out.position.x, 100, 1e-3);
    CHECK(buf.sampleAt(0.55, out));
    NEAR(out.position.x, 100000, 1e-3);

    PoseBufferConfig c;
    c.maxSamples = 4;
    PoseBuffer small(c);
    for (int i = 0; i < 10; ++i)
        small.push(pose(i, float(i), 0, 1, 0));
    CHECK(small.size() == 4 && small.newest()->timeSec == 9);
}

// Mirrors net::PlayerState's fields (the real type is checked in runtime/net/tests/pose_bridge_test.cpp).
struct MockState {
    uint32_t tick = 0;
    float pos[3] = {0, 0, 0};
    float yaw = 0;
    float vel[3] = {0, 0, 0};
    uint16_t animId = 0;
    float animTime = 0;
    uint8_t weapon = 0;
    bool mounted = false;
};

void testBridge() {
    PlayerPose p = pose(2.0, 123.5f, 0.75f, 0x2047b, 0.4f);
    p.tick = 99;
    p.velocity = {1, 2, 3};
    p.weaponRight = 0x33cbc;
    p.mounted = true;
    MockState s;
    toPlayerState(p, s);
    CHECK(s.tick == 99 && s.pos[0] == 123.5f && s.vel[2] == 3.0f);
    CHECK(s.animId == 0x047b && s.weapon == 0xbc && s.mounted);
    NEAR(s.animTime, 0.4, 1e-6);
    const PlayerPose q = fromPlayerState(s, 5.0);
    NEAR(q.yaw, 0.75, 1e-4);
    CHECK(q.lower.clip == 0x2047b && q.upper.clip == 0x2047b && q.mounted);
    NEAR(q.timeSec, 5.0, 1e-9);
    NEAR(q.position.x, 123.5, 1e-4);
    CHECK(clipFromWireId(0) == 0);
}

} // namespace

int main() {
    testRead();
    testReadFailures();
    testInterpolation();
    testBuffer();
    testBridge();
    if (g_failed)
        std::fprintf(stderr, "world_pose_test: %d failed\n", g_failed);
    else
        std::puts("world_pose_test: ok");
    return g_failed ? 1 : 0;
}
