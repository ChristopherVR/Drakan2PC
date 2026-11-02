# Campaign packs: a project as a campaign of its own (2026-11-02)

## Editor

**Project model** (`editor-core/src/project.ts`): `ModProject.campaign?: ProjectCampaign` = `{ enabled, id, title, description,
start, saves, cover }`, cover `{ kind: 'generated', palette, seed, dragon }` or `{ kind: 'picked', png (base64), name }`. Old project
files load unchanged (no `campaign`). `newCampaign(p)` fills it from the project (id = `campaignSlug(name)`, start = the first copy,
saves = id); `removeLevel` clears a removed start level. `campaignMeta(p)` gives the manifest keys when the campaign is on.

**Manifest** (`mod.ts`): `ModMeta.campaign` is any id now (`CAMPAIGN_ID_PATTERN` `^[a-z0-9][a-z0-9_-]{0,31}$`, refusing
`d1 d2 drakan ask`; `ootf` still the converted Order of the Flame), plus `campaignDescription` (one line, cut at 200) and
`campaignSaves` (`[A-Za-z0-9_-]{1,32}`, written only when it differs from the id). `buildManifestToml` writes `campaign`,
`campaign_name`, `campaign_description`, `campaign_start`, `campaign_saves` above `[target]`. `campaignCoverPath(id)` =
`previews/campaign-<id>.png`.

**Checks** (`validateCampaign(p, ctx)`, codes `campaign-*`): errors for a bad id / save folder, no title, no copy in the project,
no start level, a start level that is not in the project or is a disc level (New Game can only open a level of the pack); a warning
for every project level edited in place ("those edits also show in Drakan itself"); with the exits: every level reached from the
start (error otherwise), every exit of a copy leads to a campaign level or a disc level (error when neither; warning "leads out of
the campaign" for a disc level). The exit index the checks use is the disc's plus the project's own exit edits:
`withExitOverrides(p, index, overrides)`; the app reads the overrides with `projectExitOverrides` (campaign.ts): an exit object's
own `Transition To...` string (field 0, the "Only this exit" retarget) or an edited shared class string, looked up in the story
session. (The Project panel's own exit list still shows the disc destinations of such retargets; not changed here.)

**Cover** (`editor-core/src/cover.ts`): `renderCoverPng({ palette, seed, dragon })` draws 960 x 540 from numbers only (sky gradient,
sun or moon with a glow, four mountain ridges in haze, a dragon silhouette in flight, vignette; palettes dusk, ember, frost, forest,
night), deterministic per seed, PNG written with fflate's zlib and an own CRC (`encodePng`). About 0.6 to 0.9 s per full picture in
node; the editor preview renders it at half size. A picture of the user's own is scaled and cropped to 960 x 540 through a canvas.

**UI**: Project Settings has a **Campaign** pane at the top of the right column (`project/campaign.ts`, `campaign.css`; mounted
through the one optional `sidePane` hook in `ProjectPanelHost`, set in `install.ts`). Header switch "Make this a campaign"; when on:
Title, Short text (with a 0/200 count), Starts in (project levels, disc levels greyed with the reason), Campaign id, Save folder
("Saves go to mc0_<folder>, apart from Drakan's"; follows the id until renamed), the cover (16:9 preview with the title over it, New
picture, palette, Dragon, Use my own picture..., No picture), a note that the Menus tab's changes ship with the campaign and show only
while it is chosen, and "Campaign checks" in message-log style with "Check the exits" (reads every level's exits, then follows them
from the start). The Menus panel shows a banner "Part of the campaign ..." when the project is a campaign.

**Export** (`project/export.ts`): `buildProjectPack` refuses a campaign whose settings have errors (`Campaign: <reason>`), adds the
campaign keys after `levels`/`level_copies`, writes `previews/campaign-<id>.png` and a summary line. The Export dialog's blockers
include the Campaign pane's errors (the exit errors once "Check the exits" ran).

**Headless test pack** (`build/scratch/campaign-pack/make_campaign.ts`, scratch; run from `editor/` with `npx vite-node`): the real
`worker.ts` behind a fake `self`, the ISO by `openAsBlob`, `StorySession.addDestinationString` and the Logic panel's field-0 edit,
`buildProjectPack`, `validateCampaign`. Surdana's exit object **12250** "Transition from 1(A) to 1 (B)" (at 729088 152750 772096,
class string LEVEL1ON.XDB 13) now has its own string **LEVEL1ON.XDB 53** = `Levels\Kragmor Copy\Kragmor Copy.lvl`
(`new/LEVEL1ON.XDB/0400_53_1.bin`) in the copy `Surdana Copy`. Pack: `build/scratch/campaign-pack/mods/test.campaign/` + `mods.ini`:

```toml
levels = ["Surdana Copy", "Kragmor Copy"]
level_copies = ["Surdana Copy = Level 1A - Surdana", "Kragmor Copy = Level 1B - Kragmor"]
campaign = "test-valley"
campaign_name = "The Test Valley"
campaign_description = "Rynn wakes in a valley of copies: Surdana first, then Kragmor."
campaign_start = "Surdana Copy"
campaign_saves = "testvalley"
```

Checks on it: no errors; 8 warnings "leads out of the campaign" (Surdana Copy's other seven exits and Kragmor Copy's way back keep
the disc destinations, as intended for this test).

Tests: `editor-core/test/campaign.test.ts` (ids, manifest keys, project round trip, checks with and without exits, overrides,
deterministic PNG); full `vitest run` 764 of 765 pass (the failure is `app/test/story-place.test.ts`, Place panel work of another
session). `tsc --noEmit` clean for editor-core and app except `app/src/editor/tour/install.ts` (another session). Guide text added to
the campaign guide (`guides.ts`), `docs/reference/editor-guide.md` regenerated (it also picked up other sessions' guide edits that
had not been regenerated).

## Format

`mod.toml` top-level keys (reference: `docs/reference/modding.md` "Campaign packs"): `campaign = "<id>"` (any id of 1-32
`a-z 0-9 _ -`, not `d1 d2 drakan ask`; `ootf` keeps meaning Order of the Flame), `campaign_name`, `campaign_start`, and new
`campaign_description` (one line on the game card) and `campaign_saves` (card folder suffix, default the id). Cover:
`previews/campaign-<id>.png` in the pack (the map picture path copies it to `previews/mods/`). A campaign pack's `ui/*.ini` only
applies while that campaign is chosen. Parsed in `runtime/hle` (`Manifest::campaignDescription`, `campaignSaves`; `mod_test`).

## Runtime

* `drakan_game.{h,cpp}`: the `Game` enum is now the *kind* of the current game (`Drakan`, `Ootf`, `Custom`); the game itself is a
  card index (0 = the disc, then every valid campaign pack in load order): `games()`, `currentIndex()`, `currentName()`,
  `select(index)`, `campaignLevels()`, `validCampaignId`, `validSaveFolder`. `[general] game` / `DRAKAN_GAME` take `ask | d2 | d1 |
  ootf | <campaign id>`.
* **New Game through the level-path rewriter, not a file redirect.** The first run with the file redirect (`Level 0 - Intro` ->
  start, the old OotF route) loaded the copy, but the engine kept calling it "Level 0 - Intro": the exit was "taken in 'Level 0 -
  Intro'", the save was `Level 0 - Intro.rlt`, and Kragmor Copy's arrival fell back to 353584 174500 263193 (no marker names the
  intro). `drakan_game` now installs `drakan_lifecycle::setLevelPathRewriter` (the editor play test's mechanism): the intro load is
  pointed at `<start>` before the engine sees it, so the level has its real name. Not armed when `DRAKAN_START_LEVEL` drives the start
  (test aids, the "dark Relic" case), re-armed when the title picks a campaign, and idle while a multiplayer launch holds its own
  file redirect (`redirect(from, to)`). This applies to Order of the Flame too (`DRAKAN_GAME=d1` run below still boots its intro level).
* Memory card `<card>_<saves>` for every campaign (options seeded from the Drakan card as before); `[game] memory card folder ...`.
* Menu skin per campaign: `drakan_overrides.cpp` keeps a base skin (`ui.ini`, `DRAKAN_UI_SKIN`, packs without `campaign`) and the
  campaign packs' `ui/*.ini` per pack; `drakan_game`'s `SkinFn` lays the chosen campaign's files over the base on every switch.
  The title host (`drakan_settings_host.cpp` `titleDraw`) rebuilds the `TitleScreen` on the main page when the active skin changed
  (`skipGameChoice()`), so theme colours follow at once.
* gameui `TitleScreen`: `TitleState::games` (`{id, name, description, cover}`), the game card is a list (left) with the focused
  game's cover (`ImageKind::LevelPreview`, `mapPreviewId("campaign-<id>")`) and description (right), "Playing" on the current one,
  scrolling past seven entries; `SelectGame.value` = card index; rows are `TitleRow::GameDrakan + i`. A custom campaign's title
  plaque is its name over "A Drakan campaign". Test: `testTitleGameChoice` (gameui_saveload_test, 468 checks pass).
* Co-op host level list for a custom campaign = its start level, then its other levels (`campaignLevels()`).
* `scripts/harness/play_run.py --save-card NAME=DIR` copies another card folder (a campaign's `mc0_<saves>`) into the run.

## In-game proof (build/final-game/bin/drakan.exe, private play folder build/cp-play, empty card)

Pack: the editor-built `build/scratch/campaign-pack/mods/test.campaign` (above) plus a `ui/menus.ini` written by hand in the Menus
panel's format (accent `#20C0A0`, New Game renamed "Enter the Valley"). Recipe `build/scratch/campaign-pack/recipe_full.txt`
(title card -> Down -> Enter -> New Game -> Solo -> `DRAKAN_PLAYTEST_POS` next to the Kragmor exit -> walk in -> Cross through
the message and the level-change save), run with `play_run.py` (`DRAKAN_AUTOPRESS=0`, `DRAKAN_GAME=ask`). Output
`build/scratch/campaign-pack/runs/tv-full2/`, copies in `build/scratch/campaign-pack/evidence/`.

| Step | Evidence |
| --- | --- |
| Game card lists Drakan and The Test Valley with the generated cover and description | `t1_game_card.png`, `t2_card_campaign.png` |
| Choosing it: card folder, campaign skin, title rebuilt | log `[game] memory card folder ...\mc0_testvalley`, `[ui] skin mod test.campaign: ui/menus.ini (campaign 'test-valley'): 3 setting(s)`, `title: rebuilt for the menu skin of The Test Valley`; `t3_main_campaign.png` (plaque, "Enter the Valley", teal focus) |
| New Game opens the start level under its own name | `[game] New Game of 'test-valley' opens 'Surdana Copy'`, `afterLevelLoad level='Surdana Copy' path='.../Surdana Copy/Surdana Copy.lvl'`, `l1_first_level.png` (no Drakan 2 intro) |
| Walking the exit chains to the campaign's second level | `exit taken in 'Surdana Copy' at 729088 152750 772096`, `afterLevelLoad level='Kragmor Copy' path='.../Kragmor Copy/Kragmor Copy.lvl'` |
| Right arrival | `arrival from copy 'Surdana Copy': using the marker of its source 'Level 1A - Surdana'`, frame 200 at 342012 172626 230539 (disc route 342162 172679 230735), `l6_arrived_later.png` |
| Saves land in the campaign's own folder | `card/mc0_testvalley/BESCES-50006Slot1/Surdana Copy.rlt`, `Kragmor Copy.rsg` (+ preview); `card/mc0` stays empty |
| Continue / Load per game (`runs/tv-card`, the run's card copied in with `--save-card`) | The Test Valley: `newest save slot 1 (Kragmor Copy)`, Continue + Load Game (`c2_main_campaign_with_save.png`); back to Drakan: `newest save slot 0`, Load greyed (`c3_main_drakan_no_save.png`) |
| Skin reverts on switching back | `runs/pre-title/t5_main_drakan.png` (native logo, "New Game", red focus) |
| Order of the Flame still boots through the rewriter | `runs/ootf-d1` (`DRAKAN_GAME=d1`, pack `build/ootf-run/d1c/mods`): `New Game of 'ootf' opens 'OotF - intro level'`, loaded playable |

Not done / open: `dist/DrakanPC` was not refreshed (the build's clean step was denied: a game was running from dist); the walk used
`DRAKAN_PLAYTEST_POS` to stand next to the exit (the real walk-in fires the trigger); the Campaign pane of the editor was not
clicked in the browser/desktop app (pack built headless with the editor code); co-op on a custom campaign was not run; a campaign
whose start level is a disc level is refused by the runtime (start must be one of the pack's levels; the editor checks say so).
