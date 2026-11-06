// What the game tells the player about mod packs it found (docs/reference/modding.md "New packs"). Pure policy over a
// ModsModel, std only: the host asks it whether the title screen shows the "new mod packs" notice, what that notice
// lists, and why an enabled campaign pack is not offered on the "Choose your game" card. New packs are never enabled
// by themselves: the notice only offers it.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "settings/ModsModel.h"

namespace dsettings {

// DRAKAN_MODS_NOTICE: "0" / "off" never shows the notice, "1" / "on" shows it even under scripted input (tests);
// anything else (unset) is Auto: shown unless the input is scripted (harness runs keep their title flow).
enum class ModNoticeMode { Auto, Off, Force };
ModNoticeMode modNoticeModeFrom(const char* value);

// True when the title screen should show the notice now: there are undecided packs, the mode allows it and it was not
// shown yet in this run (it appears once per start; packs left undecided are offered again on the next start).
bool showModNotice(const ModsModel& model, ModNoticeMode mode, bool scriptedInput, bool shownThisRun);

struct ModNoticeContent {
    std::vector<std::string> names; // display names of the undecided packs, in load order
    int campaigns = 0;              // how many of them are campaigns (they add a game to the game card)
};
ModNoticeContent modNoticeContent(const ModsModel& model);

// Packs in the mods folder whose zip or manifest cannot be read, unless the player turned them off: enabled ones (the
// loader skipped them) and new ones (not offered on the new packs card, since enabling them would not load anything).
// In load order. Without a manifest the game cannot tell whether such a pack is a campaign, so the title screen names
// the file instead of explaining the game card.
std::vector<const ModInfo*> unreadablePacks(const ModsModel& model);
// The title line for them: "A pack in the mods folder could not be read: broken.zip." Empty when there is none.
// `translate` as for gameChoiceNoteText (each pack's reason is on its Mods page row).
std::string unreadablePacksText(const ModsModel& model, bool translate);

// Why an enabled campaign pack does not show up on the title's game card.
enum class GameChoiceReason {
    None,           // nothing to explain (no enabled campaign, or the card is offered)
    Pinned,         // the card has campaigns, but [general] game (or DRAKAN_GAME) is not "ask"
    PendingRestart, // a campaign pack was enabled in this session: it loads after a restart
    Skipped,        // the pack is enabled but was not loaded (wrong disc, missing dependency, broken pack ...)
    Rejected,       // the pack loaded but its campaign keys were refused (start level, id ...)
};
// GameChoiceState (what the game card does with the campaigns) is in ModsModel.h.
struct GameChoiceNote {
    GameChoiceReason reason = GameChoiceReason::None;
    std::string pack;    // display name of the pack concerned
    std::string setting; // Pinned: the value; others: empty
    std::string detail;  // Skipped / Rejected: the reason text (English, from the loader)
    bool fromEnv = false;
};
// The pack's campaign is on the game card: its id is listed there and the pack was not refused (a duplicate id shares
// the id of the card's entry but is not offered).
bool campaignOffered(const ModInfo& m, const GameChoiceState& state);
GameChoiceNote gameChoiceNote(const ModsModel& model, const GameChoiceState& state);

// One line for a menu. `translate`: through gameui::menuFormat (the title screen, follows the menu language); false
// keeps the English text (the Mods page, which is English like the rest of the PC settings). Empty for None.
std::string gameChoiceNoteText(const GameChoiceNote& note, bool translate);

} // namespace dsettings
