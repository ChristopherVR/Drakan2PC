// world::readPlayerPose / PoseBuffer / PlayerState bridge over SYNTHETIC memory (no game bytes).
#include <algorithm>
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
    uint16_t upperAnimId = 0;
    float upperAnimTime = 0;
    uint32_t lowerMask = 0;
    uint32_t weaponRight = 0, weaponLeft = 0;
    bool mounted = false;
    float pitch = 0, roll = 0;
    uint16_t clipHi = 0;
};

void testInventoryReplication() {
    InventoryPoseFilter filter;
    PlayerPose p = pose(1, 100, 0.75f, 0x2047b, 0.4f);
    p.handle = 123;
    CHECK(!filter.apply(p, true)); // joining with a preview already open cannot seed a gameplay transform
    CHECK(filter.apply(p, false));
    p.position.x = 900;
    setYaw(p, -2.4f);
    p.velocity = {100, 200, 300};
    p.weaponRight = 42;
    p.tick = 99;
    CHECK(filter.apply(p, true));
    NEAR(p.position.x, 100, 1e-4);
    NEAR(p.yaw, 0.75, 1e-4);
    NEAR(p.rotation[2], std::sin(0.75), 1e-4);
    CHECK(p.velocity.x == 0 && p.velocity.y == 0 && p.tick == 99 && p.weaponRight == 42);
    p.position.x = 900;
    setYaw(p, -2.4f);
    CHECK(filter.apply(p, false)); // flag clears before the native transform restoration
    NEAR(p.position.x, 100, 1e-4);
    NEAR(p.yaw, 0.75, 1e-4);
    p.position.x = 110;
    setYaw(p, 0.8f);
    CHECK(filter.apply(p, false));
    NEAR(p.position.x, 110, 1e-4);
    NEAR(p.yaw, 0.8, 1e-4);
    p.handle = 456;
    CHECK(!filter.apply(p, true)); // never reuse a freed / replaced actor's transform
    p.mounted = true;
    CHECK(filter.apply(p, true)); // dragon gameplay is not an inventory preview
}

void testPlaybackCadence() {
    for (int senderHz : {30, 60, 240}) {
        double finalTicks[3] = {};
        int index = 0;
        for (int receiverHz : {30, 60, 240}) {
            PosePlaybackClock clock;
            double played = 0;
            for (int i = 0; i <= receiverHz * 10; ++i) {
                const double t = double(i) / receiverHz;
                const uint32_t sampleMs = static_cast<uint32_t>(t * 20 + 1e-6) * 50;
                const uint32_t tick = 1000 + sampleMs * senderHz / 1000;
                const double next = clock.advance(tick, sampleMs, t);
                CHECK(next <= tick); // cannot extrapolate a transform ahead of actual received movement
                CHECK(next >= played);
                played = next;
            }
            finalTicks[index++] = played;
            const uint32_t stoppedTick = 1000 + 10 * senderHz;
            for (int i = 1; i <= receiverHz * 3; ++i)
                played = clock.advance(stoppedTick, 10000, 10 + double(i) / receiverHz);
            NEAR(played, stoppedTick, 1e-3); // a stalled sender settles on its real last coordinate
            // A stalled receiver coming back to the same snapshot must not replay the last few steps.
            NEAR(clock.advance(stoppedTick, 13000, 14), stoppedTick, 1e-3);
            const double resumed = clock.advance(stoppedTick + senderHz * 4, 17000, 17);
            CHECK(resumed > stoppedTick && resumed <= stoppedTick + senderHz * 4);
        }
        NEAR(finalTicks[0], finalTicks[1], senderHz * 0.04);
        NEAR(finalTicks[0], finalTicks[2], senderHz * 0.04);
    }
}

void testPlaybackCadenceChangesAndLoss() {
    // A 0.8-second observation gap hid about 2,080 units of ordinary continuous running in a paired trace.
    // On recovery, distinguish it from an equally large relocation from rest and limit single-frame catch-up.
    {
        PosePlaybackClock stalled;
        const double before = stalled.advance(1000, 1000, 1.0);
        const double after = stalled.advance(1048, 1800, 1.8);
        CHECK(after > before && after - before <= 6.01);
        for (float speed : {2600.f, 0.f}) {
            PoseBuffer sparse;
            auto first = pose(1000.0 / 60, 0, 0, 0, 0);
            auto last = pose(1048.0 / 60, 2080, 0, 0, 0);
            first.snapshotSec = 1.0;
            last.snapshotSec = 1.8;
            first.velocity.x = speed;
            sparse.push(first);
            sparse.push(last);
            PlayerPose rendered;
            CHECK(sparse.sampleAt(1024.0 / 60, rendered));
            NEAR(rendered.position.x, speed == 0 ? 0 : 1040, 0.01);
        }
    }
    PosePlaybackClock clock;
    PoseBufferConfig config;
    config.maxExtrapolateSec = 0; // network playback uses sender ticks, while velocity uses wall seconds
    PoseBuffer buffer(config);
    uint32_t tick = 1000, receivedTick = 0, serverMs = 0;
    double played = 0;
    PlayerPose shown;
    for (int frame = 0; frame <= 900; ++frame) {
        const double t = double(frame) / 60;
        // Uncapped gameplay changes to a slower menu cadence, pauses, and resumes fast gameplay.
        if (frame < 300 || (frame >= 600 && frame < 720))
            tick += 4;
        else if (frame < 600 && frame % 2 == 0)
            ++tick;
        // The server still snapshots a stalled sender. Lose a 1.5 s burst while it is moving slowly.
        if (frame % 3 == 0 && !(frame >= 420 && frame < 510)) {
            serverMs = static_cast<uint32_t>(frame * 1000 / 60);
            if (tick != receivedTick) {
                receivedTick = tick;
                auto sample = pose(tick / 60.0, static_cast<float>(tick - 1000), 0, 0, 0);
                sample.velocity.x = 1e6f; // must never invent movement during a packet gap
                buffer.push(sample);
            }
        }
        const double next = clock.advance(receivedTick, serverMs, t);
        CHECK(next >= played && next <= receivedTick);
        CHECK(buffer.sampleAt(next / 60, shown));
        CHECK(shown.position.x >= 0 && shown.position.x <= receivedTick - 1000);
        played = next;
    }
    NEAR(played, receivedTick, 1e-3);
    NEAR(shown.position.x, receivedTick - 1000, 1e-3);
}

// The live pipeline (issue #1, remote players "faster than their speed, then corrected back"): the sender's network
// thread publishes its newest pose every ~50 ms, the server snapshots every ~50 ms on its own unsynchronised loop, both
// with a few ms of polling and network jitter, and an uncapped receiver replays them. The two 20 Hz clocks beat, so the
// newest tick sometimes does not change for 100 ms and then advances two intervals at once.
struct StreamStats {
    double minRatio = 1e9, maxRatio = 0, p10 = 0, p90 = 0;
    int frozenWindows = 0, windows = 0, backward = 0;
    double meanDelaySec = 0, maxStepRatio = 0;
};

// `msTicks`: the sender stamps PlayerState.tick with its pose time in milliseconds (protocol v9) instead of counting
// its frames. Uncapped frame times vary two to three times between frames (harness traces: p10 8 ms, p90 16 ms), so
// frame counts are not a uniform timeline: replaying them at a constant rate turns the sender's frame time changes
// into speed changes of the remote body (up to 1.9 times its real speed in this model, with the old clock too).
StreamStats simulateStream(uint32_t seed, double senderHz, double receiverHz, double jitterMs, bool msTicks,
                           double outageAt = -1) {
    uint32_t rng = seed;
    const auto uni = [&rng](double lo, double hi) {
        rng = rng * 1664525u + 1013904223u;
        return lo + (hi - lo) * ((rng >> 8) / double(1u << 24));
    };
    constexpr double kSpeed = 600; // units per wall second
    constexpr double kEnd = 20;
    struct Sent {
        double at;
        uint32_t tick;
        double x;
    };
    // Sender game frames (uncapped, +-10 % frame time) and its network thread (polls every 4..6 ms, 50 ms interval).
    std::vector<Sent> toServer;
    {
        double t = 0, lastSend = -1;
        uint32_t tick = 1000;
        double frameAt = 0, lastFrame = 0;
        while (t < kEnd) {
            while (frameAt <= t) {
                tick = msTicks ? static_cast<uint32_t>(1000 + frameAt * 1000) : tick + 1;
                lastFrame = frameAt;
                frameAt += (uni(0, 1) < 0.02 ? 3.0 : uni(0.6, 1.5)) / senderHz;
            }
            if (t - lastSend >= 0.050) {
                lastSend = t;
                // The newest pose is the one read at the last frame boundary.
                toServer.push_back({t + 0.020 + uni(-jitterMs, jitterMs) * 1e-3, tick, kSpeed * lastFrame});
            }
            t += uni(0, 1) < 0.03 ? 0.030 : uni(0.004, 0.006); // a busy PC sometimes wakes the thread late
        }
    }
    // Server loop: newest received state, a snapshot every >= 50 ms.
    std::vector<Sent> toClient; // .at = arrival at the client, .x / .tick = state, server ms in snapMs
    std::vector<uint32_t> snapMs;
    {
        double t = 0.0123, lastSnap = -1;
        bool have = false;
        Sent newest{};
        while (t < kEnd) {
            for (const Sent& s : toServer)
                if (s.at <= t && (!have || s.tick > newest.tick)) {
                    newest = s;
                    have = true;
                }
            if (have && t - lastSnap >= 0.050) {
                lastSnap = t;
                if (outageAt >= 0 && t >= outageAt && t < outageAt + 0.6) { // a 600 ms outage of the server's sends
                    t += 0.005;
                    continue;
                }
                toClient.push_back({t + 0.020 + uni(-jitterMs, jitterMs) * 1e-3, newest.tick, newest.x});
                snapMs.push_back(static_cast<uint32_t>(t * 1000));
            }
            t += uni(0, 1) < 0.03 ? 0.030 : uni(0.004, 0.006);
        }
    }
    const double unit = msTicks ? 1000.0 : 60.0;
    PosePlaybackClock clock(unit);
    PoseBufferConfig config;
    config.maxExtrapolateSec = 0;
    PoseBuffer buffer(config);
    uint32_t newestTick = 0, newestMs = 0;
    bool have = false;
    std::vector<std::pair<double, double>> shown; // (wall time, x)
    double delaySum = 0;
    int delayCount = 0;
    StreamStats st;
    double prevX = -1e9, prevT = 0;
    for (double t = 0; t < kEnd; t += uni(0.85, 1.15) / receiverHz) {
        for (size_t i = 0; i < toClient.size(); ++i)
            if (toClient[i].at <= t && snapMs[i] > newestMs) { // the client keeps the newest snapshot's sample
                newestMs = snapMs[i];
                if (!have || toClient[i].tick > newestTick) {
                    newestTick = toClient[i].tick;
                    buffer.push(pose(newestTick / unit, static_cast<float>(toClient[i].x), 0, 0, 0));
                }
                have = true;
            }
        if (!have)
            continue;
        const double play = clock.advance(newestTick, newestMs, t);
        PlayerPose p;
        CHECK(buffer.sampleAt(play / unit, p));
        if (p.position.x < prevX - 1e-3)
            ++st.backward;
        if (prevX > -1e8 && t > 0.5) // no frame steps ahead faster than a catch-up (no teleport, also at the start)
            st.maxStepRatio = std::max(st.maxStepRatio, (p.position.x - prevX) / ((t - prevT) * kSpeed));
        prevX = p.position.x;
        prevT = t;
        shown.emplace_back(t, p.position.x);
        if (t > 4) {
            delaySum += t - p.position.x / kSpeed;
            ++delayCount;
        }
    }
    // Displayed speed over 50 ms windows after a 4 s warm-up, relative to the true speed.
    std::vector<double> ratios;
    size_t j = 0;
    for (size_t i = 0; i < shown.size(); ++i) {
        if (shown[i].first < 4)
            continue;
        while (j < shown.size() && shown[j].first < shown[i].first + 0.050)
            ++j;
        if (j >= shown.size())
            break;
        const double r = (shown[j].second - shown[i].second) / (shown[j].first - shown[i].first) / kSpeed;
        ratios.push_back(r);
        if (r < 0.2)
            ++st.frozenWindows;
        i = j - 1;
    }
    std::sort(ratios.begin(), ratios.end());
    st.windows = static_cast<int>(ratios.size());
    if (!ratios.empty()) {
        st.minRatio = ratios.front();
        st.maxRatio = ratios.back();
        st.p10 = ratios[ratios.size() / 10];
        st.p90 = ratios[ratios.size() * 9 / 10];
    }
    st.meanDelaySec = delayCount ? delaySum / delayCount : 0;
    return st;
}

void testPlaybackAliasedStream() {
    int seed = 1;
    for (double senderHz : {60.0, 112.0, 240.0})
        for (double receiverHz : {60.0, 144.0})
            for (double jitterMs : {2.0, 10.0}) {
                const StreamStats st =
                    simulateStream(static_cast<uint32_t>(seed++), senderHz, receiverHz, jitterMs, true);
                std::printf("aliased stream sender %3.0f Hz receiver %3.0f Hz jitter %2.0f ms: speed p10 %.2f p90 %.2f "
                            "min %.2f max %.2f, frozen %d/%d, backward %d, delay %.0f ms\n",
                            senderHz, receiverHz, jitterMs, st.p10, st.p90, st.minRatio, st.maxRatio, st.frozenWindows,
                            st.windows, st.backward, st.meanDelaySec * 1000);
                CHECK(st.windows > 200);
                CHECK(st.backward == 0);
                CHECK(st.frozenWindows == 0); // the replay never runs dry while the sender keeps moving
                CHECK(st.p10 >= 0.9 && st.p90 <= 1.1);
                CHECK(st.minRatio >= 0.8 && st.maxRatio <= 1.2); // no bursts above the real speed, no stalls
                CHECK(st.maxStepRatio <= 1.3);
                CHECK(st.meanDelaySec >= 0.1 && st.meanDelaySec <= 0.3);
            }
    // A 600 ms outage: the body stops at the last received position, then catches up without a jump.
    const StreamStats st = simulateStream(99, 112, 144, 10, true, 8.0);
    std::printf("aliased stream with a 600 ms outage: max step %.2f x the real speed, backward %d, delay %.0f ms\n",
                st.maxStepRatio, st.backward, st.meanDelaySec * 1000);
    CHECK(st.backward == 0);
    CHECK(st.maxStepRatio <= 1.8);
    CHECK(st.meanDelaySec <= 0.35);
}

void testClipRate() {
    // Millisecond ticks: buffer time is tick / 1000 (wall seconds, clip rate 1); frame ticks stored as tick / 60 from
    // a 120 Hz sender: one buffer second is half a wall second. A wrapping clip advances in clip (wall) seconds.
    PoseBuffer b;
    b.push(pose(1.0, 0, 0, 0x2047b, 0.95f));
    b.push(pose(1.1, 100, 0, 0x2047b, 0.05f)); // wrapped
    PlayerPose p;
    CHECK(b.sampleAt(1.05, p, 0.5));
    NEAR(p.lower.time, 0.975, 1e-4);
    CHECK(b.sampleAt(1.05, p));
    NEAR(p.lower.time, 1.0, 1e-4);
    NEAR(p.position.x, 50, 1e-3);
}

void testBridge() {
    PlayerPose p = pose(2.0, 123.5f, 0.75f, 0x2047b, 0.4f);
    p.tick = 99;
    p.velocity = {1, 2, 3};
    p.weaponRight = 0x33cbc;
    p.mounted = true;
    MockState s;
    toPlayerState(p, s);
    CHECK(s.tick == 99 && s.pos[0] == 123.5f && s.vel[2] == 3.0f);
    CHECK(s.animId == 0x047b && s.weaponRight == 0x33cbc && s.mounted);
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
    testInventoryReplication();
    testPlaybackCadence();
    testPlaybackCadenceChangesAndLoss();
    testPlaybackAliasedStream();
    testClipRate();
    if (g_failed)
        std::fprintf(stderr, "world_pose_test: %d failed\n", g_failed);
    else
        std::puts("world_pose_test: ok");
    return g_failed ? 1 : 0;
}
