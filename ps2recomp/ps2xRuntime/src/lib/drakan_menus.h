// Drakan: game menu additions on top of the recompiled GUI screens (title menu, load / save screens).
//
//   * Title menu: the "Quit" row that the PS2 build hides (widget 0x2303 of layout 0x2300) is revealed as the fifth
//   entry below Credits, wired
//     into the keyboard / pad focus chain, confirmed with the game's own "Are you sure you want to quit?" box and
//     closing the game cleanly.
//   * Load Game / Save Game: save pages. The game has four slots (folders BESCES-50006Slot1..4 on the memory card, the
//   folder names come from
//     a four entry string table). Page n maps the four slots to Slot(4n-3)..Slot(4n); page 1 is the original set, so
//     old saves stay valid. Next / Back are drawn every host present, work with the mouse (MenuMouse hotspots), PgUp /
//     PgDn and L1 / R1.
//
// Reverse engineering notes and the verification log: docs/reference/save-slots.md.
#pragma once
#include <cstdint>
#include <string>

class PS2Runtime;
struct R5900Context;

namespace drakan_menus {
// Replaces the hooked guest functions. Call once, after the ELF is loaded (applyDrakanOverrides).
void install(PS2Runtime& rt);
// EE frame boundary: persist native options and publish save-browser geometry.
void tick(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt);
// Signed native-canvas offset for the loading label plus ornamental bar.
float loadingGroupOffset(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt);
struct Pager {
    bool visible = false;
    int width = 512, height = 512, lineHeight = 20;
    int xBack = 0, xNext = 0, xPage = 0, y = 0;
    std::string back, next, page;
};
Pager pager(); // synchronized snapshot, safe for the presentation thread
// Current save page (0 based) and the number of pages.
int page();
int pageCount();
// The modern Save / Load screen (drakan_menu_screens.inl) owns the slot screen: the pager draws no text, offers no
// mouse hotspots and registers no thumbnails while this is on (EE thread).
void setSlotsHosted(bool on);
// Shows save page `p` on the slot screen `screen` (rewrites the slot folder cells, asks the screen to rescan), as
// PgDn / PgUp do. False when the pages are off or the folder table is not as expected (EE thread).
bool showPage(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt, uint32_t screen, int p);
// EE thread: temporary reserved page (Slot33, outside the configurable 1..32 manual slots).
// Restore only after native card I/O has left the slot screen; a live screen is rescanned on cancellation.
bool beginQuickPage(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt);
void endQuickPage(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt, uint32_t screen);
// [general] title_quit: the title's Quit row (also the modern title menu's Quit).
bool titleQuit();
} // namespace drakan_menus
