// Modern bow aiming (src/BowAim.cpp): aim / release state, the Square tap, the FOV zoom and the arrow retarget, on a
// fake guest.
#include "input/DrakanInput.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

using namespace drakan_input;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(c)) {                                                                                                    \
            ++g_fail;                                                                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                                                   \
        }                                                                                                              \
    } while (0)
#define NEAR(a, b, eps)                                                                                                \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        const double a_ = (a), b_ = (b);                                                                               \
        if (std::fabs(a_ - b_) > (eps)) {                                                                              \
            ++g_fail;                                                                                                  \
            std::printf("FAIL %s:%d  %s=%g vs %s=%g\n", __FILE__, __LINE__, #a, a_, #b, b_);                           \
        }                                                                                                              \
    } while (0)

namespace {
struct FakeGuest : IGuest {
    std::map<uint32_t, uint8_t> mem;
    std::vector<GuestCall> calls;
    uint32_t selectedTarget = 0;
    std::map<uint32_t, uint32_t> motions;
    uint8_t read8(uint32_t a) override {
        const auto it = mem.find(a);
        return it == mem.end() ? 0 : it->second;
    }
    uint32_t read32(uint32_t a) override {
        return read8(a) | (read8(a + 1) << 8) | (read8(a + 2) << 16) | (static_cast<uint32_t>(read8(a + 3)) << 24);
    }
    void write8(uint32_t a, uint8_t v) override { mem[a] = v; }
    void write32(uint32_t a, uint32_t v) override {
        for (int i = 0; i < 4; ++i)
            mem[a + i] = static_cast<uint8_t>(v >> (8 * i));
    }
    void write16(uint32_t a, uint16_t v) {
        mem[a] = static_cast<uint8_t>(v);
        mem[a + 1] = static_cast<uint8_t>(v >> 8);
    }
    uint32_t call(const GuestCall& c) override {
        calls.push_back(c);
        if (c.addr == 0x325650u)
            return motions[c.i[1]];
        return c.addr == 0x49ec60 ? selectedTarget : 0;
    }
};

constexpr uint32_t CTX = 0x600000, CAMH = 0x610000, CAM = 0x620000, CAMOBJ = 0x630000, PLH = 0x640000,
                   PLAYER = 0x650000, TABLE = 0x660000, BOW = 0x670000, ARROW = 0x680000;

void setup(FakeGuest& g) {
    g.write32(0x5565d4, CTX);
    g.write32(CTX + 0x4b4, CAMH);
    g.write32(CAMH + 0xfc, CAMH + 0x100);
    g.write32(CAMH + 0x100 + 0x10, CAM);
    g.write32(CTX + 0x4b0, PLH);
    g.write32(PLH + 0xfc, PLH + 0x100);
    g.write32(PLH + 0x100 + 0x10, PLAYER);
    g.write32(CAM + 0x14c, 6);
    g.write32(CAM + 0x148, CAMOBJ);
    g.writeF(CAM + 0x60, 60.0f);
    g.write32(PLAYER + 0xe90, 5); // bow out
    g.write32(0x4fb730, 1);
    // object table: handle 3 = arrow, handle 4 = Rynn
    g.write32(CTX + 0x7c, TABLE);
    g.write32(CTX + 0x84, 10);
    g.write32(TABLE + 2 * 8, ARROW);
    g.write32(TABLE + 3 * 8, PLH); // owner handle resolves an actor, whose +fc/+10 points at PLAYER data
    g.write16(BOW + 0x158, 3);
    g.write16(BOW + 0x60, 4);
    // camera at (0, 300, 0) looking along -x (back axis = column 2 = +x)
    g.writeF(CAMOBJ + 0x14, 0);
    g.writeF(CAMOBJ + 0x18, 300);
    g.writeF(CAMOBJ + 0x1c, 0);
    const float cam[9] = {0, 0, 1, 0, 1, 0, -1, 0, 0}; // columns: (0,0,-1), (0,1,0), (1,0,0)
    for (int i = 0; i < 9; ++i)
        g.writeF(CAMOBJ + 0x20 + 4 * static_cast<uint32_t>(i), cam[i]);
    // arrow in Rynn's hand at (-1000, 150, 40), pointing roughly ahead: column 0 = -dir
    g.writeF(ARROW + 0x14, -1000);
    g.writeF(ARROW + 0x18, 150);
    g.writeF(ARROW + 0x1c, 40);
    const float arr[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < 9; ++i)
        g.writeF(ARROW + 0x20 + 4 * static_cast<uint32_t>(i), arr[i]);
}

PadSample pad(InputModule& m, const FrameInput& f) {
    m.beginFrame(f);
    uint8_t buf[32];
    m.fillPadSample(0, 0, buf);
    return unpackPadSample(buf);
}

FrameInput mouse(uint8_t down, uint8_t pressed, uint8_t released) {
    FrameInput f;
    f.mouseDown = down;
    f.mousePressed = pressed;
    f.mouseReleased = released;
    return f;
}

float col(FakeGuest& g, int j, int i) {
    return g.readF(ARROW + 0x20 + 4 * static_cast<uint32_t>(3 * i + j));
}
} // namespace

static void testAimAndRelease() {
    Config c = Config::defaults();
    CHECK(!c.bowAimModern);
    Config::parse("[mouse]\nbow_aim = modern\n", c, nullptr);
    CHECK(c.bowAimModern);
    NEAR(c.bowZoom, 0.85, 1e-6);
    InputModule m;
    m.initHeadless(c);
    FakeGuest g;
    setup(g);
    m.beginFrame(FrameInput{});
    m.updateBowAim(g);
    // Press: aiming, no Square on the pad, zoom target 60 -> 51.
    PadSample s = pad(m, mouse(MBTN_LEFT, MBTN_LEFT, 0));
    CHECK(m.bowCrosshairVisible());
    CHECK(!(s.buttons & Pad::SQUARE));
    m.updateBowAim(g);
    NEAR(g.readF(CAM + 0x60), 51.0, 1e-4);
    s = pad(m, mouse(MBTN_LEFT, 0, 0));
    CHECK(!(s.buttons & Pad::SQUARE));
    // Release: one Square tap (two frames), the aim ends and the zoom goes back.
    s = pad(m, mouse(0, 0, MBTN_LEFT));
    CHECK(!m.bowCrosshairVisible());
    m.updateBowAim(g);
    NEAR(g.readF(CAM + 0x60), 60.0, 1e-4);
    s = pad(m, FrameInput{});
    CHECK(s.buttons & Pad::SQUARE);
    s = pad(m, FrameInput{});
    CHECK(s.buttons & Pad::SQUARE);
    s = pad(m, FrameInput{});
    CHECK(!(s.buttons & Pad::SQUARE));
    // A quick click (press and release in one frame) also shoots.
    s = pad(m, mouse(0, MBTN_LEFT, MBTN_LEFT));
    s = pad(m, FrameInput{});
    CHECK(s.buttons & Pad::SQUARE);
    // Without the bow (another weapon): LMB is the plain attack again.
    g.write32(PLAYER + 0xe90, 1);
    m.updateBowAim(g);
    for (int i = 0; i < 3; ++i)
        pad(m, FrameInput{});
    s = pad(m, mouse(MBTN_LEFT, MBTN_LEFT, 0));
    CHECK(s.buttons & Pad::SQUARE);
    CHECK(!m.bowCrosshairVisible());
    pad(m, mouse(0, 0, MBTN_LEFT));
    // The native sight open (camera bit5): native bow, no aim.
    g.write32(PLAYER + 0xe90, 5);
    g.write32(CAM + 0x48, 0x20);
    m.updateBowAim(g);
    s = pad(m, mouse(MBTN_LEFT, MBTN_LEFT, 0));
    CHECK(s.buttons & Pad::SQUARE);
    pad(m, mouse(0, 0, MBTN_LEFT));
    g.write32(CAM + 0x48, 0);
    // bow_aim = classic: the game's own bow.
    Config::parse("[mouse]\nbow_aim = classic\nbow_zoom = 1\n", c, nullptr);
    CHECK(!c.bowAimModern);
    m.reloadConfig(c);
    pad(m, FrameInput{});
    m.updateBowAim(g);
    s = pad(m, mouse(MBTN_LEFT, MBTN_LEFT, 0));
    CHECK(s.buttons & Pad::SQUARE);
    CHECK(!m.bowCrosshairVisible());
}

static void testShoulderOffset() {
    InputModule m;
    Config c = Config::defaults();
    c.bowAimModern = true;
    m.initHeadless(c);
    FakeGuest g;
    setup(g); // camera right axis (column 0) = (0, 0, -1)
    m.beginFrame(FrameInput{});
    m.updateBowAim(g);
    NEAR(g.readF(CAM + 0x114), 0.0, 1e-6);
    pad(m, mouse(MBTN_LEFT, MBTN_LEFT, 0));
    m.updateBowAim(g);
    NEAR(g.readF(CAM + 0x10c), 0.0, 1e-4);
    NEAR(g.readF(CAM + 0x114), -200.0, 1e-3); // 200 units to the camera's right
    pad(m, mouse(MBTN_LEFT, 0, 0));
    m.updateBowAim(g);
    NEAR(g.readF(CAM + 0x114), -200.0, 1e-3); // not accumulated
    pad(m, mouse(0, 0, MBTN_LEFT));
    m.updateBowAim(g);
    NEAR(g.readF(CAM + 0x10c), 0.0, 1e-6);
    NEAR(g.readF(CAM + 0x114), 0.0, 1e-6);
}

static void testZoomHandsOver() {
    InputModule m;
    Config c = Config::defaults();
    c.bowAimModern = true;
    m.initHeadless(c);
    FakeGuest g;
    setup(g);
    m.beginFrame(FrameInput{});
    m.updateBowAim(g);
    pad(m, mouse(MBTN_LEFT, MBTN_LEFT, 0));
    m.updateBowAim(g);
    NEAR(g.readF(CAM + 0x60), 51.0, 1e-4);
    // The game sets its own target meanwhile (a cut scene, the sight): it is kept on release.
    g.writeF(CAM + 0x60, 45.0f);
    pad(m, mouse(0, 0, MBTN_LEFT));
    m.updateBowAim(g);
    NEAR(g.readF(CAM + 0x60), 45.0, 1e-4);
}

static void testArrowRetarget() {
    InputModule m;
    Config c = Config::defaults();
    c.bowAimModern = true;
    m.initHeadless(c);
    FakeGuest g;
    setup(g);
    m.beginFrame(FrameInput{});
    m.updateBowAim(g);
    // No aimed shot pending: the launch is native.
    m.beforeBowLaunch(g, BOW);
    NEAR(col(g, 0, 0), 1.0, 1e-6);
    pad(m, mouse(MBTN_LEFT, MBTN_LEFT, 0));
    pad(m, mouse(0, 0, MBTN_LEFT));
    // Another actor can launch first while our animation is pending. Leave its arrow alone and retain our shot.
    constexpr uint32_t otherOwner = 0x690000;
    g.write32(TABLE + 4 * 8, otherOwner);
    g.write16(BOW + 0x60, 5);
    m.beforeBowLaunch(g, BOW);
    NEAR(col(g, 0, 0), 1.0, 1e-6);
    NEAR(col(g, 0, 1), 0.0, 1e-6);
    g.write16(BOW + 0x60, 4);
    // A locked target keeps the game's ballistic aim.
    g.write16(PLAYER + 0x4da, 7);
    m.beforeBowLaunch(g, BOW);
    NEAR(col(g, 0, 0), 1.0, 1e-6);
    g.write16(PLAYER + 0x4da, 0);
    m.beforeBowLaunch(g, BOW);
    // Aim point: camera (0,300,0) + 8000 * (-1,0,0); direction from the arrow (-1000,150,40).
    double d[3] = {-8000.0 + 1000.0, 300.0 - 150.0, -40.0};
    const double l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (double& v : d)
        v /= l;
    for (int i = 0; i < 3; ++i)
        NEAR(col(g, 0, i), -d[i], 1e-5); // column 0 = -dir, as the native retarget writes it
    // Orthonormal and right-handed like the arrow's own matrix.
    double dot01 = 0, dot02 = 0, dot12 = 0, n1 = 0, n2 = 0;
    for (int i = 0; i < 3; ++i) {
        dot01 += col(g, 0, i) * col(g, 1, i);
        dot02 += col(g, 0, i) * col(g, 2, i);
        dot12 += col(g, 1, i) * col(g, 2, i);
        n1 += col(g, 1, i) * col(g, 1, i);
        n2 += col(g, 2, i) * col(g, 2, i);
    }
    NEAR(dot01, 0, 1e-5);
    NEAR(dot02, 0, 1e-5);
    NEAR(dot12, 0, 1e-5);
    NEAR(n1, 1, 1e-5);
    NEAR(n2, 1, 1e-5);
    const double det = col(g, 0, 0) * (col(g, 1, 1) * col(g, 2, 2) - col(g, 1, 2) * col(g, 2, 1)) -
                       col(g, 0, 1) * (col(g, 1, 0) * col(g, 2, 2) - col(g, 1, 2) * col(g, 2, 0)) +
                       col(g, 0, 2) * (col(g, 1, 0) * col(g, 2, 1) - col(g, 1, 1) * col(g, 2, 0));
    NEAR(det, 1.0, 1e-4);
    // One retarget per shot.
    const float before = col(g, 0, 1);
    g.writeF(ARROW + 0x18, 0);
    m.beforeBowLaunch(g, BOW);
    CHECK(col(g, 0, 1) == before);
}

static void testImportedBowAutoAimMetadata() {
    InputModule m;
    m.initHeadless(Config::defaults());
    FakeGuest g;
    setup(g);
    constexpr uint32_t target = 0x690000, scratch = 0x6a0000, actor = 0x6c0000, registry = 0x6d0000, records = 0x6e0000;
    g.write32(TABLE + 4 * 8, target);
    g.write16(target + 0x12, 5);
    g.selectedTarget = target;
    g.write32(BOW + 0xc, actor);
    g.write16(actor + 0x10, 2);
    g.write32(CTX + 0x320, registry);
    g.write32(registry + 8, records);
    g.write32(registry + 0x10, 2);
    g.write32(records + 2 * 0x24, 0x343e2);
    g.write32(records + 2 * 0x24 + 8, 0x1004f);
    m.beginFrame(FrameInput{});
    const std::string source = "format = 1\n[bow 0x343e2]\ncone_degrees = 35\nmaximum_range = 18000\n";
    CHECK(m.setBowAimFiles({{"source", source}}) == 1);
    CHECK(m.bowAutoTarget(g, BOW, scratch) == 5);
    NEAR(g.calls.back().f[0], 35, 1e-6);
    NEAR(g.calls.back().f[1], 18000, 1e-6);
    CHECK(g.read16(PLAYER + 0x4da) == 0);
    // The metadata follows exact resource identity, not every weapon or NPC using a similar layout.
    g.write32(records + 2 * 0x24, 0x343e3);
    m.bowAutoTarget(g, BOW, scratch);
    NEAR(g.calls.back().f[0], 25, 1e-6);
    NEAR(g.calls.back().f[1], 16000, 1e-6);
    g.write32(records + 2 * 0x24, 0x343e2);
    g.write32(records + 2 * 0x24 + 8, 0x1000e);
    m.bowAutoTarget(g, BOW, scratch);
    NEAR(g.calls.back().f[0], 25, 1e-6);
    g.write32(records + 2 * 0x24 + 8, 0x1004f);
    g.write16(BOW + 0x60, 5);
    const auto count = g.calls.size();
    CHECK(m.bowAutoTarget(g, BOW, scratch) == 0);
    CHECK(g.calls.size() == count);
    g.write16(BOW + 0x60, 4);
    CHECK(m.setBowAimFiles(
              {{"first", source}, {"later", "format = 1\n[bow 0x343e2]\ncone_degrees=40\nmaximum_range=0\n"}}) == 1);
    CHECK(m.bowAutoTarget(g, BOW, scratch) == 0);
    CHECK(g.calls.size() == count); // authored zero disables autoaim
    for (const auto& invalid : {
             "[bow 0x343e2]\ncone_degrees=35\nmaximum_range=18000\n",
             "format=2\n[bow 0x343e2]\ncone_degrees=35\nmaximum_range=18000\n",
             "format=1\n[bow 0x343e2]\ncone_degrees=35\n",
             "format=1\n[bow 0x343e2]\ncone_degrees=nan\nmaximum_range=18000\n",
             "format=1\n[bow 0x343e2]\ncone_degrees=181\nmaximum_range=18000\n",
             "format=1\n[bow 0x343e2]\ncone_degrees=35\nmaximum_range=-1\n",
             "format=1\n[bow 0x343e2]\ncone_degrees=35\ncone_degrees=25\nmaximum_range=18000\n",
             "format=1\n[bow 0x1000343e2]\ncone_degrees=35\nmaximum_range=18000\n",
         })
        CHECK(m.setBowAimFiles({{"invalid", invalid}}) == 0);
    CHECK(m.setBowAimFiles({{"oversized", std::string(65537, 'x')}}) == 0);
    CHECK(m.setBowAimFiles({}) == 0);
    m.bowAutoTarget(g, BOW, scratch);
    NEAR(g.calls.back().f[0], 25, 1e-6);
}

static void testOotfAutoTarget() {
    InputModule m;
    m.initHeadless(Config::defaults());
    FakeGuest g;
    setup(g);
    constexpr uint32_t target = 0x690000, scratch = 0x6a0000;
    g.write32(TABLE + 4 * 8, target);
    g.write16(target + 0x12, 5);
    g.selectedTarget = target;
    m.beginFrame(FrameInput{});
    CHECK(m.bowAutoTarget(g, BOW, scratch) == 5);
    CHECK(g.calls.size() == 1);
    CHECK(g.calls[0].addr == 0x49ec60 && g.calls[0].i[0] == PLH);
    CHECK(g.calls[0].i[1] == scratch && g.calls[0].i[2] == scratch + 16);
    NEAR(g.calls[0].f[0], 25.0, 1e-6);
    NEAR(g.calls[0].f[1], 16000.0, 1e-6);
    NEAR(g.readF(scratch + 4), 300.0, 1e-6);
    NEAR(g.readF(scratch + 16), -1.0, 1e-6);
    CHECK(g.read16(PLAYER + 0x4da) == 0); // selection never creates a persistent lock
    g.write32(CAM + 0x48, 0x20);
    CHECK(m.bowAutoTarget(g, BOW, scratch) == 0); // sight is precise manual aim
    g.write32(CAM + 0x48, 0);
    g.write16(PLAYER + 0x4da, 8);
    CHECK(m.bowAutoTarget(g, BOW, scratch) == 0); // explicit target has priority
    g.write16(PLAYER + 0x4da, 0);
    g.write16(BOW + 0x60, 5);
    CHECK(m.bowAutoTarget(g, BOW, scratch) == 0); // another actor's bow
    g.write16(BOW + 0x60, 4);
    g.write16(target + 0x12, 6);
    CHECK(m.bowAutoTarget(g, BOW, scratch) == 0); // stale/unregistered actor
    Config c = Config::defaults();
    c.bowAimModern = true;
    m.reloadConfig(c);
    m.beginFrame(FrameInput{});
    g.write16(target + 0x12, 5);
    CHECK(m.bowAutoTarget(g, BOW, scratch) == 0);
}

void testOotfJumpAttackGate() {
    InputModule m;
    m.initHeadless(Config::defaults());
    FakeGuest g;
    setup(g);
    constexpr uint32_t caller = 0x31de5c, native = 0x104b70;
    g.write32(PLAYER + 0x94, 0x10);
    CHECK(m.bowAttackMask(g, PLAYER, caller, native) == (native & ~0x10u));
    CHECK(g.read32(PLAYER + 0x94) == 0x10); // the animation and physics retain the real jump state
    CHECK(m.bowAttackMask(g, PLAYER, caller, native | 0x1000) == ((native | 0x1000) & ~0x10u));
    g.write32(PLAYER + 0x94, 0x30);
    CHECK((m.bowAttackMask(g, PLAYER, caller, native) & g.read32(PLAYER + 0x94)) == 0x20); // falling still refuses
    CHECK(m.bowAttackMask(g, PLAYER, caller + 4, native) == native);
    CHECK(m.bowAttackMask(g, PLAYER + 4, caller, native) == native);
    CHECK(m.bowAttackMask(g, PLAYER, caller, native | 0x800000) == (native | 0x800000));
    g.write32(PLAYER + 0xe90, 1);
    CHECK(m.bowAttackMask(g, PLAYER, caller, native) == native); // melee unchanged
    g.write32(PLAYER + 0xe90, 5);
    g.write32(PLAYER + 0x94, 0);
    CHECK(m.bowAttackMask(g, PLAYER, caller, native) == native); // grounded behavior unchanged
    g.write32(PLAYER + 0x94, 0x10);
    g.write32(0x4fbafc, 1);
    CHECK(m.bowAttackMask(g, PLAYER, caller, native) == native); // scripted sequence
    g.write32(0x4fbafc, 0);
    g.write32(0x4fb730, 0);
    CHECK(m.bowAttackMask(g, PLAYER, caller, native) == native); // outside gameplay
    g.write32(0x4fb730, 1);
    auto c = Config::defaults();
    c.bowAimModern = true;
    m.reloadConfig(c);
    m.beginFrame(FrameInput{});
    CHECK(m.bowAttackMask(g, PLAYER, caller, native) == native);
}

void testOotfSightJumpCommand() {
    InputModule m;
    m.initHeadless(Config::defaults());
    FakeGuest g;
    setup(g);
    g.write32(CAM + 0x48, 0x20);
    const auto capture = [&] {
        g.write32(PLAYER + 0x50c, 1);
        m.captureBowSightJump(g, PLAYER);
        g.write32(PLAYER + 0x50c, 0); // native sight-only clear
    };
    capture();
    CHECK(!m.restoreBowSightJump(g, PLAYER, 0x311318));
    CHECK(m.restoreBowSightJump(g, PLAYER, 0x311314));
    CHECK(g.read32(PLAYER + 0x50c) == 1);
    CHECK(g.read32(PLAYER + 0x94) == 0); // no forced jump state
    g.write32(PLAYER + 0x50c, 0);
    CHECK(!m.restoreBowSightJump(g, PLAYER, 0x311314)); // consumed once
    CHECK(m.keepBowSightOnJump(g, PLAYER, CAM, 0x2f9610));
    CHECK(!m.keepBowSightOnJump(g, PLAYER, CAM, 0x2f9614));
    CHECK(!m.keepBowSightOnJump(g, PLAYER, CAM + 4, 0x2f9610));
    CHECK(!m.keepBowSightOnJump(g, PLAYER + 4, CAM, 0x2f9610));
    capture();
    g.write32(PLAYER + 0x50c, 2);
    CHECK(!m.restoreBowSightJump(g, PLAYER, 0x311314)); // preserve a newer/crouch command
    CHECK(g.read32(PLAYER + 0x50c) == 2);
    capture();
    g.write32(PLAYER + 0xe90, 1);
    CHECK(!m.restoreBowSightJump(g, PLAYER, 0x311314));
    CHECK(!m.keepBowSightOnJump(g, PLAYER, CAM, 0x2f9610));
    g.write32(PLAYER + 0xe90, 5);
    g.write32(PLAYER + 0x94, 0x10);
    capture();
    CHECK(!m.restoreBowSightJump(g, PLAYER, 0x311314)); // no second jump in air
    g.write32(PLAYER + 0x94, 0);
    capture();
    m.captureBowSightJump(g, PLAYER + 4); // new controller entry invalidates stale request
    CHECK(!m.restoreBowSightJump(g, PLAYER, 0x311314));
    capture();
    g.write32(CAM + 0x48, 0);
    CHECK(!m.restoreBowSightJump(g, PLAYER, 0x311314));
    CHECK(!m.keepBowSightOnJump(g, PLAYER, CAM, 0x2f9610));
    g.write32(CAM + 0x48, 0x20);
    auto c = Config::defaults();
    c.bowAimModern = true;
    m.reloadConfig(c);
    m.beginFrame(FrameInput{});
    capture();
    CHECK(!m.restoreBowSightJump(g, PLAYER, 0x311314));
    CHECK(!m.keepBowSightOnJump(g, PLAYER, CAM, 0x2f9610));
}

void testOotfSightForwardLook() {
    InputModule m;
    m.initHeadless(Config::defaults());
    FakeGuest g;
    setup(g);
    g.write32(CAM + 0x48, 0x20);
    CHECK(m.suppressBowSightForwardLook(g, PLAYER, 0x44e680, 2));
    CHECK(!m.suppressBowSightForwardLook(g, PLAYER, 0x44e680, 1));
    CHECK(!m.suppressBowSightForwardLook(g, PLAYER, 0x44e684, 2));
    CHECK(!m.suppressBowSightForwardLook(g, PLAYER + 4, 0x44e680, 2));
    g.write32(CAM + 0x48, 0);
    CHECK(!m.suppressBowSightForwardLook(g, PLAYER, 0x44e680, 2));
    g.write32(CAM + 0x48, 0x20);
    g.write32(PLAYER + 0xe90, 1);
    CHECK(!m.suppressBowSightForwardLook(g, PLAYER, 0x44e680, 2));
    g.write32(PLAYER + 0xe90, 5);
    FrameInput f;
    f.focused = f.captured = true;
    f.gamepad.connected = true;
    f.gamepad.ly = 1;
    m.beginFrame(f);
    CHECK(!m.suppressBowSightForwardLook(g, PLAYER, 0x44e680, 2));
    auto c = Config::defaults();
    c.bowAimModern = true;
    m.reloadConfig(c);
    m.beginFrame(FrameInput{});
    CHECK(!m.suppressBowSightForwardLook(g, PLAYER, 0x44e680, 2));
}

void testOotfSightLocomotion() {
    InputModule m;
    m.initHeadless(Config::defaults());
    FakeGuest g;
    setup(g);
    constexpr uint32_t vt = 0x710000, model = 0x720000, records = 0x730000, channels = 0x740000, modelVt = 0x750000;
    constexpr uint32_t stand = 0x20895, run = 0x20893;
    g.write32(CAM + 0x48, 0x20);
    g.write32(PLAYER + 0x80, PLH);
    g.write32(PLAYER + 0x57c, vt);
    g.write32(vt + 0x144, 0x32cf30);
    g.write32(PLH + 0x50, model);
    g.write32(model, modelVt);
    g.write32(modelVt + 0xb4, 0x11a488);
    g.write32(modelVt + 0xbc, 0x11a4e8);
    g.write32(modelVt + 0xc4, 0x11a548);
    g.write32(modelVt + 0xcc, 0x11a5a8);
    g.write32(model + 0xe8, records);
    g.write32(model + 0xdc, channels);
    g.writeF(0x512db4, 1);
    g.write32(records + 0x50, stand);
    g.write32(PLAYER + 0xabc, 0x2047b);
    g.write32(PLAYER + 0xac0, 0x205b8);
    g.motions[0x2047b] = stand;
    g.motions[0x205b8] = run;
    g.write32(PLAYER + 0x504, 0x101);
    CHECK(m.updateBowSightLocomotion(g, PLAYER, 0x311314));
    CHECK(g.calls[g.calls.size() - 3].addr == 0x32cf30);
    CHECK(g.calls[g.calls.size() - 3].i[1] == run);
    CHECK(g.calls.back().addr == 0x11a548 && g.calls.back().i[1] == 1);
    CHECK(g.read32(PLAYER + 0x94) == 0 && g.read32(PLAYER + 0x504) == 0x101);
    CHECK(g.read32(PLAYER + 0xaec) == 0 && g.read32(PLAYER + 0xab0) == 0); // no torso/layer ownership
    g.write32(records + 0x50, run);
    g.write32(model + 8, 0x50);
    CHECK(!m.updateBowSightLocomotion(g, PLAYER, 0x311314)); // keep phase while unchanged
    g.write32(PLAYER + 0x504, 0);
    CHECK(m.updateBowSightLocomotion(g, PLAYER, 0x311314));
    CHECK(g.calls[g.calls.size() - 2].addr == 0x32cf30 && g.calls[g.calls.size() - 2].i[1] == stand);
    CHECK(g.calls.back().addr == 0x11a5a8 && g.calls.back().i[1] == 0);
    g.write32(records + 0x50, stand);
    g.write32(model + 8, 0);
    g.write32(PLAYER + 0x504, 1);
    CHECK(m.updateBowSightLocomotion(g, PLAYER, 0x311314));
    g.write32(records + 0x50, run);
    g.write32(model + 8, 0x50);
    g.write32(CAM + 0x48, 0);
    CHECK(!m.updateBowSightLocomotion(g, PLAYER, 0x311314));
    CHECK(g.calls.back().addr == 0x11a5a8 && g.calls.back().i[1] == 0); // exit stops own run
    g.write32(CAM + 0x48, 0x20);
    g.write32(records + 0x50, stand);
    CHECK(m.updateBowSightLocomotion(g, PLAYER, 0x311314));
    g.write32(records + 0x50, run);
    g.write32(0x4fb730, 0);
    m.updateBowAim(g); // native load invalidates ownership even if the next actor reuses its address
    g.write32(0x4fb730, 1);
    g.write32(CAM + 0x48, 0);
    const auto calls = g.calls.size();
    CHECK(!m.updateBowSightLocomotion(g, PLAYER, 0x311314));
    CHECK(g.calls.size() == calls);
}

void testSightOrientation() {
    InputModule m;
    m.initHeadless(Config::defaults());
    FakeGuest g;
    setup(g);
    g.write32(CAM + 0x48, 0x20);
    g.write32(PLAYER + 0x80, PLH);
    g.writeF(PLH + 0x28, 1);
    g.writeF(PLH + 0x40, 0);
    g.writeF(CAM + 0x84, 0.2f);
    CHECK(!m.stabilizeBowSightOrientation(g, CAM, 0x421368));     // normal follow caller stays native
    CHECK(!m.stabilizeBowSightOrientation(g, CAM + 4, 0x218794)); // only context camera
    CHECK(m.stabilizeBowSightOrientation(g, CAM, 0x218794));
    NEAR(g.readF(CAMOBJ + 0x34), std::sin(-0.3f), 1e-6);
    NEAR(g.readF(CAMOBJ + 0x28), std::cos(-0.3f), 1e-6);
    NEAR(g.readF(CAMOBJ + 0x18), 300, 1e-6); // native origin/bob remains untouched
    NEAR(g.readF(CAMOBJ + 0x38), -1, 1e-6);
    g.writeF(PLH + 0x28, 0);
    g.writeF(PLH + 0x40, -1);
    CHECK(m.stabilizeBowSightOrientation(g, CAM, 0x218794));
    NEAR(g.readF(CAMOBJ + 0x40), -std::cos(-0.3f), 1e-6); // follows actual actor yaw
    g.writeF(PLH + 0x40, 0);
    CHECK(!m.stabilizeBowSightOrientation(g, CAM, 0x218794)); // degenerate heading never writes NaNs
    g.writeF(PLH + 0x40, -1);
    g.write32(CAM + 0x48, 0);
    CHECK(!m.stabilizeBowSightOrientation(g, CAM, 0x218794));
    g.write32(CAM + 0x48, 0x20);
    g.write32(PLAYER + 0xe90, 0);
    CHECK(!m.stabilizeBowSightOrientation(g, CAM, 0x218794));
    g.write32(PLAYER + 0xe90, 5);
    auto c = Config::defaults();
    c.bowAimModern = true;
    m.reloadConfig(c);
    m.beginFrame(FrameInput{});
    CHECK(!m.stabilizeBowSightOrientation(g, CAM, 0x218794));
    c.bowAimModern = false;
    c.gamepadEnabled = true;
    m.reloadConfig(c);
    FrameInput f;
    f.focused = f.captured = true;
    f.gamepad.connected = true;
    f.gamepad.ry = 1;
    m.beginFrame(f);
    CHECK(!m.stabilizeBowSightOrientation(g, CAM, 0x218794)); // controller keeps its native sight orientation
}

void testSightShotBasis() {
    InputModule m;
    m.initHeadless(Config::defaults());
    FakeGuest g;
    setup(g);
    g.write32(CAM + 0x48, 0x20);
    g.write32(CTX + 0x320, 0x780000);
    g.write32(0x780008, 0x781000);
    g.write32(0x780010, 1);
    g.write32(0x781000, 0x3379d);
    g.write32(0x781008, 0x1005f);
    g.write32(ARROW + 0x54, 0x782000);
    g.write32(0x782008, 0x783000);
    g.write32(0x78301c, 0x192e48);
    g.write32(0x782004, 0x784000);
    for (unsigned i = 0; i < 9; ++i) {
        g.writeF(CAMOBJ + 0x20 + 4 * i, i % 4 == 0 ? 1.0f : 0.0f);
        g.writeF(ARROW + 0x20 + 4 * i, i % 4 == 0 ? 2.0f + static_cast<float>(i / 4) : 0.0f);
    }
    CHECK(m.beforeBowSightLaunch(g, BOW));
    NEAR(g.readF(ARROW + 0x38), 2, 1e-6); // launch direction follows camera, retaining native first-axis magnitude
    NEAR(g.readF(ARROW + 0x30), 3, 1e-6);
    NEAR(g.readF(ARROW + 0x28), -4, 1e-6);
    NEAR(g.readF(ARROW + 0x14), -1000, 1e-6); // never relocate the projectile
    g.write32(0x781000, 0x337af);
    CHECK(m.beforeBowSightLaunch(g, BOW)); // Long Arrow, same native class95 non-tracking contract
    g.write32(0x781000, 0x337b0);
    CHECK(m.beforeBowSightLaunch(g, BOW)); // Composite Arrow
    g.write32(0x781000, 0x337ec);
    g.write32(0x784040, 1);
    CHECK(!m.beforeBowSightLaunch(g, BOW)); // Bow of the Order's intentional tracking stays native
    g.write32(0x784040, 0);
    g.write32(0x781008, 0x1003c);
    CHECK(!m.beforeBowSightLaunch(g, BOW)); // engine-class check, not a name/resource heuristic
    g.write32(0x781008, 0x1005f);
    g.write32(0x78301c, 0x192e50);
    CHECK(!m.beforeBowSightLaunch(g, BOW)); // unsupported field reader is never interpreted
    g.write32(0x78301c, 0x192e48);
    g.write32(0x782004, 0x1fffffc);
    CHECK(!m.beforeBowSightLaunch(g, BOW)); // complete field storage must lie inside guest RAM
    g.write32(0x782004, 0x784000);
    g.write32(0x781000, 0x3379d);
    g.write16(BOW + 0x60, 5);
    CHECK(!m.beforeBowSightLaunch(g, BOW)); // NPC/foreign owner
    g.write16(BOW + 0x60, 4);
    g.write16(PLAYER + 0x4da, 1);
    CHECK(!m.beforeBowSightLaunch(g, BOW)); // explicit native target retains its solver
    g.write16(PLAYER + 0x4da, 0);
    g.write32(CAM + 0x48, 0);
    CHECK(!m.beforeBowSightLaunch(g, BOW)); // third person untouched
    g.write32(CAM + 0x48, 0x20);
    auto c = Config::defaults();
    c.bowAimModern = true;
    m.reloadConfig(c);
    m.beginFrame(FrameInput{});
    CHECK(!m.beforeBowSightLaunch(g, BOW));
    c.bowAimModern = false;
    c.gamepadEnabled = true;
    m.reloadConfig(c);
    FrameInput f;
    f.focused = f.captured = true;
    f.gamepad.connected = true;
    f.gamepad.ly = 1;
    m.beginFrame(f);
    CHECK(!m.beforeBowSightLaunch(g, BOW));
    g.write32(PLAYER + 0x50c, 1);
    m.captureBowSightJump(g, PLAYER);
    g.write32(PLAYER + 0x50c, 0);
    CHECK(!m.restoreBowSightJump(g, PLAYER, 0x311314));
    CHECK(!m.keepBowSightOnJump(g, PLAYER, CAM, 0x2f9610)); // controller never receives half the new sight policy
    CHECK(!m.updateBowSightLocomotion(g, PLAYER, 0x311314));
    CHECK(!m.stabilizeBowSightOrientation(g, CAM, 0x218794));
    c.lookMode = LookMode::Stick;
    m.reloadConfig(c);
    m.beginFrame(FrameInput{});
    CHECK(!m.beforeBowSightLaunch(g, BOW));
    CHECK(!m.keepBowSightOnJump(g, PLAYER, CAM, 0x2f9610));
    c.lookMode = LookMode::Direct;
    c.game.push_back(Binding{Binding::Key, 0xcd, Binding::RStick, 3});
    m.reloadConfig(c);
    f = FrameInput{};
    f.focused = f.captured = true;
    f.keyDown.set(0xcd);
    m.beginFrame(f);
    CHECK(!m.beforeBowSightLaunch(g, BOW)); // explicit native-look binding retains the complete native path
    CHECK(!m.keepBowSightOnJump(g, PLAYER, CAM, 0x2f9610));
}

int main() {
    {
        InputModule m;
        m.initHeadless(Config::defaults());
        FakeGuest g;
        setup(g);
        m.beginFrame(FrameInput{});
        m.updateBowAim(g);
        const PadSample press = pad(m, mouse(MBTN_LEFT, MBTN_LEFT, 0));
        CHECK(press.buttons & Pad::SQUARE); // original PC behavior: attack starts on press
        CHECK(!m.bowCrosshairVisible());
        const PadSample release = pad(m, mouse(0, 0, MBTN_LEFT));
        CHECK(!(release.buttons & Pad::SQUARE));
        m.updateBowAim(g);
        NEAR(g.readF(CAM + 0x60), 60.0, 1e-4);
    }
    testAimAndRelease();
    testZoomHandsOver();
    testShoulderOffset();
    testArrowRetarget();
    testOotfAutoTarget();
    testImportedBowAutoAimMetadata();
    testOotfJumpAttackGate();
    testOotfSightJumpCommand();
    testOotfSightForwardLook();
    testOotfSightLocomotion();
    testSightOrientation();
    testSightShotBasis();
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
