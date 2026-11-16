// Action catalogue + input source names for drakan_input.ini (platform free).
#pragma once
#include <string>
#include <string_view>
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
// Which of the game's two pad -> command tables applies: on foot (0x511ed8) or while Rynn rides Arokh (0x511ee8).
enum class PadMode { OnFoot, OnArokh };

// The label to show for an action. For the eight scheme buttons of the gameplay catalogue (Triangle, Cross, Square,
// Circle, L1, L2, R1, R2) it is what the button does on foot in the game's current controller scheme; otherwise
// `d.label`. A campaign without spells shows command 0x4007 as "Parry" instead of "Use Magic / Parry".
const char* actionLabel(const ActionDef& d);
// What a gameplay scheme button does while riding Arokh when that differs from actionLabel (Arokh scheme A: L1 is the
// previous item / breath command, R1 attacks); nullptr when it is the same or the action is no scheme button.
const char* actionLabelOnArokh(const ActionDef& d);
// `d.hint`, for scheme buttons followed by the scheme letter and the Arokh meaning when it differs.
std::string actionHint(const ActionDef& d);
// The game command a pad action token sends (0x4006 ...): the scheme table for the eight scheme buttons, 0x400a for
// R3, 0 for anything else. `scheme` 0..2 = A..C (out of range = A).
unsigned padCommand(std::string_view padAction, int scheme, PadMode mode);
// Short label of a game command (nullptr = unknown); follows gameHasSpells() for 0x4007.
const char* commandLabel(unsigned command, PadMode mode);
// Whether the current game has spells (the host sets it when the game is chosen; default true). Thread safe.
void setGameHasSpells(bool spells);
bool gameHasSpells();
// The game's controller scheme (options object +0x2e8, JoyConfig_Scheme in Drakan 2.cfg): 0 = A, 1 = B, 2 = C. The host
// publishes it while the game runs; default 0. Out-of-range values read as A. Thread safe.
void setControllerScheme(int scheme);
int controllerScheme();
char controllerSchemeLetter(int scheme);
// Own wording for the disc's interface strings that speak of spells (INTERFCE.XDB, PAL string ids: 10 the Circle action
// of the game's controller screen, 150 the Skills help box), shown while the current game has no spells. Windows-1252
// bytes for the language slot `lang` (0 English, 1 German, 2 French, 3 Italian, 4 Spanish); nullptr = keep the disc's.
const char* textWithoutSpells(unsigned interfaceTextId, int lang);

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
