// The drakan_input.ini schema: the one description of the file that both of its readers use.
//
//   drakan_input::Config::parse   (runtime/input)    what the game plays with
//   dsettings::SettingsStore      (runtime/settings) the settings menus that edit the file
//   drakan_ui::Bindings           (runtime/ui)       the key prompts (default bindings only)
//
// It holds every scalar key ([general] .. [advanced]) with its type, accepted range / spellings and default, the value
// rules for each type, the name tables of the [bindings] sources and actions (keys, mouse buttons, DualShock buttons)
// and the default file itself. Platform free and without dependencies (CMake target drakan::input_ini), so the settings
// and ui libraries can use it without linking the whole input module.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace drakan_input {

// ---- name tables of the [bindings] sections ------------------------------------------------------------------------
// Key names are DirectInput scancodes (DIK_*): "SPACE" <-> 0x39. Lookups ignore case.
bool keyFromName(const std::string& name, int* dik);
const char* nameOfKey(int dik); // nullptr for a scancode without a name
// Every key name in table order (the settings menu's key lists).
const std::vector<std::string>& allKeyNames();
// DualShock button names as the bindings write them: "CROSS", "DPAD_UP" ... -> Pad:: mask (input/FrameInput.h).
bool padButtonFromName(const std::string& name, uint16_t* mask);
// "L3+R3" -> the DualShock mask (button names joined by '+'); NONE / OFF / "" -> 0. False when a name is unknown
// (*mask is left unchanged).
bool padComboFromText(const std::string& text, uint16_t* mask);
std::string padComboToText(uint16_t mask); // "L3+R3"; "NONE" for 0

namespace ini {

// How a value is written and which spellings are accepted.
enum class Type {
    Bool,     // 1 | 0, true | false, yes | no, on | off (any case)
    Float,    // a finite number; out of float range (ERANGE) is rejected
    Int,      // an integer (decimal, 0x hex or leading-0 octal) within [minInt, maxInt]
    Choice,   // one of `choices` (any case)
    Key,      // a key name (keyFromName) or NONE
    PadCombo, // DualShock buttons joined by '+' (padComboFromText), or NONE / OFF
    Ignored,  // obsolete key: any value is accepted and has no effect (old files stay quiet)
};

// One accepted spelling of a Choice key and the value it stands for. Several spellings may share a value (aliases); the
// first spelling of a value is the one written back.
struct Choice {
    const char* name;
    int value;
};

struct KeySpec {
    const char* section; // lower case, as the file writes it: "mouse"
    const char* key;     // "sensitivity_x"
    Type type;
    const char* defaultText; // the value defaultIniText() writes; "" = the default file does not have the key
    long long minInt = 0;    // Type::Int only (inclusive)
    long long maxInt = 0;
    std::vector<Choice> choices; // Type::Choice only
};

// A parsed value; only the member for the key's type is meaningful.
struct Value {
    bool b = false;   // Bool
    float f = 0.0f;   // Float
    long long i = 0;  // Int
    int choice = 0;   // Choice: Choice::value of the matched spelling
    int dik = -1;     // Key: scancode, -1 = NONE
    uint16_t pad = 0; // PadCombo: DualShock mask, 0 = NONE
};

// Every scalar key, grouped by section in file order.
const std::vector<KeySpec>& keys();
// The spec of section/key (any case), or nullptr when the schema does not know it.
const KeySpec* findKey(std::string_view section, std::string_view key);
// True for the six scalar sections ([general] [mouse] [keyboard] [gamepad] [menu] [advanced]), any case.
bool isScalarSection(std::string_view section);

// Parses `text` (already trimmed) by the rule of spec.type. False when the value is rejected; *why (if given) then
// holds the warning without a line number ("bad number for walk_scale").
bool parseValue(const KeySpec& spec, const std::string& text, Value& out, std::string* why = nullptr);
// The spelling written back for a parsed value of a Choice / Key / PadCombo / Bool key ("twin_stick", "F10", "L3+R3",
// "1"); empty for the other types (their number format is the writer's choice).
std::string canonicalText(const KeySpec& spec, const Value& value);
// Parse + canonicalText in one step: "TWIN_STICK" -> "twin_stick". False (out unchanged) when rejected.
bool canonicalize(const KeySpec& spec, const std::string& text, std::string& out);

// The individual value rules (also used for the binding tokens and the environment overrides).
bool parseBool(const std::string& text, bool* out);
bool parseFloat(const std::string& text, float* out);
bool parseInt(const std::string& text, long long* out); // the whole text, no range check beyond long long

// A [bindings] / [menu_bindings] source: a key name, MOUSE1..MOUSE5 or WHEELUP / WHEELDOWN (any case).
bool isBindingSource(const std::string& name);

// The default drakan_input.ini, written when the file does not exist. Its values are the defaults of `keys()` (the
// schema test checks that the two agree) and its [bindings] / [menu_bindings] sections are the default bindings.
const char* defaultIniText();

} // namespace ini
} // namespace drakan_input
