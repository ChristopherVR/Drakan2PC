// Which game the player is in: Drakan: The Ancients' Gates (the disc, "d2"), Drakan: Order of the Flame ("d1", a
// converted campaign pack with `campaign = "ootf"` in its mod.toml) or a custom campaign (a pack with any other
// `campaign = "<id>"`, made with the editor's Project Settings > Campaign); docs/reference/modding.md "Campaign packs".
// The choice is made on the title's game card (or drakan.ini [general] game / DRAKAN_GAME) and decides
//   * what New Game opens: for a campaign the pack's start level replaces the opening cut scene level ("Level 0 -
//     Intro", a file redirect), so no D2 movie plays and the campaign's own first level comes first;
//   * the memory card: a campaign's saves live in their own card folder (<mc0>_<saves>, `campaign_saves`, default the
//     campaign id; Order of the Flame <mc0>_ootf), so the games' saves never mix;
//   * the menus: a campaign pack's `ui/*.ini` skin applies only while that campaign is chosen (SkinFn).
// Multiplayer does not look at this: it stays shared (its launches set their own redirect and clear it through
// redirect(), which falls back to the campaign redirect).
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace drakan_game {

// The kind of the current game: the disc, Order of the Flame, or a custom campaign (OotF-only engine aids, such as the
// remote exit touch of drakan_levelchange, test for Ootf).
enum class Game : int { Drakan = 0, Ootf = 1, Custom = 2 };

struct Campaign {
    std::string id;          // "ootf", "test-valley"
    std::string name;        // card title
    std::string description; // card text (one line)
    std::string modId;       // the pack
    std::string start;       // level title New Game opens
    std::string saves;       // card folder suffix (<card>_<saves>)
    std::vector<std::string> levels;
};

// One entry of the title's game card. The card lists the campaigns first (Order of the Flame leads) and the disc game
// last; games(), currentIndex() and select() all use that card order.
struct GameInfo {
    std::string id; // "d2", "ootf", the campaign id
    std::string name, description;
    Game kind = Game::Drakan;
};

using RedirectFn = std::function<void(const std::string& from, const std::string& to)>;
// The menu skin to use for the game being switched to: `campaign` = null for the disc game.
using SkinFn = std::function<void(const Campaign* campaign)>;

// After the mod packs are installed: the campaigns found, the file redirect setter, the skin switch and the initial
// game (DRAKAN_GAME / [general] game = ask | d2 | d1 | ootf | <campaign id>; "ask" or an unavailable game = d2).
void configure(std::vector<Campaign> campaigns, RedirectFn redirect, SkinFn skin = {});

bool ootfAvailable();
Game current();
// The game card's entries (the campaigns in load order, then Drakan: The Ancients' Gates) and the index of the current
// one.
std::vector<GameInfo> games();
int currentIndex();
// The pack id of the current game's campaign, empty for the disc game (campaign credits, ResourceOverride.h).
std::string currentModId();
// The name of the current game ("Drakan", "Drakan: Order of the Flame", the campaign's title).
std::string currentName();
// True when the title menu has a choice to offer (a campaign pack is installed and [general] game is "ask").
bool offerChoice();
// The [general] game value configure() read ("ask" by default); `fromEnv` = DRAKAN_GAME set it.
std::string choiceSetting(bool* fromEnv = nullptr);
// Switches game by its card index: redirect, memory card folder, menu skin. False when there is no such game.
bool select(int index);
// A level load redirect request from the multiplayer layer: empty `from` or `to` clears it and restores the campaign
// redirect of the current game. Use this in place of calling the file layer directly.
void redirect(const std::string& from, const std::string& to);
// The level the campaign of the current game begins with: the pack's start level for a campaign, the disc's
// "Level 1A - Surdana" otherwise (what a co-op party opens).
std::string firstLevel();
// The levels of the current campaign in play order (empty for the disc game).
std::vector<std::string> campaignLevels();
// True when `level` (a level title) belongs to any installed campaign pack, whichever game is being played (a converted
// Order of the Flame level also loads under Drakan 2 through the editor's Play / DRAKAN_START_LEVEL).
bool isCampaignPackLevel(const std::string& level);
// The level a "Level 0 - Intro" request really loads in the current game (name unchanged otherwise).
std::string mapLevelName(const std::string& requested);
// "Level 0 - ..." is the opening cut scene level of the disc; a campaign pack's start level is not one.
bool isDiscCutsceneLevel(const std::string& level);
// The rules of a campaign id (`campaign = "..."`): 1-32 of a-z 0-9 _ -, a letter or digit first, not d1 / d2 /
// drakan / ask. A save folder suffix: 1-32 of letters, digits, _ -.
bool validCampaignId(const std::string& id);
bool validSaveFolder(const std::string& s);

} // namespace drakan_game
