// Controller combat layout (issue #18): [gamepad] directional_attack (left stick + Attack with a held modifier),
// stick_click_crouch (L3 = the scheme's Crouch) and quick_bar_stick (the right stick steps the held Quick Inventory
// bar). Plain asserts, exit code != 0 on failure.
#include "input/DrakanInput.h"
#include "input/QuickSlots.h"
#include <cstdio>
#include <initializer_list>
#include <string>

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

namespace {

constexpr uint16_t kDirections = Pad::UP | Pad::DOWN | Pad::LEFT | Pad::RIGHT;
constexpr PadSchemeButtons kSchemeA{Pad::SQUARE, Pad::CROSS};
constexpr PadSchemeButtons kSchemeC{Pad::R1, Pad::L2};

FrameInput pad(uint16_t buttons, float lx = 0.0f, float ly = 0.0f, float seconds = 1.0f / 60.0f) {
    FrameInput f;
    f.gamepad.connected = true;
    f.gamepad.buttons = buttons;
    f.gamepad.lx = lx;
    f.gamepad.ly = ly;
    f.frameSeconds = seconds;
    return f;
}

struct Synth {
    Config cfg = Config::defaults();
    PadSynthState st;
    PadSchemeButtons scheme = kSchemeA;
    bool melee = true;
    bool menu = false;
    PadSample operator()(const FrameInput& f) {
        return synthesizePad(cfg, f, menu ? cfg.menu : cfg.game, menu, true, false, st, melee, scheme);
    }
};

bool centered(const PadSample& s) {
    return s.lx == 0x80 && s.ly == 0x80;
}

void testConfig() {
    const Config d = Config::defaults();
    CHECK(d.padDirectionalAttack == DirectionalAttackMode::Modifier);
    CHECK(d.padDirectionalModifier == Pad::L2);
    CHECK(d.padStickCrouch && d.padQuickBarStick);
    Config c = Config::defaults();
    std::string w;
    Config::parse("[gamepad]\ndirectional_attack = tap\ndirectional_modifier = R2\nstick_click_crouch = 0\n"
                  "quick_bar_stick = 0\n",
                  c, &w);
    CHECK(w.empty());
    CHECK(c.padDirectionalAttack == DirectionalAttackMode::Tap);
    CHECK(c.padDirectionalModifier == Pad::R2);
    CHECK(!c.padStickCrouch && !c.padQuickBarStick);
    Config none = Config::defaults();
    Config::parse("[gamepad]\ndirectional_modifier = NONE\n", none, nullptr);
    CHECK(none.padDirectionalModifier == 0);
    Config bad = Config::defaults();
    w.clear();
    Config::parse("[gamepad]\ndirectional_attack = sometimes\n", bad, &w);
    CHECK(bad.padDirectionalAttack == DirectionalAttackMode::Modifier && !w.empty());
}

void testStickDirections() {
    CHECK(padStickDirections(0.0f, 0.0f) == 0);
    CHECK(padStickDirections(0.0f, 1.0f) == Pad::UP);
    CHECK(padStickDirections(0.0f, -1.0f) == Pad::DOWN);
    CHECK(padStickDirections(-0.8f, 0.0f) == Pad::LEFT);
    CHECK(padStickDirections(0.7f, 0.7f) == (Pad::UP | Pad::RIGHT));
    CHECK(padStickDirections(0.3f, 0.4f) == 0); // a light push is not a direction
}

void testModifier() {
    Synth s;
    // A plain attack while running forward: the attack goes through at once and the stick keeps moving Rynn.
    for (int i = 0; i < 3; ++i) {
        const PadSample a = s(pad(Pad::SQUARE, 0.0f, 1.0f));
        CHECK(a.buttons == Pad::SQUARE && !centered(a));
    }
    s(pad(0));
    // Holding L2 + stick + Attack: one neutral preparation sample, then Attack + the direction, stick centered.
    const float lx[] = {0.0f, 0.0f, -1.0f, 1.0f};
    const float ly[] = {1.0f, -1.0f, 0.0f, 0.0f};
    const uint16_t bit[] = {Pad::UP, Pad::DOWN, Pad::LEFT, Pad::RIGHT};
    for (int i = 0; i < 4; ++i) {
        s(pad(Pad::L2, lx[i], ly[i]));
        const PadSample prep = s(pad(Pad::L2 | Pad::SQUARE, lx[i], ly[i]));
        CHECK(prep.buttons == Pad::L2 && centered(prep));
        const PadSample hit = s(pad(Pad::L2 | Pad::SQUARE, lx[i], ly[i]));
        CHECK(hit.buttons == (Pad::L2 | Pad::SQUARE | bit[i]) && centered(hit));
        // Releasing the modifier mid-swing keeps the latched move.
        const PadSample held = s(pad(Pad::SQUARE, lx[i], ly[i]));
        CHECK(held.buttons == (Pad::SQUARE | bit[i]) && centered(held));
        s(pad(0));
    }
    // L2 + Attack without a stick push is an ordinary attack; the modifier keeps its own meaning (passes through).
    CHECK(s(pad(Pad::L2 | Pad::SQUARE)).buttons == (Pad::L2 | Pad::SQUARE));
    s(pad(0));
    // A short tap is buffered across the preparation sample.
    CHECK(s(pad(Pad::L2 | Pad::SQUARE, 0.0f, 1.0f)).buttons == Pad::L2);
    CHECK(s(pad(Pad::L2, 0.0f, 1.0f)).buttons == (Pad::L2 | Pad::SQUARE | Pad::UP));
    s(pad(0));
    // Scheme C: Attack is R1, and its D-pad moves work the same way.
    Synth c;
    c.scheme = kSchemeC;
    c(pad(Pad::L2, 1.0f, 0.0f));
    CHECK(c(pad(Pad::L2 | Pad::R1, 1.0f, 0.0f)).buttons == Pad::L2);
    CHECK(c(pad(Pad::L2 | Pad::R1, 1.0f, 0.0f)).buttons == (Pad::L2 | Pad::R1 | Pad::RIGHT));
    // No modifier configured: never directional.
    Synth n;
    n.cfg.padDirectionalModifier = 0;
    CHECK(n(pad(Pad::SQUARE, 0.0f, 1.0f)).buttons == Pad::SQUARE);
}

void testTapAlwaysOff() {
    Synth t;
    t.cfg.padDirectionalAttack = DirectionalAttackMode::Tap;
    for (int i = 0; i < 30; ++i)
        t(pad(0, 0.0f, 1.0f)); // running for half a second: a plain attack
    CHECK(t(pad(Pad::SQUARE, 0.0f, 1.0f)).buttons == Pad::SQUARE);
    t(pad(0));
    // Flicking the stick right with the attack is a special move.
    CHECK(t(pad(Pad::SQUARE, 1.0f, 0.0f)).buttons == 0);
    CHECK(t(pad(Pad::SQUARE, 1.0f, 0.0f)).buttons == (Pad::SQUARE | Pad::RIGHT));
    t(pad(0));

    Synth a;
    a.cfg.padDirectionalAttack = DirectionalAttackMode::Always;
    CHECK(a(pad(Pad::SQUARE, 0.0f, -1.0f)).buttons == 0);
    CHECK(a(pad(Pad::SQUARE, 0.0f, -1.0f)).buttons == (Pad::SQUARE | Pad::DOWN));

    Synth o;
    o.cfg.padDirectionalAttack = DirectionalAttackMode::Off;
    const PadSample off = o(pad(Pad::L2 | Pad::SQUARE, 0.0f, 1.0f));
    CHECK(off.buttons == (Pad::L2 | Pad::SQUARE) && !centered(off));
}

void testNotMeleeMenuMounted() {
    // A bow, a summon or Arokh: the modifier changes nothing.
    Synth b;
    b.melee = false;
    CHECK(b(pad(Pad::L2 | Pad::SQUARE, 0.0f, 1.0f)).buttons == (Pad::L2 | Pad::SQUARE));
    Synth mounted;
    mounted.scheme = {};
    const PadSample m = mounted(pad(Pad::L2 | Pad::SQUARE | Pad::L3, 0.0f, 1.0f));
    CHECK(m.buttons == (Pad::L2 | Pad::SQUARE | Pad::L3) && !centered(m));
    // Menus pass the pad through untouched.
    Synth menu;
    menu.menu = true;
    const PadSample inMenu = menu(pad(Pad::L2 | Pad::SQUARE | Pad::L3, 0.0f, 1.0f));
    CHECK((inMenu.buttons & kDirections) == 0 && (inMenu.buttons & Pad::CROSS) == 0);
}

void testStickCrouch() {
    Synth s;
    CHECK(s(pad(Pad::L3, 0.0f, 1.0f)).buttons == (Pad::L3 | Pad::CROSS));
    Synth c;
    c.scheme = kSchemeC;
    CHECK(c(pad(Pad::L3)).buttons == (Pad::L3 | Pad::L2));
    Synth off;
    off.cfg.padStickCrouch = false;
    CHECK(off(pad(Pad::L3)).buttons == Pad::L3);
}

int steps(uint16_t bits, uint16_t& last, int& count) {
    if ((bits & ~last) != 0)
        ++count;
    last = bits;
    return count;
}

void testQuickBarStick() {
    constexpr float dt = 1.0f / 60.0f;
    QuickBarStickStepper q;
    // Inactive: nothing, whatever the stick does.
    CHECK(q.update(false, 1.0f, dt) == 0);
    // A flick right: exactly one Right step, pressed kPressFrames samples.
    uint16_t last = 0;
    int n = 0;
    int pressed = 0;
    for (int i = 0; i < 10; ++i) {
        const uint16_t b = q.update(true, i < 6 ? 1.0f : 0.0f, dt);
        CHECK((b & ~Pad::RIGHT) == 0);
        pressed += b ? 1 : 0;
        steps(b, last, n);
    }
    CHECK(n == 1 && pressed == QuickBarStickStepper::kPressFrames);
    // A flick left steps left.
    last = 0;
    n = 0;
    for (int i = 0; i < 10; ++i) {
        const uint16_t b = q.update(true, i < 4 ? -1.0f : 0.0f, dt);
        CHECK((b & ~Pad::LEFT) == 0);
        steps(b, last, n);
    }
    CHECK(n == 1);
    // Holding right for one second repeats: one step, then after the delay one every kRepeatRate.
    q.reset();
    last = 0;
    n = 0;
    for (int i = 0; i < 60; ++i)
        steps(q.update(true, 1.0f, dt), last, n);
    const int expected =
        1 + 1 + static_cast<int>((1.0f - QuickBarStickStepper::kRepeatDelay - dt) / QuickBarStickStepper::kRepeatRate);
    CHECK(n >= expected - 1 && n <= expected + 1);
    // Every step is a fresh edge: no two pressed runs touch.
    q.reset();
    uint16_t prev = 0;
    bool touching = false;
    int run = 0;
    for (int i = 0; i < 120; ++i) {
        const uint16_t b = q.update(true, 1.0f, dt);
        if (b && prev)
            ++run;
        else
            run = b ? 1 : 0;
        touching |= run > QuickBarStickStepper::kPressFrames;
        prev = b;
    }
    CHECK(!touching);
    // Hysteresis: easing back to 0.4 keeps the push (no new step), dropping below kOff ends it.
    q.reset();
    last = 0;
    n = 0;
    for (int i = 0; i < 8; ++i)
        steps(q.update(true, i < 2 ? 0.9f : 0.4f, dt), last, n);
    CHECK(n == 1);
    q.update(true, 0.0f, dt);
    for (int i = 0; i < 8; ++i)
        steps(q.update(true, 0.9f, dt), last, n);
    CHECK(n == 2);
    // Turning inactive mid-press drops the press at once.
    q.reset();
    CHECK(q.update(true, 1.0f, dt) == Pad::RIGHT);
    CHECK(q.update(false, 1.0f, dt) == 0);
}

void testGestureHolding() {
    QuickInventoryGesture g;
    CHECK(!g.holding());
    g.update(Pad::R1, Pad::R1, 0.0f);
    CHECK(!g.holding());
    g.update(Pad::R1, Pad::R1, 0.3f);
    CHECK(g.holding());
    g.update(0, Pad::R1, 0.01f);
    CHECK(!g.holding());
}

} // namespace

int main() {
    testConfig();
    testStickDirections();
    testModifier();
    testTapAlwaysOff();
    testNotMeleeMenuMounted();
    testStickCrouch();
    testQuickBarStick();
    testGestureHolding();
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
