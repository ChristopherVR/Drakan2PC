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

Runs (private play folder, 960x540, evidence in `build/d1menu/`, `build/ootf-run/d1c/`):

* `ootf_level_run.py --campaign` (Intro level as a one-level campaign, `DRAKAN_GAME=d1`): log `[game] playing d1`, the first
  `beforeLevelLoad` is the converted level opened through `Level 0 - Intro` (redirect), no Drakan 2 intro level or movie; HUD up,
  PASS (`build/ootf-run/d1c/run/s_start.png`, copy `build/ootf-run/d1-boot-direct.png`).
* Title menu with the pack installed (`build/d1menu/d1menu_ootf/t_game_card.png`, `t_main_ootf.png`): the card opens first, Order of
  the Flame selected, the label shows under the menu, Load Game greyed (empty D1 card).
* Switching with a Drakan 2 card that holds a save (`build/d1menu/d1menu_switch/s2_d2_main.png`, `s3_ootf_main.png`,
  `s4_d2_again.png`): Drakan shows Continue / Load Game enabled, Order of the Flame none (`title: newest save slot 0`), back to
  Drakan restores them. The native Load row's enable state is refreshed by calling the title's own `43f398(screen)` after the switch.
* Without a campaign pack the card never shows and `[game] playing d2` is logged (pack-free runs are unchanged).

Open: co-op hosting for Order of the Flame should open `drakan_game::firstLevel()` (exposed, not wired: the co-op menu belongs to the
multiplayer session); a campaign in `ask` mode shows the card also when returning from a match to the title. The title art still
says "The Ancients' Gates" (the native art; a separate OotF art would be a pack file). Unit test: `testTitleGameChoice`
(`gameui_saveload_test.cpp`, not run here: no free runtime-tests build; covered by the in-game runs above).

## Follow-up (co-op start level, title plaque)

* Co-op: `setRedirect` (drakan_netplay.cpp) treats `drakan_game::firstLevel()` like the disc's first level (New Game already opens it;
  for d1 through the campaign redirect), and the Host level list for d1 is the campaign start first, then the pack's other levels.
  Not run in game (no free instance: other sessions had drakan.exe open); host-side only by code reading.
* Title: `TitleState::brandTitle/brandSubtitle` draw a generated "DRAKAN / Order of the Flame" plaque over the native logo when d1 is
  chosen. A pack-supplied `campaign_title_image` is not implemented. `testTitleGameChoice` and all gameui tests pass (runtime-tests).
* dist/DrakanPC not refreshed (games running); run `Build-LocalGame.ps1 -Incremental` when idle.
