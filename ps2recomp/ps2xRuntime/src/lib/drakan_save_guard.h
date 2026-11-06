// Saves and mod pack builds (docs/reference/modding.md "Saves and pack builds"): a save made with one build of a
// content pack is refused, with a message, when the packs active now are another build or missing, instead of
// stalling in the restore. Policy and record format: runtime/hle SaveRecord.h. This file is the game side:
//  * every save written while content packs are active gets `<save>.rsg.mods` beside it (removed when a save is
//    written without content packs);
//  * the modern Load screen marks such a save unavailable with the reason (drakan_menu_screens.inl), Continue stops
//    on the Load screen, quick load shows the reason as a notice;
//  * the native save validation (0x23f170, every load path) refuses it as an unreadable save, the safe fallback.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

#include "gameui/SaveSlots.h"

class PS2Runtime;
namespace hle::mods {
class ModManager;
}

namespace drakan_save_guard {

// The active packs (after ModManager::load and installFiles; null = no mods folder). Call once at start-up.
void configure(const hle::mods::ModManager* mods);
// Hooks the native save validation and the memory card's write observer.
void install(PS2Runtime& runtime);

struct Verdict {
    gameui::SavePackProblem problem = gameui::SavePackProblem::None;
    std::string pack;    // display name of the pack the problem is about
    std::string message; // what the player reads (menu language); empty when the save loads
    bool refused() const { return problem != gameui::SavePackProblem::None; }
};
// A save file on the host (`<card>/<saveId>Slot<n>/<level>.rsg`).
Verdict checkSave(const std::filesystem::path& rsg);
// The save in slot `slot` of the current card (1..32, 33 = the quicksave); a slot without a save loads (nothing to
// refuse). Cached for a second per slot: the Load screen asks every frame.
Verdict checkSlot(int slot);

// A short message over the game for a few seconds (HudSlot::Notice), from any thread.
void showNotice(const std::string& text);
// Registers the notice layer (called with the other HUD layers).
void installHudLayer();

} // namespace drakan_save_guard
