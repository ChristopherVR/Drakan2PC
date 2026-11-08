// Reads the [bindings] / [menu_bindings] sections of drakan_input.ini (format: runtime/input/README.md, "<source> =
// <action> | <action>") and answers "which keyboard/mouse source is bound to this DualShock button". Standalone: no
// dependency on the input module.
#pragma once
#include <string>
#include <vector>

namespace drakan_ui {

// How a bound source is shown to the player.
struct Label {
    enum Kind { None, Key, Mouse, Wheel, ArrowKey, Pad } kind = None; // Pad: a button of an Xbox / PS5 controller
    std::string cap;     // text on a key cap, at most 3 characters ("W", "SPC", "CTL", "LMB")
    std::string name;    // text for sentences ("Space", "Left Ctrl", "Left Mouse Button")
    int mouseButton = 0; // 1 = left, 2 = right, 3 = middle, 4/5 = side buttons (Kind::Mouse)
    int arrow = 0;       // 0 up, 1 down, 2 left, 3 right (Kind::ArrowKey) ; wheel: 0 up, 1 down (Kind::Wheel)
    // Kind::Pad: the face buttons are round badges (padTone 1..4 = the button's colour), the others text caps.
    // padSymbol: 0 = letters in `cap` (A B X Y LB RT ...), 1 cross, 2 circle, 3 square, 4 triangle (PlayStation 5).
    int padSymbol = 0;
    int padTone = 0;
    bool padPs5 = false;
    bool bound() const { return kind != None; }
};

// Which controller the prompts name instead of keys and mouse buttons (ini [general] ui_prompts = xbox | ps5).
enum class PadStyle { None, Xbox, Ps5 };

class Bindings {
public:
    // The prompts of a controller: the face buttons, shoulders, triggers and Start / Select of the original pad are
    // answered with that controller's names; the stick and D-pad directions keep the game's own arrow pictures.
    static Bindings pad(PadStyle style);
    bool isPad() const { return pad_ != PadStyle::None; }

    // Built-in copy of the shipped default bindings (used when the ini is missing or has no [bindings] section).
    static Bindings defaults();
    // Missing / unreadable file -> defaults(). *found tells which one happened.
    static Bindings load(const std::string& path, bool* found = nullptr);
    void parse(const std::string& text);

    // Sources ("SPACE", "MOUSE1", "WHEELUP", ...) bound to a pad action name ("CROSS", "L1", "LSTICK_UP", "DPAD_LEFT",
    // "START" ...), in file order. menu = true reads [menu_bindings].
    std::vector<std::string> sources(const std::string& action, bool menu = false) const;
    // Label of the first source bound to the action (None if unbound).
    Label primary(const std::string& action, bool menu = false) const;
    // Label for one source name.
    static Label labelFor(const std::string& source);

private:
    struct Entry {
        std::string source, action;
    };
    std::vector<Entry> game_, menu_;
    PadStyle pad_ = PadStyle::None;
    bool haveGame_ = false, haveMenu_ = false;
};

} // namespace drakan_ui
