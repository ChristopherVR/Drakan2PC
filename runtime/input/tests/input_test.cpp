// Unit tests with a fake guest. Plain asserts, no framework. Exit code != 0 on failure.
#include "input/DrakanInput.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <thread>
#include <atomic>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <limits>

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
        double _a = (a), _b = (b);                                                                                     \
        if (std::fabs(_a - _b) > (eps)) {                                                                              \
            ++g_fail;                                                                                                  \
            std::printf("FAIL %s:%d  %s=%g vs %s=%g\n", __FILE__, __LINE__, #a, _a, #b, _b);                           \
        }                                                                                                              \
    } while (0)

struct FakeGuest : IGuest {
    std::map<uint32_t, uint8_t> mem;
    struct Rec {
        GuestCall c;
    };
    std::vector<GuestCall> calls;
    std::map<uint32_t, std::function<uint32_t(const GuestCall&)>> stubs;
    uint8_t read8(uint32_t a) override {
        auto it = mem.find(a);
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
        mem[a] = v & 255;
        mem[a + 1] = v >> 8;
    }
    uint32_t call(const GuestCall& c) override {
        calls.push_back(c);
        auto it = stubs.find(c.addr);
        return it == stubs.end() ? 0u : it->second(c);
    }
    // asyncPost: post() only records the call and stays busy until the test clears `busy` (a guest call still running
    // on its own thread); otherwise post() runs synchronously like the default.
    bool asyncPost = false;
    bool busy = false;
    std::vector<GuestCall> posted;
    void post(const GuestCall& c) override {
        if (!asyncPost) {
            call(c);
            return;
        }
        posted.push_back(c);
        busy = true;
    }
    bool postBusy() const override { return busy; }
};

// Tests written before [mouse] pitch_view_gain check cam+0x84 arithmetic 1:1.
static Config legacyPitch(Config c) {
    c.pitchViewGain = 1.0f;
    return c;
}

static FrameInput keys(std::initializer_list<const char*> names, uint8_t mouse = 0) {
    FrameInput f;
    for (const char* n : names) {
        int d;
        if (keyFromName(n, &d))
            f.keyDown[static_cast<size_t>(d)] = true;
    }
    f.mouseDown = mouse;
    return f;
}

static PadSample sampleOf(InputModule& m, const FrameInput& f) {
    m.beginFrame(f);
    uint8_t buf[32];
    CHECK(m.fillPadSample(0, 0, buf));
    return unpackPadSample(buf);
}

static void testConfig() {
    Config c = Config::defaults();
    CHECK(c.lookMode == LookMode::Direct);
    CHECK(!c.game.empty() && !c.menu.empty());
    std::string w;
    Config d = Config::defaults();
    Config::parse(
        "[bindings]\nX = CROSS | ACT:0x12\nBOGUS = CROSS\nQ = NOPE\n[mouse]\nsensitivity_x = 0.5\ninvert_y = 1\n", d,
        &w);
    // X has two actions; migration adds Enter, keypad Enter, map, quicksave and quickload.
    CHECK(d.game.size() == 7);
    {
        int enters = 0;
        for (const auto& bd : d.game)
            if (bd.src == Binding::Key && (bd.code == 0x1C || bd.code == 0x9C) && bd.act == Binding::PadBtn &&
                bd.value == Pad::CROSS)
                ++enters;
        CHECK(enters == 2);
        Config e = Config::defaults();
        Config::parse("[bindings]\nENTER = SQUARE\nNUMPADENTER = NONE\n", e, nullptr);
        int n = 0, sq = 0;
        for (const auto& bd : e.game)
            if (bd.src == Binding::Key && bd.code == 0x1C) {
                ++n;
                if (bd.value == Pad::SQUARE)
                    ++sq;
            }
        CHECK(n == 1 && sq == 1); // an explicit Enter binding is kept, not duplicated
        Config g = Config::defaults();
        int cross = 0;
        for (const auto& bd : g.game)
            if (bd.src == Binding::Key && (bd.code == 0x1C || bd.code == 0x9C) && bd.value == Pad::CROSS)
                ++cross;
        CHECK(cross == 2); // default gameplay layer: Enter and keypad Enter confirm
    }
    NEAR(d.sensX, 0.5, 1e-6);
    CHECK(d.invertY);
    CHECK(w.find("BOGUS") != std::string::npos && w.find("NOPE") != std::string::npos);
    CHECK(Config::defaults().game.size() > 20); // the replacement above did not touch the global defaults
}

static void testStickCurve() {
    // inverse response round-trips through the game's pow-2 curve to linear
    for (int i = -100; i <= 100; ++i) {
        float u = i / 100.0f;
        uint8_t b = encodeStickLinear(u);
        float back = gameStickResponse(b);
        NEAR(back, u, 0.02); // 1 count of f = 2f/128 in u
        if (u != 0)
            CHECK((back < 0) == (u < 0));
    }
    CHECK(encodeStickLinear(0) == 128);
    CHECK(encodeStickLinear(1.0f) == 255);
    CHECK(encodeStickLinear(-1.0f) == 0);
    CHECK(encodeStickLinear(5.0f) == 255);
}

static void testPadSamples() {
    InputModule m;
    m.initHeadless(legacyPitch(Config::defaults()));
    // idle
    PadSample s = sampleOf(m, FrameInput{});
    CHECK(s.buttons == 0 && s.lx == 128 && s.ly == 128 && s.rx == 128 && s.ry == 128);
    uint8_t raw[32];
    m.beginFrame(FrameInput{});
    m.fillPadSample(0, 0, raw);
    CHECK(raw[0] == 0 && raw[1] == 0x73 && raw[2] == 0xff && raw[3] == 0xff); // active low, nothing pressed
    CHECK(!m.fillPadSample(1, 0, raw));
    // W: forward = stick up = byte 0 on ly
    s = sampleOf(m, keys({"W"}));
    CHECK(s.ly == 0 && s.lx == 128);
    // D: right
    s = sampleOf(m, keys({"D"}));
    CHECK(s.lx == 255 && s.ly == 128);
    // W+D diagonal: normalised, linear output 0.7071 on each axis after the game's curve
    s = sampleOf(m, keys({"W", "D"}));
    NEAR(gameStickResponse(s.lx), 0.7071, 0.03);
    NEAR(gameStickResponse(s.ly), -0.7071, 0.03);
    // opposite keys cancel
    s = sampleOf(m, keys({"A", "D", "W", "S"}));
    CHECK(s.lx == 128 && s.ly == 128);
    // walk: half speed
    s = sampleOf(m, keys({"W", "LSHIFT"}));
    NEAR(gameStickResponse(s.ly), -0.5, 0.03);
    // buttons: LMB = Square (0x80), RMB = Circle, Space = L1
    FrameInput f = keys({"SPACE"}, MBTN_LEFT | MBTN_RIGHT);
    f.captured = true;
    s = sampleOf(m, f);
    CHECK(s.buttons == (Pad::SQUARE | Pad::CIRCLE | Pad::TRIANGLE));
    m.beginFrame(f);
    m.fillPadSample(0, 0, raw);
    uint16_t onWire = static_cast<uint16_t>((raw[2] << 8) | raw[3]);
    CHECK(static_cast<uint16_t>(~onWire) == (Pad::SQUARE | Pad::CIRCLE | Pad::TRIANGLE));
    CHECK((onWire & Pad::SQUARE) == 0); // active low
    // a tap (press+release inside one frame) still produces one frame of button
    FrameInput tap;
    int d;
    keyFromName("F", &d);
    tap.keyPressed[static_cast<size_t>(d)] = true;
    tap.keyReleased[static_cast<size_t>(d)] = true;
    s = sampleOf(m, tap);
    CHECK(s.buttons == Pad::R2);
    // XInput merge: A button = Cross, pad stick overrides when deflected
    FrameInput g;
    g.gamepad.connected = true;
    g.gamepad.buttons = Pad::CROSS;
    g.gamepad.lx = 1.0f;
    g.gamepad.ly = 1.0f; // up-right
    s = sampleOf(m, g);
    CHECK(s.buttons == Pad::CROSS && s.lx == 255 && s.ly == 1);
}

static void testWalkControl() {
    Config c = Config::defaults();
    CHECK(!c.toggleRun);
    Config::parse("[keyboard]\ntoggle_run = 1\n", c, nullptr);
    CHECK(c.toggleRun);
    InputModule m;
    m.initHeadless(c);
    FakeGuest g;
    auto checkCommand = [&](bool down) {
        int count = 0;
        for (const auto& call : g.calls)
            if (call.addr == c.addrInvokeCallback && call.i[1] == 0x4010) {
                CHECK(call.i[0] == c.addrPadMgr && call.i[2] == 0);
                CHECK(call.i[3] == (down ? 1u : 0u));
                ++count;
            }
        CHECK(count == 1);
        g.calls.clear();
    };
    auto tick = [&](const FrameInput& f) {
        PadSample s = sampleOf(m, f);
        m.afterPadMgrUpdate(g);
        return s;
    };
    NEAR(gameStickResponse(tick(keys({"W"})).ly), -1, 0.03);
    CHECK(g.calls.empty());
    NEAR(gameStickResponse(tick(keys({"W", "LSHIFT"})).ly), -0.5, 0.03);
    checkCommand(true);
    // Holding the key, repeated pad reads and releasing it must not retrigger a toggle.
    NEAR(gameStickResponse(tick(keys({"W", "LSHIFT"})).ly), -0.5, 0.03);
    uint8_t buf[32];
    CHECK(m.fillPadSample(0, 0, buf));
    NEAR(gameStickResponse(buf[7]), -0.5, 0.03);
    NEAR(gameStickResponse(tick(keys({"W"})).ly), -0.5, 0.03);
    CHECK(g.calls.empty());
    NEAR(gameStickResponse(tick(keys({"W", "LSHIFT"})).ly), -1, 0.03);
    checkCommand(false);
    tick(keys({}));
    // A complete tap inside one frame switches to walk, then persists.
    FrameInput tap = keys({"W"});
    tap.keyPressed[0x2a] = tap.keyReleased[0x2a] = true;
    NEAR(gameStickResponse(tick(tap).ly), -0.5, 0.03);
    checkCommand(true);
    tick(keys({"W"}));
    CHECK(g.calls.empty());
    // Menus release the native walk command and reset the latch to run.
    m.setMenuActive(true);
    tick(keys({}));
    checkCommand(false);
    m.setMenuActive(false);
    NEAR(gameStickResponse(tick(keys({"W"})).ly), -1, 0.03);
    tick(keys({"LSHIFT"}));
    checkCommand(true);
    m.setSuspended(true); // can change ownership between pad reads
    m.afterPadMgrUpdate(g);
    checkCommand(false);
    m.setSuspended(false);
    NEAR(gameStickResponse(tick(keys({"W"})).ly), -1, 0.03);
    // Hold mode releases immediately and any WALK binding drives the same command.
    m.mutableConfig().toggleRun = false;
    Config::parse("[bindings]\nRSHIFT = WALK\nW = LSTICK_UP\n", m.mutableConfig(), nullptr);
    NEAR(gameStickResponse(tick(keys({"W", "RSHIFT"})).ly), -0.5, 0.03);
    checkCommand(true);
    NEAR(gameStickResponse(tick(keys({"W"})).ly), -1, 0.03);
    checkCommand(false);
    tick(keys({"RSHIFT"}));
    checkCommand(true);
    FrameInput unfocused;
    unfocused.focused = false;
    tick(unfocused);
    checkCommand(false);
    // A physical stick retains native speed and receives no keyboard walk command.
    FrameInput pad = keys({"RSHIFT"});
    pad.gamepad.connected = true;
    pad.gamepad.ly = 1;
    NEAR(gameStickResponse(tick(pad).ly), -1, 0.03);
    CHECK(g.calls.empty());
    // Arokh must not receive the on-foot walk command.
    m.beforePadMgrUpdate(g, true);
    g.calls.clear();
    tick(keys({"RSHIFT"}));
    CHECK(g.calls.empty());
    m.beforePadMgrUpdate(g, false);
    tick(keys({}));
    g.calls.clear();
    m.mutableConfig().walkScale = 0.25f;
    // This custom layer only maps W, so add D explicitly for diagonal coverage.
    Binding right;
    right.code = 0x20;
    right.act = Binding::LStick;
    right.value = 3;
    m.mutableConfig().game.push_back(right);
    PadSample diagonal = tick(keys({"W", "D", "RSHIFT"}));
    NEAR(gameStickResponse(diagonal.lx), 0.25 * 0.70710678, 0.03);
    NEAR(gameStickResponse(diagonal.ly), -0.25 * 0.70710678, 0.03);
    checkCommand(true);
}

static void testMouseStickMode() {
    Config c = Config::defaults();
    c.lookMode = LookMode::Stick;
    InputModule m;
    m.initHeadless(legacyPitch(c));
    FrameInput f;
    f.captured = true;
    f.dx = 10;
    f.dy = -20; // right, up
    PadSample s = sampleOf(m, f);
    NEAR(gameStickResponse(s.rx), 10 / 40.0, 0.03);
    NEAR(gameStickResponse(s.ry), -20 / 40.0, 0.03);
    // the same counts over a 1/120 s tick are a 4x faster mouse: deflection is per 1/30 s, not per tick
    InputModule fast;
    fast.initHeadless(legacyPitch(c));
    f.frameSeconds = 1.0f / 120.0f;
    f.dx = 5;
    f.dy = -5;
    s = sampleOf(fast, f);
    NEAR(gameStickResponse(s.rx), 5 / 10.0, 0.03);
    NEAR(gameStickResponse(s.ry), -5 / 10.0, 0.03);
    f.frameSeconds = 1.0f / 30.0f;
    f.dx = 10;
    f.dy = -20;
    // direct mode: the right stick stays centred even with mouse movement
    InputModule d;
    d.initHeadless(legacyPitch(Config::defaults()));
    s = sampleOf(d, f);
    CHECK(s.rx == 128 && s.ry == 128);
}

static void testJitterDither() {
    // a constant small deflection must not be a constant byte (the game blanks changes < 4 inside +-47)
    Config c = Config::defaults();
    c.lookMode = LookMode::Stick;
    InputModule m;
    m.initHeadless(legacyPitch(c));
    FrameInput f;
    f.captured = true;
    f.dx = 4; // small steady rate
    int blanked = 0;
    uint8_t last = 128;
    for (int i = 0; i < 20; ++i) {
        PadSample s = sampleOf(m, f);
        int v = s.rx - 128;
        if (std::abs(v) <= 47 && std::abs(static_cast<int>(s.rx) - last) < 4)
            ++blanked; // what PadMgr_ProcessStickAxis does
        last = s.rx;
        CHECK(s.rx > 128);
    }
    CHECK(blanked <= 1);
}

// ---- camera hook ----
static const uint32_t CTX = 0x5565d4, PLAYER = 0x700000, CAMH = 0x710000, INNER = 0x720000, CAM = 0x730000,
                      HEAD = 0x740000, VT = 0x750000, LIM = 0x760000;

static void setupCamera(FakeGuest& g, float pitch, float limit) {
    g.write32(CTX, 0x600000);
    g.write32(0x600000 + 0x4b4, CAMH);
    g.write32(CAMH + 0xfc, INNER);
    g.write32(INNER + 0x10, CAM);
    g.writeF(CAM + 0x84, pitch);
    g.write32(PLAYER + 0x80, HEAD);
    g.write32(PLAYER + 0x57c, VT);
    g.write32(VT + 0x174, 0xAAAA00); // gate fn 1 (returns nonzero)
    g.write32(VT + 0x17c, 0x32cda8); // gate fn 2 (returns zero)
    g.write32(VT + 0x170, 0);
    g.write32(VT + 0x178, 0);
    g.writeF(LIM, limit);
    g.stubs[0xAAAA00] = [](const GuestCall&) { return 1u; };
    g.stubs[0x32cda8] = [](const GuestCall&) { return 0u; };
}

static void testCamera() {
    Config c = Config::defaults();
    c.pitchSign = -1.0f;   // the default since controls v2 (verified with screenshots: negative pitch looks down, mouse
                           // up raises it)
    c.dragonStick = false; // these tests cover the direct write in dragon mode too
    c.sensX = 0.01f;
    c.sensY = 0.01f;
    FakeGuest g;
    setupCamera(g, 0.0f, 0.6283f);
    InputModule m;
    m.initHeadless(legacyPitch(c));
    FrameInput f;
    f.captured = true;
    f.dx = 10;
    f.dy = 20;
    m.beginFrame(f);
    CHECK(m.afterCameraUpdateLook(g, PLAYER, LIM, false));
    // pitch -= dy*sens (pitch_sign -1): 0 - 0.2
    NEAR(g.readF(CAM + 0x84), -0.2, 1e-5);
    // gate calls then the turn call
    const GuestCall* turn = nullptr;
    for (auto& cl : g.calls)
        if (cl.addr == c.addrTurnFn)
            turn = &cl;
    CHECK(turn != nullptr);
    if (turn) {
        CHECK(turn->nf == 1 && turn->ni == 2);
        NEAR(turn->f[0], -0.1, 1e-6);     // -dx*sens
        CHECK(turn->i[0] == HEAD + 0x14); // *(player+0x80)+0x14
        CHECK(turn->i[1] == 0x4e8c48);
    }
    // consumed: a second call in the same frame does nothing
    size_t n = g.calls.size();
    m.afterCameraUpdateLook(g, PLAYER, LIM, false); // may re-write the held pitch, but must not turn again
    CHECK(g.calls.size() == n);

    // clamp: ground [-lim, +0.7 lim]
    g.writeF(CAM + 0x84, 0.0f);
    f.dx = 0;
    f.dy = 100000; // looks way down
    m.beginFrame(f);
    m.afterCameraUpdateLook(g, PLAYER, LIM, false);
    NEAR(g.readF(CAM + 0x84), -0.6283, 1e-4);
    f.dy = -100000;
    m.beginFrame(f);
    m.afterCameraUpdateLook(g, PLAYER, LIM, false);
    NEAR(g.readF(CAM + 0x84), 0.6283 * 0.7, 1e-4);
    // dragon: [-0.45 lim, +0.55 lim] with the 1.0472 limit
    g.writeF(LIM, 1.0472f);
    f.dy = 100000;
    m.beginFrame(f);
    m.afterCameraUpdateLook(g, PLAYER, LIM, true);
    NEAR(g.readF(CAM + 0x84), -1.0472 * 0.45, 1e-4);
    f.dy = -100000;
    m.beginFrame(f);
    m.afterCameraUpdateLook(g, PLAYER, LIM, true);
    NEAR(g.readF(CAM + 0x84), 1.0472 * 0.55, 1e-4);
    // bad limits pointer -> falls back to the known constants
    g.writeF(LIM, 0.0f);
    f.dy = 100000;
    m.beginFrame(f);
    m.afterCameraUpdateLook(g, PLAYER, LIM, false);
    NEAR(g.readF(CAM + 0x84), -0.6283, 1e-4);

    // invert Y flips direction; GPR ABI passes bits in a0
    Config c2 = c;
    c2.invertY = true;
    c2.turnCallAbi = CallAbi::GprBits;
    InputModule m2;
    m2.initHeadless(legacyPitch(c2));
    FakeGuest g2;
    setupCamera(g2, 0.0f, 0.6283f);
    f.dx = 5;
    f.dy = 10;
    m2.beginFrame(f);
    m2.afterCameraUpdateLook(g2, PLAYER, LIM, false);
    NEAR(g2.readF(CAM + 0x84), +0.1, 1e-5);
    const GuestCall* t2 = nullptr;
    for (auto& cl : g2.calls)
        if (cl.addr == c2.addrTurnFn)
            t2 = &cl;
    CHECK(t2 && t2->nf == 0 && t2->ni == 3);
    if (t2 && t2->ni == 3) {
        float a;
        uint32_t b = t2->i[0];
        std::memcpy(&a, &b, 4);
        NEAR(a, -0.05, 1e-6);
    }

    // turn gate closed (second vcall nonzero): pitch still applied, no turn call
    FakeGuest g3;
    setupCamera(g3, 0.0f, 0.6283f);
    g3.stubs[0x32cda8] = [](const GuestCall&) { return 1u; };
    InputModule m3;
    m3.initHeadless(legacyPitch(c));
    f.dx = 5;
    f.dy = 0;
    m3.beginFrame(f);
    m3.afterCameraUpdateLook(g3, PLAYER, LIM, false);
    bool anyTurn = false;
    for (auto& cl : g3.calls)
        if (cl.addr == c.addrTurnFn)
            anyTurn = true;
    CHECK(!anyTurn);

    // not captured / menu: nothing happens
    InputModule m4;
    m4.initHeadless(legacyPitch(c));
    FakeGuest g4;
    setupCamera(g4, 0.0f, 0.6283f);
    FrameInput fu = f;
    fu.captured = true;
    m4.setMenuActive(true);
    m4.beginFrame(fu);
    CHECK(!m4.afterCameraUpdateLook(g4, PLAYER, LIM, false));
}

static void writeFile(const std::string& p, const std::string& t);
// ---- controls v2: persistent pitch, strafe, capture vs menu context ----
static void testControlsV2() {
    Config c = Config::defaults();
    c.sensX = c.sensY = 0.01f;
    const uint32_t FLAG = c.addrStickModeFlag, REC = c.addrStickMap;
    // pitch re-centring is off while the mouse looks: the flag is cleared for the call and restored afterwards; the
    // pitch stays put
    {
        FakeGuest g;
        setupCamera(g, 0.0f, 0.6283f);
        g.write32(FLAG, 1);
        InputModule m;
        m.initHeadless(legacyPitch(c));
        FrameInput f;
        f.captured = true;
        f.dy = -20; // mouse up
        m.beginFrame(f);
        m.beforeCameraUpdateLook(g, false);
        CHECK(g.read32(FLAG) == 0);
        m.afterCameraUpdateLook(g, PLAYER, LIM, false);
        CHECK(g.read32(FLAG) == 1);
        NEAR(g.readF(CAM + 0x84), 0.2, 1e-5); // pitch_sign -1: mouse up (dy < 0) -> pitch rises -> looks up
        // no more mouse movement: the module leaves the pitch alone (no hold hack) and keeps the flag cleared only
        // around the call
        FrameInput f2;
        f2.captured = true;
        m.beginFrame(f2);
        m.beforeCameraUpdateLook(g, false);
        CHECK(g.read32(FLAG) == 0);
        CHECK(!m.afterCameraUpdateLook(g, PLAYER, LIM, false));
        CHECK(g.read32(FLAG) == 1);
        NEAR(g.readF(CAM + 0x84), 0.2, 1e-5);
    }
    // a physical right stick keeps the game's re-centring; menus / uncaptured leave the flag alone
    {
        FakeGuest g;
        setupCamera(g, 0.0f, 0.6283f);
        g.write32(FLAG, 1);
        InputModule m;
        m.initHeadless(legacyPitch(c));
        FrameInput f;
        f.captured = true;
        f.gamepad.connected = true;
        f.gamepad.ry = 0.5f;
        m.beginFrame(f);
        m.beforeCameraUpdateLook(g, false);
        CHECK(g.read32(FLAG) == 1);
        Config c2 = c;
        c2.pitchPersist = false;
        InputModule m2;
        m2.initHeadless(legacyPitch(c2));
        FrameInput f2;
        f2.captured = true;
        m2.beginFrame(f2);
        m2.beforeCameraUpdateLook(g, false);
        CHECK(g.read32(FLAG) == 1);
    }
    // A / D strafe: the left stick's X record is pointed at axis 3 (scale 1), the game's mapping is restored in dragon
    // mode / menus / when switched off
    {
        FakeGuest g;
        g.write32(REC, 0);
        g.writeF(REC + 8, 0.8f);
        InputModule m;
        m.initHeadless(legacyPitch(c));
        FrameInput f;
        f.captured = true;
        m.beginFrame(f);
        m.beforePadMgrUpdate(g, false);
        CHECK(g.read32(REC) == 3);
        NEAR(g.readF(REC + 8), 1.0, 1e-6);
        m.beforePadMgrUpdate(g, false); // idempotent
        CHECK(g.read32(REC) == 3);
        m.beforePadMgrUpdate(g, true); // Arokh: the game's own mapping
        CHECK(g.read32(REC) == 0);
        NEAR(g.readF(REC + 8), 0.8, 1e-6);
        m.beforePadMgrUpdate(g, false);
        CHECK(g.read32(REC) == 3);
        m.setMenuActive(true);
        m.beginFrame(f);
        m.beforePadMgrUpdate(g, false);
        CHECK(g.read32(REC) == 0);
        Config c3 = c;
        c3.strafeKeys = false;
        InputModule m3;
        m3.initHeadless(legacyPitch(c3));
        FakeGuest g3;
        g3.write32(REC, 0);
        m3.beginFrame(f);
        m3.beforePadMgrUpdate(g3, false);
        CHECK(g3.read32(REC) == 0);
    }
    // capture: cut scenes / loading (menu layer without a menu context) keep the mouse captured; a game menu or the
    // overlay release it
    {
        InputModule m;
        m.initHeadless(legacyPitch(c));
        FrameInput f;
        m.setMenuActive(true);
        m.beginFrame(f);
        CHECK(m.captured());
        m.setMenuContext(true);
        m.beginFrame(f);
        CHECK(!m.captured());
        m.setMenuContext(false);
        m.setSuspended(true);
        m.beginFrame(f);
        CHECK(!m.captured());
        // the drawn cursor is only available in a menu context
        m.setSuspended(false);
        CHECK(!m.menuCursorVisible());
    }
    // ini upgrade: an old file without controls_version gets the marker block appended once, user values are kept
    {
        const std::string path = "controls_v2_test.ini";
        writeFile(path, "[general]\nlook_mode = direct\n[mouse]\nsensitivity_x = 0.005\npitch_hold_ms = "
                        "1200\npitch_sign = 1\n[keyboard]\nwalk_scale = 0.4\n");
        Config a = Config::loadOrCreate(path, nullptr);
        NEAR(a.sensX, 0.005, 1e-6);
        CHECK(a.strafeKeys && a.pitchPersist);
        CHECK(a.pitchSign == 1.0f); // explicit legacy inversion is user intent, even without a version marker
        std::ifstream in(path);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string t1 = ss.str();
        CHECK(t1.find("controls_version = 2") != std::string::npos && t1.find("strafe = 1") != std::string::npos &&
              t1.find("pitch_sign = 1") != std::string::npos);
        Config b = Config::loadOrCreate(path, nullptr); // second load: no second block
        CHECK(b.controlsVersion == 2);
        std::ifstream in2(path);
        std::stringstream s2;
        s2 << in2.rdbuf();
        CHECK(s2.str() == t1);
        std::remove(path.c_str());
    }
}

static void testCommands() {
    InputModule m;
    m.initHeadless(legacyPitch(Config::defaults()));
    FakeGuest g;
    const uint32_t PAD = 0x4e90c0;
    g.write16(PAD + 8 + 2 * 0x0e, 0x403a); // keymap[DIK '6' slot action 0x0e] = 0x403a (6 = ACT:0x0e)
    g.stubs[0x4af7f8] = [](const GuestCall&) { return 1234u; };
    FrameInput f;
    int d;
    keyFromName("6", &d);
    f.keyDown[static_cast<size_t>(d)] = true;
    f.keyPressed[static_cast<size_t>(d)] = true;
    m.beginFrame(f);
    m.afterPadMgrUpdate(g);
    const GuestCall* inv = nullptr;
    for (auto& c : g.calls)
        if (c.addr == 0x131fa8)
            inv = &c;
    CHECK(inv && inv->ni == 5);
    if (inv && inv->ni == 5) {
        CHECK(inv->i[0] == PAD && inv->i[1] == 0x403a && inv->i[2] == 0x0e && inv->i[3] == 1 && inv->i[4] == 1234);
    }
    // held: no new events; release: one release
    g.calls.clear();
    f.keyPressed.reset();
    m.beginFrame(f);
    m.afterPadMgrUpdate(g);
    CHECK(g.calls.empty());
    for (auto& c : g.calls)
        std::printf("call %x ni=%d i1=%x i2=%x i3=%x\n", c.addr, c.ni, c.i[1], c.i[2], c.i[3]);
    f.keyDown.reset();
    f.keyReleased[static_cast<size_t>(d)] = true;
    m.beginFrame(f);
    m.afterPadMgrUpdate(g);
    int rel = 0;
    for (auto& c : g.calls)
        if (c.addr == 0x131fa8 && c.i[3] == 0)
            ++rel;
    CHECK(rel == 1);
    // unmapped action ids are skipped
    g.calls.clear();
    FrameInput w;
    w.wheel = 1; // WHEELUP = ACT:0x114, keymap empty in the fake
    m.beginFrame(w);
    m.afterPadMgrUpdate(g);
    CHECK(g.calls.empty());
    g.write16(PAD + 8 + 2 * 0x114, 0x4034);
    m.beginFrame(w);
    m.afterPadMgrUpdate(g);
    int inv2 = 0;
    for (auto& c : g.calls)
        if (c.addr == 0x131fa8)
            ++inv2;
    CHECK(inv2 == 2); // press + release
}

static void testExternalCapture() {
    InputModule module;
    module.initHeadless(Config::defaults());
    module.setExternalCapture(false);
    auto frame = keys({"W"}, MBTN_LEFT);
    auto sample = sampleOf(module, frame);
    CHECK(!module.captured());
    CHECK(sample.ly < 128); // Releasing the pointer leaves keyboard movement active.
    CHECK((sample.buttons & Pad::SQUARE) == 0);
    module.setExternalCapture(true);
    frame.mouseDown = frame.mousePressed = 0;
    sampleOf(module, frame);
    CHECK(module.captured());
    frame.focused = false;
    sample = sampleOf(module, frame);
    CHECK(!module.captured() && sample.buttons == 0 && sample.ly == 128);
    frame.focused = true;
    sampleOf(module, frame);
    CHECK(module.captured()); // The external host already owns recapture policy.
    module.setMenuActive(true);
    module.setMenuContext(true);
    frame = keys({"ENTER"});
    sample = sampleOf(module, frame);
    CHECK(!module.captured() && (sample.buttons & Pad::CROSS) != 0);
}

static void testMenuAndCapture() {
    Config c = Config::defaults();
    c.menuCursor = true;
    InputModule m;
    m.initHeadless(legacyPitch(c));
    FakeGuest g;
    g.write32(0x5565c8, 0x600000);
    g.write32(0x600000 + 0xf8, 640);
    g.write32(0x600000 + 0xfc, 448);
    const uint32_t PAD = 0x4e90c0, VTM = 0x780000;
    g.write32(PAD + 0x434, VTM);
    g.write32(VTM + 0x10c, 0xCCCC00);
    g.write32(VTM + 0x108, 0);
    m.setMenuActive(true);
    FrameInput f;
    f.cursorX = 400;
    f.cursorY = 300;
    f.clientW = 800;
    f.clientH = 600;
    m.beginFrame(f);
    m.afterPadMgrUpdate(g);
    const GuestCall* sc = nullptr;
    for (auto& cl : g.calls)
        if (cl.addr == 0xCCCC00)
            sc = &cl;
    CHECK(sc && sc->ni == 3);
    if (sc && sc->ni == 3)
        CHECK(sc->i[0] == PAD && sc->i[1] == 320 && sc->i[2] == 224);
    // menu layer: W = dpad up, sticks centred; LMB is not Cross any more (MenuMouse handles the click natively), RMB =
    // Circle
    PadSample s = sampleOf(m, [] {
        FrameInput x = keys({"W"}, MBTN_LEFT);
        return x;
    }());
    CHECK(s.buttons == Pad::UP && s.ly == 128);
    s = sampleOf(m, [] {
        FrameInput x = keys({}, MBTN_RIGHT);
        return x;
    }());
    CHECK(s.buttons == (Pad::CIRCLE | Pad::START));
    { // [menu] mouse = 0 keeps the old behaviour (left click = Cross)
        Config old = Config::defaults();
        Config::parse("[menu]\nmouse = 0\n[menu_bindings]\nMOUSE1 = CROSS\n", old, nullptr);
        InputModule om;
        om.initHeadless(legacyPitch(old));
        om.setMenuActive(true);
        CHECK((sampleOf(om,
                        [] {
                            FrameInput x = keys({}, MBTN_LEFT);
                            return x;
                        }())
                   .buttons &
               Pad::CROSS) != 0);
    }

    // capture state machine: Esc toggles, focus loss releases and click recaptures
    InputModule k;
    Config kc = Config::defaults();
    kc.releaseOnEsc = true; // default is off since the pause-menu auto release
    k.initHeadless(legacyPitch(kc));
    FrameInput idle;
    k.beginFrame(idle);
    CHECK(k.captured());
    FrameInput esc;
    esc.keyPressed[1] = true;
    k.beginFrame(esc);
    CHECK(!k.captured());
    k.beginFrame(esc);
    CHECK(k.captured());
    FrameInput away;
    away.focused = false;
    k.beginFrame(away);
    CHECK(!k.captured());
    k.beginFrame(idle);
    CHECK(!k.captured());
    FrameInput click;
    click.mousePressed = MBTN_LEFT;
    click.mouseDown = MBTN_LEFT;
    k.beginFrame(click);
    CHECK(k.captured());
    // a click on the free cursor in gameplay does not attack
    k.beginFrame(esc);
    FrameInput c2;
    c2.mouseDown = MBTN_LEFT;
    c2.mousePressed = MBTN_LEFT;
    c2.dx = 50;
    s = sampleOf(k, c2);
    CHECK(!k.captured() && s.buttons == 0);
}

static void writeFile(const std::string& p, const std::string& t) {
    std::ofstream f(p, std::ios::binary);
    f << t;
}

static void testReloadAndPause() {
    {
        InputModule title;
        title.initHeadless(Config::defaults());
        title.setActiveScreen(8);
        title.setHostScreen(true);
        FrameInput edge;
        edge.keyPressed[0x42] = true;
        title.beginFrame(edge);
        CHECK(title.titleQuickLoadPressed());
        CHECK(title.frame().keyPressed.none());
        CHECK(sampleOf(title, FrameInput{}).buttons == 0);
        CHECK(!title.titleQuickLoadPressed());
        title.setSuspended(true);
        title.beginFrame(edge);
        CHECK(!title.titleQuickLoadPressed());
    }
    const char* tmp = std::getenv("TEMP");
    std::string path = std::string(tmp ? tmp : ".") + "/drakan_input_reload_test.ini";
    writeFile(path, "[general]\nlook_mode = direct\n[mouse]\nsensitivity_x = 0.005\ninvert_y = 1\n[bindings]\nQ = "
                    "CROSS\n[menu_bindings]\nQ = CIRCLE\n");
    InputModule m;
    m.initHeadless(legacyPitch(Config::defaults()));
    FrameInput idle;
    m.beginFrame(idle);
    CHECK(m.captured());
    CHECK(m.reloadConfig(path));
    NEAR(m.config().sensX, 0.0020, 1e-6); // not applied until the next frame
    PadSample s = sampleOf(m, keys({"Q"}));
    CHECK(m.configGeneration() == 1);
    NEAR(m.config().sensX, 0.005, 1e-6);
    CHECK(m.config().invertY);
    CHECK(s.buttons == Pad::CROSS); // new binding active, same frame
    CHECK(m.captured());            // capture state kept
    // user releases capture with F10, reload keeps it released
    FrameInput f10;
    f10.keyPressed[0x44] = true;
    m.beginFrame(f10);
    CHECK(!m.captured());
    writeFile(path, "[mouse]\nsensitivity_x = 0.007\n[bindings]\nQ = SQUARE\n");
    CHECK(m.reloadConfig(path));
    s = sampleOf(m, keys({"Q"}));
    CHECK(!m.captured());
    NEAR(m.config().sensX, 0.007, 1e-6);
    CHECK(s.buttons == Pad::SQUARE);
    m.beginFrame(f10);
    CHECK(m.captured());
    // missing file: false, config untouched
    CHECK(!m.reloadConfig(path + ".missing"));
    sampleOf(m, idle);
    CHECK(m.configGeneration() == 2);
    // held command released when its binding disappears
    {
        InputModule c;
        Config cfg = Config::defaults();
        c.initHeadless(legacyPitch(cfg));
        Config none = cfg;
        none.game.clear();
        c.reloadConfig(none);
        c.beginFrame(idle);
        CHECK(c.config().game.empty());
    }
    // thread safety: reload from another thread while frames run
    std::atomic<bool> stop{false};
    std::thread th([&] {
        while (!stop) {
            m.reloadConfig(path);
            std::this_thread::yield();
        }
    });
    for (int i = 0; i < 200000 && m.configGeneration() < 20; ++i) {
        FrameInput f = keys({"Q"});
        f.dx = 3;
        m.beginFrame(f);
        uint8_t b[32];
        m.fillPadSample(0, 0, b);
    }
    stop = true;
    th.join();
    CHECK(m.configGeneration() > 2);

    // setMenuActive pauses gameplay input: game bindings and mouse buttons ignored, mouse released, resumes cleanly
    InputModule p;
    p.initHeadless(legacyPitch(Config::defaults()));
    s = sampleOf(p, keys({"W"}, MBTN_LEFT));
    CHECK(p.captured() && s.ly < 128);
    p.setMenuActive(true);
    p.setMenuContext(true);
    CHECK(p.menuActive());
    FrameInput mf = keys({"Space"}, MBTN_RIGHT);
    mf.dx = 40;
    s = sampleOf(p, mf);
    CHECK(!p.captured() && s.rx == 128 && s.ry == 128);
    s = sampleOf(p, keys({"W"}));
    CHECK(s.ly == 128); // W is not a gameplay move in the menu layer
    p.setMenuActive(false);
    p.setMenuContext(false);
    s = sampleOf(p, keys({"W"}));
    CHECK(p.captured() && s.ly < 128);
    std::remove(path.c_str());
}

// ---- MenuMouse: fake GUI screen with three button widgets (layout of the title menu), see
// docs/reference/mouse-menus.md
static void testMenuMouse() {
    // mapping
    {
        MenuMouseSample s;
        s.clientW = 1920;
        s.clientH = 1080;
        s.cx = 960;
        s.cy = 540;
        int gx = 0, gy = 0;
        MenuView v; // not valid: full client
        CHECK(MenuMouse::mapToGui(s, v, 512, 512, &gx, &gy) && gx == 256 && gy == 256);
        v.valid = true;
        v.x = 240;
        v.y = 0;
        v.w = 1440;
        v.h = 1080; // 4:3 pillarbox in 16:9
        s.cx = 240;
        CHECK(MenuMouse::mapToGui(s, v, 512, 512, &gx, &gy) && gx == 0);
        s.cx = 239;
        CHECK(!MenuMouse::mapToGui(s, v, 512, 512, &gx, &gy));
        s.cx = 1679;
        s.cy = 1079;
        CHECK(MenuMouse::mapToGui(s, v, 512, 512, &gx, &gy) && gx == 511 && gy == 511);
        s.cx = 1680;
        CHECK(!MenuMouse::mapToGui(s, v, 512, 512, &gx, &gy));
    }
    FakeGuest g;
    const uint32_t S = 0x700000, A = 0x710000, B = 0x710100, C = 0x710200, V = 0x720000, V2 = 0x730000, SC = 0x740000;
    const uint32_t E = 0x750000, O = 0x750100, VTB = 0x750200, P = 0x750300, CTXA = 0x760000, SND = 0x760100,
                   SVT = 0x760200;
    g.write32(0x4fb60c, S);
    g.write32(0x5565c8, SC);
    g.write32(SC + 0xf8, 512);
    g.write32(SC + 0xfc, 512);
    g.write32(S + 0x18, 8);
    g.write32(S + 0x7c, A);
    g.write32(S + 0x80, C);
    g.write32(S + 0x84, A);
    g.write32(S + 0x98, V2);
    g.write32(V2 + 0x2c, 0x303000);
    g.write16(V2 + 0x28, 0);
    const uint32_t ws[3] = {A, B, C};
    const int ys[3] = {191, 227, 263};
    for (int i = 0; i < 3; ++i) {
        g.write32(ws[i] + 0, i ? ws[i - 1] : 0);
        g.write32(ws[i] + 4, i < 2 ? ws[i + 1] : 0);
        g.write16(ws[i] + 0xc, 3);
        g.write32(ws[i] + 0x10, i == 0 ? 0x241 : 0x201);
        g.write16(ws[i] + 0x1c, 0);
        g.write16(ws[i] + 0x1e, static_cast<uint16_t>(ys[i]));
        g.write16(ws[i] + 0x20, 512);
        g.write16(ws[i] + 0x22, 32);
        g.write32(ws[i] + 0x3c, 1);
        g.write32(ws[i] + 0x40, 1);
        g.write32(ws[i] + 0x54, V);
    }
    g.write16(V + 0x70, 0);
    g.write32(V + 0x74, 0x301000); // canFocus
    g.write16(V + 0x28, 0);
    g.write32(V + 0x2c, 0x302000); // press / release
    g.write16(V + 0x30, 0);
    g.write32(V + 0x34, 0x302100); // drag
    g.stubs[0x301000] = [](const GuestCall&) { return 1u; };
    g.stubs[0x2bec48] = [&](const GuestCall&) { return E; };
    g.write32(E + 0xc, O);
    g.write32(O + 8, VTB);
    g.write16(VTB + 0x18, 0);
    g.write32(VTB + 0x1c, 0x304000);
    g.stubs[0x304000] = [&](const GuestCall&) { return P; };
    g.write32(P, 77);
    g.write32(0x5565d4, CTXA);
    g.write32(CTXA + 0x32c, SND);
    g.write32(SND, SVT);
    g.write16(SVT + 0x110, 0);
    g.write32(SVT + 0x114, 0x305000);
    auto callsTo = [&](uint32_t a) {
        int n = 0;
        for (const auto& c : g.calls)
            if (c.addr == a)
                ++n;
        return n;
    };

    MenuMouse mm;
    MenuView view;
    auto sample = [&](float gx, float gy) { // client = 1024 x 1024 -> 2 px per GUI unit
        MenuMouseSample s;
        s.clientW = 1024;
        s.clientH = 1024;
        s.cx = gx * 2;
        s.cy = gy * 2;
        s.inside = true;
        return s;
    };
    mm.update(g, sample(100, 100), view, 5000); // first sample: no movement yet
    CHECK(g.read32(S + 0x84) == A);
    MenuMouseResult r = mm.update(g, sample(100, 240), view, 5000); // hover row B (y 227..259)
    CHECK(g.read32(S + 0x84) == B);
    CHECK((g.read32(A + 0x10) & 0x40) == 0 && (g.read32(B + 0x10) & 0x40) != 0);
    CHECK(callsTo(0x303000) == 1); // listener told about the old widget (message 0xc)
    CHECK(callsTo(0x305000) == 1); // tick sound
    CHECK(mm.cursorVisible());
    CHECK(r.padTap == 0);
    mm.update(g, sample(100, 241), view, 5000); // still on B: no second focus change
    CHECK(callsTo(0x305000) == 1);
    mm.update(g, sample(100, 275), view, 5000); // row C
    CHECK(g.read32(S + 0x84) == C);
    // press + release on C = native press (state 3) then release (state 2) with the widget captured in between
    MenuMouseSample p = sample(100, 275);
    p.pressed = MBTN_LEFT;
    p.down = MBTN_LEFT;
    g.calls.clear();
    mm.update(g, p, view, 5000);
    CHECK(callsTo(0x302000) == 1 && g.calls.back().i[1] == 3);
    CHECK(g.read32(0x4fb19c) == C);
    MenuMouseSample q = sample(100, 275);
    q.released = MBTN_LEFT;
    mm.update(g, q, view, 5000);
    CHECK(callsTo(0x302000) == 2 && g.calls.back().i[1] == 2);
    CHECK(g.read32(0x4fb19c) == 0);
    // press on C, drag off it, release outside: no activation for a button
    g.calls.clear();
    p = sample(100, 275);
    p.pressed = MBTN_LEFT;
    p.down = MBTN_LEFT;
    mm.update(g, p, view, 5000);
    g.write32(C + 0x10, g.read32(C + 0x10) | 0x80);
    q = sample(100, 20);
    q.released = MBTN_LEFT;
    mm.update(g, q, view, 5000);
    CHECK(callsTo(0x302000) == 1);
    CHECK((g.read32(C + 0x10) & 0x80) == 0);
    // Radio activation must be dispatched by native pad input. A synchronous
    // press would enter the checkbox sound's resource wait on a copied context.
    mm.reset();
    g.write16(C + 0xc, 5);
    g.calls.clear();
    p = sample(100, 275);
    p.pressed = p.down = MBTN_LEFT;
    r = mm.update(g, p, view, 5000);
    CHECK(r.padTap == Pad::CROSS);
    CHECK(callsTo(0x302000) == 0);
    CHECK(g.read32(S + 0x84) == C && g.read32(0x4fb19c) == 0);
    q = sample(100, 275);
    q.released = MBTN_LEFT;
    r = mm.update(g, q, view, 5000);
    CHECK(r.padTap == 0 && callsTo(0x302000) == 0);
    g.write16(C + 0xc, 3);
    // Slider: its native handlers may wait for a sound resource, so they run as posted guest calls, one at a time. The
    // press lands on the thumb centre (the native press only pages left / right of it) and a drag then moves the thumb
    // to the click; the captured widget stays set until the release has run.
    mm.reset();
    g.write16(C + 0xc, 9);
    g.write32(C + 0x10, 0x4201);
    auto putF = [&](uint32_t a, float f) {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        g.write32(a, u);
    };
    putF(C + 0x7c, 0.0f);   // min
    putF(C + 0x80, 100.0f); // max
    putF(C + 0x84, 50.0f);  // value
    putF(C + 0x88, 10.0f);  // page step
    g.write32(0x4fb408, 0); // track margins and minimum thumb length of the skin
    g.write32(0x4fb438, 0);
    g.write32(0x4fb3f8, 4);
    g.asyncPost = true;
    g.posted.clear();
    g.calls.clear();
    p = sample(60, 275);
    p.pressed = p.down = MBTN_LEFT;
    mm.update(g, p, view, 5000);
    // track 512, thumb = 10 / 100 * 512 = 51 wide at (512 - 51) / 2 = 230: centre 255
    CHECK(g.posted.size() == 1 && g.posted[0].addr == 0x302000 && g.posted[0].i[1] == 3 && g.posted[0].i[2] == 255);
    CHECK(callsTo(0x302000) == 0); // nothing runs nested in the hook
    CHECK(g.read32(0x4fb19c) == C);
    MenuMouseSample held = sample(60, 275);
    held.down = MBTN_LEFT;
    mm.update(g, held, view, 5000); // the press is still running: the drag waits
    CHECK(g.posted.size() == 1);
    g.busy = false;
    mm.update(g, held, view, 5000);
    CHECK(g.posted.size() == 2 && g.posted[1].addr == 0x302100 && g.posted[1].i[2] == 60); // drag to the click
    q = sample(60, 275);
    q.released = MBTN_LEFT;
    g.busy = false;
    mm.update(g, q, view, 5000);
    CHECK(g.posted.size() == 3 && g.posted[2].addr == 0x302000 && g.posted[2].i[1] == 2);
    CHECK(g.read32(0x4fb19c) == C); // release still running
    g.busy = false;
    mm.update(g, sample(60, 275), view, 5000);
    CHECK(g.read32(0x4fb19c) == 0);
    g.asyncPost = false;
    g.write16(C + 0xc, 3);
    g.write32(C + 0x10, 0x201);
    // wheel: down moves down (D-pad), a slider takes left / right
    MenuMouseSample w = sample(100, 240);
    w.wheel = -1;
    r = mm.update(g, w, view, 5000);
    CHECK(r.padTap == Pad::DOWN);
    w.wheel = 1;
    r = mm.update(g, w, view, 5000);
    CHECK(r.padTap == Pad::UP);
    g.write16(B + 0xc, 9);
    r = mm.update(g, w, view, 5000);
    CHECK(r.padTap == Pad::RIGHT);
    // no active screen: everything resets, nothing is called
    g.write32(0x4fb60c, 0);
    g.calls.clear();
    mm.update(g, sample(100, 240), view, 5000);
    CHECK(g.calls.empty() && !mm.cursorVisible() && mm.activeScreenId() == -1);
    // a screen without focusable widgets: a click does nothing (it used to be a Cross tap; a click that misses every
    // widget must never activate anything)
    g.write32(0x4fb60c, S);
    g.stubs[0x301000] = [](const GuestCall&) { return 0u; };
    MenuMouseSample c = sample(100, 240);
    c.pressed = MBTN_LEFT;
    c.down = MBTN_LEFT;
    r = mm.update(g, c, view, 5000);
    CHECK(r.padTap == 0);
    // hotspots (load / save slot grid and page buttons): hover once per entry, press, no widget logic, wheel still taps
    // the D-pad
    {
        std::vector<std::pair<int, HotspotEvent>> ev;
        MenuMouse hm;
        hm.setHotspotHandler([&](IGuest&, int id, HotspotEvent e) { ev.push_back({id, e}); });
        hm.setHotspots(S, {{1, 50, 50, 100, 40}, {2, 300, 50, 100, 40}});
        hm.update(g, sample(10, 10), view, 5000); // first sample, outside
        hm.update(g, sample(70, 60), view, 5000); // moved inside hotspot 1
        CHECK(ev.size() == 1 && ev[0].first == 1 && ev[0].second == HotspotEvent::Hover && hm.hoveredHotspot() == 1);
        hm.update(g, sample(75, 60), view, 5000); // still hotspot 1: no second hover
        CHECK(ev.size() == 1);
        MenuMouseSample hp = sample(75, 60);
        hp.pressed = MBTN_LEFT;
        hp.down = MBTN_LEFT;
        hm.update(g, hp, view, 5000);
        CHECK(ev.size() == 2 && ev[1].first == 1 && ev[1].second == HotspotEvent::Press);
        hm.update(g, sample(10, 10), view, 5000); // outside every hotspot
        CHECK(hm.hoveredHotspot() == -1);
        MenuMouseSample hw = sample(10, 10);
        hw.wheel = -1;
        CHECK(hm.update(g, hw, view, 5000).padTap == Pad::DOWN);
        hm.setHotspots(0x799999, {{1, 0, 0, 512, 512}}); // other screen: ignored
        hm.update(g, sample(11, 11), view, 5000);
        CHECK(hm.hoveredHotspot() == -1);
    }
    // menu rows: a button with a centred text only reacts over its text (+ icon), not over the whole full-width row
    {
        const uint32_t T = 0x7a0000;
        g.write32(T, 0x41424344);
        g.write8(T + 4, 0);     // "DCBA"
        g.write32(A + 0x5c, T); // widget A: text pointer, flag bit 0 (centred) is set in 0x241
        g.stubs[0x2885c8] = [](const GuestCall&) { return 80u; }; // measured width
        g.stubs[0x301000] = [](const GuestCall&) { return 1u; };
        MenuMouse lm;
        lm.update(g, sample(5, 100), view, 5000);
        lm.update(g, sample(10, 200), view, 5000); // row A (y 191..223) far left of the text: no focus change
        CHECK(g.read32(S + 0x84) != A || lm.debug().hover == 0);
        CHECK(lm.debug().hover == 0);
        lm.update(g, sample(256, 205), view, 5000); // on the text (row centre)
        CHECK(lm.debug().hover == A);
    }
    // A tutorial dialog rendered over inventory inherits the page's left
    // origin. Its visible OK label must focus/activate the ordinary widget.
    {
        g.write32(S + 0x18, 1);
        for (float scale : {1.0f, .75f, 4.0f / 7.0f, .375f}) {
            MenuMouse dialog;
            MenuView modalView;
            modalView.valid = true;
            modalView.w = 1024;
            modalView.h = 1024;
            modalView.hudScaleX = scale;
            modalView.hudAnchorX = 0;
            g.calls.clear();
            auto click = sample(256 * scale, 205);
            click.pressed = MBTN_LEFT;
            click.down = MBTN_LEFT;
            dialog.update(g, click, modalView, 5000);
            CHECK(dialog.debug().hover == A && g.read32(0x4fb19c) == A);
            click.pressed = click.down = 0;
            click.released = MBTN_LEFT;
            dialog.update(g, click, modalView, 5000);
            CHECK(callsTo(0x302000) == 2 && g.calls.back().i[1] == 2);
            click = sample(10 * scale, 205);
            click.pressed = click.down = MBTN_LEFT;
            g.calls.clear();
            dialog.update(g, click, modalView, 5000);
            CHECK(dialog.debug().hover == 0 && callsTo(0x302000) == 0);
        }
        g.write32(S + 0x18, 8);
    }
    // Inventory pages without captured content: pointer travel never steps the selection (it drifted away from the
    // cursor); wheel and right click keep their section-wide meaning.
    {
        const uint32_t INV = 0x780000;
        g.write32(0x4fb60c, INV);
        g.write32(INV + 0x18, kInventoryScreenId);
        g.write32(INV + 0x154, 2);
        MenuMouse im;
        im.update(g, sample(100, 100), view, 5000); // first sample
        CHECK(im.inventorySection() == -1 || im.inventorySection() == 2);
        r = im.update(g, sample(110, 100), view, 5000);
        CHECK(r.padTap == 0 && im.inventorySection() == 2);
        r = im.update(g, sample(140, 100), view, 5000); // 40 units right: no relative step
        CHECK(r.padTap == 0);
        r = im.update(g, sample(140, 140), view, 5000);
        CHECK(r.padTap == 0);
        r = im.update(g, sample(140, 100), view, 5000);
        CHECK(r.padTap == 0);
        MenuMouseSample iw = sample(140, 100);
        iw.wheel = 1;
        r = im.update(g, iw, view, 5000);
        CHECK(r.padTap == Pad::L1);
        iw.wheel = -1;
        r = im.update(g, iw, view, 5000);
        CHECK(r.padTap == Pad::R1);
        MenuMouseSample ic = sample(140, 100);
        ic.pressed = MBTN_LEFT;
        ic.down = MBTN_LEFT;
        r = im.update(g, ic, view, 5000);
        CHECK(r.padTap == 0);      // Uncertified Skills click cannot confirm
        g.write32(INV + 0x154, 2); // Skills
        r = im.update(g, ic, view, 5000);
        CHECK(r.padTap == 0 && im.inventorySection() == 2);
        MenuMouseSample irc = sample(140, 100);
        irc.pressed = MBTN_RIGHT;
        irc.down = MBTN_RIGHT;
        r = im.update(g, irc, view, 5000);
        CHECK(r.padTap == Pad::SELECT); // right click = back = SELECT (closes the inventory)
        MenuMouseOptions mo;
        mo.inventoryClickSquare = false;
        im.setOptions(mo);
        g.write32(INV + 0x154, 0);
        r = im.update(g, ic, view, 5000);
        CHECK(r.padTap == 0); // Equipment waits for native draw geometry
        mo.inventoryMouse = false;
        im.setOptions(mo);
        r = im.update(g, ic, view, 5000);
        CHECK(r.padTap == 0);
        g.write32(0x4fb60c, 0);
    }
}

// InputModule integration: suspended input is neutral (but injected taps still apply), left click in the menu layer is
// not Cross any more
static void testMenuMouseModule() {
    InputModule m;
    m.initHeadless(legacyPitch(Config::defaults()));
    m.setMenuActive(true);
    FrameInput f;
    f.mouseDown = MBTN_LEFT;
    PadSample s = sampleOf(m, f);
    CHECK((s.buttons & Pad::CROSS) == 0); // MOUSE1 = CROSS was dropped from the menu layer (handled natively)
    FrameInput k = keys({"ENTER"});
    s = sampleOf(m, k);
    CHECK((s.buttons & Pad::CROSS) != 0);
    m.setSuspended(true);
    s = sampleOf(m, k);
    CHECK(s.buttons == 0); // neutral while the overlay owns the input
    m.injectPadTap(Pad::START, 2);
    s = sampleOf(m, k);
    CHECK(s.buttons == Pad::START);
    s = sampleOf(m, k);
    CHECK(s.buttons == Pad::START);
    s = sampleOf(m, k);
    CHECK(s.buttons == 0);
    m.setSuspended(false);
    s = sampleOf(m, k);
    CHECK((s.buttons & Pad::CROSS) != 0);
}

static void testOwnershipFollowup() {
    for (bool delivered : {false, true}) {
        InputModule commands;
        commands.initHeadless(legacyPitch(Config::defaults()));
        FakeGuest guest;
        guest.write16(0x4e90c0 + 8 + 2 * 0x0e, 0x403a);
        commands.beginFrame(keys({"6"}));
        if (delivered)
            commands.afterPadMgrUpdate(guest);
        guest.calls.clear();
        commands.setSuspended(true);
        commands.setSuspended(false);
        commands.afterPadMgrUpdate(guest);
        int releases = 0, presses = 0;
        for (const auto& call : guest.calls)
            if (call.addr == 0x131fa8) {
                if (call.i[3])
                    ++presses;
                else
                    ++releases;
            }
        CHECK(presses == 0);
        CHECK(releases == 1);
    }
    // The overlay may open and close without any intervening guest hook.
    for (bool cached : {false, true}) {
        InputModule transient;
        transient.initHeadless(legacyPitch(Config::defaults()));
        FakeGuest guest;
        setupCamera(guest, 0, 0.6283f);
        FrameInput held = keys({"D"});
        held.mouseDown = held.mousePressed = MBTN_LEFT;
        held.dx = 30;
        held.dy = -20;
        transient.beginFrame(held);
        uint8_t bytes[32];
        if (cached)
            transient.fillPadSample(0, 0, bytes);
        transient.setSuspended(true);
        transient.setSuspended(false);
        transient.fillPadSample(0, 0, bytes);
        const auto neutral = unpackPadSample(bytes);
        CHECK(neutral.buttons == 0);
        CHECK(neutral.lx == 128 && neutral.rx == 128);
        CHECK(!transient.afterCameraUpdateLook(guest, PLAYER, LIM, false));
        NEAR(guest.readF(CAM + 0x84), 0, 1e-6);
        CHECK(sampleOf(transient, held).buttons & Pad::SQUARE);
    }
    InputModule m;
    m.initHeadless(legacyPitch(Config::defaults()));
    FrameInput f;
    sampleOf(m, f);
    f.focused = false;
    sampleOf(m, f);
    f.focused = true;
    f.mouseDown = f.mousePressed = MBTN_LEFT;
    CHECK((sampleOf(m, f).buttons & Pad::SQUARE) == 0);
    CHECK(m.captured());
    f.mousePressed = 0;
    CHECK((sampleOf(m, f).buttons & Pad::SQUARE) == 0); // recapture drag remains harmless
    f.mouseDown = 0;
    f.mouseReleased = MBTN_LEFT;
    CHECK((sampleOf(m, f).buttons & Pad::SQUARE) == 0);
    f.mouseReleased = 0;
    f.mouseDown = f.mousePressed = MBTN_LEFT;
    CHECK((sampleOf(m, f).buttons & Pad::SQUARE) != 0); // next deliberate click attacks

    // Suspension may arrive after the first of several pad reads in one frame.
    m.setSuspended(true);
    uint8_t buf[32];
    m.fillPadSample(0, 0, buf);
    CHECK(unpackPadSample(buf).buttons == 0);
    CHECK(unpackPadSample(buf).lx == 128 && unpackPadSample(buf).rx == 128);
    m.setSuspended(false);
    m.fillPadSample(0, 0, buf);
    CHECK(unpackPadSample(buf).buttons == 0); // no replay of cached pre-overlay attack

    // Focus restoration inside a real menu must keep its native mouse click.
    InputModule menu;
    menu.initHeadless(legacyPitch(Config::defaults()));
    menu.setMenuActive(true);
    menu.setMenuContext(true);
    f = FrameInput{};
    f.focused = false;
    menu.beginFrame(f);
    f.focused = true;
    f.mouseDown = f.mousePressed = MBTN_LEFT;
    menu.beginFrame(f);
    CHECK(!menu.captured());
    CHECK(menu.frame().mousePressed == MBTN_LEFT);
}

static void testLookAndCombatFollowup() {
    Config cfg = Config::defaults();
    std::string warnings;
    Config::parse(
        "[mouse]\nsensitivity_x=nan\nsensitivity_y=inf\nyaw_sign=1e999\npitch_sign=-inf\nstick_full_counts=nan\n", cfg,
        &warnings);
    CHECK(!warnings.empty());
    CHECK(std::isfinite(cfg.sensX) && std::isfinite(cfg.sensY));
    CHECK(std::isfinite(cfg.yawSign) && std::isfinite(cfg.pitchSign));
    CHECK(std::isfinite(cfg.stickFullCounts));

    for (const char* sign : {"1", "-1"}) {
        const std::string path = "followup_legacy.ini";
        const std::string original = std::string("; intentional custom controls\r\n[mouse]\r\npitch_sign=") + sign +
                                     "\r\ninvert_y=1\r\nyaw_sign=1\r\nsensitivity_x=.003\r\n";
        writeFile(path, original);
        const Config loaded = Config::loadOrCreate(path, nullptr);
        CHECK(loaded.invertY && loaded.yawSign == 1);
        NEAR(loaded.pitchSign, std::strtof(sign, nullptr), 1e-6);
        std::ifstream file(path, std::ios::binary);
        std::stringstream contents;
        contents << file.rdbuf();
        CHECK(contents.str().rfind(original, 0) == 0);
        file.close();
        std::remove(path.c_str());
    }

    for (bool inverse : {false, true}) {
        Config c = Config::defaults();
        Config::parse(inverse ? "[mouse]\ninvert_x=1\ninvert_y=1\nsensitivity_x=.004\nsensitivity_y=.003\n"
                              : "[mouse]\ninvert_x=0\ninvert_y=0\nsensitivity_x=.004\nsensitivity_y=.003\n",
                      c, nullptr);
        CHECK(c.invertX == inverse && c.invertY == inverse);
        InputModule direct;
        direct.initHeadless(legacyPitch(c));
        FakeGuest guest;
        setupCamera(guest, 0, 0.6283f);
        FrameInput f;
        f.dx = 10;
        f.dy = -10;
        direct.beginFrame(f);
        direct.afterCameraUpdateLook(guest, PLAYER, LIM, false);
        NEAR(guest.readF(CAM + 0x84), inverse ? -.03f : .03f, 1e-6);
        for (const auto& call : guest.calls)
            if (call.addr == c.addrTurnFn)
                NEAR(call.f[0], inverse ? .04f : -.04f, 1e-6);
        c.lookMode = LookMode::Stick;
        InputModule stick;
        stick.initHeadless(legacyPitch(c));
        const auto sample = sampleOf(stick, f);
        CHECK(inverse ? sample.rx < 128 : sample.rx > 128);
        CHECK(inverse ? sample.ry > 128 : sample.ry < 128);
    }

    // Same raw distance over different frame partitions: no dt or second-hook multiplier.
    for (int parts : {1, 2, 4}) {
        FakeGuest g;
        setupCamera(g, 0.0f, 0.6283f);
        InputModule m;
        m.initHeadless(legacyPitch(Config::defaults()));
        float yaw = 0;
        g.stubs[m.config().addrTurnFn] = [&](const GuestCall& call) {
            yaw += call.f[0];
            return 0u;
        };
        FrameInput f;
        f.dx = 40 / parts;
        f.dy = -40 / parts;
        for (int i = 0; i < parts; ++i) {
            m.beginFrame(f);
            CHECK(m.afterCameraUpdateLook(g, PLAYER, LIM, false));
            CHECK(!m.afterCameraUpdateLook(g, PLAYER, LIM, false));
        }
        NEAR(yaw, -0.08f, 1e-5);
        NEAR(g.readF(CAM + 0x84), 0.08f, 1e-5);
    }

    InputModule combat;
    combat.initHeadless(legacyPitch(Config::defaults()));
    FrameInput f = keys({"W", "D", "F", "Q"}, MBTN_LEFT | MBTN_RIGHT);
    f.mousePressed = f.mouseDown;
    for (int i = 0; i < 20; ++i) {
        const auto s = sampleOf(combat, f);
        CHECK((s.buttons & (Pad::SQUARE | Pad::CIRCLE | Pad::R1 | Pad::R2)) ==
              (Pad::SQUARE | Pad::CIRCLE | Pad::R1 | Pad::R2));
        NEAR(gameStickResponse(s.lx), 0.7071f, 0.025);
        NEAR(gameStickResponse(s.ly), -0.7071f, 0.025);
        f.mousePressed = 0;
        f.dx = 17;
        f.dy = -12; // dragging does not create an attack edge
    }
    CHECK(sampleOf(combat, FrameInput{}).buttons == 0);
}

// Pure script tests: fake timestamps, no sleeps, window, game, or GPU.
struct ScriptTest {
    InputScript script;
    FrameInput out;
    uint16_t pad = 0;
    ScriptEvents ev;
    std::string warnings;

    explicit ScriptTest(const std::string& text) {
        const std::string path = "script_clock_test.txt";
        writeFile(path, text);
        CHECK(script.load(path, &warnings));
        std::remove(path.c_str());
    }
    void step(int frame, int playRun = 0, std::optional<uint64_t> ms = std::nullopt) {
        out = FrameInput{};
        ev = ScriptEvents{};
        script.advance(frame, playRun, out, pad, ev, ms);
    }
};

static void testScriptLegacy() {
    const std::string text = "# Default clock remains PadMgr call numbers\n"
                             "1 key down W\n1 mouse 3 -2 3\n"
                             "2 key tap SPACE\n2 button tap 1\n2 pad tap CROSS\n2 wheel -1\n"
                             "2 cursor 12 34\n2 dump first\n2 shot first\n2 log on 4\n2 menu on\n"
                             "3 sync play 2\n0 dump synced\n1 key up W\n1 quit\n";
    ScriptTest plain(text), timestamped(text);
    CHECK(plain.warnings.empty() && timestamped.warnings.empty());
    int w = 0, space = 0;
    CHECK(keyFromName("W", &w) && keyFromName("SPACE", &space));
    for (int frame = 0; frame <= 7; ++frame) {
        const int play = frame >= 5 ? 2 : 0;
        plain.step(frame, play);
        // Supplying timestamps (including backwards time) cannot affect old scripts.
        timestamped.step(frame, play, frame % 2 ? 9000000 : 0);
        const auto& a = plain.out;
        const auto& b = timestamped.out;
        CHECK(a.keyDown == b.keyDown && a.keyPressed == b.keyPressed && a.keyReleased == b.keyReleased);
        CHECK(a.mouseDown == b.mouseDown && a.mousePressed == b.mousePressed && a.mouseReleased == b.mouseReleased);
        CHECK(a.dx == b.dx && a.dy == b.dy && a.wheel == b.wheel);
        CHECK(a.cursorX == b.cursorX && a.cursorY == b.cursorY && a.clientW == b.clientW && a.clientH == b.clientH);
        CHECK(a.focused == b.focused && a.captured == b.captured && plain.pad == timestamped.pad);
        CHECK(plain.ev.dumps == timestamped.ev.dumps && plain.ev.shots == timestamped.ev.shots);
        CHECK(plain.ev.quit == timestamped.ev.quit && plain.ev.logOn == timestamped.ev.logOn &&
              plain.ev.menu == timestamped.ev.menu);
        CHECK(a.keyDown[w] == (frame >= 1 && frame < 6));
        CHECK(a.keyPressed[w] == (frame == 1) && a.keyReleased[w] == (frame == 6));
        CHECK(a.keyDown[space] == (frame == 2) && a.keyReleased[space] == (frame == 3));
        CHECK(a.mouseDown == (frame == 2 ? MBTN_LEFT : 0));
        CHECK(a.mouseReleased == (frame == 3 ? MBTN_LEFT : 0));
        CHECK(plain.pad == (frame == 2 ? Pad::CROSS : 0));
        CHECK(a.dx == (frame >= 1 && frame <= 3 ? 3 : 0));
        CHECK(a.dy == (frame >= 1 && frame <= 3 ? -2 : 0));
        CHECK(a.wheel == (frame == 2 ? -1 : 0));
        CHECK(a.cursorX == (frame >= 2 ? 12 : 320) && a.cursorY == (frame >= 2 ? 34 : 256));
        CHECK(plain.ev.dumps == (frame == 2   ? std::vector<std::string>{"first"}
                                 : frame == 5 ? std::vector<std::string>{"synced"}
                                              : std::vector<std::string>{}));
        CHECK(plain.ev.shots == (frame == 2 ? std::vector<std::string>{"first"} : std::vector<std::string>{}));
        CHECK(plain.ev.logOn == (frame == 2 ? 4 : -1) && plain.ev.menu == (frame == 2 ? 1 : -2));
        CHECK(plain.ev.quit == (frame == 6));
    }
}

static void testScriptDualWieldSetting() {
    ScriptTest s("clock milliseconds\n100 dual_wield off\n200 dual_wield on\n300 dual_wield invalid\n");
    CHECK(!s.warnings.empty());
    s.step(1, 0, 1000);
    CHECK(s.ev.dualWield == -1);
    s.step(2, 0, 1100);
    CHECK(s.ev.dualWield == 0);
    s.step(3, 0, 1150);
    CHECK(s.ev.dualWield == -1);
    s.step(4, 0, 1200);
    CHECK(s.ev.dualWield == 1);
    s.step(5, 0, 1400);
    CHECK(s.ev.dualWield == -1);
}

static void testScriptScreenSync() {
    ScriptTest s("1 sync screen 5 2\n0 dump on5\n2 quit\n3 sync screen x\n");
    CHECK(!s.warnings.empty()); // the bad sync line is reported and skipped
    const int screens[] = {-1, 1, 5, 1, 5, 5, 5, 5, 5};
    for (int frame = 0; frame < 9; ++frame) {
        s.script.setScreen(screens[frame]);
        s.step(frame);
        // screen 5 is first up for two consecutive samples at frame 5: the dump runs there, `2 quit` two later
        CHECK(s.ev.dumps == (frame == 5 ? std::vector<std::string>{"on5"} : std::vector<std::string>{}));
        CHECK(s.ev.quit == (frame == 7));
    }
}

static void testScriptElapsed() {
    ScriptTest s("clock milliseconds # opt in\n0 key down W\n1000 key up W\n"
                 "1000 dump end\n1000 shot end\n1000 quit\n1100 dump later\n");
    CHECK(s.warnings.empty());
    int w = 0;
    CHECK(keyFromName("W", &w));
    s.step(999999); // Missing time must not accidentally interpret frame counts as milliseconds.
    CHECK(s.out.keyDown.none() && !s.ev.quit);
    const uint64_t start = 5000000000ULL; // Arbitrary origin, beyond 32-bit uptime.
    s.step(999999, 0, start);
    CHECK(s.out.keyPressed[w]);
    s.step(1000000, 0, start);
    CHECK(s.out.keyDown[w] && s.out.keyPressed.none());
    s.step(1000001, 0, start + 999);
    CHECK(s.out.keyDown[w] && s.ev.dumps.empty());
    s.step(1, 0, start + 1000); // Frame numbers are irrelevant in this mode.
    CHECK(s.out.keyReleased[w] && !s.out.keyDown[w]);
    CHECK(s.ev.dumps == std::vector<std::string>{"end"} && s.ev.shots == std::vector<std::string>{"end"} && s.ev.quit);
    for (uint64_t ms : {start + 1000, start - 100, start + 999, start + 1099}) {
        s.step(2, 0, ms);
        CHECK(s.out.keyDown.none() && s.out.keyReleased.none() && s.ev.dumps.empty() && s.ev.shots.empty() &&
              !s.ev.quit);
    }
    s.step(2, 0, start + 1100);
    CHECK(s.ev.dumps == std::vector<std::string>{"later"});
}

static void testScriptElapsedSync() {
    ScriptTest s("clock milliseconds\n0 sync play 2\n0 dump ready\n100 key down W\n"
                 "200 sync play 3\n0 key up W\n0 dump second\n50 quit\n");
    CHECK(s.warnings.empty());
    int w = 0;
    CHECK(keyFromName("W", &w));
    s.step(1, 0, 1000);
    s.step(2, 1, 2000);
    s.step(3, 0, 3000);
    CHECK(s.ev.dumps.empty() && s.out.keyDown.none());
    s.step(4, 2); // Even an already satisfied sync needs a timestamp for its new origin.
    CHECK(s.ev.dumps.empty());
    s.step(5, 2, 4000);
    CHECK(s.ev.dumps == std::vector<std::string>{"ready"} && s.out.keyDown.none());
    s.step(6, 2, 4099);
    CHECK(s.out.keyDown.none());
    s.step(7, 2, 4100);
    CHECK(s.out.keyPressed[w]);
    s.step(8, 0, 4200);
    CHECK(s.out.keyDown[w] && s.ev.dumps.empty());
    s.step(9, 2, 6000);
    CHECK(s.out.keyDown[w] && s.ev.dumps.empty());
    s.step(10, 3, 7000);
    CHECK(s.out.keyReleased[w] && s.ev.dumps == std::vector<std::string>{"second"} && !s.ev.quit);
    s.step(11, 3, 7049);
    CHECK(!s.ev.quit);
    s.step(12, 3, 7050);
    CHECK(s.ev.quit);

    // A sync encountered during a stall starts its next segment at the current sample, not its due time.
    ScriptTest stalled("clock milliseconds\n100 sync play 1\n0 dump ready\n10 quit\n");
    stalled.step(1, 1, 0);
    stalled.step(2, 1, 1000);
    CHECK(stalled.ev.dumps == std::vector<std::string>{"ready"} && !stalled.ev.quit);
    stalled.step(3, 1, 1009);
    CHECK(!stalled.ev.quit);
    stalled.step(4, 1, 1010);
    CHECK(stalled.ev.quit);
}

static void testScriptElapsedSamples() {
    ScriptTest s("clock milliseconds\n0 key tap SPACE\n0 button tap 1\n0 pad tap CROSS\n"
                 "0 mouse 3 -2 3\n0 wheel 2\n0 log on 5\n0 sync play 1\n0 dump synced\n");
    CHECK(s.warnings.empty());
    int space = 0;
    CHECK(keyFromName("SPACE", &space));
    s.step(1, 0, 0);
    CHECK(s.out.keyPressed[space] && s.out.mousePressed == MBTN_LEFT && s.pad == Pad::CROSS);
    CHECK(s.out.dx == 3 && s.out.dy == -2 && s.out.wheel == 2 && s.ev.logOn == 5);
    s.step(2); // Taps expire and mouse repetitions continue even without a timestamp.
    CHECK(s.out.keyReleased[space] && s.out.mouseReleased == MBTN_LEFT && s.pad == 0);
    CHECK(s.out.dx == 3 && s.out.dy == -2 && s.out.wheel == 0 && s.ev.logOn == -1);
    s.step(3, 0, 10000); // Still waiting, repetitions count samples despite the long gap.
    CHECK(s.out.dx == 3 && s.out.dy == -2 && s.out.keyDown.none());
    s.step(4, 1, 10000);
    CHECK(s.out.dx == 0 && s.out.dy == 0 && s.ev.dumps == std::vector<std::string>{"synced"});

    // A long gap drains due one-shot events once, in file order; each mouse rep starts on this sample.
    ScriptTest gap("clock milliseconds\n10 dump a\n20 dump b\n20 mouse 2 4 2\n30 mouse 1 -1\n30 wheel -1\n");
    gap.step(1, 0, 0);
    gap.step(2, 0, 1000);
    CHECK((gap.ev.dumps == std::vector<std::string>{"a", "b"}));
    CHECK(gap.out.dx == 3 && gap.out.dy == 3 && gap.out.wheel == -1);
    gap.step(3, 0, 500); // Regressed time cannot repeat already consumed events.
    CHECK(gap.ev.dumps.empty() && gap.out.dx == 2 && gap.out.dy == 4 && gap.out.wheel == 0);
    gap.step(4, 0, 1000);
    CHECK(gap.out.dx == 0 && gap.out.dy == 0);
}

static void testScriptElapsedRates() {
    // Different sample rates hold W for exactly the same fake real time.
    int w = 0;
    CHECK(keyFromName("W", &w));
    for (int stepMs : {40, 20, 10, 5}) {
        ScriptTest s("clock milliseconds\n0 key down W\n1000 key up W\n1000 dump end\n");
        CHECK(s.warnings.empty());
        int heldMs = 0, dumps = 0;
        for (int ms = 0, frame = 1; ms <= 1200; ms += stepMs, ++frame) {
            s.step(frame, 0, static_cast<uint64_t>(ms));
            if (s.out.keyDown[w])
                heldMs += stepMs;
            dumps += static_cast<int>(s.ev.dumps.size());
        }
        CHECK(heldMs == 1000 && dumps == 1);
    }
}

static void testScriptClockErrors() {
    ScriptTest s("clock seconds\nclock milliseconds extra\nclock\n"
                 "clock milliseconds\nclock milliseconds\n0 dump valid\n");
    for (int line : {1, 2, 3, 5})
        CHECK(s.warnings.find("line " + std::to_string(line) + ":") != std::string::npos);
    s.step(1, 0, 9999);
    CHECK(s.ev.dumps == std::vector<std::string>{"valid"});
    ScriptTest late("1 dump first\nclock milliseconds\n2 quit\n");
    CHECK(late.warnings.find("line 2:") != std::string::npos);
    late.step(1, 0, 10000);
    CHECK(!late.ev.quit);
    late.step(2, 0, 10000);
    CHECK(late.ev.quit); // Invalid late directive keeps the frame clock.
}

static void testRefreshAlignedMouseWindow() {
    // A steady sweep of one count per millisecond, frames starting up to +-2 ms off a 60 Hz grid.
    InputModule m;
    m.initHeadless(Config::defaults());
    m.setSampleRefresh(1.0 / 60.0);
    InputAccumulator a;
    const int64_t period = 16666, t0 = 1000000;
    const int jitter[] = {0, 1800, -1500, 400, 2000, -2000, 900, -300, 1500, -1900, 0, 1200};
    int64_t fed = t0 - 50000;
    std::vector<int> perFrame, rawPerFrame;
    int64_t prevNow = 0;
    for (int frame = 0; frame < 120; ++frame) {
        const int64_t now = t0 + frame * period + jitter[frame % 12];
        for (; fed <= now; fed += 1000)
            a.onMouseMove(1, 0, fed);
        FrameInput f;
        a.consume(f, m.mouseCutoffUs(now));
        perFrame.push_back(f.dx);
        if (prevNow)
            rawPerFrame.push_back(static_cast<int>((now - prevNow) / 1000));
        prevNow = now;
    }
    // After the window settles on the earliest phase, every frame gets 16 or 17 counts (the packet grid), while
    // the host time between frames varies from 12 to 19 ms.
    int lo = 1 << 30, hi = 0, total = 0;
    for (size_t i = 24; i < perFrame.size(); ++i) {
        lo = std::min(lo, perFrame[i]);
        hi = std::max(hi, perFrame[i]);
        total += perFrame[i];
    }
    CHECK(lo >= 16 && hi <= 17);
    NEAR(total / double(perFrame.size() - 24), 16.67, 0.1);
    CHECK(*std::min_element(rawPerFrame.begin(), rawPerFrame.end()) <= 13);
    CHECK(*std::max_element(rawPerFrame.begin(), rawPerFrame.end()) >= 19);
    // A stalled frame (three refreshes) takes three refreshes of motion; nothing is lost or invented.
    FrameInput f;
    const int64_t late = prevNow + 3 * period;
    for (; fed <= late; fed += 1000)
        a.onMouseMove(1, 0, fed);
    a.consume(f, m.mouseCutoffUs(late));
    CHECK(f.dx >= 48 && f.dx <= 52);
    // Without a locked refresh the window is simply "now".
    m.setSampleRefresh(0);
    CHECK(m.mouseCutoffUs(late + 5000) == late + 5000);
}

static void testUncappedMouseAccumulator() {
    InputAccumulator a;
    FrameInput f;
    // Many packets and one equivalent packet deliver the same displacement.
    for (int i = 0; i < 8000; ++i)
        a.onMouseMove(25, -25);
    a.consume(f);
    CHECK(f.dx == 200000 && f.dy == -200000);
    a.consume(f);
    CHECK(f.dx == 0 && f.dy == 0);
    a.onMouseMove(200000, -200000);
    a.consume(f);
    CHECK(f.dx == 200000 && f.dy == -200000);
    const int max = std::numeric_limits<int>::max();
    const int min = std::numeric_limits<int>::min();
    a.onMouseMove(max, min);
    a.onMouseMove(17, -19);
    a.consume(f);
    CHECK(f.dx == max && f.dy == min);
    a.consume(f);
    CHECK(f.dx == 17 && f.dy == -19);
    a.onMouseMove(max, min);
    a.onMouseMove(17, -19);
    a.consume(f);
    a.releaseAll();
    a.consume(f);
    CHECK(f.dx == 0 && f.dy == 0); // ownership loss also cancels overflow
}

// A key held through an ownership change (the modern Tab screen opened on Tab and took the input; F1) is not a new
// press when Raw Input repeats it afterwards; it counts again after its release.
static void testHeldOverKeys() {
    constexpr int kTab = 0x0F;
    InputAccumulator a;
    FrameInput f;
    a.onKey(kTab, true);
    a.consume(f);
    CHECK(f.keyPressed[kTab] && f.keyDown[kTab]);
    a.releaseAll(); // the host screen took the input
    a.consume(f);
    CHECK(!f.keyDown[kTab] && !f.keyPressed[kTab]);
    for (int i = 0; i < 5; ++i) { // auto-repeat make codes while still held
        a.onKey(kTab, true);
        a.consume(f);
        CHECK(!f.keyPressed[kTab] && !f.keyDown[kTab]);
    }
    a.onKey(kTab, false); // released: no edge (the game already saw a release)
    a.consume(f);
    CHECK(!f.keyPressed[kTab] && !f.keyReleased[kTab]);
    a.onKey(kTab, true); // a real new press
    a.consume(f);
    CHECK(f.keyPressed[kTab] && f.keyDown[kTab]);
    // Keys that were up at the change are not affected.
    a.releaseAll();
    a.consume(f);
    a.onKey(0x17, true);
    a.consume(f);
    CHECK(f.keyPressed[0x17]);
}

static void testMouseDisplacementOwnership() {
    for (int samples : {30, 60, 120, 240}) {
        for (bool invert : {false, true}) {
            Config c = Config::defaults();
            c.invertX = c.invertY = invert;
            InputModule m;
            m.initHeadless(legacyPitch(c));
            FakeGuest g;
            setupCamera(g, 0, 0.6283f);
            float yaw = 0;
            g.stubs[c.addrTurnFn] = [&](const GuestCall& call) {
                yaw += call.f[0];
                return 0u;
            };
            // Equal total counts over one second, camera only runs every other sample.
            for (int i = 0; i < samples; ++i) {
                FrameInput f;
                f.dx = 240 / samples;
                f.dy = -(120 * (i + 1) / samples - 120 * i / samples);
                m.beginFrame(f);
                if (i % 2) {
                    CHECK(m.afterCameraUpdateLook(g, PLAYER, LIM, false));
                    CHECK(!m.afterCameraUpdateLook(g, PLAYER, LIM, false));
                }
            }
            NEAR(yaw, invert ? 0.48f : -0.48f, 1e-5);
            const float expectedPitch = invert ? -0.24f : 0.24f;
            NEAR(g.readF(CAM + 0x84), expectedPitch, 1e-5);
            FrameInput f;
            f.dx = 10;
            m.beginFrame(f); // pending, then focus is lost
            f.focused = false;
            m.beginFrame(f);
            f.focused = true;
            f.mouseDown = f.mousePressed = MBTN_LEFT;
            f.dx = 0;
            m.beginFrame(f);
            CHECK(!m.afterCameraUpdateLook(g, PLAYER, LIM, false));
        }
    }
    // Fast horizontal travel remains displacement, with no stick saturation.
    for (int parts : {1, 20}) {
        Config c = Config::defaults();
        InputModule m;
        m.initHeadless(legacyPitch(c));
        FakeGuest g;
        setupCamera(g, 0, 0.6283f);
        float yaw = 0;
        g.stubs[c.addrTurnFn] = [&](const GuestCall& call) {
            yaw += call.f[0];
            return 0u;
        };
        FrameInput f;
        f.dx = 200000 / parts;
        for (int i = 0; i < parts; ++i) {
            m.beginFrame(f);
            CHECK(m.afterCameraUpdateLook(g, PLAYER, LIM, false));
        }
        NEAR(yaw, -400.0f, 0.001);
        f.dx = 10;
        m.beginFrame(f);
        m.setSuspended(true);
        CHECK(!m.afterCameraUpdateLook(g, PLAYER, LIM, false));
        m.setSuspended(false);
        CHECK(!m.afterCameraUpdateLook(g, PLAYER, LIM, false));
    }
    Config c = Config::defaults();
    InputModule m;
    m.initHeadless(legacyPitch(c));
    FakeGuest g;
    setupCamera(g, 0, 0.6283f);
    g.write32(c.addrStickMap, 0);
    g.writeF(c.addrStickMap + 8, 0.8f);
    m.beginFrame(keys({"D"}));
    m.beforePadMgrUpdate(g, false);
    CHECK(g.read32(c.addrStickMap) == 3);
    FrameInput f;
    f.gamepad.connected = true;
    f.gamepad.lx = 0.5f;
    m.beginFrame(f);
    m.beforePadMgrUpdate(g, false);
    CHECK(g.read32(c.addrStickMap) == 0);
    NEAR(g.readF(c.addrStickMap + 8), 0.8f, 1e-6);
    c.game.push_back({Binding::Key, 0xC8, Binding::RStick, 0});
    m.reloadConfig(legacyPitch(c));
    m.beginFrame(keys({"UP"}));
    g.write32(c.addrStickModeFlag, 1);
    m.beforeCameraUpdateLook(g, false);
    CHECK(g.read32(c.addrStickModeFlag) == 1);
    m.afterCameraUpdateLook(g, PLAYER, LIM, false);
}

static void setupAimCamera(FakeGuest& g) {
    setupCamera(g, .2f, .6283f);
    g.write32(CAM + 0x48, 0);
    g.write32(PLAYER + 0x94, 0);
    g.write32(PLAYER + 0x90, 0x4000);
    g.write32(CAM + 0x14c, 6);
    g.writeF(PLAYER + 0x114, .1f);
    g.write32(0x4fb730, 1);
    g.write32(0x4fbafc, 0);
    g.write32(0x600000 + 0x7c, 0x770000);
    g.write32(0x600000 + 0x84, 1);
    g.write16(PLAYER + 0x4da, 1);
    g.write32(0x770000, 0x780000);
}

static void testNativePitchVetoes() {
    for (int veto = 0; veto < 5; ++veto) {
        Config c = Config::defaults();
        InputModule m;
        m.initHeadless(legacyPitch(c));
        FakeGuest g;
        setupCamera(g, .2f, .6283f);
        if (veto == 0)
            g.write32(CAM + 0x48, 0x1000);
        if (veto == 1)
            g.write32(PLAYER + 0x94, 4);
        if (veto == 2)
            g.stubs[0x32cda8] = [](const GuestCall&) { return 1u; };
        if (veto == 3)
            g.write32(CAM + 0x48, 0x1800); // freeze takes precedence over alternate channel
        if (veto == 4)
            g.write32(VT + 0x17c, 0); // unavailable gate fails closed
        FrameInput f;
        f.dy = -20;
        m.beginFrame(f);
        CHECK(!m.afterCameraUpdateLook(g, PLAYER, LIM, false));
        NEAR(g.readF(CAM + 0x84), .2f, 1e-6);
        g.write32(CAM + 0x48, 0);
        g.write32(PLAYER + 0x94, 0);
        g.write32(VT + 0x17c, 0x32cda8);
        g.stubs[0x32cda8] = [](const GuestCall&) { return 0u; };
        CHECK(!m.afterCameraUpdateLook(g, PLAYER, LIM, false)); // discarded, no replay
        f.dx = 10;
        m.beginFrame(f);
        CHECK(m.afterCameraUpdateLook(g, PLAYER, LIM, false));
        NEAR(g.readF(CAM + 0x84), .24f, 1e-6);
    }
    // An unknown vtable override must never be called under the input mutex.
    {
        Config cfg = Config::defaults();
        InputModule t;
        t.initHeadless(legacyPitch(cfg));
        FakeGuest guest;
        setupCamera(guest, 0, .6283f);
        bool entered = false;
        guest.write32(VT + 0x17c, 0xBBBB00);
        guest.stubs[0xBBBB00] = [&](const GuestCall&) {
            entered = true;
            return 0u;
        };
        FrameInput mouse;
        mouse.dy = -20;
        t.beginFrame(mouse);
        CHECK(!t.afterCameraUpdateLook(guest, PLAYER, LIM, false));
        CHECK(!entered);
        guest.write32(VT + 0x17c, 0x32cda8);
        guest.write16(VT + 0x178, 0xfffc);
        guest.stubs[0x32cda8] = [&](const GuestCall& call) {
            CHECK(call.ni == 1 && call.i[0] == PLAYER - 4);
            return 0u;
        };
        t.beginFrame(mouse);
        CHECK(t.afterCameraUpdateLook(guest, PLAYER, LIM, false));
    }
    // Camera pitch freeze does not veto independent yaw.
    Config c = Config::defaults();
    InputModule m;
    m.initHeadless(legacyPitch(c));
    FakeGuest g;
    setupCamera(g, 0, .6283f);
    g.write32(CAM + 0x48, 0x1000);
    FrameInput f;
    f.dx = 10;
    f.dy = -20;
    m.beginFrame(f);
    m.afterCameraUpdateLook(g, PLAYER, LIM, false);
    bool turned = false;
    for (const auto& call : g.calls)
        if (call.addr == c.addrTurnFn) {
            turned = true;
            NEAR(call.f[0], -.02f, 1e-6);
        }
    CHECK(turned);
    NEAR(g.readF(CAM + 0x84), 0, 1e-6);
}

static void testStableAimPitch() {
    Config c = Config::defaults();
    c.freeCombat = false;
    InputModule m;
    m.initHeadless(legacyPitch(c));
    FakeGuest g;
    setupAimCamera(g);
    // Native target acquisition is free to move pitch until vertical mouse input.
    m.beginFrame(FrameInput{});
    m.beforeCameraUpdateLook(g, false, PLAYER);
    g.writeF(CAM + 0x84, .15f); // simulate native update; this is not guest execution
    CHECK(!m.afterCameraUpdateLook(g, PLAYER, LIM, false));
    NEAR(g.readF(CAM + 0x84), .15f, 1e-6);
    FrameInput f;
    f.dy = -20;
    m.beginFrame(f);
    m.beforeCameraUpdateLook(g, false, PLAYER);
    g.writeF(CAM + 0x84, .05f);
    CHECK(m.afterCameraUpdateLook(g, PLAYER, LIM, false));
    NEAR(g.readF(CAM + 0x84), .19f, 1e-6); // pre-call .15 plus raw .04
    for (int i = 0; i < 30; ++i) {
        m.beginFrame(FrameInput{});
        m.beforeCameraUpdateLook(g, false, PLAYER);
        g.writeF(CAM + 0x84, .03f);
        g.writeF(CAM + 0x9c, .7f);
        g.writeF(CAM + 0xa0, .8f);
        g.writeF(CAM + 0xa4, .9f);
        g.writeF(CAM + 0x90, .11f);
        g.writeF(CAM + 0x94, .12f);
        g.writeF(CAM + 0x98, .13f);
        CHECK(m.afterCameraUpdateLook(g, PLAYER, LIM, false));
        NEAR(g.readF(CAM + 0x84), .19f, 1e-6);
        for (auto field : {0x9c, 0xa0, 0xa4, 0x90, 0x94, 0x98})
            NEAR(g.readF(CAM + field),
                 field == 0x9c   ? .7f
                 : field == 0xa0 ? .8f
                 : field == 0xa4 ? .9f
                 : field == 0x90 ? .11f
                 : field == 0x94 ? .12f
                                 : .13f,
                 1e-6);
        CHECK(!m.afterCameraUpdateLook(g, PLAYER, LIM, false)); // snapshot consumed once
    }
    // Ownership must also be abandoned when takeover occurs between callbacks,
    // including returning to the same target after focus/overlay/menu loss.
    for (int takeover = 0; takeover < 9; ++takeover) {
        Config cfg = c;
        cfg.game.push_back({Binding::Key, 0xC8, Binding::RStick, 0});
        InputModule t;
        t.initHeadless(legacyPitch(cfg));
        FakeGuest guest;
        setupAimCamera(guest);
        FrameInput mouse;
        mouse.dy = -20;
        t.beginFrame(mouse);
        t.beforeCameraUpdateLook(guest, false, PLAYER);
        guest.writeF(CAM + 0x84, .05f);
        CHECK(t.afterCameraUpdateLook(guest, PLAYER, LIM, false));
        NEAR(guest.readF(CAM + 0x84), .24f, 1e-6);
        FrameInput neutral;
        switch (takeover) {
            case 0:
                neutral.focused = false;
                break;
            case 1:
                t.setSuspended(true);
                t.setSuspended(false);
                break;
            case 2:
                t.setMenuActive(true);
                t.setMenuContext(true);
                break;
            case 3:
                neutral.gamepad.connected = true;
                neutral.gamepad.ry = .5f;
                break;
            case 4:
                neutral.keyDown[0xC8] = true;
                break;
            case 5:
                cfg.pitchPersist = false;
                t.reloadConfig(legacyPitch(cfg));
                break;
            case 6:
                guest.write32(CAM + 0x48, 0x800);
                break;
            case 7:
                guest.write32(0x770000, 0x780100);
                break;
            case 8:
                guest.write32(PLAYER + 0x90, 0);
                break;
        }
        t.beginFrame(neutral);
        t.beforeCameraUpdateLook(guest, false, PLAYER);
        guest.writeF(CAM + 0x84, .05f);
        CHECK(!t.afterCameraUpdateLook(guest, PLAYER, LIM, false));
        NEAR(guest.readF(CAM + 0x84), .05f, 1e-6);
        setupAimCamera(guest);
        t.setMenuActive(false);
        t.setMenuContext(false);
        cfg.pitchPersist = true;
        t.reloadConfig(legacyPitch(cfg));
        t.beginFrame(FrameInput{});
        t.beforeCameraUpdateLook(guest, false, PLAYER);
        guest.writeF(CAM + 0x84, .07f);
        CHECK(!t.afterCameraUpdateLook(guest, PLAYER, LIM, false));
        NEAR(guest.readF(CAM + 0x84), .07f, 1e-6);
    }
    // Every mutation is between the before and after hooks. No stale pitch or counts.
    for (int change = 0; change < 21; ++change) {
        InputModule t;
        t.initHeadless(legacyPitch(c));
        FakeGuest guest;
        setupAimCamera(guest);
        FrameInput mouse;
        mouse.dy = -20;
        t.beginFrame(mouse);
        t.beforeCameraUpdateLook(guest, false, PLAYER);
        guest.writeF(CAM + 0x84, .05f);
        switch (change) {
            case 0:
                guest.write32(PLAYER + 0x90, 0);
                break; // aim exit
            case 1:
                guest.write16(PLAYER + 0x4da, 0);
                break; // target loss
            case 2:
                guest.write32(0x770000, 0x780100);
                break; // target slot reuse
            case 3:
                guest.writeF(PLAYER + 0x114, std::numeric_limits<float>::quiet_NaN());
                break;
            case 4:
                guest.write32(CAM + 0x48, 0x800);
                break;
            case 5:
                guest.write32(CAM + 0x48, 0x1000);
                break;
            case 6:
                guest.write32(PLAYER + 0x94, 4);
                break;
            case 7:
                guest.stubs[0x32cda8] = [](const GuestCall&) { return 1u; };
                break;
            case 8:
                guest.write32(CAM + 0x14c, 1);
                break;
            case 9:
                guest.write32(PLAYER + 0x80, HEAD + 0x100);
                break;
            case 10:
                guest.write32(0x4fbafc, 1);
                break;
            case 11:
                guest.write32(VT + 0x17c, 0x7ffff0); // unknown native predicate: fail closed
                break;
            case 12:
                guest.write32(CAM + 0x48, 0x80000);
                break;
            case 13:
                t.setMenuActive(true);
                break;
            case 14:
                t.setSuspended(true);
                t.setSuspended(false);
                break;
            case 15:
                guest.write32(INNER + 0x10, CAM + 0x1000);
                break;
            case 16:
                guest.write32(0x600000 + 0x4b4, 0);
                break;
            case 17:
                t.reloadConfig(legacyPitch(c));
                t.beginFrame(FrameInput{});
                break;
            case 18:
                mouse.focused = false;
                t.beginFrame(mouse);
                break;
            case 19:
                guest.write16(VT + 0x178, 4);
                break;
            case 20:
                guest.write32(VT + 0x17c, 0x276e40);
                break;
        }
        CHECK(!t.afterCameraUpdateLook(guest, PLAYER, LIM, false));
        NEAR(guest.readF(CAM + 0x84), .05f, 1e-6);
        // Restore native state, then run neutral. A transition requires fresh mouse ownership.
        setupAimCamera(guest);
        guest.stubs[0x32cda8] = [](const GuestCall&) { return 0u; };
        t.setMenuActive(false);
        t.beginFrame(FrameInput{});
        t.beforeCameraUpdateLook(guest, false, PLAYER);
        guest.writeF(CAM + 0x84, .07f);
        CHECK(!t.afterCameraUpdateLook(guest, PLAYER, LIM, false));
        NEAR(guest.readF(CAM + 0x84), .07f, 1e-6);
    }
    // Native controls and opt-out never acquire persistent aim pitch ownership.
    for (int exclusion = 0; exclusion < 6; ++exclusion) {
        Config cfg = c;
        if (exclusion == 0)
            cfg.pitchPersist = false;
        if (exclusion == 1)
            cfg.lookMode = LookMode::Stick;
        if (exclusion == 2)
            cfg.game.push_back({Binding::Key, 0xC8, Binding::RStick, 0});
        if (exclusion == 5)
            cfg.dragonStick = false;
        InputModule t;
        t.initHeadless(legacyPitch(cfg));
        FakeGuest guest;
        setupAimCamera(guest);
        FrameInput mouse;
        mouse.dy = -20;
        if (exclusion == 2)
            mouse.keyDown[0xC8] = true;
        if (exclusion == 3) {
            mouse.gamepad.connected = true;
            mouse.gamepad.ry = .5f;
        }
        const bool dragon = exclusion >= 4;
        t.beginFrame(mouse);
        t.beforeCameraUpdateLook(guest, dragon, PLAYER);
        guest.writeF(CAM + 0x84, .05f);
        t.afterCameraUpdateLook(guest, PLAYER, LIM, dragon);
        NEAR(guest.readF(CAM + 0x84), (exclusion >= 1 && exclusion <= 4) ? .05f : .09f, 1e-6);
        t.beginFrame(FrameInput{});
        t.beforeCameraUpdateLook(guest, dragon, PLAYER);
        guest.writeF(CAM + 0x84, .03f);
        CHECK(!t.afterCameraUpdateLook(guest, PLAYER, LIM, dragon));
        NEAR(guest.readF(CAM + 0x84), .03f, 1e-6);
    }
}

static void testMovingTargetAimOwnership() {
    Config c = Config::defaults();
    c.freeCombat = false;
    InputModule m;
    m.initHeadless(legacyPitch(c));
    FakeGuest g;
    setupAimCamera(g);
    // Read-only native 32cda8 predicate, modeled from its guest instructions.
    // Bit8 is intentionally absent; other transient states may still veto pitch.
    g.stubs[0x32cda8] = [&](const GuestCall&) {
        const uint32_t flags = g.read32(PLAYER + 0x94);
        return static_cast<uint32_t>(
            (flags & 0xc000u) || ((flags & 0x80u) && !(g.read32(PLAYER + 0xab0) & 0x200800u)) ||
            ((flags & 0x10u) && (g.read32(PLAYER + 0xaf0) == 3 || g.read32(PLAYER + 0xaf0) == 4)));
    };
    // Actual build02 acquired/moved/neutral state, including the moving anchor.
    g.write32(PLAYER + 0x90, 0x66000);
    g.write32(CAM + 0x48, 0x10200);
    g.writeF(PLAYER + 0x114, -.0486692227f);
    g.writeF(CAM + 0x84, -.00856110733f);
    g.write32(PLAYER + 0xab0, 0x100000);
    g.write32(PLAYER + 0xaf0, 12746080);
    FrameInput mouse;
    mouse.dy = -20;
    m.beginFrame(mouse);
    m.beforeCameraUpdateLook(g, false, PLAYER);
    // Native pitch/anchor advancement within a call must not discard the raw increment.
    g.writeF(PLAYER + 0x114, -.2258444875f);
    g.write32(PLAYER + 0x94, 0x100);
    g.writeF(CAM + 0x84, -.1215199083f);
    CHECK(m.afterCameraUpdateLook(g, PLAYER, LIM, false));
    const float owned = -.00856110733f + .04f;
    NEAR(g.readF(CAM + 0x84), owned, 1e-6);
    // Moving same target, input/recovery bit8 toggles, neutral mouse under attack.
    // Model BOTH normal between-frame anchor updates and updates within the pair.
    for (int tick = 0; tick < 60; ++tick) {
        g.writeF(PLAYER + 0x114, -.2258444875f - .001f * tick);
        g.write32(PLAYER + 0x94, (tick & 1) ? 0x100 : 0);
        m.beginFrame(FrameInput{});
        m.beforeCameraUpdateLook(g, false, PLAYER);
        g.writeF(PLAYER + 0x114, -.2268444875f - .001f * tick);
        g.write32(PLAYER + 0x94, (tick & 1) ? 0 : 0x100);
        g.writeF(CAM + 0x84, -.2992094159f);
        g.writeF(CAM + 0x9c, -.0366283581f);
        CHECK(m.afterCameraUpdateLook(g, PLAYER, LIM, false));
        NEAR(g.readF(CAM + 0x84), owned, 1e-6);
        NEAR(g.readF(CAM + 0x9c), -.0366283581f, 1e-6);
        NEAR(g.readF(PLAYER + 0x114), -.2268444875f - .001f * tick, 1e-6);
        CHECK(!m.afterCameraUpdateLook(g, PLAYER, LIM, false));
    }
    // All flags except documented bit8 still cancel ownership, even when the
    // native predicate currently returns zero. No broad combat-state exemption.
    for (int bit = 0; bit < 32; ++bit) {
        if (bit == 8)
            continue;
        InputModule t;
        t.initHeadless(legacyPitch(c));
        FakeGuest guest;
        setupAimCamera(guest);
        t.beginFrame(mouse);
        t.beforeCameraUpdateLook(guest, false, PLAYER);
        guest.writeF(CAM + 0x84, .05f);
        CHECK(t.afterCameraUpdateLook(guest, PLAYER, LIM, false));
        guest.write32(PLAYER + 0x94, uint32_t{1} << bit);
        t.beginFrame(FrameInput{});
        t.beforeCameraUpdateLook(guest, false, PLAYER);
        guest.writeF(CAM + 0x84, -.2f);
        CHECK(!t.afterCameraUpdateLook(guest, PLAYER, LIM, false));
        NEAR(guest.readF(CAM + 0x84), -.2f, 1e-6);
    }
    // Veto/cutscene/target change end a previously owned moving-target hold.
    for (int transition = 0; transition < 5; ++transition) {
        InputModule t;
        t.initHeadless(legacyPitch(c));
        FakeGuest guest;
        setupAimCamera(guest);
        t.beginFrame(mouse);
        t.beforeCameraUpdateLook(guest, false, PLAYER);
        guest.writeF(CAM + 0x84, .05f);
        CHECK(t.afterCameraUpdateLook(guest, PLAYER, LIM, false));
        t.beginFrame(FrameInput{});
        t.beforeCameraUpdateLook(guest, false, PLAYER);
        guest.writeF(PLAYER + 0x114, -.3f);
        switch (transition) {
            case 0:
                guest.stubs[0x32cda8] = [](const GuestCall&) { return 1u; };
                break;
            case 1:
                guest.write32(0x4fbafc, 1);
                break;
            case 2:
                guest.write32(0x770000, 0x780100);
                break;
            case 3:
                guest.write16(PLAYER + 0x4da, 0);
                break;
            case 4:
                guest.write32(CAM + 0x48, 0x210200);
                break; // native camera transition bit21
        }
        guest.writeF(CAM + 0x84, -.2f);
        CHECK(!t.afterCameraUpdateLook(guest, PLAYER, LIM, false));
        NEAR(guest.readF(CAM + 0x84), -.2f, 1e-6);
    }
}

static void testAimMotionNotificationPulse() {
    Config c = Config::defaults();
    c.freeCombat = false;
    InputModule m;
    m.initHeadless(legacyPitch(c));
    FakeGuest g;
    setupAimCamera(g);
    g.write32(PLAYER + 0x90, 0x66000);
    g.write32(CAM + 0x48, 0x10200);
    g.writeF(CAM + 0x84, -.2268347377f);
    FrameInput mouse;
    mouse.dy = -20;
    m.beginFrame(mouse);
    m.beforeCameraUpdateLook(g, false, PLAYER);
    g.writeF(CAM + 0x84, -.3f);
    CHECK(m.afterCameraUpdateLook(g, PLAYER, LIM, false));
    const float owned = -.1868347377f;
    NEAR(g.readF(CAM + 0x84), owned, 1e-6);
    // Replay build04 frames 647..667: same identity/aim, moving target,
    // bit94.8 toggles, and flags90 66000 ->660c0 ->66000 on the second hit.
    for (int tick = 0; tick <= 20; ++tick) {
        const uint32_t flags90 = tick == 19 ? 0x660c0u : 0x66000u;
        g.write32(PLAYER + 0x90, flags90);
        g.write32(PLAYER + 0x94, (tick == 0 || tick >= 19) ? 0x100 : 0);
        g.writeF(PLAYER + 0x114, -.3171044588f - tick * .0027f);
        m.beginFrame(FrameInput{});
        m.beforeCameraUpdateLook(g, false, PLAYER);
        // 32cc00 may consume the motion notification within the same callback pair.
        const uint32_t consumed = flags90 & ~0xc0u;
        g.write32(PLAYER + 0x90, consumed);
        g.writeF(CAM + 0x84, -.3134988248f);
        g.writeF(CAM + 0x9c, -.575745821f);
        CHECK(m.afterCameraUpdateLook(g, PLAYER, LIM, false));
        NEAR(g.readF(CAM + 0x84), owned, 1e-6);
        CHECK(g.read32(PLAYER + 0x90) == consumed); // native notification not rewritten
        NEAR(g.readF(CAM + 0x9c), -.575745821f, 1e-6);
    }
    // Other90 bits retain their transition meaning. There is no blanket
    // flags90 exemption or automatic recovery through native permission vetoes.
    for (int bit = 0; bit < 32; ++bit) {
        if (bit == 6 || bit == 7)
            continue;
        InputModule t;
        t.initHeadless(legacyPitch(c));
        FakeGuest guest;
        setupAimCamera(guest);
        t.beginFrame(mouse);
        t.beforeCameraUpdateLook(guest, false, PLAYER);
        guest.writeF(CAM + 0x84, -.3f);
        CHECK(t.afterCameraUpdateLook(guest, PLAYER, LIM, false));
        t.beginFrame(FrameInput{});
        t.beforeCameraUpdateLook(guest, false, PLAYER);
        guest.write32(PLAYER + 0x90, guest.read32(PLAYER + 0x90) ^ (uint32_t{1} << bit));
        guest.writeF(CAM + 0x84, -.3f);
        CHECK(!t.afterCameraUpdateLook(guest, PLAYER, LIM, false));
        NEAR(guest.readF(CAM + 0x84), -.3f, 1e-6);
    }
    // A real native veto still wins even during the documented motion pulse.
    m.beginFrame(FrameInput{});
    m.beforeCameraUpdateLook(g, false, PLAYER);
    g.write32(PLAYER + 0x90, 0x660c0);
    g.stubs[0x32cda8] = [](const GuestCall&) { return 1u; };
    g.writeF(CAM + 0x84, -.3f);
    CHECK(!m.afterCameraUpdateLook(g, PLAYER, LIM, false));
    NEAR(g.readF(CAM + 0x84), -.3f, 1e-6);
    g.stubs[0x32cda8] = [](const GuestCall&) { return 0u; };
    g.write32(PLAYER + 0x90, 0x66000);
    m.beginFrame(FrameInput{});
    m.beforeCameraUpdateLook(g, false, PLAYER);
    g.writeF(CAM + 0x84, -.25f);
    CHECK(!m.afterCameraUpdateLook(g, PLAYER, LIM, false));
    NEAR(g.readF(CAM + 0x84), -.25f, 1e-6); // no speculative permission-recovery policy
}

static void testAlternateMouseAimAndMomentum() {
    for (bool invert : {false, true}) {
        Config c = Config::defaults();
        c.invertX = c.invertY = invert;
        InputModule m;
        m.initHeadless(legacyPitch(c));
        FakeGuest g;
        setupCamera(g, .2f, .6283f);
        g.write32(CAM + 0x48, 0x800);
        // The real Rynn turn predicate returns zero in this camera channel.
        g.stubs[0xAAAA00] = [](const GuestCall&) { return 0u; };
        g.writeF(CAM + 0x94, .1f);
        g.writeF(CAM + 0x98, -.1f);
        FrameInput f;
        f.dx = 120;
        f.dy = -60;
        const auto pad = sampleOf(m, f);
        CHECK(pad.rx == 128 && pad.ry == 128); // never encode mouse aim as a stick
        CHECK(m.afterCameraUpdateLook(g, PLAYER, LIM, false));
        NEAR(g.readF(CAM + 0x94), invert ? -.02f : .22f, 1e-6);
        NEAR(g.readF(CAM + 0x98), invert ? .14f : -.34f, 1e-6);
        NEAR(g.readF(CAM + 0x84), .2f, 1e-6);
        CHECK(g.calls.empty());                                 // alternate aim never rotates the body
        CHECK(!m.afterCameraUpdateLook(g, PLAYER, LIM, false)); // consumed once
        const float pitch = g.readF(CAM + 0x94), yaw = g.readF(CAM + 0x98);
        m.beginFrame(FrameInput{});
        CHECK(!m.afterCameraUpdateLook(g, PLAYER, LIM, false));
        NEAR(g.readF(CAM + 0x94), pitch, 1e-6);
        NEAR(g.readF(CAM + 0x98), yaw, 1e-6);
        g.write32(PLAYER + 0x94, 4);
        m.beginFrame(f);
        CHECK(m.afterCameraUpdateLook(g, PLAYER, LIM, false));
        NEAR(g.readF(CAM + 0x94), pitch, 1e-6); // native vertical suppression
        g.write32(PLAYER + 0x94, 0);
        g.write32(CAM + 0x48, 0);
        m.beginFrame(f);
        m.afterCameraUpdateLook(g, PLAYER, LIM, false);
        CHECK(!g.calls.empty()); // normal body look resumes after leaving aim
    }
    for (int mode = 0; mode < 9; ++mode) {
        Config c = Config::defaults();
        if (mode == 1)
            c.pitchPersist = false; // horizontal momentum is independent of pitch setting
        if (mode == 2)
            c.lookMode = LookMode::Stick;
        InputModule m;
        m.initHeadless(legacyPitch(c));
        FakeGuest g;
        setupCamera(g, .2f, .6283f);
        FrameInput f;
        if (mode == 3) {
            f.gamepad.connected = true;
            f.gamepad.rx = .5f;
        }
        if (mode == 4)
            m.setMenuActive(true);
        if (mode == 5)
            m.setSuspended(true);
        if (mode == 6)
            g.write32(CAM + 0x48, 0x1000);
        if (mode == 7)
            g.write32(CAM + 0x48, 0x800);
        if (mode == 8) {
            f.gamepad.connected = true;
            f.gamepad.lx = .5f;
        }
        for (int tick = 0; tick < 3; ++tick) {
            for (auto field : {0x9c, 0xa0, 0xa4})
                g.writeF(CAM + field, .75f);
            m.beginFrame(f);
            m.beforeCameraUpdateLook(g, false, PLAYER);
            for (auto field : {0x9c, 0xa0, 0xa4})
                NEAR(g.readF(CAM + field), (mode <= 1 || mode == 7) ? 0.f : .75f, 1e-6);
            // Stand in for native rate integration: idle mouse must have no residual displacement.
            g.writeF(CAM + 0x84, g.readF(CAM + 0x84) + g.readF(CAM + 0x9c) * .016f);
            m.afterCameraUpdateLook(g, PLAYER, LIM, false);
        }
        if (mode <= 1 || mode == 7)
            NEAR(g.readF(CAM + 0x84), .2f, 1e-6);
    }
}

static void testRightClickAfterInventory() {
    Config c = Config::defaults();
    InputModule m;
    m.initHeadless(legacyPitch(c));
    FakeGuest g;
    g.write32(c.addrActiveScreenVar, 0x700000);
    g.write32(0x700000 + 0x18, kInventoryScreenId);
    m.setMenuActive(true);
    m.setMenuContext(true);
    m.beginFrame(FrameInput{});
    m.afterPadMgrUpdate(g);
    CHECK(m.guiScreenId() == kInventoryScreenId);
    m.setMenuActive(false);
    m.setMenuContext(false);
    m.afterPadMgrUpdate(g); // reset intentionally retains the last observed screen id
    CHECK(m.guiScreenId() == kInventoryScreenId);
    for (int frame = 0; frame < 3; ++frame)
        CHECK(sampleOf(m, keys({}, MBTN_RIGHT)).buttons & Pad::CIRCLE);
    CHECK(!(sampleOf(m, FrameInput{}).buttons & Pad::CIRCLE));
}

static void testEscClosesInventoryOnly() {
    Config c = Config::defaults();
    InputModule m;
    m.initHeadless(legacyPitch(c));
    FakeGuest g;
    g.write32(c.addrActiveScreenVar, 0x700000);
    g.write32(0x700000 + 0x18, kInventoryScreenId);
    m.setMenuActive(true);
    m.setMenuContext(true);
    m.beginFrame(FrameInput{});
    m.afterPadMgrUpdate(g);
    CHECK(m.guiScreenId() == kInventoryScreenId);
    // Esc (START | CIRCLE in the menu layer) closes the inventory with SELECT; START would open the pause menu
    PadSample s = sampleOf(m, keys({"ESC"}));
    CHECK((s.buttons & Pad::SELECT) && !(s.buttons & Pad::START));
    // the inventory is gone but the layer has not switched yet: the held key must not turn into a START edge
    g.write32(c.addrActiveScreenVar, 0);
    m.afterPadMgrUpdate(g);
    CHECK(m.guiScreenId() == -1);
    s = sampleOf(m, keys({"ESC"}));
    CHECK((s.buttons & Pad::SELECT) && !(s.buttons & Pad::START));
    CHECK(sampleOf(m, FrameInput{}).buttons == 0);
    // released: Esc is START | CIRCLE again on the other menus (resume from the pause menu)
    s = sampleOf(m, keys({"ESC"}));
    CHECK(s.buttons == (Pad::START | Pad::CIRCLE));
}

static void testHeldMouseBowSight() {
    Config c = Config::defaults();
    InputModule m;
    m.initHeadless(legacyPitch(c));
    FakeGuest g;
    setupCamera(g, 0, .6283f);
    g.write32(CAM + 0x48, 0x220);
    m.beginFrame(keys({}, MBTN_RIGHT));
    m.afterPadMgrUpdate(g);
    CHECK(g.calls.empty()); // holding an open sight never repeats a toggle
    m.beginFrame(FrameInput{});
    m.afterPadMgrUpdate(g);
    CHECK(g.calls.size() == 2 && g.calls.back().addr == c.addrInvokeCallback);
    CHECK(g.calls.back().i[1] == 0x4007 && g.calls.back().i[3] == 1);
    m.setSuspended(true);
    m.beginFrame(FrameInput{});
    m.afterPadMgrUpdate(g);
    CHECK(g.calls.size() == 4 && g.calls.back().i[3] == 0); // release even on ownership loss
    m.setSuspended(false);
    m.beginFrame(FrameInput{});
    m.afterPadMgrUpdate(g);
    CHECK(g.calls.size() == 4); // no repeats or closing a sight opened by another source
}

static void testFreeMouseCombat() {
    Config c = Config::defaults();
    CHECK(c.freeCombat);
    InputModule m;
    m.initHeadless(legacyPitch(c));
    FakeGuest g;
    setupCamera(g, 0, .6283f);
    g.write32(CAM + 0x14c, 6);
    g.write32(0x4fb730, 1);
    // These are native animation predicates that used to swallow mouse motion.
    g.stubs[0xAAAA00] = [](const GuestCall&) { return 0u; };
    g.stubs[0x32cda8] = [](const GuestCall&) { return 1u; };
    FrameInput f;
    f.dx = 100;
    f.dy = -50;
    f.mouseDown = MBTN_LEFT | MBTN_RIGHT;
    m.beginFrame(f);
    CHECK(m.freeCombatAllowed(g, PLAYER, CAM));
    CHECK(m.afterCameraUpdateLook(g, PLAYER, LIM, false));
    NEAR(g.readF(CAM + 0x84), .1f, 1e-6);
    CHECK(g.calls.size() == 1 && g.calls[0].addr == c.addrTurnFn);
    NEAR(g.calls[0].f[0], -.2f, 1e-6);
    CHECK(!m.afterCameraUpdateLook(g, PLAYER, LIM, false));
    for (uint32_t flags : {1u, 2u, 0x4000u, 0x8000u}) {
        g.write32(PLAYER + 0x94, flags);
        CHECK(!m.freeCombatAllowed(g, PLAYER, CAM));
    }
    g.write32(PLAYER + 0x94, 0);
    g.write32(0x4fbafc, 1);
    CHECK(!m.freeCombatAllowed(g, PLAYER, CAM));
    g.write32(0x4fbafc, 0);
    g.write32(CAM + 0x48, 0x1000);
    CHECK(!m.freeCombatAllowed(g, PLAYER, CAM));
    g.write32(CAM + 0x48, 0);
    // Explicit targeting and alternate aiming retain native camera ownership,
    // even while free mouse combat is enabled.
    g.write32(PLAYER + 0x90, 0x4000);
    CHECK(!m.freeCombatAllowed(g, PLAYER, CAM));
    g.write32(PLAYER + 0x90, 0);
    g.write32(CAM + 0x48, 0x800);
    CHECK(!m.freeCombatAllowed(g, PLAYER, CAM));
    g.writeF(CAM + 0x94, 0);
    g.writeF(CAM + 0x98, 0);
    f = FrameInput{};
    f.dx = 20;
    f.dy = -10;
    f.mouseDown = MBTN_RIGHT;
    m.beginFrame(f);
    CHECK(m.afterCameraUpdateLook(g, PLAYER, LIM, false));
    NEAR(g.readF(CAM + 0x94), .02f, 1e-6);
    NEAR(g.readF(CAM + 0x98), -.04f, 1e-6);
    CHECK(g.read32(CAM + 0x48) == 0x800);
    g.write32(CAM + 0x48, 0);
    // Controller ownership survives releasing its targeting button; mouse resumes explicitly.
    f = FrameInput{};
    f.gamepad.connected = true;
    f.gamepad.buttons = Pad::R2;
    m.beginFrame(f);
    CHECK(!m.freeCombatActive());
    m.beginFrame(FrameInput{});
    CHECK(!m.freeCombatActive());
    f = keys({"W"});
    m.beginFrame(f);
    CHECK(m.freeCombatAllowed(g, PLAYER, CAM));
    m.setMenuActive(true);
    CHECK(!m.freeCombatActive());
    m.setMenuActive(false);
    m.setSuspended(true);
    CHECK(!m.freeCombatActive());
    m.setSuspended(false);
    m.beginFrame(f);
    m.beforePadMgrUpdate(g, true);
    CHECK(!m.freeCombatActive());
    m.beforePadMgrUpdate(g, false);
    m.beginFrame(f);
    CHECK(m.freeCombatActive());
    Config::parse("[mouse]\nfree_combat=0\n", c, nullptr);
    CHECK(!c.freeCombat);
    m.reloadConfig(legacyPitch(c));
    m.beginFrame(f);
    CHECK(!m.freeCombatActive());
}

static void testCameraFollowAndPitchGain() {
    Config c = Config::defaults();
    NEAR(c.cameraFollow, 5.0, 1e-6);
    NEAR(c.pitchViewGain, 1.8, 1e-6);
    c.sensX = c.sensY = 0.01f;
    InputModule m;
    m.initHeadless(c);
    FakeGuest g;
    setupCamera(g, 0, .6283f);
    g.write32(CAM + 0x14c, 6);
    g.write32(0x4fb730, 1);
    g.writeF(CAM + 0x210, 2.0f); // native rubber band strength
    g.writeF(CAM + 0x20c, 2.0f);
    const uint32_t camObj = 0x790000, playerHandle = 0x780000;
    g.write32(CAM + 0x148, camObj); // camera render object, position at +0x14
    g.write32(0x600000 + 0x4b0, playerHandle);
    g.write32(playerHandle + 0xfc, playerHandle + 0x100);
    g.write32(playerHandle + 0x100 + 0x10, PLAYER); // Rynn's transform: *(PLAYER+0x80) = HEAD, position at +0x14
    g.writeF(0x600000 + 0x24, 1.0f / 60.0f);        // tick dt
    FrameInput f;
    f.dy = -18;
    m.beginFrame(f);
    // Vertical counts are divided by the view gain of the plain on-foot camera: 18 counts up = 0.18 / 1.8 rad.
    CHECK(m.afterCameraUpdateLook(g, PLAYER, LIM, false));
    NEAR(g.readF(CAM + 0x84), 0.1, 1e-6);
    // Other camera branches keep the raw increment.
    g.writeF(CAM + 0x84, 0);
    g.writeF(CAM + 0x168, 0.5f);
    f.dy = -10;
    m.beginFrame(f);
    m.afterCameraUpdateLook(g, PLAYER, LIM, false);
    NEAR(g.readF(CAM + 0x84), 0.1, 1e-6);
    g.writeF(CAM + 0x168, 0);

    // The rate is set for the tick and restored afterwards; the first call only records Rynn's heading.
    g.writeF(HEAD + 0x20, 1.0f); // heading row 0 = +x
    g.writeF(HEAD + 0x28, 0.0f);
    g.writeF(camObj + 0x14, -1000.0f); // camera 1000 units behind her (at -x), 300 up
    g.writeF(camObj + 0x18, 300.0f);
    m.beginFrame(FrameInput{});
    m.beforeCameraFollow(g, CAM);
    NEAR(g.readF(CAM + 0x210), 5.0, 1e-6);
    g.writeF(CAM + 0x210, 7.9f); // the native easing step inside the tick
    m.beforeCameraFollow(g, CAM);
    NEAR(g.readF(CAM + 0x210), 5.0, 1e-6); // rewritten every tick
    NEAR(g.readF(camObj + 0x14), -1000.0, 1e-3);
    // She turns 90 degrees (row 0 -> +z): before the next tick the camera is carried round with her, so the
    // native easing has no swing left to do.
    g.writeF(HEAD + 0x20, 0.0f);
    g.writeF(HEAD + 0x28, 1.0f);
    g.writeF(HEAD + 0x14, 50.0f); // and moved; the pivot is her current position
    g.writeF(camObj + 0x14, -950.0f);
    m.beforeCameraFollow(g, CAM);
    NEAR(g.readF(camObj + 0x14), 50.0, 1e-2);
    NEAR(g.readF(camObj + 0x1c), 1000.0, 1e-2); // row 0 +x -> +z puts the camera at +z (game convention)
    NEAR(g.readF(camObj + 0x18), 300.0, 1e-6);  // height untouched
    // No turn: no change.
    m.beforeCameraFollow(g, CAM);
    NEAR(g.readF(camObj + 0x14), 50.0, 1e-2);
    NEAR(g.readF(camObj + 0x1c), 1000.0, 1e-2);
    // A one-tick gap in ownership keeps the orbit state: a turn made meanwhile is still carried (camera +z -> +x).
    m.setMenuActive(true);
    m.beginFrame(FrameInput{});
    m.beforeCameraFollow(g, CAM);
    m.setMenuActive(false);
    m.beginFrame(FrameInput{});
    g.writeF(HEAD + 0x20, -1.0f);
    g.writeF(HEAD + 0x28, 0.0f);
    m.beforeCameraFollow(g, CAM);
    NEAR(g.readF(camObj + 0x14), 50.0 + 1000.0, 1e-1);
    NEAR(g.readF(camObj + 0x1c), 0.0, 1e-1);
    // After a long pause the heading is re-recorded first: a turn made meanwhile is not replayed.
    m.setMenuActive(true);
    m.beginFrame(FrameInput{});
    for (int i = 0; i < 10; ++i)
        m.beforeCameraFollow(g, CAM);
    m.setMenuActive(false);
    m.beginFrame(FrameInput{});
    g.writeF(HEAD + 0x20, 1.0f);
    m.beforeCameraFollow(g, CAM);
    NEAR(g.readF(camObj + 0x14), 50.0 + 1000.0, 1e-1);
    // Looking up 0.1 (view 0.18 in the plain branch) carries the camera down round the anchor point by 0.18 rad.
    {
        g.writeF(HEAD + 0x14, 0.0f); // Rynn at the origin, anchor 240 up
        g.writeF(PLAYER + 0x520, 240.0f);
        g.writeF(CAM + 0x84, 0.0f);
        m.beforeCameraFollow(g, CAM); // sync the recorded heading / pitch
        g.writeF(camObj + 0x14, -1000.0f);
        g.writeF(camObj + 0x18, 540.0f);
        g.writeF(camObj + 0x1c, 0.0f);
        const float axes[9] = {0, 0, -1, 0, 1, 0, 1, 0, 0}; // columns: right (0,0,1), up (0,1,0), back (-1,0,0)
        for (int i = 0; i < 9; ++i)
            g.writeF(camObj + 0x20 + 4 * static_cast<uint32_t>(i), axes[i]);
        g.writeF(CAM + 0x84, 0.0f);
        m.beforeCameraFollow(g, CAM); // records pitch 0 (heading unchanged)
        NEAR(g.readF(camObj + 0x18), 540.0, 1e-2);
        g.writeF(CAM + 0x84, 0.1f);
        m.beforeCameraFollow(g, CAM);
        const double ox = g.readF(camObj + 0x14), oy = g.readF(camObj + 0x18) - 240.0, oz = g.readF(camObj + 0x1c);
        NEAR(std::sqrt(ox * ox + oy * oy + oz * oz), std::sqrt(1000.0 * 1000.0 + 300.0 * 300.0), 1e-2);
        NEAR(std::atan2(300.0, 1000.0) - std::atan2(oy, -ox), 0.18, 1e-4); // lowered by 0.18 rad
        NEAR(oz, 0.0, 1e-3);
    }
    // A collision push inside the tick resets 0x210 to 0.75 x strength; the next tick rewrites the rate.
    g.writeF(CAM + 0x210, 1.5f);
    m.beforeCameraFollow(g, CAM);
    NEAR(g.readF(CAM + 0x210), 5.0, 1e-6);
    // Losing ownership keeps the rate through a short gap (no one-tick hitch) and hands the native value (2.0)
    // back after kFollowReleaseTicks (10) camera ticks.
    auto checkRelease = [&](const char* why) {
        for (int i = 0; i < 9; ++i)
            m.beforeCameraFollow(g, CAM);
        const bool held = g.readF(CAM + 0x210) == 5.0f;
        m.beforeCameraFollow(g, CAM);
        const bool back = g.readF(CAM + 0x210) == 2.0f;
        if (!held || !back)
            std::printf("  camera follow release (%s): held=%d back=%d\n", why, held, back);
        CHECK(held && back);
    };
    const auto reown = [&] {
        m.beforeCameraFollow(g, CAM);
        CHECK(g.readF(CAM + 0x210) == 5.0f);
    };
    for (uint32_t flags : {0x20u, 0x80u, 0x100u, 0x20000u}) {
        reown();
        g.write32(CAM + 0x48, flags);
        checkRelease("camera flags");
        g.write32(CAM + 0x48, 0);
    }
    reown();
    g.write32(CAM + 0x14c, 5);
    checkRelease("camera mode");
    g.write32(CAM + 0x14c, 6);
    reown();
    g.write32(INNER + 0x10, CAM + 0x2000); // the context switched cameras (level change): never write to the old one
    for (int i = 0; i < 12; ++i)
        m.beforeCameraFollow(g, CAM);
    CHECK(g.readF(CAM + 0x210) == 5.0f);
    g.write32(INNER + 0x10, CAM);
    g.writeF(CAM + 0x210, 2.0f);
    reown();
    g.writeF(CAM + 0x21c, 0.5f);
    checkRelease("orientation blend timer");
    g.writeF(CAM + 0x21c, 0);
    reown();
    g.write32(0x4fbafc, 1);
    checkRelease("cut scene");
    g.write32(0x4fbafc, 0);
    g.write32(CAM + 0x1000 + 0x14c, 6); // a camera object that is not the context's player camera
    g.writeF(CAM + 0x1000 + 0x210, 2.0f);
    m.beforeCameraFollow(g, CAM + 0x1000);
    CHECK(g.readF(CAM + 0x1000 + 0x210) == 2.0f);
    reown();
    m.beforePadMgrUpdate(g, true);
    checkRelease("dragon");
    m.beforePadMgrUpdate(g, false);
    reown();
    f = FrameInput{};
    f.gamepad.connected = true;
    f.gamepad.buttons = Pad::CROSS;
    m.beginFrame(f);
    m.beginFrame(FrameInput{}); // an idle pad keeps controller ownership
    checkRelease("controller owner");
    m.beginFrame(keys({"W"}));
    reown();
    m.setMenuActive(true);
    m.beginFrame(FrameInput{});
    checkRelease("menu");
    m.setMenuActive(false);
    m.beginFrame(FrameInput{});
    reown();
    // camera_follow = 0 / pitch_view_gain = 1 restore the original behaviour.
    Config::parse("[mouse]\ncamera_follow=0\npitch_view_gain=1\n", c, nullptr);
    CHECK(c.cameraFollow == 0.0f && c.pitchViewGain == 1.0f);
    m.reloadConfig(c);
    m.beginFrame(FrameInput{});
    m.beforeCameraFollow(g, CAM);
    CHECK(g.readF(CAM + 0x210) == 2.0f); // switched off: handed back at once
    m.beforeCameraFollow(g, CAM);
    CHECK(g.readF(CAM + 0x210) == 2.0f);
    g.writeF(CAM + 0x84, 0);
    f = FrameInput{};
    f.dy = -10;
    m.beginFrame(f);
    m.afterCameraUpdateLook(g, PLAYER, LIM, false);
    NEAR(g.readF(CAM + 0x84), 0.1, 1e-6);
}

int main() {
    {
        InputModule basic;
        basic.setDirectLookAvailable(false);
        Config direct;
        direct.lookMode = LookMode::Direct;
        basic.initHeadless(direct);
        CHECK(basic.config().lookMode == LookMode::Stick);
        basic.reloadConfig(direct);
        basic.beginFrame(FrameInput{});
        CHECK(basic.config().lookMode == LookMode::Stick);
        basic.injectPadTap(Pad::START, 1);
        basic.beginFrame(FrameInput{});
        uint8_t sample[32];
        CHECK(basic.fillPadSample(0, 0, sample));
        CHECK((sample[2] & 8) == 0);
        basic.setSuspended(true);
        basic.beginFrame(keys({"ESC", "ENTER"}));
        CHECK(basic.fillPadSample(0, 0, sample));
        CHECK(sample[2] == 0xff && sample[3] == 0xff);
        basic.setSuspended(false);
        basic.beginFrame(FrameInput{});
        CHECK(basic.fillPadSample(0, 0, sample));
        CHECK(sample[2] == 0xff && sample[3] == 0xff);
        InputModule advanced;
        advanced.initHeadless(direct);
        advanced.reloadConfig(direct);
        advanced.beginFrame(FrameInput{});
        CHECK(advanced.config().lookMode == LookMode::Direct);
    }
    {
        InputModule external;
        external.initHeadless(Config{});
        FrameInput frame;
        external.beginExternalFrame(frame);
        external.beginExternalFrame(frame);
        CHECK(external.frameNo() == 2);
        external.beginFrame();
        CHECK(external.frameNo() == 3);
    }
    testRefreshAlignedMouseWindow();
    testCameraFollowAndPitchGain();
    testFreeMouseCombat();
    testHeldMouseBowSight();
    testRightClickAfterInventory();
    testEscClosesInventoryOnly();
    testAlternateMouseAimAndMomentum();
    testAimMotionNotificationPulse();
    testMovingTargetAimOwnership();
    testNativePitchVetoes();
    testStableAimPitch();
    testUncappedMouseAccumulator();
    testHeldOverKeys();
    testMouseDisplacementOwnership();
    testScriptLegacy();
    testScriptDualWieldSetting();
    testScriptScreenSync();
    testScriptElapsed();
    testScriptElapsedSync();
    testScriptElapsedSamples();
    testScriptElapsedRates();
    testScriptClockErrors();
    testOwnershipFollowup();
    testLookAndCombatFollowup();
    testConfig();
    testStickCurve();
    testPadSamples();
    testMouseStickMode();
    testJitterDither();
    testCamera();
    testControlsV2();
    testCommands();
    testWalkControl();
    testExternalCapture();
    testMenuAndCapture();
    testReloadAndPause();
    testMenuMouse();
    testMenuMouseModule();
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
