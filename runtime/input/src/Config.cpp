#include "input/Config.h"
#include "input/FrameInput.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace drakan_input {
namespace {

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

std::string trim(std::string s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}
std::string upper(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool toNum(const std::string& s, long* v) {
    char* e = nullptr;
    *v = std::strtol(s.c_str(), &e, 0);
    return e && *e == 0 && !s.empty();
}
bool toFloat(const std::string& s, float* v) {
    char* e = nullptr;
    *v = std::strtof(s.c_str(), &e);
    return e && *e == 0 && !s.empty() && std::isfinite(*v);
}
bool toBool(const std::string& s, bool* v) {
    std::string u = upper(s);
    if (u == "1" || u == "TRUE" || u == "YES" || u == "ON") {
        *v = true;
        return true;
    }
    if (u == "0" || u == "FALSE" || u == "NO" || u == "OFF") {
        *v = false;
        return true;
    }
    return false;
}

bool parseSource(const std::string& n, Binding& b) {
    std::string u = upper(n);
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
bool parseAction(const std::string& a, Binding& b) {
    std::string u = upper(a);
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
    long v = 0;
    if (u.rfind("ACT:", 0) == 0 && toNum(u.substr(4), &v)) {
        b.act = Binding::Action;
        b.value = static_cast<uint32_t>(v);
        return true;
    }
    if (u.rfind("CMD:", 0) == 0 && toNum(u.substr(4), &v)) {
        b.act = Binding::Command;
        b.value = static_cast<uint32_t>(v);
        return true;
    }
    if (u == "QUICKSAVE" || u == "QUICKLOAD") {
        b.act = Binding::Save;
        b.value = u == "QUICKSAVE" ? kQuickSave : kQuickLoad;
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

bool keyFromName(const std::string& name, int* dik) {
    std::string u = upper(name);
    for (auto& k : kKeys)
        if (u == k.name) {
            *dik = k.dik;
            return true;
        }
    return false;
}
const char* nameOfKey(int dik) {
    for (auto& k : kKeys)
        if (k.dik == dik)
            return k.name;
    return nullptr;
}
bool padButtonFromName(const std::string& name, uint16_t* mask) {
    std::string u = upper(name);
    for (auto& k : kPads)
        if (u == k.name) {
            *mask = k.mask;
            return true;
        }
    return false;
}

const char* Config::defaultIniText() {
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
;             QUICK1 .. QUICK9           [bindings] only: use / equip the Nth quick-slot item (marked with Cross / F in
;                                        the inventory), in the order of the game's Quick Inventory bar (R1 / Q)
; The comments follow the game's Standard controller scheme (Options > Controller): Triangle = Jump, Cross = Crouch,
; Square = Attack, Circle = Magic/Parry, L1 = Call Arokh/Use, R1 = Quick Inventory, R2 = Toggle Targeting,
; L2 = Next Target/Strafe, Select = Inventory/Map, Start = Game Menu. The button-prompt generator shows the FIRST key bound.

[general]
capture_mouse = 1          ; hide + clip the cursor while playing
release_on_esc = 0         ; 1: Esc also toggles the capture by hand (not needed: the cursor is freed automatically whenever the game is not in gameplay, e.g. pause menu)
toggle_key = F10           ; manual capture toggle
look_mode = direct         ; direct = raw angles into camera/player, stick = emulate the right stick
controls_version = 2       ; 2 = Order-of-the-Flame style controls (A/D strafe, mouse pitch stays); written by the upgrade of older files

[mouse]
sensitivity_x = 0.0020     ; radians per mouse count (direct mode)
sensitivity_y = 0.0020
invert_x = 0              ; reverse horizontal mouse look (direct and stick modes)
invert_y = 0
pitch_persist = 1          ; the camera pitch stays where the mouse left it (the game's automatic re-centring is switched off while you look with the mouse; a pad right stick still re-centres)
free_combat = 1            ; free combat look; explicit target lock and bow aiming remain available
camera_follow = 5          ; mouse / keyboard on foot: turning carries the camera round Rynn at once, its travel behind her eases at this rate (1/s; higher = stiffer, 0 = the game's own slow rubber band)
pitch_view_gain = 1.8      ; the game shows 1.8x the camera pitch on foot; mouse up / down is divided by this so both axes turn the view equally (1 = old feel)
bow_aim = modern           ; bow out on foot: hold the left button to aim (slight zoom, crosshair), release to shoot at the crosshair; classic = the game's own bow (left click shoots)
bow_zoom = 0.85            ; field of view factor while aiming the bow (1 = no zoom)
dragon_mode = stick        ; Arokh flight: stick = the mouse steers through the game's own right-stick path (OotF style flight), direct = raw angles like on foot (unverified)
yaw_sign = -1              ; verified in the running game: mouse right turns right
pitch_sign = -1            ; negative camera pitch looks DOWN; mouse up raises it. Explicit legacy signs are preserved.
stick_full_counts = 40     ; stick mode: counts per 1/30 s for full deflection (independent of the frame rate)
stick_sign_x = 1
stick_sign_y = 1

[keyboard]
walk_scale = 0.5
toggle_run = 0             ; 1 = press WALK to switch between running and walking; starts running
strafe = 1                 ; A / D strafe like Drakan: Order of the Flame (0 = the game's own mapping: A / D turn). Needs no change in the game's Options.
curve_compensation = 1     ; undo the game's squared stick response so WASD is full speed

[gamepad]
enabled = 1                ; XInput pad is merged with keyboard/mouse
deadzone = 0.15

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
; combat / actions: these follow the game's own Standard controller scheme (Options > Controller screen)
MOUSE1 = SQUARE            ; Attack
MOUSE2 = CIRCLE            ; Bow sight / Use Magic / Parry
MOUSE3 = R2                ; Toggle Targeting (lock-on)
SPACE = TRIANGLE           ; Jump
LCONTROL = CROSS           ; Crouch
C = CROSS                  ; Crouch
ENTER = CROSS              ; confirm popups / info boxes ("OK"); like Ctrl/C it is also Crouch if no popup is detected
NUMPADENTER = CROSS
E = L1                     ; Call Arokh / Use
Q = R1                     ; Quick Inventory
F = R2                     ; Toggle Targeting
R = L2                     ; Next Target / Strafe
V = R3
TAB = SELECT               ; Inventory / Map
ESC = START                ; Game Menu (pause)
; quick slots: 1..5 use / equip the 1st..5th quick-slot item (weapons are drawn, potions drunk) through the game's own
; Quick Inventory bar; 6 = the shipped PC keymap's Quick Inventory (command 0x403a, the same as Q)
1 = QUICK1
2 = QUICK2
3 = QUICK3
4 = QUICK4
5 = QUICK5
6 = ACT:0x0e
I = VIEW_STRIP             ; inventory strip along the bottom (Tab = the full inventory screen)
F5 = QUICKSAVE
F8 = QUICKLOAD
M = VIEW_MAP               ; full-screen map
WHEELUP = ACT:0x114        ; 0x4034 previous weapon
WHEELDOWN = ACT:0x115      ; 0x4035 next weapon
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
            if ((line[i] == ';' || line[i] == '#') &&
                (i == 0 || std::isspace(static_cast<unsigned char>(line[i - 1])))) {
                line.resize(i);
                break;
            }
        line = trim(line);
        if (line.empty())
            continue;
        if (line.front() == '[' && line.back() == ']') {
            section = upper(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            w("expected key = value");
            continue;
        }
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        std::string K = upper(k);
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
                if (section == "BINDINGS" && upper(a) == "NONE" && proto.src == Binding::Key)
                    c.gameKeysNone.push_back(proto.code);
                if (a.empty() || upper(a) == "NONE")
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
        long n;
        float f;
        bool bo;
        auto B = [&](bool& dst) {
            if (toBool(v, &bo))
                dst = bo;
            else
                w("bad bool for " + k);
        };
        auto F = [&](float& dst) {
            if (toFloat(v, &f))
                dst = f;
            else
                w("bad number for " + k);
        };
        auto A = [&](uint32_t& dst) {
            if (toNum(v, &n))
                dst = static_cast<uint32_t>(n);
            else
                w("bad number for " + k);
        };
        if (section == "GENERAL") {
            if (K == "CAPTURE_MOUSE")
                B(c.captureMouse);
            else if (K == "RELEASE_ON_ESC")
                B(c.releaseOnEsc);
            else if (K == "TOGGLE_KEY") {
                int d;
                if (keyFromName(v, &d))
                    c.toggleKeyDik = d;
                else if (upper(v) == "NONE")
                    c.toggleKeyDik = -1;
                else
                    w("unknown key " + v);
            } else if (K == "CONTROLS_VERSION") {
                if (toNum(v, &n))
                    c.controlsVersion = static_cast<int>(n);
                else
                    w("bad number for " + k);
            } else if (K == "LOOK_MODE") {
                std::string u = upper(v);
                if (u == "DIRECT")
                    c.lookMode = LookMode::Direct;
                else if (u == "STICK")
                    c.lookMode = LookMode::Stick;
                else
                    w("look_mode must be direct|stick");
            } else
                w("unknown key " + k);
        } else if (section == "MOUSE") {
            if (K == "SENSITIVITY_X")
                F(c.sensX);
            else if (K == "SENSITIVITY_Y")
                F(c.sensY);
            else if (K == "INVERT_X")
                B(c.invertX);
            else if (K == "INVERT_Y")
                B(c.invertY);
            else if (K == "PITCH_HOLD_MS")
                F(c.pitchHoldMs);
            else if (K == "PITCH_PERSIST")
                B(c.pitchPersist);
            else if (K == "DRAGON_MODE") {
                std::string u = upper(v);
                if (u == "STICK")
                    c.dragonStick = true;
                else if (u == "DIRECT")
                    c.dragonStick = false;
                else
                    w("dragon_mode must be stick|direct");
            } else if (K == "YAW_SIGN")
                F(c.yawSign);
            else if (K == "PITCH_SIGN")
                F(c.pitchSign);
            else if (K == "FREE_COMBAT")
                B(c.freeCombat);
            else if (K == "CAMERA_FOLLOW")
                F(c.cameraFollow);
            else if (K == "BOW_AIM") {
                const std::string u = upper(v);
                if (u == "MODERN")
                    c.bowAimModern = true;
                else if (u == "CLASSIC")
                    c.bowAimModern = false;
                else
                    w("bow_aim must be modern|classic");
            } else if (K == "BOW_ZOOM")
                F(c.bowZoom);
            else if (K == "PITCH_VIEW_GAIN")
                F(c.pitchViewGain);
            else if (K == "STICK_FULL_COUNTS")
                F(c.stickFullCounts);
            else if (K == "STICK_SIGN_X")
                F(c.stickSignX);
            else if (K == "STICK_SIGN_Y")
                F(c.stickSignY);
            else
                w("unknown key " + k);
        } else if (section == "KEYBOARD") {
            if (K == "WALK_SCALE")
                F(c.walkScale);
            else if (K == "TOGGLE_RUN")
                B(c.toggleRun);
            else if (K == "STRAFE")
                B(c.strafeKeys);
            else if (K == "CURVE_COMPENSATION")
                B(c.curveCompensation);
            else
                w("unknown key " + k);
        } else if (section == "GAMEPAD") {
            if (K == "ENABLED")
                B(c.gamepadEnabled);
            else if (K == "DEADZONE")
                F(c.gamepadDeadzone);
            else
                w("unknown key " + k);
        } else if (section == "MENU") {
            if (K == "CURSOR")
                B(c.menuCursor);
            else if (K == "CLICKS")
                B(c.menuClicks);
            else if (K == "MOUSE")
                B(c.menuMouse);
            else if (K == "DRAW_CURSOR")
                B(c.menuDrawCursor);
            else if (K == "INVENTORY_MOUSE")
                B(c.inventoryMouse);
            else if (K == "INVENTORY_STEP") {
                // Obsolete (pointer travel no longer steps the inventory selection); kept so old files stay quiet.
            } else if (K == "INVENTORY_CLICK") {
                std::string u = upper(v);
                if (u == "SQUARE")
                    c.inventoryClickSquare = true;
                else if (u == "CROSS")
                    c.inventoryClickSquare = false;
                else
                    w("inventory_click must be square|cross");
            } else if (K == "CURSOR_IDLE_MS")
                F(c.menuCursorIdleMs);
            else if (K == "MAP_VIEW")
                B(c.mapView);
            else if (K == "INVENTORY_STRIP")
                B(c.inventoryStrip);
            else
                w("unknown key " + k);
        } else if (section == "ADVANCED") {
            if (K == "CHECK_TURN_GATE")
                B(c.checkTurnGate);
            else if (K == "TURN_CALL_ABI") {
                std::string u = upper(v);
                if (u == "FPR")
                    c.turnCallAbi = CallAbi::FprFloat;
                else if (u == "GPR")
                    c.turnCallAbi = CallAbi::GprBits;
                else
                    w("turn_call_abi must be fpr|gpr");
            } else if (K == "ADDR_CTX_PTR")
                A(c.addrCtxPtr);
            else if (K == "ADDR_GUI_PTR")
                A(c.addrGuiPtr);
            else if (K == "ADDR_SCREEN_PTR")
                A(c.addrScreenPtr);
            else if (K == "ADDR_PADMGR")
                A(c.addrPadMgr);
            else if (K == "ADDR_TURN_FN")
                A(c.addrTurnFn);
            else if (K == "ADDR_TURN_UPVEC")
                A(c.addrTurnUpVec);
            else if (K == "ADDR_INVOKE_CALLBACK")
                A(c.addrInvokeCallback);
            else if (K == "ADDR_TIMER_MS")
                A(c.addrTimerMs);
            else if (K == "GUI_VTABLE_OFFSET")
                A(c.guiVtableOffset);
            else if (K == "ADDR_ACTIVE_SCREEN")
                A(c.addrActiveScreenVar);
            else if (K == "ADDR_MOUSE_CALLBACK")
                A(c.addrMouseCallback);
            else if (K == "ADDR_STICK_MODE_FLAG")
                A(c.addrStickModeFlag);
            else if (K == "ADDR_STICK_MAP")
                A(c.addrStickMap);
            else if (K == "ADDR_UI_ENTRY")
                A(c.addrUiEntry);
            else if (K == "ADDR_CAPTURED_WIDGET")
                A(c.addrCapturedWidgetVar);
            else
                w("unknown key " + k);
        } else
            w("key outside a known section: " + k);
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
        return c;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    f.close();
    const std::string text = ss.str();
    parse(text, c, warnings);
    if (c.controlsVersion < 2) {
        upgradeFile(path, text);
    }
    return c;
}

// Files written before the Order-of-the-Flame style controls (A / D strafe, persistent mouse pitch) get a marker block
// appended: the new keys all default to the new behaviour when absent, so the block only makes them visible (and stops
// the upgrade from running again). Existing user bindings / sensitivities are never touched; the obsolete pitch_hold_ms
// line is left in place (ignored).
void Config::upgradeFile(const std::string& path, const std::string& text) {
    const std::string up = upper(text);
    // An explicit legacy sign may be intentional inversion. Keep every original
    // byte; an absent sign already inherits the current default from defaults().
    const std::string& body = text;
    std::string add = "\n; ---- controls v2 (added automatically): Order of the Flame style controls ----\n";
    add += "[general]\ncontrols_version = 2\n";
    if (up.find("STRAFE") == std::string::npos)
        add += "[keyboard]\nstrafe = 1                 ; A / D strafe (0 = A / D turn like the stock PS2 scheme A)\n";
    if (up.find("PITCH_PERSIST") == std::string::npos)
        add += "[mouse]\npitch_persist = 1          ; the camera pitch stays where the mouse left it (pitch_hold_ms "
               "above is obsolete)\n";
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (o)
        o << body << add;
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
