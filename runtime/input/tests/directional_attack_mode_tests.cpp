// [keyboard] directional_attack: which keyboard / mouse attacks become directional special moves, and that a plain
// attack never swallows keyboard movement (issue #1 follow-up). Plain asserts, exit code != 0 on failure.
#include "input/DrakanInput.h"
#include <cstdio>
#include <initializer_list>
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

namespace {

constexpr uint16_t kDirections = Pad::UP | Pad::DOWN | Pad::LEFT | Pad::RIGHT;

FrameInput keys(std::initializer_list<const char*> names, uint8_t mouse = 0, float seconds = 1.0f / 60.0f) {
    FrameInput f;
    for (const char* n : names) {
        int d;
        if (keyFromName(n, &d))
            f.keyDown[static_cast<size_t>(d)] = true;
    }
    f.mouseDown = mouse;
    f.frameSeconds = seconds;
    return f;
}

Config withMode(DirectionalAttackMode mode) {
    Config c = Config::defaults();
    c.directionalAttack = mode;
    return c;
}

struct Synth {
    Config cfg;
    PadSynthState st;
    bool melee = true; // native Rynn holds a melee weapon on foot
    PadSample operator()(const FrameInput& f) { return synthesizePad(cfg, f, cfg.game, false, true, false, st, melee); }
};

bool moving(const PadSample& s) {
    return s.lx != 128 || s.ly != 128;
}

// A plain attack: Square at once, no D-pad, keyboard movement keeps its full vector on every sample.
void checkPlainAttack(Synth& s, std::initializer_list<const char*> move) {
    const PadSample walking = s(keys(move));
    CHECK(moving(walking));
    for (int i = 0; i < 4; ++i) {
        const PadSample a = s(keys(move, MBTN_LEFT));
        CHECK(a.buttons == Pad::SQUARE);
        CHECK(a.lx == walking.lx && a.ly == walking.ly);
    }
    const PadSample after = s(keys(move));
    CHECK(after.buttons == 0 && after.lx == walking.lx && after.ly == walking.ly);
    s(FrameInput{});
}

// A directional attack: one neutral preparation sample, then Square + the direction with the keyboard stick centered.
void checkDirectionalAttack(Synth& s, std::initializer_list<const char*> held, uint16_t bit) {
    const PadSample prepare = s(keys(held, MBTN_LEFT));
    CHECK(prepare.buttons == 0 && !moving(prepare));
    const PadSample attack = s(keys(held, MBTN_LEFT));
    CHECK(attack.buttons == (Pad::SQUARE | bit));
    CHECK(!moving(attack));
    s(FrameInput{});
}

void testConfigText() {
    const Config d = Config::defaults();
    CHECK(d.directionalAttack == DirectionalAttackMode::Modifier);
    int lalt = -1;
    CHECK(keyFromName("LALT", &lalt));
    bool bound = false;
    for (const Binding& b : d.game)
        if (b.act == Binding::DirAttack)
            bound = b.src == Binding::Key && b.code == lalt;
    CHECK(bound); // LALT = DIRECTIONAL_ATTACK in the shipped file

    for (const auto& [text, mode] : std::initializer_list<std::pair<const char*, DirectionalAttackMode>>{
             {"modifier", DirectionalAttackMode::Modifier},
             {"TAP", DirectionalAttackMode::Tap},
             {"always", DirectionalAttackMode::Always},
             {"off", DirectionalAttackMode::Off}}) {
        Config c = Config::defaults();
        std::string w;
        Config::parse(std::string("[keyboard]\ndirectional_attack = ") + text + "\n", c, &w);
        CHECK(c.directionalAttack == mode);
        CHECK(w.empty());
        DirectionalAttackMode back;
        CHECK(directionalAttackModeFromName(directionalAttackModeName(mode), &back) && back == mode);
    }
    Config bad = Config::defaults();
    std::string w;
    Config::parse("[keyboard]\ndirectional_attack = sometimes\n", bad, &w);
    CHECK(bad.directionalAttack == DirectionalAttackMode::Modifier && !w.empty());

    // Files that predate the modifier get Left Alt, unless they use it, unbind it or bind the action elsewhere.
    auto dirSources = [](const Config& c) {
        std::map<int, int> n;
        for (const Binding& b : c.game)
            if (b.act == Binding::DirAttack)
                ++n[b.code];
        return n;
    };
    Config old = Config::defaults();
    Config::parse("[bindings]\nW = LSTICK_UP\nMOUSE1 = SQUARE\n", old, nullptr);
    CHECK(dirSources(old).size() == 1 && dirSources(old).count(lalt));
    Config mine = Config::defaults();
    Config::parse("[bindings]\nLALT = CROSS\n", mine, nullptr);
    CHECK(dirSources(mine).empty());
    Config none = Config::defaults();
    Config::parse("[bindings]\nLALT = NONE\n", none, nullptr);
    CHECK(dirSources(none).empty());
    Config moved = Config::defaults();
    Config::parse("[bindings]\nX = DIRECTIONAL_ATTACK\n", moved, nullptr);
    int x = -1;
    keyFromName("X", &x);
    CHECK(dirSources(moved).size() == 1 && dirSources(moved).count(x));
}

void testModifierMode() {
    Synth s{withMode(DirectionalAttackMode::Modifier)};
    // The tester's case: walking or strafing while swinging just works.
    checkPlainAttack(s, {"W"});
    checkPlainAttack(s, {"A"});
    checkPlainAttack(s, {"D"});
    checkPlainAttack(s, {"S"});
    checkPlainAttack(s, {"W", "D"});
    // Left Alt + direction + attack is the special move, in every direction, whether Alt or the key came first.
    const char* move[] = {"W", "S", "A", "D"};
    const uint16_t bits[] = {Pad::UP, Pad::DOWN, Pad::LEFT, Pad::RIGHT};
    for (int i = 0; i < 4; ++i) {
        s(keys({move[i]}));
        s(keys({move[i], "LALT"}));
        checkDirectionalAttack(s, {move[i], "LALT"}, bits[i]);
    }
    checkDirectionalAttack(s, {"W", "LALT"}, Pad::UP); // Alt, W and the click on the same sample
    // Alt alone never moves anything or adds buttons; Alt + attack without movement is an ordinary attack.
    CHECK(s(keys({"LALT"})).buttons == 0);
    CHECK(s(keys({"LALT"}, MBTN_LEFT)).buttons == Pad::SQUARE);
    s(FrameInput{});
    // The choice is made on the press: releasing Alt during the swing does not drop the latched move, and pressing
    // it during a plain swing does not turn that swing into one.
    s(keys({"W", "LALT"}, MBTN_LEFT));
    CHECK(s(keys({"W"}, MBTN_LEFT)).buttons == (Pad::SQUARE | Pad::UP));
    s(FrameInput{});
    const PadSample plain = s(keys({"W"}, MBTN_LEFT));
    CHECK(plain.buttons == Pad::SQUARE && moving(plain));
    const PadSample late = s(keys({"W", "LALT"}, MBTN_LEFT));
    CHECK(late.buttons == Pad::SQUARE && moving(late));
    s(FrameInput{});
    // The next press with Alt still held is directional again.
    checkDirectionalAttack(s, {"W", "LALT"}, Pad::UP);
    // A short click with Alt is buffered for the preparation sample like before.
    FrameInput tap = keys({"W", "LALT"});
    tap.mousePressed = tap.mouseReleased = MBTN_LEFT;
    CHECK(s(tap).buttons == 0);
    CHECK(s(FrameInput{}).buttons == (Pad::SQUARE | Pad::UP));
    s(FrameInput{});
    // A rebound modifier follows the binding.
    Config custom = withMode(DirectionalAttackMode::Modifier);
    Config::parse("[bindings]\nW = LSTICK_UP\nMOUSE1 = SQUARE\nMOUSE4 = DIRECTIONAL_ATTACK\nLALT = NONE\n", custom,
                  nullptr);
    Synth r{custom};
    checkPlainAttack(r, {"W"});
    CHECK(r(keys({"W", "LALT"}, MBTN_LEFT)).buttons == Pad::SQUARE);
    r(FrameInput{});
    const PadSample prep = r(keys({"W"}, MBTN_LEFT | MBTN_X1));
    CHECK(prep.buttons == 0);
    CHECK(r(keys({"W"}, MBTN_LEFT | MBTN_X1)).buttons == (Pad::SQUARE | Pad::UP));
}

void testTapMode() {
    Synth s{withMode(DirectionalAttackMode::Tap)};
    // Held movement (longer than the window) gives a plain, moving attack.
    for (int i = 0; i < 30; ++i)
        s(keys({"W"}));
    checkPlainAttack(s, {"W"});
    // The key pressed together with the click, or just before it, is a special move.
    checkDirectionalAttack(s, {"D"}, Pad::RIGHT);
    s(keys({"A"}));
    s(keys({"A"}));
    checkDirectionalAttack(s, {"A"}, Pad::LEFT);
    // Just too late: the window has passed.
    s(keys({"S"}));
    s(keys({"S"}, 0, kDirectionalTapSeconds + 0.01f));
    const PadSample late = s(keys({"S"}, MBTN_LEFT));
    CHECK(late.buttons == Pad::SQUARE && moving(late));
    s(FrameInput{});
    // Changing direction while walking counts as a fresh tap.
    for (int i = 0; i < 30; ++i)
        s(keys({"W"}));
    checkDirectionalAttack(s, {"W", "D"}, Pad::UP | Pad::RIGHT);
}

void testAlwaysAndOff() {
    Synth a{withMode(DirectionalAttackMode::Always)};
    for (int i = 0; i < 30; ++i)
        a(keys({"W"}));
    checkDirectionalAttack(a, {"W"}, Pad::UP); // the 2026-11-05 behaviour
    Synth o{withMode(DirectionalAttackMode::Off)};
    checkPlainAttack(o, {"W"});
    CHECK(o(keys({"W", "LALT"}, MBTN_LEFT)).buttons == Pad::SQUARE);
    o(FrameInput{});
    CHECK(o(keys({"UP"}, MBTN_LEFT)).buttons == (Pad::SQUARE | Pad::UP)); // arrows stay the explicit D-pad
}

void testNotMeleeAndController() {
    // Without a drawn melee weapon (bow, mounted, summon) the modifier changes nothing either.
    Synth s{withMode(DirectionalAttackMode::Modifier)};
    s.melee = false;
    const PadSample bow = s(keys({"W", "LALT"}, MBTN_LEFT));
    CHECK(bow.buttons == Pad::SQUARE && moving(bow));
    // Controller input merges unchanged: physical Square with keyboard W keeps moving, D-pad stays the pad's own.
    Synth c{withMode(DirectionalAttackMode::Modifier)};
    FrameInput f = keys({"W"});
    f.gamepad.connected = true;
    f.gamepad.buttons = Pad::SQUARE | Pad::LEFT;
    const PadSample pad = c(f);
    CHECK(pad.buttons == (Pad::SQUARE | Pad::LEFT) && moving(pad));
    // Menus never see a projected attack: the menu layer's own W = DPAD_UP passes through unchanged.
    Synth m{withMode(DirectionalAttackMode::Modifier)};
    const PadSample menu =
        synthesizePad(m.cfg, keys({"W", "LALT"}, MBTN_LEFT), m.cfg.menu, true, true, false, m.st, true);
    CHECK((menu.buttons & kDirections) == Pad::UP && (menu.buttons & Pad::SQUARE) == 0);
}

} // namespace

int main() {
    testConfigText();
    testModifierMode();
    testTapMode();
    testAlwaysAndOff();
    testNotMeleeAndController();
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
