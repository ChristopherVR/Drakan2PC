#include "ui/Bindings.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace drakan_ui {

static std::string up(std::string s) {
    for (auto& c : s)
        c = (char)std::toupper((unsigned char)c);
    return s;
}
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static const char* kDefaultIni = R"INI(
[bindings]
W = LSTICK_UP
S = LSTICK_DOWN
A = LSTICK_LEFT
D = LSTICK_RIGHT
MOUSE1 = SQUARE
MOUSE2 = CIRCLE
MOUSE3 = R2
SPACE = TRIANGLE
LCONTROL = CROSS
C = CROSS
E = L1
Q = R1
R = L2
F = R2
V = R3
TAB = SELECT
ESC = START
UP = DPAD_UP
DOWN = DPAD_DOWN
LEFT = DPAD_LEFT
RIGHT = DPAD_RIGHT
[menu_bindings]
UP = DPAD_UP
DOWN = DPAD_DOWN
LEFT = DPAD_LEFT
RIGHT = DPAD_RIGHT
ENTER = CROSS
SPACE = CROSS
E = SQUARE
Q = TRIANGLE
PGUP = L1
PGDN = R1
LBRACKET = L1
RBRACKET = R1
MOUSE2 = START | CIRCLE
BACKSPACE = START | CIRCLE
ESC = START | CIRCLE
TAB = SELECT
)INI";

Bindings Bindings::defaults() {
    Bindings b;
    b.parse(kDefaultIni);
    return b;
}

Bindings Bindings::load(const std::string& path, bool* found) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (found)
            *found = false;
        return defaults();
    }
    std::stringstream ss;
    ss << in.rdbuf();
    Bindings b;
    b.parse(ss.str());
    if (!b.haveGame_) {
        Bindings d = defaults();
        b.game_ = d.game_;
        b.haveGame_ = true;
    }
    if (!b.haveMenu_) {
        Bindings d = defaults();
        b.menu_ = d.menu_;
        b.haveMenu_ = true;
    }
    if (found)
        *found = true;
    return b;
}

void Bindings::parse(const std::string& text) {
    std::istringstream in(text);
    std::string line, section;
    while (std::getline(in, line)) {
        for (size_t i = 0; i < line.size(); ++i)
            if ((line[i] == ';' || line[i] == '#') && (i == 0 || std::isspace((unsigned char)line[i - 1]))) {
                line.resize(i);
                break;
            }
        line = trim(line);
        if (line.empty())
            continue;
        if (line.front() == '[' && line.back() == ']') {
            section = up(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        if (section != "BINDINGS" && section != "MENU_BINDINGS")
            continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string src = up(trim(line.substr(0, eq)));
        std::stringstream acts(line.substr(eq + 1));
        std::string a;
        auto& v = section == "BINDINGS" ? game_ : menu_;
        (section == "BINDINGS" ? haveGame_ : haveMenu_) = true;
        while (std::getline(acts, a, '|')) {
            a = up(trim(a));
            if (!a.empty())
                v.push_back({src, a});
        }
    }
}

std::vector<std::string> Bindings::sources(const std::string& action, bool menu) const {
    std::vector<std::string> r;
    std::string A = up(action);
    for (auto& e : menu ? menu_ : game_)
        if (e.action == A)
            r.push_back(e.source);
    return r;
}

Label Bindings::primary(const std::string& action, bool menu) const {
    auto s = sources(action, menu);
    return s.empty() ? Label{} : labelFor(s.front());
}

Label Bindings::labelFor(const std::string& source) {
    std::string s = up(source);
    Label l;
    if (s.size() == 6 && s.rfind("MOUSE", 0) == 0 && s[5] >= '1' && s[5] <= '5') {
        int n = s[5] - '0';
        l.kind = Label::Mouse;
        l.mouseButton = n;
        static const char* caps[] = {"", "LMB", "RMB", "MMB", "M4", "M5"};
        static const char* names[] = {
            "", "Left Mouse Button", "Right Mouse Button", "Middle Mouse Button", "Mouse Button 4", "Mouse Button 5"};
        l.cap = caps[n];
        l.name = names[n];
        return l;
    }
    if (s == "WHEELUP" || s == "WHEELDOWN") {
        l.kind = Label::Wheel;
        l.arrow = s == "WHEELUP" ? 0 : 1;
        l.cap = s == "WHEELUP" ? "WHU" : "WHD";
        l.name = s == "WHEELUP" ? "Mouse Wheel Up" : "Mouse Wheel Down";
        return l;
    }
    struct N {
        const char* src;
        const char* cap;
        const char* name;
    };
    static const N table[] = {
        {"SPACE", "SPC", "Space"},
        {"LCONTROL", "CTL", "Left Ctrl"},
        {"RCONTROL", "CTL", "Right Ctrl"},
        {"LSHIFT", "SHF", "Left Shift"},
        {"RSHIFT", "SHF", "Right Shift"},
        {"LALT", "ALT", "Left Alt"},
        {"RALT", "ALT", "Right Alt"},
        {"TAB", "TAB", "Tab"},
        {"ENTER", "ENT", "Enter"},
        {"NUMPADENTER", "ENT", "Numpad Enter"},
        {"ESC", "ESC", "Esc"},
        {"BACKSPACE", "BSP", "Backspace"},
        {"CAPSLOCK", "CAP", "Caps Lock"},
        {"HOME", "HOM", "Home"},
        {"END", "END", "End"},
        {"PGUP", "PGU", "Page Up"},
        {"PGDN", "PGD", "Page Down"},
        {"INSERT", "INS", "Insert"},
        {"DELETE", "DEL", "Delete"},
        {"MINUS", "-", "-"},
        {"EQUALS", "=", "="},
        {"COMMA", ",", ","},
        {"PERIOD", ".", "."},
        {"SLASH", "/", "/"},
        {"BACKSLASH", "\\", "\\"},
        {"SEMICOLON", ";", ";"},
        {"APOSTROPHE", "'", "'"},
        {"GRAVE", "`", "`"},
        {"LBRACKET", "[", "["},
        {"RBRACKET", "]", "]"},
        {"NUMPADPLUS", "N+", "Numpad +"},
        {"NUMPADMINUS", "N-", "Numpad -"},
        {"NUMPADMUL", "N*", "Numpad *"},
        {"NUMPADDIV", "N/", "Numpad /"},
        {"NUMPADDOT", "N.", "Numpad ."},
    };
    if (s == "UP" || s == "DOWN" || s == "LEFT" || s == "RIGHT") {
        l.kind = Label::ArrowKey;
        l.arrow = s == "UP" ? 0 : s == "DOWN" ? 1 : s == "LEFT" ? 2 : 3;
        static const char* nm[] = {"Up Arrow", "Down Arrow", "Left Arrow", "Right Arrow"};
        l.name = nm[l.arrow];
        l.cap = s == "UP" ? "UP" : s == "DOWN" ? "DN" : s == "LEFT" ? "LT" : "RT";
        return l;
    }
    l.kind = Label::Key;
    for (auto& n : table)
        if (s == n.src) {
            l.cap = n.cap;
            l.name = n.name;
            return l;
        }
    if (s.size() == 7 && s.rfind("NUMPAD", 0) == 0 && std::isdigit((unsigned char)s[6])) {
        l.cap = std::string("N") + s[6];
        l.name = std::string("Numpad ") + s[6];
        return l;
    }
    l.cap = s.substr(0, 3); // single letters, digits, F1..F12 and anything unknown
    l.name = s.size() == 1 ? s : std::string(1, s[0]) + (s.size() > 1 ? [&] {
                                     std::string t = s.substr(1);
                                     for (auto& c : t)
                                         c = (char)std::tolower((unsigned char)c);
                                     return t;
                                 }()
                                                                      : "");
    return l;
}

} // namespace drakan_ui
