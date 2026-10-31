# Game choice (Drakan / Order of the Flame) and the boot path (2026-11-01)

## Why the Drakan 2 intro kept playing

New Game on the title screen opens `Level 0 - Intro` (the opening movie / cut scene level) first and Level 1A after it. A mod pack
only registered levels in `LEVELLST.TXT`, and the `DRAKAN_START_LEVEL` redirect replaces **Level 1A**, so every run of a converted
Order of the Flame level played the whole Drakan 2 intro and then switched. Nothing told the runtime that a pack is a whole game.

## What was built

* Manifest keys `campaign = "ootf"`, `campaign_name`, `campaign_start` (`ModPack.h` `Manifest`, documented in
  `docs/reference/modding.md` "Campaign packs"; the editor writes them: `ModMeta.campaign*`, `ootf-convert.ts --campaign 1`).
* `drakan_game.{h,cpp}`: the chosen game, the campaign list (built in `drakan_overrides.cpp` from the loaded packs), the file
  redirect `Level 0 - Intro` -> campaign start for Order of the Flame (so the Drakan 2 movie never plays), the level-name mapper
  fallback (lifecycle hooks report the real level), and the memory card root: Order of the Flame uses `<card>_ootf`.
  The multiplayer layer's own redirect calls `drakan_game::redirect`; clearing it restores the campaign redirect.
* Title menu: `TitlePage::GameChoice` card (Drakan / Order of the Flame) shown on every visit to the title when a campaign is
  installed and `[general] game = ask`; Back on the main page reopens it; `TitleActionKind::SelectGame` is applied on the EE thread
  at once (`titleExecute`), the save scan is repeated. The Multiplayer pages and co-op are untouched (shared).
* `drakan.ini [general] game = ask|d2|d1`, `DRAKAN_GAME`; `scripts/harness/ootf_level_run.py --campaign`.
* Cut-scene tests that looked for the substring `Intro` in a level name now use `isDiscCutsceneLevel` (`Level 0` prefix), so the
  campaign's own `OotF 00 - Intro` level counts as a level (exit scans, `DRAKAN_LEVELCHANGE`).

## Verification

See the end of this file (appended after the in-game runs).
