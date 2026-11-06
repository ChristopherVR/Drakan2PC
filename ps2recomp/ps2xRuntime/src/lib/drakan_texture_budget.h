#pragma once
class PS2Runtime;
namespace drakan_texture_budget {
// PAL: while a campaign pack is played, raises the native texture cache budget (0x566666 bytes in a level) to 8 MiB
// whenever the cache would overflow. A STOMP cut scene only starts once everything its first 4 s use is loaded; with
// the converted Order of the Flame levels the full cache could not take the next scene's textures, so Ruined Village's
// scene 1.2a waited from 0 to over 110 s for buffers to free up (docs/journal/2026-11-06/
// issue-5-village-scene-timing.md). The disc game keeps the native budget. DRAKAN_TEX_CACHE_KB=<KiB> sets the budget
// for every game, 0 turns the raise off (drakan_texture_budget_policy.h).
void install(PS2Runtime& runtime);
} // namespace drakan_texture_budget
