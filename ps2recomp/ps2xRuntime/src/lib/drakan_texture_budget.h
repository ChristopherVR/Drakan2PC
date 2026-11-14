#pragma once
#include <string>
#include <vector>
class PS2Runtime;
namespace drakan_texture_budget {
// PAL: while a campaign pack is played, or a level from a pack is loaded in any game (a direct start, the editor's play
// test, a multiplayer map), raises the native texture cache budget (0x566666 bytes in a level) to 8 MiB whenever the
// cache would overflow. A STOMP cut scene only starts once everything its first 4 s use is loaded; with the converted
// Order of the Flame levels the full cache could not take the next scene's textures, so Ruined Village's scene 1.2a
// waited from 0 to over 110 s for buffers to free up (docs/journal/2026-11-06/issue-5-village-scene-timing.md), and a
// direct start of Wartok Canyons stayed on the loading screen (docs/journal/2026-11-14/ootf-performance.md). Disc
// levels keep the native budget. drakan.ini [video] game_texture_cache_kb (env DRAKAN_TEX_CACHE_KB): auto (that rule),
// native (never raised), or a number of KiB for every game and level (drakan_texture_budget_policy.h). Each level load
// logs the previous level's over-budget texture loads ("[texbudget] level ...").
void install(PS2Runtime& runtime);
// The LEVELLST.TXT titles of the levels the mod packs add (after the packs are loaded).
void setPackLevels(const std::vector<std::string>& levels);
} // namespace drakan_texture_budget
