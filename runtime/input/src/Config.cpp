#include "input/Config.h"
#include "input/FrameInput.h"
#include "input/IniSchema.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "common/Text.h"

namespace drakan_input {
namespace {

using drakan::common::asciiUpper;
using drakan::common::trim;

// ---- scalar keys: where each key of the schema (input/IniSchema.h) lands in Config --------------------------------
// The schema has already validated the value (ini::parseValue), so an entry only stores it.

// ini Choice values, in the order of input/IniSchema.cpp.
static_assert(static_cast<int>(LookMode::Direct) == 0 && static_cast<int>(LookMode::Stick) == 1);
static_assert(static_cast<int>(CallAbi::FprFloat) == 0 && static_cast<int>(CallAbi::GprBits) == 1);
static_assert(static_cast<int>(DirectionalAttackMode::Modifier) == 0 &&
              static_cast<int>(DirectionalAttackMode::Off) == 3);
static_assert(static_cast<int>(GamepadLayout::Auto) == 0 && static_cast<int>(GamepadLayout::TwinStick) == 2);
constexpr int kBowAimNative = 1, kBowAimModern = 2; // 0 = ootf
constexpr int kDragonModeStick = 1;                 // 0 = mouse
constexpr int kInventoryClickSquare = 0;            // 1 = cross

using ApplyFn = void (*)(Config&, const ini::Value&);
struct ConfigField {
    const char* section;
    const char* key;
    ApplyFn apply;
};

// FIELD(section, key, statement): `statement` stores the parsed value `v` into the Config `c`.
#define FIELD(section, key, statement) {section, key, [](Config& c, const ini::Value& v) { statement; }}
const ConfigField kFields[] = {
    // [general]
    FIELD("general", "capture_mouse", c.captureMouse = v.b),
    FIELD("general", "release_on_esc", c.releaseOnEsc = v.b),
    FIELD("general", "toggle_key", c.toggleKeyDik = v.dik), // -1 = NONE
    FIELD("general", "look_mode", c.lookMode = static_cast<LookMode>(v.choice)),
    FIELD("general", "controls_version", c.controlsVersion = static_cast<int>(v.i)),
    // [mouse]
    FIELD("mouse", "sensitivity_x", c.sensX = v.f),
    FIELD("mouse", "sensitivity_y", c.sensY = v.f),
    FIELD("mouse", "invert_x", c.invertX = v.b),
    FIELD("mouse", "invert_y", c.invertY = v.b),
    FIELD("mouse", "pitch_hold_ms", c.pitchHoldMs = v.f),
    FIELD("mouse", "pitch_persist", c.pitchPersist = v.b),
    FIELD("mouse", "free_combat", c.freeCombat = v.b),
    FIELD("mouse", "camera_follow", c.cameraFollow = v.f),
    FIELD("mouse", "camera_lag_in_place", c.cameraLagInPlace = v.b),
    FIELD("mouse", "pitch_view_gain", c.pitchViewGain = v.f),
    FIELD("mouse", "bow_draw", c.bowDrawPose = v.b),
    FIELD("mouse", "bow_aim", c.bowAimModern = v.choice == kBowAimModern; c.bowAimNative = v.choice == kBowAimNative),
    FIELD("mouse", "bow_zoom", c.bowZoom = v.f),
    FIELD("mouse", "dragon_mode", c.dragonStick = v.choice == kDragonModeStick),
    FIELD("mouse", "dragon_sensitivity", c.dragonScale = v.f),
    FIELD("mouse", "dragon_camera_carry", c.dragonCameraCarry = v.b),
    FIELD("mouse", "dragon_smoothing", c.dragonSmoothingMs = v.f),
    // The two nose limits are magnitudes: up is stored positive, down negative, whatever sign the file writes.
    FIELD("mouse", "dragon_pitch_up", c.dragonPitchMax = std::fabs(v.f)),
    FIELD("mouse", "dragon_pitch_down", c.dragonPitchMin = -std::fabs(v.f)),
    FIELD("mouse", "yaw_sign", c.yawSign = v.f),
    FIELD("mouse", "pitch_sign", c.pitchSign = v.f),
    FIELD("mouse", "stick_full_counts", c.stickFullCounts = v.f),
    FIELD("mouse", "stick_sign_x", c.stickSignX = v.f),
    FIELD("mouse", "stick_sign_y", c.stickSignY = v.f),
    // [keyboard]
    FIELD("keyboard", "walk_scale", c.walkScale = v.f),
    FIELD("keyboard", "toggle_run", c.toggleRun = v.b),
    FIELD("keyboard", "strafe", c.strafeKeys = v.b),
    FIELD("keyboard", "dragon_strafe", c.dragonStrafe = v.b),
    FIELD("keyboard", "curve_compensation", c.curveCompensation = v.b),
    FIELD("keyboard", "directional_attack", c.directionalAttack = static_cast<DirectionalAttackMode>(v.choice)),
    // [gamepad]
    FIELD("gamepad", "layout", c.gamepadLayout = static_cast<GamepadLayout>(v.choice)),
    FIELD("gamepad", "enabled", c.gamepadEnabled = v.b),
    FIELD("gamepad", "deadzone", c.gamepadDeadzone = v.f),
    FIELD("gamepad", "debug_menu_combo", c.debugMenuCombo = v.pad),
    // [menu]
    FIELD("menu", "mouse", c.menuMouse = v.b),
    FIELD("menu", "inventory_mouse", c.inventoryMouse = v.b),
    FIELD("menu", "inventory_click", c.inventoryClickSquare = v.choice == kInventoryClickSquare),
    FIELD("menu", "draw_cursor", c.menuDrawCursor = v.b),
    FIELD("menu", "cursor_idle_ms", c.menuCursorIdleMs = v.f),
    FIELD("menu", "map_view", c.mapView = v.b),
    FIELD("menu", "inventory_strip", c.inventoryStrip = v.b),
    FIELD("menu", "cursor", c.menuCursor = v.b),
    FIELD("menu", "clicks", c.menuClicks = v.b),
    // [advanced]
    FIELD("advanced", "check_turn_gate", c.checkTurnGate = v.b),
    FIELD("advanced", "turn_call_abi", c.turnCallAbi = static_cast<CallAbi>(v.choice)),
    FIELD("advanced", "addr_ctx_ptr", c.addrCtxPtr = static_cast<uint32_t>(v.i)),
    FIELD("advanced", "addr_gui_ptr", c.addrGuiPtr = static_cast<uint32_t>(v.i)),
    FIELD("advanced", "addr_screen_ptr", c.addrScreenPtr = static_cast<uint32_t>(v.i)),
    FIELD("advanced", "addr_padmgr", c.addrPadMgr = static_cast<uint32_t>(v.i)),
    FIELD("advanced", "addr_turn_fn", c.addrTurnFn = static_cast<uint32_t>(v.i)),
    FIELD("advanced", "addr_turn_upvec", c.addrTurnUpVec = static_cast<uint32_t>(v.i)),
    FIELD("advanced", "addr_invoke_callback", c.addrInvokeCallback = static_cast<uint32_t>(v.i)),
    FIELD("advanced", "addr_timer_ms", c.addrTimerMs = static_cast<uint32_t>(v.i)),
    FIELD("advanced", "gui_vtable_offset", c.guiVtableOffset = static_cast<uint32_t>(v.i)),
    FIELD("advanced", "addr_active_screen", c.addrActiveScreenVar = static_cast<uint32_t>(v.i)),
    FIELD("advanced", "addr_mouse_callback", c.addrMouseCallback = static_cast<uint32_t>(v.i)),
    FIELD("advanced", "addr_ui_entry", c.addrUiEntry = static_cast<uint32_t>(v.i)),
    FIELD("advanced", "addr_captured_widget", c.addrCapturedWidgetVar = static_cast<uint32_t>(v.i)),
    FIELD("advanced", "addr_stick_mode_flag", c.addrStickModeFlag = static_cast<uint32_t>(v.i)),
    FIELD("advanced", "addr_stick_map", c.addrStickMap = static_cast<uint32_t>(v.i)),
};
#undef FIELD

const ConfigField* findField(const ini::KeySpec& spec) {
    for (const auto& f : kFields)
        if (std::strcmp(f.section, spec.section) == 0 && std::strcmp(f.key, spec.key) == 0)
            return &f;
    return nullptr;
}

// One "key = value" line of a scalar section ([general] .. [advanced]). Returns the warning, empty when it applied.
std::string applyScalar(Config& c, const std::string& section, const std::string& key, const std::string& value) {
    if (!ini::isScalarSection(section))
        return "key outside a known section: " + key;
    const ini::KeySpec* spec = ini::findKey(section, key);
    if (!spec)
        return "unknown key " + key;
    ini::Value v;
    std::string why;
    if (!ini::parseValue(*spec, value, v, &why))
        return why; // the key keeps its previous value
    if (const ConfigField* f = findField(*spec))
        f->apply(c, v);
    else if (spec->type != ini::Type::Ignored)
        return "schema key without a Config field: " + key; // a programming error, caught by input_ini_schema_test
    return {};
}

// ---- [bindings] / [menu_bindings] -----------------------------------------------------------------------------------
bool parseSource(const std::string& n, Binding& b) {
    std::string u = asciiUpper(n);
    if (u.size() == 6 && u.rfind("MOUSE", 0) == 0 && u[5] >= '1' && u[5] <= '5') {
        b.src = Binding::Mouse;
        b.code = 1 << (u[5] - '1');
        return true;
    }
    if (u == "WHEELUP") {
        b.src = Binding::WheelUp;
        return true;
    }
    if (u == "WHEELDOWN") {
        b.src = Binding::WheelDown;
        return true;
    }
    int d;
    if (keyFromName(u, &d)) {
        b.src = Binding::Key;
        b.code = d;
        return true;
    }
    return false;
}
// "ACT:0x17" / "CMD:0x400a": the id after the prefix (any integer spelling).
bool parseId(const std::string& digits, uint32_t* id) {
    long long v = 0;
    if (!ini::parseInt(digits, &v))
        return false;
    *id = static_cast<uint32_t>(v);
    return true;
}
bool parseAction(const std::string& a, Binding& b) {
    std::string u = asciiUpper(a);
    uint16_t m;
    if (padButtonFromName(u, &m)) {
        b.act = Binding::PadBtn;
        b.value = m;
        return true;
    }
    static const char* dirs[] = {"UP", "DOWN", "LEFT", "RIGHT"};
    for (int i = 0; i < 4; ++i) {
        if (u == std::string("LSTICK_") + dirs[i]) {
            b.act = Binding::LStick;
            b.value = static_cast<uint32_t>(i);
            return true;
        }
        if (u == std::string("RSTICK_") + dirs[i]) {
            b.act = Binding::RStick;
            b.value = static_cast<uint32_t>(i);
            return true;
        }
    }
    if (u == "WALK") {
        b.act = Binding::Walk;
        return true;
    }
    if (u.rfind("ACT:", 0) == 0 && parseId(u.substr(4), &b.value)) {
        b.act = Binding::Action;
        return true;
    }
    if (u.rfind("CMD:", 0) == 0 && parseId(u.substr(4), &b.value)) {
        b.act = Binding::Command;
        return true;
    }
    if (u == "QUICKSAVE" || u == "QUICKLOAD") {
        b.act = Binding::Save;
        b.value = u == "QUICKSAVE" ? kQuickSave : kQuickLoad;
        return true;
    }
    if (u == "DEBUG_MENU") {
        b.act = Binding::DebugMenu;
        return true;
    }
    if (u == "DIRECTIONAL_ATTACK") {
        b.act = Binding::DirAttack;
        b.value = 0;
        return true;
    }
    if (u == "VIEW_MAP" || u == "VIEW_STRIP") {
        b.act = Binding::View;
        b.value = u == "VIEW_MAP" ? kViewMap : kViewStrip;
        return true;
    }
    if (u.size() == 6 && u.rfind("QUICK", 0) == 0 && u[5] >= '1' && u[5] < char('1' + kQuickSlotMax)) {
        b.act = Binding::Quick;
        b.value = static_cast<uint32_t>(u[5] - '0');
        return true;
    }
    return false;
}

} // namespace

const char* directionalAttackModeName(DirectionalAttackMode m) {
    switch (m) {
        case DirectionalAttackMode::Modifier:
            return "modifier";
        case DirectionalAttackMode::Tap:
            return "tap";
        case DirectionalAttackMode::Always:
            return "always";
        case DirectionalAttackMode::Off:
            return "off";
    }
    return "modifier";
}

bool directionalAttackModeFromName(const std::string& name, DirectionalAttackMode* m) {
    ini::Value v;
    if (!ini::parseValue(*ini::findKey("keyboard", "directional_attack"), trim(name), v))
        return false;
    *m = static_cast<DirectionalAttackMode>(v.choice);
    return true;
}

const char* Config::defaultIniText() {
    return ini::defaultIniText();
}

void Config::parse(const std::string& text, Config& c, std::string* warn) {
    std::istringstream in(text);
    std::string line, section;
    int ln = 0;
    bool gameCleared = false, menuCleared = false;
    auto w = [&](const std::string& m) {
        if (warn)
            *warn += "line " + std::to_string(ln) + ": " + m + "\n";
    };
    while (std::getline(in, line)) {
        ++ln;
        for (size_t i = 0; i < line.size(); ++i)
            if ((line[i] == ';' || line[i] == '#') && (i == 0 || drakan::common::isAsciiSpace(line[i - 1]))) {
                line.resize(i);
                break;
            }
        line = trim(line);
        if (line.empty())
            continue;
        if (line.front() == '[' && line.back() == ']') {
            section = asciiUpper(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            w("expected key = value");
            continue;
        }
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (section == "BINDINGS" || section == "MENU_BINDINGS") {
            auto& vec = section == "BINDINGS" ? c.game : c.menu;
            bool& cleared = section == "BINDINGS" ? gameCleared : menuCleared;
            if (!cleared) {
                vec.clear();
                if (section == "BINDINGS")
                    c.gameKeysNone.clear();
                cleared = true;
            }
            Binding proto;
            if (!parseSource(k, proto)) {
                w("unknown input '" + k + "'");
                continue;
            }
            std::stringstream acts(v);
            std::string a;
            while (std::getline(acts, a, '|')) {
                a = trim(a);
                if (section == "BINDINGS" && asciiUpper(a) == "NONE" && proto.src == Binding::Key)
                    c.gameKeysNone.push_back(proto.code);
                if (a.empty() || asciiUpper(a) == "NONE")
                    continue;
                Binding b = proto;
                if (!parseAction(a, b)) {
                    w("unknown action '" + a + "'");
                    continue;
                }
                vec.push_back(b);
            }
            continue;
        }
        if (const std::string problem = applyScalar(c, section, k, v); !problem.empty())
            w(problem);
    }
    migrate(c);
}

Config Config::defaults() {
    Config c;
    parse(defaultIniText(), c, nullptr);
    return c;
}

Config Config::loadOrCreate(const std::string& path, std::string* warnings) {
    Config c = defaults();
    std::ifstream f(path, std::ios::binary);
    if (f)
        c.controlsVersion = 0; // the defaults carry version 2; an existing file has to say so itself
    if (!f) {
        std::ofstream o(path, std::ios::binary);
        if (o)
            o << defaultIniText();
        else if (warnings)
            *warnings += "could not create " + path + "\n";
        applyEnvOverrides(c);
        return c;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    f.close();
    const std::string text = ss.str();
    parse(text, c, warnings);
    if (c.controlsVersion < 3) {
        const std::string upgraded = upgradeText(text, c.controlsVersion);
        parse(upgraded, c, nullptr);
        std::ofstream o(path, std::ios::binary | std::ios::trunc);
        if (o)
            o << upgraded;
    }
    applyEnvOverrides(c);
    return c;
}

// DRAKAN_DRAGON_MODE = mouse | stick, DRAKAN_DRAGON_SENS (dragon_sensitivity), DRAKAN_DRAGON_SMOOTHING (ms) and
// DRAKAN_DRAGON_STRAFE = 0 | 1 override the file for one run (docs/reference/env-flags.md); the file is not changed.
void Config::applyEnvOverrides(Config& c) {
    if (const char* e = std::getenv("DRAKAN_DRAGON_MODE")) {
        const std::string u = asciiUpper(trim(e));
        if (u == "STICK")
            c.dragonStick = true;
        else if (u == "MOUSE" || u == "DIRECT")
            c.dragonStick = false;
    }
    auto num = [](const char* name, float& dst) {
        if (const char* e = std::getenv(name); e && *e) {
            char* end = nullptr;
            const float v = std::strtof(e, &end);
            if (end != e && std::isfinite(v) && v >= 0.0f)
                dst = v;
        }
    };
    num("DRAKAN_DRAGON_SENS", c.dragonScale);
    num("DRAKAN_DRAGON_SMOOTHING", c.dragonSmoothingMs);
    if (const char* e = std::getenv("DRAKAN_DRAGON_STRAFE"); e && (e[0] == '0' || e[0] == '1'))
        c.dragonStrafe = e[0] == '1';
}

// Files written before the Order-of-the-Flame style controls (A / D strafe, persistent mouse pitch) get a marker block
// appended (version 2): the new keys all default to the new behaviour when absent, so the block only makes them
// visible. Existing user bindings / sensitivities are never touched; the obsolete pitch_hold_ms line is left in place
// (ignored).
//
// Version 3 (2026-11-16, mouse steering for Arokh): `dragon_mode = stick` was the written default of every older file
// and `direct` never steered the flight (Camera_UpdateLook does not run for Arokh in flight), so that line is replaced
// by the new default and controls_version is set to 3 in place; every other line is kept byte for byte.
std::string Config::upgradeText(const std::string& text, int version) {
    std::string body = text;
    if (version < 2) {
        const std::string up = asciiUpper(text);
        // An explicit legacy sign may be intentional inversion. Keep every original
        // byte; an absent sign already inherits the current default from defaults().
        std::string add = "\n; ---- controls v2 (added automatically): Order of the Flame style controls ----\n";
        add += "[general]\ncontrols_version = 2\n";
        if (up.find("STRAFE") == std::string::npos)
            add += "[keyboard]\nstrafe = 1                 ; A / D strafe (0 = A / D turn like the stock PS2 scheme "
                   "A)\n";
        if (up.find("PITCH_PERSIST") == std::string::npos)
            add += "[mouse]\npitch_persist = 1          ; the camera pitch stays where the mouse left it "
                   "(pitch_hold_ms above is obsolete)\n";
        body += add;
    }
    std::string out;
    size_t pos = 0;
    bool versionWritten = false;
    while (pos < body.size()) {
        size_t nl = body.find('\n', pos);
        const bool last = nl == std::string::npos;
        if (last)
            nl = body.size();
        std::string line = body.substr(pos, nl - pos);
        const bool cr = !line.empty() && line.back() == '\r';
        if (cr)
            line.pop_back();
        const size_t eq = line.find('=');
        const std::string key = eq == std::string::npos ? std::string() : asciiUpper(trim(line.substr(0, eq)));
        if (key == "CONTROLS_VERSION") {
            line = "controls_version = 3       ; 3 = mouse steering for Arokh (written by the upgrade of older files)";
            versionWritten = true;
        } else if (key == "DRAGON_MODE") {
            std::string v = line.substr(eq + 1);
            if (const size_t sc = v.find(';'); sc != std::string::npos)
                v.resize(sc);
            const std::string u = asciiUpper(trim(v));
            if (u == "STICK" || u == "DIRECT")
                line = "dragon_mode = mouse        ; Arokh: mouse = the mouse turns and pitches him directly (stick = "
                       "the game's right stick, the PS2 flight feel)";
        }
        out += line;
        if (cr)
            out += '\r';
        if (!last)
            out += '\n';
        pos = nl + 1;
    }
    if (!versionWritten)
        out += "\n[general]\ncontrols_version = 3\n";
    return out;
}

// Older drakan_input.ini files (written before Enter confirmed in-game popups) have no Enter binding in the gameplay
// layer: add Enter / keypad Enter = CROSS there (and in the menu layer for keypad Enter) unless the user bound those
// keys to something.
void Config::migrate(Config& c) {
    auto has = [](const std::vector<Binding>& v, int dik) {
        for (const auto& b : v)
            if (b.src == Binding::Key && b.code == dik)
                return true;
        return false;
    };
    auto add = [](std::vector<Binding>& v, int dik) {
        Binding b;
        b.src = Binding::Key;
        b.code = dik;
        b.act = Binding::PadBtn;
        b.value = Pad::CROSS;
        v.push_back(b);
    };
    if (c.menuMouse) { // the left click is handled by MenuMouse (hit test + the widget's own press / release): no extra
                       // Cross
        c.menu.erase(std::remove_if(c.menu.begin(), c.menu.end(),
                                    [](const Binding& b) {
                                        return b.src == Binding::Mouse && b.code == 1 && b.act == Binding::PadBtn &&
                                               b.value == Pad::CROSS;
                                    }),
                     c.menu.end());
    }
    if (c.menuMouse) { // "back" in the GUI is the Esc key id (START); Circle alone is its right-mouse id, which the
                       // screens ignore
        auto hasBtn = [&](Binding::Src src, int code, uint16_t pad) {
            for (const auto& b : c.menu)
                if (b.src == src && b.code == code && b.act == Binding::PadBtn && b.value == pad)
                    return true;
            return false;
        };
        for (auto [src, code] :
             {std::pair<Binding::Src, int>{Binding::Mouse, 2}, std::pair<Binding::Src, int>{Binding::Key, 0x0E}}) {
            if (hasBtn(src, code, Pad::CIRCLE) && !hasBtn(src, code, Pad::START)) {
                Binding b;
                b.src = src;
                b.code = code;
                b.act = Binding::PadBtn;
                b.value = Pad::START;
                c.menu.push_back(b);
            }
        }
    }
    for (int dik : {0x1C, 0x9C}) {
        if (!has(c.game, dik))
            add(c.game, dik);
        if (!has(c.menu, dik))
            add(c.menu, dik);
    }
    // Files that predate the Order-of-the-Flame style map: M (unbound before) opens it, unless the file binds M
    // itself or says M = NONE, or already binds VIEW_MAP to another key.
    const auto none = [&](int dik) {
        return std::find(c.gameKeysNone.begin(), c.gameKeysNone.end(), dik) != c.gameKeysNone.end();
    };
    const auto bindsView = [&](uint32_t view) {
        return std::any_of(c.game.begin(), c.game.end(),
                           [&](const Binding& b) { return b.act == Binding::View && b.value == view; });
    };
    // Add new shortcuts only if neither the key nor the action was explicitly configured.
    for (auto [dik, action] : {std::pair{0x3f, kQuickSave}, std::pair{0x42, kQuickLoad}}) {
        const bool bound = std::any_of(c.game.begin(), c.game.end(), [wantedAction = action](const Binding& b) {
            return b.act == Binding::Save && b.value == wantedAction;
        });
        if (!has(c.game, dik) && !none(dik) && !bound)
            c.game.push_back({Binding::Key, dik, Binding::Save, action});
    }
    constexpr int kDikF3 = 0x3d;
    const bool debugBound =
        std::any_of(c.game.begin(), c.game.end(), [](const Binding& b) { return b.act == Binding::DebugMenu; });
    if (!has(c.game, kDikF3) && !none(kDikF3) && !debugBound)
        c.game.push_back({Binding::Key, kDikF3, Binding::DebugMenu, 0});
    // Files that predate the directional attack modifier: Left Alt (unbound before) becomes DIRECTIONAL_ATTACK unless
    // the file binds it, says LALT = NONE or already binds the action to another source.
    constexpr int kDikLAlt = 0x38;
    const bool dirAttackBound =
        std::any_of(c.game.begin(), c.game.end(), [](const Binding& b) { return b.act == Binding::DirAttack; });
    if (!has(c.game, kDikLAlt) && !none(kDikLAlt) && !dirAttackBound)
        c.game.push_back({Binding::Key, kDikLAlt, Binding::DirAttack, 0});
    constexpr int kDikM = 0x32;
    if (!has(c.game, kDikM) && !none(kDikM) && !bindsView(kViewMap)) {
        Binding b;
        b.src = Binding::Key;
        b.code = kDikM;
        b.act = Binding::View;
        b.value = kViewMap;
        c.game.push_back(b);
    }
    // The old default I = ACT:0x17 (the shipped keymap's Inventory, the same screen as Tab) becomes the strip. Any
    // other binding of I is the user's and stays; inventory_strip = 0 gives I the normal screen back.
    constexpr int kDikI = 0x17;
    const auto isI = [&](const Binding& b) { return b.src == Binding::Key && b.code == kDikI; };
    if (std::count_if(c.game.begin(), c.game.end(), isI) == 1 && !bindsView(kViewStrip))
        for (auto& b : c.game)
            if (isI(b) && b.act == Binding::Action && b.value == 0x17) {
                b.act = Binding::View;
                b.value = kViewStrip;
            }
    // The old defaults 1..5 = ACT:0x02..ACT:0x06 sent the keymap's slot commands 0x403b..0x403f, which the game does
    // not handle (docs/reference/combat-controls.md): whatever source carries them now uses quick slots 1..5.
    for (auto& b : c.game)
        if (b.act == Binding::Action && b.value >= 0x02 && b.value <= 0x06) {
            b.act = Binding::Quick;
            b.value -= 0x01;
        }
}

} // namespace drakan_input
