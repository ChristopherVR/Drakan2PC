// Drakan: the engine's developer debug menu (docs/reference/debug-menu.md).
// Both discs still hold the menu: the object (PAL 0x4e8d48) with 125 options on 11 pages, their handlers and the
// value formatter, and Engine_Tick still updates it while it is open. The retail build compiled out what opened it,
// what routed keys to it and what drew it. The runtime supplies those: the DEBUG_MENU binding (F3 by default) opens and
// closes it (drakan.ini [general] debug_menu = 1, Settings > Gameplay > Developer). While it is open the pad manager's
// command callback is the menu's own key callback (as on the PC development build), the game's input is muted
// (InputModule::setGuestMuted: WASD and the mouse do not move Rynn) and the keyboard / controller reach the native
// handler as pad taps; the settings overlay's HUD draws the current page.
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

void install(PS2Runtime& runtime); // the hook on the menu's key callback (Cross = Enter, Start = close), the HUD
// EE thread, PadMgr_Update boundary after the input frame began: the toggle binding, opening, closing, the callback
// swap and the menu keys (taps the pad manager dispatches to the menu).
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
