// Which game the player is in: Drakan: The Ancients' Gates (the disc, "d2") or Drakan: Order of the Flame ("d1", a
// converted campaign pack with `campaign = "ootf"` in its mod.toml, docs/reference/modding.md "Campaign packs").
// The choice is made on the title menu (or drakan.ini [general] game / DRAKAN_GAME) and decides
//   * what New Game opens: for d1 the pack's start level replaces the opening cut scene level ("Level 0 - Intro", a file
//     redirect), so no D2 movie plays and the campaign's own intro level comes first;
//   * the memory card: d1 saves live in their own card folder (<mc0>_ootf), so the two games' saves never mix.
// Multiplayer and co-op do not look at this: they stay shared (their launches set their own redirect and clear it
// through redirect(), which falls back to the campaign redirect).
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace drakan_game {

enum class Game : int { Drakan = 0, Ootf = 1 };

struct Campaign {
    std::string id;    // "ootf"
    std::string name;  // menu label
    std::string modId; // the pack
    std::string start; // level title New Game opens
};

using RedirectFn = std::function<void(const std::string& from, const std::string& to)>;

// After the mod packs are installed: the campaigns found, the file redirect setter, and the initial game
// (DRAKAN_GAME / [general] game; "ask" or an unavailable game = d2).
void configure(std::vector<Campaign> campaigns, RedirectFn redirect);

bool ootfAvailable();
Game current();
// "Drakan" / "Drakan: Order of the Flame" (the campaign pack's name when it gave one).
std::string name(Game g);
// True when the title menu has a choice to offer (a campaign pack is installed and [general] game is "ask").
bool offerChoice();
// Switches game: redirect, memory card folder. False when the game is not available.
bool select(Game g);
// A level load redirect request from the multiplayer layer: empty `from` or `to` clears it and restores the campaign
// redirect of the current game. Use this in place of calling the file layer directly.
void redirect(const std::string& from, const std::string& to);
// The level the campaign of the current game begins with: the pack's start level for Order of the Flame, the disc's
// "Level 1A - Surdana" otherwise (what a co-op party opens).
std::string firstLevel();
// The level a "Level 0 - Intro" request really loads in the current game (name unchanged otherwise).
std::string mapLevelName(const std::string& requested);
// "Level 0 - ..." is the opening cut scene level of the disc; a campaign pack's start level is not one.
bool isDiscCutsceneLevel(const std::string& level);

} // namespace drakan_game
