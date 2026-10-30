// Drakan: the engine's developer debug menu (docs/reference/debug-menu.md).
// The PAL ELF still holds the menu: the object at 0x4e8d48 with 125 options on 11 pages, their handlers and the value
// formatter, and Engine_Tick still updates it while it is open. The retail build compiled out what opened it, what
// routed keys to it and what drew it. The runtime supplies those: F3 opens and closes it (drakan.ini [general]
// debug_menu = 1, Settings > Gameplay > Developer); while it is open the pad manager's command callback is the menu's
// own key callback (as on the PC development build), so the bound keys and the controller drive the native handler,
// and the settings overlay's HUD draws the current page.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class PS2Runtime;
namespace drakan_input {
class IGuest;
class InputModule;
} // namespace drakan_input

namespace drakan_debug_menu {

void configure();         // [general] debug_menu (env DRAKAN_DEBUG_MENU)
void setEnabled(bool on); // thread safe; off closes an open menu at the next frame
bool enabled();
bool isOpen(); // thread safe

void install(PS2Runtime& runtime); // the hook on the menu's key callback (Cross = Enter, Start = close)
// EE thread, PadMgr_Update boundary after the input frame began: F3 / PgUp / PgDn, opening, closing and the callback
// swap. The pad manager dispatches this frame's keys to the menu.
void beforePadMgr(drakan_input::IGuest& guest, drakan_input::InputModule& input, bool settingsOpen);
// EE thread, after PadMgr_Update: copies the current page (names and formatted values) for the HUD.
void afterPadMgr(drakan_input::IGuest& guest);

struct Row {
    std::string name, value;
};
struct Page {
    int page = 0, pages = 0; // 1-based position among the non-empty pages
    int cursor = 0;
    std::vector<Row> rows;
};
Page snapshot(); // thread safe; empty rows while closed

void installHudLayer(); // draws the page while open
} // namespace drakan_debug_menu
