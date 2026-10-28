// Modern bow aiming (src/BowAim.cpp): aim / release state, the Square tap, the FOV zoom and the arrow retarget, on a
// fake guest.
#include "input/DrakanInput.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

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
    uint32_t call(const GuestCall&) override { return 0; }
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
    g.write32(TABLE + 3 * 8, PLAYER);
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
    m.initHeadless(Config::defaults());
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
    m.initHeadless(Config::defaults());
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
    m.initHeadless(Config::defaults());
    FakeGuest g;
    setup(g);
    m.beginFrame(FrameInput{});
    m.updateBowAim(g);
    // No aimed shot pending: the launch is native.
    m.beforeBowLaunch(g, BOW);
    NEAR(col(g, 0, 0), 1.0, 1e-6);
    pad(m, mouse(MBTN_LEFT, MBTN_LEFT, 0));
    pad(m, mouse(0, 0, MBTN_LEFT));
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

int main() {
    testAimAndRelease();
    testZoomHandsOver();
    testShoulderOffset();
    testArrowRetarget();
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
