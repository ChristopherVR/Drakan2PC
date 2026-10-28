// Action catalogue + input source names for drakan_input.ini (platform free).
#pragma once
#include <string>
#include <vector>

namespace dsettings {

struct ActionDef {
    const char* id;    // token in the ini: TRIANGLE, LSTICK_UP, ACT:0x02 ...
    const char* label; // the game's own term: "Jump", "Crouch" ...
    const char* hint;  // short note ("Triangle on the PS2 pad")
    const char* group; // section heading in the editor
};

// Gameplay layer ([bindings]) and menu layer ([menu_bindings]) action lists, in display order.
const std::vector<ActionDef>& gameActions();
const std::vector<ActionDef>& menuActions();

// Input source names accepted by drakan_input.ini (same table as input/Config.cpp).
bool isKnownSource(const std::string& upperName);
// "LCONTROL" -> "Left Ctrl", "MOUSE1" -> "Left Mouse", "NUMPAD7" -> "Num 7" ...
std::string sourceDisplayName(const std::string& upperName);
// All key names (for the toggle-key combo).
const std::vector<std::string>& allKeyNames();

// Keys the host application keeps for itself; binding them still works but the editor warns.
// Returns a reason string or empty.
std::string reservedReason(const std::string& source, const std::string& toggleKey);

// The built-in default bindings (a copy of input/Config.cpp defaultIniText [bindings]/[menu_bindings]); the host can
// override it through ISettingsHost::defaultInputIni().
const char* builtinDefaultInputIni();

std::string upperCopy(std::string s);

} // namespace dsettings
