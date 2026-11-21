#include "settings/Bindings.h"
#include "settings/Types.h"

#include <algorithm>
#include <atomic>
#include <string_view>
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
        {"DIRECTIONAL_ATTACK", "Directional attack",
         "Hold with Attack and a movement key for a special move (Left Alt by default; see Directional attacks)",
         "Actions"},
        // The eight scheme buttons: the label shown is the command of the game's current controller scheme on foot
        // (actionLabel), the hint adds what the button does on Arokh (actionHint). `label` is scheme A on foot.
        {"TRIANGLE", "Jump", "Triangle", "Actions"},
        {"CROSS", "Crouch", "Cross", "Actions"},
        {"SQUARE", "Attack", "Square", "Actions"},
        {"CIRCLE", "Use Magic / Parry", "Circle", "Actions"},
        {"L1", "Call Arokh / Use", "L1", "Actions"},
        {"R1", "Quick Inventory", "R1", "Actions"},
        {"R2", "Toggle Targeting", "R2", "Actions"},
        {"L2", "Next Target / Strafe", "L2", "Actions"},
        {"R3", "Camera view toggle",
         "R3 (command 0x400a, the same in every scheme): switches the camera view. Mounting Arokh is Call Arokh / Use",
         "Actions"},
        {"L3", "Left stick click", "L3 (no game function known)", "Actions"},
        {"SELECT", "Inventory / Map", "Select", "Interface"},
        {"START", "Game Menu (pause)", "Start", "Interface"},
        {"QUICKSAVE", "Quick save", "Replace the dedicated quicksave (F5 by default)", "Interface"},
        {"QUICKLOAD", "Quick load", "Load the quicksave; unsaved progress is lost (F8 by default)", "Interface"},
        {"DEBUG_MENU", "Debug menu",
         "Open / close the game's developer debug menu (F3 by default; needs Gameplay > Developer)", "Interface"},
        {"ACT:0x114", "Previous item (context-dependent)",
         "Mouse wheel up by default. The game's previous command (0x4034); what it switches depends on what Rynn "
         "holds and whether she rides Arokh",
         "Weapons and items"},
        {"ACT:0x115", "Next item (context-dependent)",
         "Mouse wheel down by default. The game's next command (0x4035); what it switches depends on what Rynn holds "
         "and whether she rides Arokh",
         "Weapons and items"},
        {"QUICK1", "Quick slot 1", "Use / equip the 1st quick-slot item (Quick Inventory order)", "Weapons and items"},
        {"QUICK2", "Quick slot 2", "Use / equip the 2nd quick-slot item", "Weapons and items"},
        {"QUICK3", "Quick slot 3", "Use / equip the 3rd quick-slot item", "Weapons and items"},
        {"QUICK4", "Quick slot 4", "Use / equip the 4th quick-slot item", "Weapons and items"},
        {"QUICK5", "Quick slot 5", "Use / equip the 5th quick-slot item", "Weapons and items"},
        {"ACT:0x0e", "Quick Inventory (key 6)", "PC keymap command 0x403a (Quick Inventory) in every controller scheme",
         "Weapons and items"},
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

namespace {
std::atomic<bool> g_gameHasSpells{true};
std::atomic<int> g_controllerScheme{0};

// The scheme buttons in the order of the game's pad action ids 0x122..0x129.
constexpr const char* kSchemeButtons[8] = {"TRIANGLE", "CROSS", "SQUARE", "CIRCLE", "L1", "L2", "R1", "R2"};
// Pad -> command tables of SCES_500.06 (analysis/input/tables.txt): 0x511ed8 on foot, 0x511ee8 while the dragon-mode
// predicate 0x3d2798 holds (Options_ApplyButtonScheme 0x3fa258); one row per scheme A / B / C, columns as
// kSchemeButtons. The D-pad entries of the same tables are identical in all six and are not listed.
constexpr unsigned kSchemeCommands[2][3][8] = {
    {
        {0x4011, 0x4012, 0x4006, 0x4007, 0x4009, 0x400c, 0x403a, 0x400b}, // foot A
        {0x4009, 0x4006, 0x403a, 0x4007, 0x4011, 0x4012, 0x400b, 0x400c}, // foot B
        {0x4009, 0x400b, 0x403a, 0x400c, 0x4011, 0x4012, 0x4006, 0x4007}, // foot C
    },
    {
        {0x4011, 0x4012, 0x4006, 0x4007, 0x4034, 0x400c, 0x4006, 0x400b}, // Arokh A
        {0x4009, 0x4006, 0x4034, 0x4007, 0x4011, 0x4012, 0x400b, 0x400c}, // Arokh B
        {0x4009, 0x400b, 0x4034, 0x400c, 0x4011, 0x4012, 0x4006, 0x4007}, // Arokh C
    },
};

// Whether `d` is an entry of the gameplay catalogue (not the menu layer's entries of the same ids).
bool isGameAction(const ActionDef& d) {
    const auto& v = gameActions();
    return !v.empty() && &d >= v.data() && &d < v.data() + v.size();
}

int clampScheme(int s) {
    return s < 0 || s > 2 ? 0 : s;
}
} // namespace

void setGameHasSpells(bool spells) {
    g_gameHasSpells.store(spells, std::memory_order_relaxed);
}
bool gameHasSpells() {
    return g_gameHasSpells.load(std::memory_order_relaxed);
}
void setControllerScheme(int scheme) {
    g_controllerScheme.store(clampScheme(scheme), std::memory_order_relaxed);
}
int controllerScheme() {
    return g_controllerScheme.load(std::memory_order_relaxed);
}
char controllerSchemeLetter(int scheme) {
    return static_cast<char>('A' + clampScheme(scheme));
}

unsigned padCommand(std::string_view padAction, int scheme, PadMode mode) {
    for (int i = 0; i < 8; ++i)
        if (padAction == kSchemeButtons[i])
            return kSchemeCommands[mode == PadMode::OnArokh ? 1 : 0][clampScheme(scheme)][i];
    if (padAction == "R3")
        return 0x400a; // the shipped keymap (0x54b7b8), outside the scheme tables: the camera view toggle
    return 0;
}

const char* commandLabel(unsigned command, PadMode mode) {
    const bool air = mode == PadMode::OnArokh;
    switch (command) {
        case 0x4011: // measured: Jump on foot; Space (Triangle) climbs on Arokh (input.md "Arokh on mouse and
                     // keyboard")
            return air ? "Climb" : "Jump";
        case 0x4012:
            return air ? "Dive" : "Crouch";
        case 0x4006:
            return "Attack";
        case 0x4007:
            return gameHasSpells() ? "Use Magic / Parry" : "Parry";
        case 0x4009:
            return "Call Arokh / Use";
        case 0x400c:
            return "Next Target / Strafe";
        case 0x403a:
            return "Quick Inventory";
        case 0x400b:
            return "Toggle Targeting";
        case 0x400a: // acts on the camera object (ctx+0x4b4), journal 2026-10-17/combat-encounters.md
            return "Camera view toggle";
        case 0x4034: // the wheel's previous command; in flight it steps Arokh's breath (journal 2026-10-23)
            return air ? "Previous breath / item" : "Previous item (context-dependent)";
        case 0x4035:
            return "Next item (context-dependent)";
        default:
            return nullptr;
    }
}

const char* actionLabel(const ActionDef& d) {
    if (!d.id)
        return d.label;
    const std::string_view id(d.id);
    if (id == "R3")
        return d.label;
    if (const unsigned cmd = padCommand(id, controllerScheme(), PadMode::OnFoot)) {
        if (isGameAction(d)) // the menu layer reuses the ids with its own labels
            if (const char* l = commandLabel(cmd, PadMode::OnFoot))
                return l;
    }
    return d.label;
}

const char* actionLabelOnArokh(const ActionDef& d) {
    if (!d.id || std::string_view(d.id) == "R3")
        return nullptr;
    if (!isGameAction(d))
        return nullptr;
    const int s = controllerScheme();
    const unsigned foot = padCommand(d.id, s, PadMode::OnFoot), air = padCommand(d.id, s, PadMode::OnArokh);
    if (!air)
        return nullptr;
    const char* a = commandLabel(air, PadMode::OnArokh);
    const char* f = commandLabel(foot, PadMode::OnFoot);
    if (!a || (f && std::string_view(a) == f))
        return nullptr;
    return a;
}

std::string actionHint(const ActionDef& d) {
    std::string h = d.hint ? d.hint : "";
    if (!d.id || !padCommand(d.id, 0, PadMode::OnFoot) || std::string_view(d.id) == "R3")
        return h;
    if (!isGameAction(d))
        return h;
    h += " in controller scheme ";
    h += controllerSchemeLetter(controllerScheme());
    h += " (the game's Options > Controls)";
    if (padCommand(d.id, controllerScheme(), PadMode::OnFoot) == 0x4009)
        h += ". On foot, tap to use or hold to call Arokh";
    if (const char* a = actionLabelOnArokh(d))
        h += std::string(". On Arokh: ") + a;
    return h;
}

const char* textWithoutSpells(unsigned interfaceTextId, int lang) {
    // Our own sentences (not the disc's), one per language slot; no spells, no Magic skill.
    static const char* const kParry[5] = {
        "Parry", "Parieren", "Parade", "Para", "Esquivar",
    };
    static const char* const kSkillsHelp[5] = {
        "Skills: As she defeats creatures, Rynn earns skill points to spend on Melee or Archery. Higher skill levels "
        "let her use stronger weapons.",
        "Fertigkeiten: F\xfcr besiegte Kreaturen erh\xe4lt Rynn Punkte f\xfcr Nahkampf oder Bogenschie\xdf"
        "en. H\xf6here Stufen erlauben st\xe4rkere Waffen.",
        "Comp\xe9tences : en battant des cr\xe9"
        "atures, Rynn gagne des points \xe0 r\xe9partir entre le combat et le tir \xe0 l'arc. Un niveau plus "
        "\xe9lev\xe9 donne acc\xe8s \xe0 des armes plus puissantes.",
        "Abilit\xe0: sconfiggendo le creature, Rynn ottiene punti da assegnare a Mischia o Arco. Livelli pi\xf9 alti "
        "permettono di usare armi pi\xf9 potenti.",
        "Habilidades: al derrotar criaturas, Rynn gana puntos para lucha o tiro con arco. Los niveles m\xe1s altos le "
        "permiten usar armas m\xe1s poderosas.",
    };
    if (lang < 0 || lang >= 5)
        return nullptr;
    switch (interfaceTextId) {
        case 10:
            return kParry[lang];
        case 150:
            return kSkillsHelp[lang];
        default:
            return nullptr;
    }
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
bow_aim = ootf
dragon_mode = mouse
dragon_sensitivity = 1.0
dragon_smoothing = 0
dragon_camera_carry = 1
yaw_sign = -1
pitch_sign = -1
stick_full_counts = 40
stick_sign_x = 1
stick_sign_y = 1

[keyboard]
strafe = 1
dragon_strafe = 1
walk_scale = 0.5
curve_compensation = 1
directional_attack = modifier

[gamepad]
enabled = 1
deadzone = 0.15
layout = auto

[bindings]
W = LSTICK_UP
S = LSTICK_DOWN
A = LSTICK_LEFT
D = LSTICK_RIGHT
LSHIFT = WALK
LALT = DIRECTIONAL_ATTACK
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
F3 = DEBUG_MENU
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
