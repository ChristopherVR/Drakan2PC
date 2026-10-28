#include "settings/Bindings.h"
#include "settings/Types.h"

#include <algorithm>
#include <cctype>

namespace dsettings {

std::string upperCopy(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// ---- BindingLayer ---------------------------------------------------------------------------------------------------
std::vector<std::string> BindingLayer::sourcesFor(const std::string& a) const {
    std::vector<std::string> out;
    for (const auto& l : lines)
        if (std::find(l.actions.begin(), l.actions.end(), a) != l.actions.end())
            out.push_back(l.source);
    return out;
}
std::vector<std::string> BindingLayer::actionsFor(const std::string& s) const {
    for (const auto& l : lines)
        if (l.source == s)
            return l.actions;
    return {};
}
void BindingLayer::bind(const std::string& a, const std::string& s) {
    for (auto& l : lines)
        if (l.source == s) {
            if (std::find(l.actions.begin(), l.actions.end(), a) == l.actions.end())
                l.actions.push_back(a);
            return;
        }
    BindingLine n;
    n.source = s;
    n.actions.push_back(a);
    lines.push_back(std::move(n));
}
void BindingLayer::unbind(const std::string& a, const std::string& s) {
    for (size_t i = 0; i < lines.size(); ++i)
        if (lines[i].source == s) {
            auto& v = lines[i].actions;
            v.erase(std::remove(v.begin(), v.end(), a), v.end());
            if (v.empty())
                lines.erase(lines.begin() + static_cast<long>(i));
            return;
        }
}
void BindingLayer::clearAction(const std::string& a) {
    for (size_t i = 0; i < lines.size();) {
        auto& v = lines[i].actions;
        v.erase(std::remove(v.begin(), v.end(), a), v.end());
        if (v.empty())
            lines.erase(lines.begin() + static_cast<long>(i));
        else
            ++i;
    }
}
void BindingLayer::clearSource(const std::string& s) {
    lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const BindingLine& l) { return l.source == s; }),
                lines.end());
}

// ---- catalogue ------------------------------------------------------------------------------------------------------
const std::vector<ActionDef>& gameActions() {
    static const std::vector<ActionDef> v = {
        {"LSTICK_UP", "Move forward", "Left stick up", "Movement"},
        {"LSTICK_DOWN", "Move backward", "Left stick down", "Movement"},
        {"LSTICK_LEFT", "Turn left", "Left stick left (strafe in control scheme C)", "Movement"},
        {"LSTICK_RIGHT", "Turn right", "Left stick right (strafe in control scheme C)", "Movement"},
        {"WALK", "Walk / run", "Hold to walk, or switch pace with Toggle run enabled", "Movement"},
        {"TRIANGLE", "Jump", "Triangle", "Actions"},
        {"CROSS", "Crouch", "Cross", "Actions"},
        {"SQUARE", "Attack", "Square", "Actions"},
        {"CIRCLE", "Use Magic / Parry", "Circle", "Actions"},
        {"L1", "Call Arokh / Use", "L1", "Actions"},
        {"R1", "Quick Inventory", "R1", "Actions"},
        {"R2", "Toggle Targeting", "R2", "Targeting"},
        {"L2", "Next Target / Strafe", "L2", "Targeting"},
        {"R3", "Right stick click", "R3 (no game function known)", "Targeting"},
        {"L3", "Left stick click", "L3 (no game function known)", "Targeting"},
        {"SELECT", "Inventory / Map", "Select", "Interface"},
        {"START", "Game Menu (pause)", "Start", "Interface"},
        {"QUICKSAVE", "Quick save", "Replace the dedicated quicksave (F5 by default)", "Interface"},
        {"QUICKLOAD", "Quick load", "Load the quicksave; unsaved progress is lost (F8 by default)", "Interface"},
        {"ACT:0x114", "Previous weapon", "Mouse wheel up by default", "Weapons and items"},
        {"ACT:0x115", "Next weapon", "Mouse wheel down by default", "Weapons and items"},
        {"QUICK1", "Quick slot 1", "Use / equip the 1st quick-slot item (Quick Inventory order)", "Weapons and items"},
        {"QUICK2", "Quick slot 2", "Use / equip the 2nd quick-slot item", "Weapons and items"},
        {"QUICK3", "Quick slot 3", "Use / equip the 3rd quick-slot item", "Weapons and items"},
        {"QUICK4", "Quick slot 4", "Use / equip the 4th quick-slot item", "Weapons and items"},
        {"QUICK5", "Quick slot 5", "Use / equip the 5th quick-slot item", "Weapons and items"},
        {"ACT:0x0e", "Quick Inventory (key 6)", "PC keymap id 0x403a, the same as R1", "Weapons and items"},
        {"ACT:0x17", "Item shortcut", "PC keymap action 0x17", "Weapons and items"},
        {"DPAD_UP", "Special move: up", "D-pad up", "Special moves (D-pad)"},
        {"DPAD_DOWN", "Special move: down", "D-pad down", "Special moves (D-pad)"},
        {"DPAD_LEFT", "Special move: left", "D-pad left", "Special moves (D-pad)"},
        {"DPAD_RIGHT", "Special move: right", "D-pad right", "Special moves (D-pad)"},
        {"RSTICK_UP", "Look up (keys)", "Right stick up; the mouse has its own look mode", "Camera keys"},
        {"RSTICK_DOWN", "Look down (keys)", "Right stick down", "Camera keys"},
        {"RSTICK_LEFT", "Look left (keys)", "Right stick left", "Camera keys"},
        {"RSTICK_RIGHT", "Look right (keys)", "Right stick right", "Camera keys"},
    };
    return v;
}

const std::vector<ActionDef>& menuActions() {
    static const std::vector<ActionDef> v = {
        {"DPAD_UP", "Menu up", "D-pad up", "Navigation"},
        {"DPAD_DOWN", "Menu down", "D-pad down", "Navigation"},
        {"DPAD_LEFT", "Menu left / decrease", "D-pad left", "Navigation"},
        {"DPAD_RIGHT", "Menu right / increase", "D-pad right", "Navigation"},
        {"CROSS", "Confirm / hot slot", "Cross", "Buttons"},
        {"CIRCLE", "Back / cancel", "Circle", "Buttons"},
        {"SQUARE", "Use item", "Square (inventory, store)", "Buttons"},
        {"TRIANGLE", "Drop / sell item", "Triangle (inventory, store)", "Buttons"},
        {"L1", "Previous inventory page", "L1", "Inventory pages"},
        {"R1", "Next inventory page", "R1", "Inventory pages"},
        {"SELECT", "Select", "Select", "System"},
        {"START", "Resume game (Start)", "Start", "System"},
    };
    return v;
}

// ---- input source names (copy of input/Config.cpp kKeys)
// ---------------------------------------------------------------
static const char* kKeyNames[] = {
    "ESC",     "1",           "2",         "3",           "4",        "5",          "6",         "7",
    "8",       "9",           "0",         "MINUS",       "EQUALS",   "BACKSPACE",  "TAB",       "Q",
    "W",       "E",           "R",         "T",           "Y",        "U",          "I",         "O",
    "P",       "LBRACKET",    "RBRACKET",  "ENTER",       "LCONTROL", "A",          "S",         "D",
    "F",       "G",           "H",         "J",           "K",        "L",          "SEMICOLON", "APOSTROPHE",
    "GRAVE",   "LSHIFT",      "BACKSLASH", "Z",           "X",        "C",          "V",         "B",
    "N",       "M",           "COMMA",     "PERIOD",      "SLASH",    "RSHIFT",     "NUMPADMUL", "LALT",
    "SPACE",   "CAPSLOCK",    "F1",        "F2",          "F3",       "F4",         "F5",        "F6",
    "F7",      "F8",          "F9",        "F10",         "F11",      "F12",        "NUMPAD7",   "NUMPAD8",
    "NUMPAD9", "NUMPADMINUS", "NUMPAD4",   "NUMPAD5",     "NUMPAD6",  "NUMPADPLUS", "NUMPAD1",   "NUMPAD2",
    "NUMPAD3", "NUMPAD0",     "NUMPADDOT", "NUMPADENTER", "RCONTROL", "NUMPADDIV",  "RALT",      "HOME",
    "UP",      "PGUP",        "LEFT",      "RIGHT",       "END",      "DOWN",       "PGDN",      "INSERT",
    "DELETE",
};

const std::vector<std::string>& allKeyNames() {
    static const std::vector<std::string> v(std::begin(kKeyNames), std::end(kKeyNames));
    return v;
}

bool isKnownSource(const std::string& u) {
    if (u == "WHEELUP" || u == "WHEELDOWN")
        return true;
    if (u.size() == 6 && u.rfind("MOUSE", 0) == 0 && u[5] >= '1' && u[5] <= '5')
        return true;
    for (const char* k : kKeyNames)
        if (u == k)
            return true;
    return false;
}

std::string sourceDisplayName(const std::string& u) {
    struct M {
        const char* ini;
        const char* shown;
    };
    static const M map[] = {
        {"MOUSE1", "Left Mouse"},
        {"MOUSE2", "Right Mouse"},
        {"MOUSE3", "Middle Mouse"},
        {"MOUSE4", "Mouse 4"},
        {"MOUSE5", "Mouse 5"},
        {"WHEELUP", "Wheel Up"},
        {"WHEELDOWN", "Wheel Down"},
        {"LCONTROL", "Left Ctrl"},
        {"RCONTROL", "Right Ctrl"},
        {"LSHIFT", "Left Shift"},
        {"RSHIFT", "Right Shift"},
        {"LALT", "Left Alt"},
        {"RALT", "Right Alt"},
        {"ESC", "Esc"},
        {"BACKSPACE", "Backspace"},
        {"TAB", "Tab"},
        {"ENTER", "Enter"},
        {"SPACE", "Space"},
        {"CAPSLOCK", "Caps Lock"},
        {"MINUS", "-"},
        {"EQUALS", "="},
        {"LBRACKET", "["},
        {"RBRACKET", "]"},
        {"SEMICOLON", ";"},
        {"APOSTROPHE", "'"},
        {"GRAVE", "`"},
        {"BACKSLASH", "\\"},
        {"COMMA", ","},
        {"PERIOD", "."},
        {"SLASH", "/"},
        {"NUMPADMUL", "Num *"},
        {"NUMPADMINUS", "Num -"},
        {"NUMPADPLUS", "Num +"},
        {"NUMPADDOT", "Num ."},
        {"NUMPADENTER", "Num Enter"},
        {"NUMPADDIV", "Num /"},
        {"HOME", "Home"},
        {"END", "End"},
        {"PGUP", "Page Up"},
        {"PGDN", "Page Down"},
        {"INSERT", "Insert"},
        {"DELETE", "Delete"},
        {"UP", "Up"},
        {"DOWN", "Down"},
        {"LEFT", "Left"},
        {"RIGHT", "Right"},
    };
    for (const auto& m : map)
        if (u == m.ini)
            return m.shown;
    if (u.rfind("NUMPAD", 0) == 0 && u.size() == 7)
        return std::string("Num ") + u[6];
    return u; // letters, digits, F1..F12
}

std::string reservedReason(const std::string& s, const std::string& toggleKey) {
    if (s == "F1")
        return "F1 opens this settings menu";
    if (s == "F11")
        return "F11 toggles fullscreen";
    if (!toggleKey.empty() && upperCopy(toggleKey) == s && s != "NONE")
        return "this key toggles mouse capture ([general] toggle_key)";
    return {};
}

const char* builtinDefaultInputIni() {
    return R"INI(; drakan_input.ini - mouse + keyboard configuration for the Drakan port.
[general]
capture_mouse = 1
release_on_esc = 0
toggle_key = F10
look_mode = direct

[mouse]
sensitivity_x = 0.0020
sensitivity_y = 0.0020
invert_x = 0
invert_y = 0
pitch_persist = 1
camera_follow = 5
bow_aim = modern
yaw_sign = -1
pitch_sign = -1
stick_full_counts = 40
stick_sign_x = 1
stick_sign_y = 1

[keyboard]
strafe = 1
walk_scale = 0.5
curve_compensation = 1

[gamepad]
enabled = 1
deadzone = 0.15

[bindings]
W = LSTICK_UP
S = LSTICK_DOWN
A = LSTICK_LEFT
D = LSTICK_RIGHT
LSHIFT = WALK
MOUSE1 = SQUARE
MOUSE2 = CIRCLE
MOUSE3 = R2
SPACE = TRIANGLE
LCONTROL = CROSS
C = CROSS
E = L1
Q = R1
F = R2
R = L2
V = R3
TAB = SELECT
ESC = START
F5 = QUICKSAVE
F8 = QUICKLOAD
1 = QUICK1
2 = QUICK2
3 = QUICK3
4 = QUICK4
5 = QUICK5
6 = ACT:0x0e
I = ACT:0x17
WHEELUP = ACT:0x114
WHEELDOWN = ACT:0x115
UP = DPAD_UP
DOWN = DPAD_DOWN
LEFT = DPAD_LEFT
RIGHT = DPAD_RIGHT

[menu_bindings]
UP = DPAD_UP
DOWN = DPAD_DOWN
LEFT = DPAD_LEFT
RIGHT = DPAD_RIGHT
W = DPAD_UP
S = DPAD_DOWN
A = DPAD_LEFT
D = DPAD_RIGHT
ENTER = CROSS
SPACE = CROSS
E = SQUARE
Q = TRIANGLE
LBRACKET = L1
RBRACKET = R1
PGUP = L1
PGDN = R1
MOUSE2 = START | CIRCLE
BACKSPACE = START | CIRCLE
ESC = START | CIRCLE
TAB = SELECT
)INI";
}

} // namespace dsettings
