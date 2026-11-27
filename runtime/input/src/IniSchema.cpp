#include "input/IniSchema.h"
#include "input/FrameInput.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <sstream>

#include "common/Text.h"

namespace drakan_input {
namespace {

using drakan::common::asciiUpper;
using drakan::common::iequals;
using drakan::common::trim;

struct KeyName {
    const char* name;
    int dik;
};
const KeyName kKeys[] = {
    {"ESC", 0x01},       {"1", 0x02},        {"2", 0x03},          {"3", 0x04},           {"4", 0x05},
    {"5", 0x06},         {"6", 0x07},        {"7", 0x08},          {"8", 0x09},           {"9", 0x0A},
    {"0", 0x0B},         {"MINUS", 0x0C},    {"EQUALS", 0x0D},     {"BACKSPACE", 0x0E},   {"TAB", 0x0F},
    {"Q", 0x10},         {"W", 0x11},        {"E", 0x12},          {"R", 0x13},           {"T", 0x14},
    {"Y", 0x15},         {"U", 0x16},        {"I", 0x17},          {"O", 0x18},           {"P", 0x19},
    {"LBRACKET", 0x1A},  {"RBRACKET", 0x1B}, {"ENTER", 0x1C},      {"LCONTROL", 0x1D},    {"A", 0x1E},
    {"S", 0x1F},         {"D", 0x20},        {"F", 0x21},          {"G", 0x22},           {"H", 0x23},
    {"J", 0x24},         {"K", 0x25},        {"L", 0x26},          {"SEMICOLON", 0x27},   {"APOSTROPHE", 0x28},
    {"GRAVE", 0x29},     {"LSHIFT", 0x2A},   {"BACKSLASH", 0x2B},  {"Z", 0x2C},           {"X", 0x2D},
    {"C", 0x2E},         {"V", 0x2F},        {"B", 0x30},          {"N", 0x31},           {"M", 0x32},
    {"COMMA", 0x33},     {"PERIOD", 0x34},   {"SLASH", 0x35},      {"RSHIFT", 0x36},      {"NUMPADMUL", 0x37},
    {"LALT", 0x38},      {"SPACE", 0x39},    {"CAPSLOCK", 0x3A},   {"F1", 0x3B},          {"F2", 0x3C},
    {"F3", 0x3D},        {"F4", 0x3E},       {"F5", 0x3F},         {"F6", 0x40},          {"F7", 0x41},
    {"F8", 0x42},        {"F9", 0x43},       {"F10", 0x44},        {"F11", 0x57},         {"F12", 0x58},
    {"NUMPAD7", 0x47},   {"NUMPAD8", 0x48},  {"NUMPAD9", 0x49},    {"NUMPADMINUS", 0x4A}, {"NUMPAD4", 0x4B},
    {"NUMPAD5", 0x4C},   {"NUMPAD6", 0x4D},  {"NUMPADPLUS", 0x4E}, {"NUMPAD1", 0x4F},     {"NUMPAD2", 0x50},
    {"NUMPAD3", 0x51},   {"NUMPAD0", 0x52},  {"NUMPADDOT", 0x53},  {"NUMPADENTER", 0x9C}, {"RCONTROL", 0x9D},
    {"NUMPADDIV", 0xB5}, {"RALT", 0xB8},     {"HOME", 0xC7},       {"UP", 0xC8},          {"PGUP", 0xC9},
    {"LEFT", 0xCB},      {"RIGHT", 0xCD},    {"END", 0xCF},        {"DOWN", 0xD0},        {"PGDN", 0xD1},
    {"INSERT", 0xD2},    {"DELETE", 0xD3},
};

struct PadName {
    const char* name;
    uint16_t mask;
};
// Mask order: padComboToText lists the buttons of a combination in this order.
const PadName kPads[] = {
    {"L2", Pad::L2},
    {"R2", Pad::R2},
    {"L1", Pad::L1},
    {"R1", Pad::R1},
    {"TRIANGLE", Pad::TRIANGLE},
    {"CIRCLE", Pad::CIRCLE},
    {"CROSS", Pad::CROSS},
    {"SQUARE", Pad::SQUARE},
    {"SELECT", Pad::SELECT},
    {"L3", Pad::L3},
    {"R3", Pad::R3},
    {"START", Pad::START},
    {"DPAD_UP", Pad::UP},
    {"DPAD_RIGHT", Pad::RIGHT},
    {"DPAD_DOWN", Pad::DOWN},
    {"DPAD_LEFT", Pad::LEFT},
};

constexpr int kMouseButtons = 5; // MOUSE1..MOUSE5: left, right, middle, x1, x2

} // namespace

// ---- name tables
// -----------------------------------------------------------------------------------------------------
bool keyFromName(const std::string& name, int* dik) {
    for (const auto& k : kKeys)
        if (iequals(name, k.name)) {
            *dik = k.dik;
            return true;
        }
    return false;
}

const char* nameOfKey(int dik) {
    for (const auto& k : kKeys)
        if (k.dik == dik)
            return k.name;
    return nullptr;
}

const std::vector<std::string>& allKeyNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> v;
        for (const auto& k : kKeys)
            v.emplace_back(k.name);
        return v;
    }();
    return names;
}

bool padButtonFromName(const std::string& name, uint16_t* mask) {
    for (const auto& p : kPads)
        if (iequals(name, p.name)) {
            *mask = p.mask;
            return true;
        }
    return false;
}

bool padComboFromText(const std::string& text, uint16_t* mask) {
    const std::string u = asciiUpper(trim(text));
    if (u.empty() || u == "NONE" || u == "OFF") {
        *mask = 0;
        return true;
    }
    uint16_t m = 0;
    std::stringstream parts(u);
    std::string part;
    while (std::getline(parts, part, '+')) { // a trailing '+' adds no empty part ("L3+" = L3)
        uint16_t button = 0;
        if (!padButtonFromName(trim(part), &button))
            return false;
        m = static_cast<uint16_t>(m | button);
    }
    if (!m)
        return false;
    *mask = m;
    return true;
}

std::string padComboToText(uint16_t mask) {
    std::string s;
    for (const auto& p : kPads)
        if (mask & p.mask) {
            if (!s.empty())
                s += '+';
            s += p.name;
        }
    return s.empty() ? std::string("NONE") : s;
}

namespace ini {

// ---- value rules
// -----------------------------------------------------------------------------------------------------
bool parseBool(const std::string& text, bool* out) {
    const std::string u = asciiUpper(text);
    if (u == "1" || u == "TRUE" || u == "YES" || u == "ON") {
        *out = true;
        return true;
    }
    if (u == "0" || u == "FALSE" || u == "NO" || u == "OFF") {
        *out = false;
        return true;
    }
    return false;
}

bool parseFloat(const std::string& text, float* out) {
    if (text.empty())
        return false;
    char* end = nullptr;
    errno = 0;
    const float v = std::strtof(text.c_str(), &end);
    if (end != text.c_str() + text.size() || errno == ERANGE || !std::isfinite(v))
        return false;
    *out = v;
    return true;
}

bool parseInt(const std::string& text, long long* out) {
    if (text.empty())
        return false;
    char* end = nullptr;
    errno = 0;
    const long long v = std::strtoll(text.c_str(), &end, 0); // base 0: 0x.. hex like the [advanced] addresses
    if (end != text.c_str() + text.size() || errno == ERANGE)
        return false;
    *out = v;
    return true;
}

bool isBindingSource(const std::string& name) {
    if (iequals(name, "WHEELUP") || iequals(name, "WHEELDOWN"))
        return true;
    if (name.size() == 6 && iequals(name.substr(0, 5), "MOUSE") && name[5] >= '1' && name[5] < '1' + kMouseButtons)
        return true;
    int dik = 0;
    return keyFromName(name, &dik);
}

// ---- the keys
// --------------------------------------------------------------------------------------------------------
namespace {

constexpr long long kAddressMax = 0xFFFFFFFFll; // guest addresses are 32 bit

KeySpec boolKey(const char* section, const char* key, const char* def) {
    return {section, key, Type::Bool, def};
}
KeySpec floatKey(const char* section, const char* key, const char* def) {
    return {section, key, Type::Float, def};
}
KeySpec intKey(const char* section, const char* key, const char* def, long long lo, long long hi) {
    return {section, key, Type::Int, def, lo, hi};
}
KeySpec addressKey(const char* key, const char* def) {
    return intKey("advanced", key, def, 0, kAddressMax);
}
KeySpec choiceKey(const char* section, const char* key, const char* def, std::vector<Choice> choices) {
    return {section, key, Type::Choice, def, 0, 0, std::move(choices)};
}

// The choice values are the order of the matching enum in input/Config.h (Config.cpp static_asserts the ones that map
// onto an enum).
std::vector<KeySpec> buildKeys() {
    return {
        // [general]
        boolKey("general", "capture_mouse", "1"),
        boolKey("general", "release_on_esc", "0"),
        {"general", "toggle_key", Type::Key, "F10"},
        choiceKey("general", "look_mode", "direct", {{"direct", 0}, {"stick", 1}}),
        intKey("general", "controls_version", "3", INT_MIN, INT_MAX),
        // [mouse]
        floatKey("mouse", "sensitivity_x", "0.0020"),
        floatKey("mouse", "sensitivity_y", "0.0020"),
        boolKey("mouse", "invert_x", "0"),
        boolKey("mouse", "invert_y", "0"),
        floatKey("mouse", "pitch_hold_ms", ""), // deprecated since controls v2 (pitch_persist), parsed but unused
        boolKey("mouse", "pitch_persist", "1"),
        boolKey("mouse", "free_combat", "1"),
        floatKey("mouse", "camera_follow", "5"),
        boolKey("mouse", "camera_lag_in_place", "1"),
        floatKey("mouse", "pitch_view_gain", "1.8"),
        boolKey("mouse", "bow_draw", "1"),
        // classic was the old name of ootf, ps2 is an alias of native.
        choiceKey("mouse", "bow_aim", "ootf", {{"ootf", 0}, {"classic", 0}, {"native", 1}, {"ps2", 1}, {"modern", 2}}),
        floatKey("mouse", "bow_zoom", "0.85"),
        // direct is what files before controls v3 wrote; it steers like mouse.
        choiceKey("mouse", "dragon_mode", "mouse", {{"mouse", 0}, {"direct", 0}, {"stick", 1}}),
        floatKey("mouse", "dragon_sensitivity", "1.0"),
        boolKey("mouse", "dragon_camera_carry", "1"),
        floatKey("mouse", "dragon_smoothing", "0"),
        floatKey("mouse", "dragon_pitch_up", "0.75"),  // magnitude (the sign is ignored)
        floatKey("mouse", "dragon_pitch_down", "1.0"), // magnitude (the sign is ignored)
        floatKey("mouse", "yaw_sign", "-1"),
        floatKey("mouse", "pitch_sign", "-1"),
        floatKey("mouse", "stick_full_counts", "40"),
        floatKey("mouse", "stick_sign_x", "1"),
        floatKey("mouse", "stick_sign_y", "1"),
        // [keyboard]
        floatKey("keyboard", "walk_scale", "0.5"),
        boolKey("keyboard", "toggle_run", "0"),
        boolKey("keyboard", "strafe", "1"),
        boolKey("keyboard", "dragon_strafe", "1"),
        boolKey("keyboard", "curve_compensation", "1"),
        choiceKey("keyboard", "directional_attack", "modifier",
                  {{"modifier", 0}, {"tap", 1}, {"always", 2}, {"off", 3}}),
        // [gamepad]
        choiceKey("gamepad", "layout", "auto", {{"auto", 0}, {"native", 1}, {"twin_stick", 2}}),
        boolKey("gamepad", "enabled", "1"),
        floatKey("gamepad", "deadzone", "0.15"),
        {"gamepad", "debug_menu_combo", Type::PadCombo, "L3+R3"},
        // [menu]
        boolKey("menu", "mouse", "1"),
        boolKey("menu", "inventory_mouse", "1"),
        choiceKey("menu", "inventory_click", "square", {{"square", 0}, {"cross", 1}}),
        boolKey("menu", "draw_cursor", "1"),
        floatKey("menu", "cursor_idle_ms", "5000"),
        boolKey("menu", "map_view", "1"),
        boolKey("menu", "inventory_strip", "1"),
        boolKey("menu", "cursor", "0"),
        boolKey("menu", "clicks", "0"),
        {"menu", "inventory_step", Type::Ignored, ""}, // obsolete: pointer travel no longer steps the selection
        // [advanced] guest addresses of SCES_500.06
        boolKey("advanced", "check_turn_gate", "1"),
        choiceKey("advanced", "turn_call_abi", "fpr", {{"fpr", 0}, {"gpr", 1}}),
        addressKey("addr_ctx_ptr", "0x5565d4"),
        addressKey("addr_gui_ptr", "0x5565d8"),
        addressKey("addr_screen_ptr", "0x5565c8"),
        addressKey("addr_padmgr", "0x4e90c0"),
        addressKey("addr_turn_fn", "0x13ee90"),
        addressKey("addr_turn_upvec", "0x4e8c48"),
        addressKey("addr_invoke_callback", "0x131fa8"),
        addressKey("addr_timer_ms", "0x4af7f8"),
        addressKey("gui_vtable_offset", "0"),
        addressKey("addr_active_screen", "0x4fb60c"),
        addressKey("addr_mouse_callback", "0x2cc7c0"),
        addressKey("addr_ui_entry", "0x2bec48"),
        addressKey("addr_captured_widget", "0x4fb19c"),
        addressKey("addr_stick_mode_flag", "0x4e9594"),
        addressKey("addr_stick_map", "0x4e9558"),
    };
}

std::string choiceList(const KeySpec& spec) {
    std::string s;
    std::vector<int> seen;
    for (const auto& c : spec.choices) {
        if (std::find(seen.begin(), seen.end(), c.value) != seen.end())
            continue; // an alias: only the written spelling is listed
        seen.push_back(c.value);
        if (!s.empty())
            s += '|';
        s += c.name;
    }
    return s;
}

} // namespace

const std::vector<KeySpec>& keys() {
    static const std::vector<KeySpec> table = buildKeys();
    return table;
}

const KeySpec* findKey(std::string_view section, std::string_view key) {
    for (const auto& k : keys())
        if (iequals(section, k.section) && iequals(key, k.key))
            return &k;
    return nullptr;
}

bool isScalarSection(std::string_view section) {
    for (const char* s : {"general", "mouse", "keyboard", "gamepad", "menu", "advanced"})
        if (iequals(section, s))
            return true;
    return false;
}

bool parseValue(const KeySpec& spec, const std::string& text, Value& out, std::string* why) {
    auto reject = [&](std::string message) {
        if (why)
            *why = std::move(message);
        return false;
    };
    switch (spec.type) {
        case Type::Bool:
            return parseBool(text, &out.b) || reject(std::string("bad bool for ") + spec.key);
        case Type::Float:
            return parseFloat(text, &out.f) || reject(std::string("bad number for ") + spec.key);
        case Type::Int: {
            long long v = 0;
            if (!parseInt(text, &v) || v < spec.minInt || v > spec.maxInt)
                return reject(std::string("bad number for ") + spec.key);
            out.i = v;
            return true;
        }
        case Type::Choice:
            for (const auto& c : spec.choices)
                if (iequals(text, c.name)) {
                    out.choice = c.value;
                    return true;
                }
            return reject(std::string(spec.key) + " must be " + choiceList(spec));
        case Type::Key:
            if (iequals(text, "NONE")) {
                out.dik = -1;
                return true;
            }
            return keyFromName(text, &out.dik) || reject("unknown key " + text);
        case Type::PadCombo:
            return padComboFromText(text, &out.pad) || reject("bad button combination '" + text + "'");
        case Type::Ignored:
            return true;
    }
    return reject(std::string("unknown key ") + spec.key);
}

std::string canonicalText(const KeySpec& spec, const Value& value) {
    switch (spec.type) {
        case Type::Bool:
            return value.b ? "1" : "0";
        case Type::Choice:
            for (const auto& c : spec.choices)
                if (c.value == value.choice)
                    return c.name;
            return {};
        case Type::Key: {
            const char* name = value.dik < 0 ? nullptr : nameOfKey(value.dik);
            return name ? name : "NONE";
        }
        case Type::PadCombo:
            return padComboToText(value.pad);
        default:
            return {};
    }
}

bool canonicalize(const KeySpec& spec, const std::string& text, std::string& out) {
    Value v;
    if (!parseValue(spec, trim(text), v))
        return false;
    out = canonicalText(spec, v);
    return true;
}

const char* defaultIniText() {
    return R"INI(; drakan_input.ini - mouse + keyboard configuration for the Drakan port.
; Bindings are:  <source> = <action> [| <action> ...]
;   sources : key names (A..Z, 0..9, SPACE, LSHIFT, LCONTROL, TAB, ENTER, ESC, UP/DOWN/LEFT/RIGHT, F1..F12 ...),
;             MOUSE1..MOUSE5 (left,right,middle,x1,x2), WHEELUP, WHEELDOWN
;   actions : DualShock buttons  CROSS CIRCLE SQUARE TRIANGLE L1 L2 L3 R1 R2 R3 START SELECT
;                                DPAD_UP DPAD_DOWN DPAD_LEFT DPAD_RIGHT
;             LSTICK_UP/DOWN/LEFT/RIGHT  RSTICK_UP/DOWN/LEFT/RIGHT   (digital deflection of an analog stick)
;             WALK                       (while held, scales the keyboard left stick by walk_scale)
;             ACT:<id>                   inject a game "action id" (DirectInput scancode or 0x100..) through the keymap
;             CMD:<id>                   inject a game command id (0x4002..0x4069) directly (meanings partly unconfirmed)
;             VIEW_MAP / VIEW_STRIP      [bindings] only: open the full-screen map / the inventory strip (the same key,
;                                        Esc, right click or Tab closes it)
;             QUICKSAVE / QUICKLOAD      [bindings] only: replace / load the dedicated quicksave (F5 / F8)
;             DIRECTIONAL_ATTACK         [bindings] only: hold with Attack and a movement key for a directional special
;                                        move ([keyboard] directional_attack = modifier; Left Alt by default)
;             DEBUG_MENU                 [bindings] only: open / close the game's developer debug menu (F3; needs
;                                        drakan.ini [general] debug_menu = 1)
;             QUICK1 .. QUICK9           [bindings] only: use / equip the Nth quick-slot item (marked with Cross / F in
;                                        the inventory), in the order of the game's Quick Inventory bar (R1 / Q)
; The comments name what the buttons do on foot in the game's controller scheme A (Options > Controls): Triangle = Jump,
; Cross = Crouch, Square = Attack, Circle = Magic/Parry, L1 = Call Arokh/Use, R1 = Quick Inventory, R2 = Toggle Targeting,
; L2 = Next Target/Strafe, Select = Inventory/Map, Start = Game Menu. Schemes B and C put other commands on the same
; buttons, and riding Arokh the game uses its own flight layout (scheme A: L1 = previous breath / item, R1 = Attack);
; F1 > Controls shows the meanings for the scheme the game runs. The button-prompt generator shows the FIRST key bound.

[general]
capture_mouse = 1          ; hide + clip the cursor while playing
release_on_esc = 0         ; 1: Esc also toggles the capture by hand (not needed: the cursor is freed automatically whenever the game is not in gameplay, e.g. pause menu)
toggle_key = F10           ; manual capture toggle
look_mode = direct         ; direct = raw angles into camera/player, stick = emulate the right stick
controls_version = 3       ; 2 = Order-of-the-Flame style controls (A/D strafe, mouse pitch stays), 3 = mouse steering for Arokh; written by the upgrade of older files

[mouse]
sensitivity_x = 0.0020     ; radians per mouse count (direct mode)
sensitivity_y = 0.0020
invert_x = 0              ; reverse horizontal mouse look (direct and stick modes)
invert_y = 0
pitch_persist = 1          ; the camera pitch stays where the mouse left it (the game's automatic re-centring is switched off while you look with the mouse; a pad right stick still re-centres)
free_combat = 1            ; free combat look; explicit target lock and bow aiming remain available
camera_follow = 5          ; mouse / keyboard on foot: turning carries the camera round Rynn at once, its travel behind her eases at this rate (1/s; higher = stiffer, 0 = the game's own slow rubber band)
camera_lag_in_place = 1    ; with camera_follow: the distance the camera trails behind a running Rynn is shortened by a turn instead of being swung round her whole, so a quick turn in a narrow room cannot swing it through a wall (0 = the whole offset turns, as before)
pitch_view_gain = 1.8      ; the game shows 1.8x the camera pitch on foot; mouse up / down is divided by this so both axes turn the view equally (1 = old feel)
bow_draw = 1             ; hold left to show the drawn bow (aiming stance on the torso until the release); 0 = pose only on release
bow_aim = ootf             ; ootf: hold left to draw and aim (crosshair), release to shoot, walk any direction, right click toggles the first-person sight; native = the original PS2 bow (click fires); modern = ootf without the first-person sight adapters
bow_zoom = 0.85            ; field of view factor while aiming the bow (1 = no zoom)
dragon_mode = mouse        ; Arokh: mouse = the mouse turns and pitches him directly like on foot (no stick, dead zone or rate lag); stick = the mouse drives the game's right stick (the PS2 flight feel)
dragon_sensitivity = 1.0   ; dragon_mode = mouse: multiplies sensitivity_x / sensitivity_y while you steer Arokh
dragon_camera_carry = 1    ; dragon_mode = mouse: the view behind Arokh turns with each mouse turn at once (0 = the game's flight camera swings round him about half a second later)
dragon_smoothing = 0       ; dragon_mode = mouse: ease the mouse steering over this many ms (0 = raw, every count turns him at once)
dragon_pitch_up = 0.75     ; dragon_mode = mouse: highest nose-up angle the mouse sets (radians; the game's own hard limit while slow)
dragon_pitch_down = 1.0    ; dragon_mode = mouse: steepest nose-down angle the mouse sets (radians; the game's own hard limit while slow)
yaw_sign = -1              ; verified in the running game: mouse right turns right
pitch_sign = -1            ; negative camera pitch looks DOWN; mouse up raises it. Explicit legacy signs are preserved.
stick_full_counts = 40     ; stick mode: counts per 1/30 s for full deflection (independent of the frame rate)
stick_sign_x = 1
stick_sign_y = 1

[keyboard]
walk_scale = 0.5
toggle_run = 0             ; 1 = press WALK to switch between running and walking; starts running
strafe = 1                 ; A / D strafe like Drakan: Order of the Flame (0 = the game's own mapping: A / D turn). Needs no change in the game's Options.
dragon_strafe = 1          ; with strafe = 1, A / D strafe Arokh too, flying or on the ground, either dragon_mode (the mouse turns him); 0 = A / D turn him
curve_compensation = 1     ; undo the game's squared stick response so WASD is full speed
directional_attack = modifier ; when WASD + attack is a directional special move: modifier = only while DIRECTIONAL_ATTACK (Left Alt) is held, so a plain attack keeps walking / strafing; tap = press the movement key and attack together; always = whenever a movement key is held; off = arrow keys only

[gamepad]
layout = auto               ; auto = twin-stick on foot in Order of the Flame, native in other campaigns
enabled = 1                ; XInput pad is merged with keyboard/mouse
deadzone = 0.15
debug_menu_combo = L3+R3   ; hold 0.5 s to open / close the debug menu (Gameplay > Developer); NONE = off

[menu]
mouse = 1                  ; the mouse drives the game's own menus: hover = highlight, left click = confirm, wheel = up/down (or slider left/right), right click = back
inventory_mouse = 1        ; inventory / map / shop: hover selects the item under the pointer, left click = use / equip / buy / sell, wheel = previous / next section, right click = back
inventory_click = square   ; Equipment: left click = Square (use / equip; Cross is "hot slot"); cross = always Cross
draw_cursor = 1            ; draw a menu cursor in the overlay layer (the OS cursor is hidden over the game window while a menu is open)
cursor_idle_ms = 5000      ; the drawn cursor fades out after this long without mouse movement (0 = always visible)
map_view = 1               ; VIEW_MAP (M) shows the map full screen like Order of the Flame; 0 = the normal inventory screen on its Map page
inventory_strip = 1        ; VIEW_STRIP (I) shows Equipment as a strip along the bottom over the world; 0 = the normal inventory screen
cursor = 0                 ; legacy: push the host cursor into InputDev_SetCursorPos (the game does not use it)
clicks = 0                 ; legacy: synthesize GUI key events 0x100/0x101

[advanced]
check_turn_gate = 1        ; replicate the game's vcall gate before turning the player
turn_call_abi = fpr        ; fpr = angle in $f12, gpr = float bits in $a0
addr_ctx_ptr = 0x5565d4
addr_gui_ptr = 0x5565d8
addr_screen_ptr = 0x5565c8
addr_padmgr = 0x4e90c0
addr_turn_fn = 0x13ee90
addr_turn_upvec = 0x4e8c48
addr_invoke_callback = 0x131fa8
addr_timer_ms = 0x4af7f8
gui_vtable_offset = 0
addr_active_screen = 0x4fb60c   ; GUI screen manager: active screen object
addr_mouse_callback = 0x2cc7c0  ; the game's GUI mouse handler (type, button id, x, y)
addr_ui_entry = 0x2bec48        ; UI property table entry (menu sounds)
addr_captured_widget = 0x4fb19c ; widget that owns the pressed mouse button
addr_stick_mode_flag = 0x4e9594 ; PadMgr+0x4d4: "stick position mode" flag (the pitch re-centring), cleared around Camera_UpdateLook while the mouse looks
addr_stick_map = 0x4e9558       ; PadMgr+0x498: stick -> axis records (A / D strafe remaps the left stick to axis 3)

[bindings]
; movement (8-way normalised)
W = LSTICK_UP
S = LSTICK_DOWN
A = LSTICK_LEFT
D = LSTICK_RIGHT
LSHIFT = WALK
LALT = DIRECTIONAL_ATTACK  ; hold with Attack and W / A / S / D for a directional special move
; combat / actions: the comments are controller scheme A on foot (see the top of this file for B, C and Arokh)
MOUSE1 = SQUARE            ; Attack
MOUSE2 = CIRCLE            ; Bow sight / Use Magic / Parry
MOUSE3 = R2                ; Toggle Targeting (lock-on)
SPACE = TRIANGLE           ; Jump
LCONTROL = CROSS           ; Crouch
C = CROSS                  ; Crouch
ENTER = CROSS              ; confirm popups / info boxes ("OK"); like Ctrl/C it is also Crouch if no popup is detected
NUMPADENTER = CROSS
E = L1                     ; Tap to use, hold to call Arokh (scheme A)
Q = R1                     ; Quick Inventory
F = R2                     ; Toggle Targeting
R = L2                     ; Next Target / Strafe
V = R3                     ; Camera view toggle (command 0x400a, every scheme)
TAB = SELECT               ; Inventory / Map
ESC = START                ; Game Menu (pause)
; quick slots: 1..5 use / equip the 1st..5th quick-slot item (weapons are drawn, potions drunk) through the game's own
; Quick Inventory bar; 6 = the shipped PC keymap's Quick Inventory (command 0x403a in every scheme)
1 = QUICK1
2 = QUICK2
3 = QUICK3
4 = QUICK4
5 = QUICK5
6 = ACT:0x0e
I = VIEW_STRIP             ; inventory strip along the bottom (Tab = the full inventory screen)
F5 = QUICKSAVE
F8 = QUICKLOAD
F3 = DEBUG_MENU            ; developer debug menu (Settings > Gameplay > Developer)
M = VIEW_MAP               ; full-screen map
WHEELUP = ACT:0x114        ; 0x4034 previous item (context-dependent; Arokh's breath in flight)
WHEELDOWN = ACT:0x115      ; 0x4035 next item (context-dependent)
UP = DPAD_UP               ; Special Moves
DOWN = DPAD_DOWN
LEFT = DPAD_LEFT
RIGHT = DPAD_RIGHT

[menu_bindings]
; used while a menu / cutscene / pause screen is active (the game is not running its player camera)
UP = DPAD_UP
DOWN = DPAD_DOWN
LEFT = DPAD_LEFT
RIGHT = DPAD_RIGHT
W = DPAD_UP
S = DPAD_DOWN
A = DPAD_LEFT
D = DPAD_RIGHT
ENTER = CROSS              ; confirm / hot slot (inventory, store)
NUMPADENTER = CROSS
SPACE = CROSS
E = SQUARE                 ; use (inventory, store)
Q = TRIANGLE               ; drop / sell (inventory, store)
PGUP = L1
PGDN = R1
LBRACKET = L1              ; previous inventory section
RBRACKET = R1              ; next inventory section
MOUSE2 = START | CIRCLE    ; right click = back (left click / wheel are handled by [menu] mouse = 1)
BACKSPACE = START | CIRCLE ; back (the game's GUI maps START to Esc = back; Circle alone is its R-MOUSE id)
ESC = START | CIRCLE       ; back (Circle) and resume from the pause menu (Start)
TAB = SELECT
)INI";
}

} // namespace ini
} // namespace drakan_input
