# Order of the Flame without a gold counter: a campaign switch

The owner: "Handle the gold counter in the inventory (tie this in editor when building campaigns)." Order of the Flame has
no gold and no shops, and round 7 already stopped the gold and trinket drops (`campaign_treasure = false`,
`ootf-no-drops.md`), but the inventory still showed Rynn's gold (always 0).

## Where the gold is shown

Seen in the "before" runs below (dist exe and pack of 15 November 01:40):

| Screen | Gold shown | Drawn by |
| --- | --- | --- |
| Modern Tab screen (owner's setting) | header, top right: bag, amount, "Gold" | `gameui::InventoryScreen::drawHeader` |
| Modern Tab screen, Character page | "Gold" line under Health in Attributes | `InventoryScreen` Character page |
| Native Tab screen, Equipment page | spinning bag in a ring and the amount box, bottom right | `4330b0` from the Equipment draw `433610`: text box `42ae00`, then the bag record (section `+0x34`) through `4316c8` (= `42b0a8` placement + `42b308` draw) |
| PC pause menu (modern) | Gold row with the bag in the status panel | `gameui::PauseScreen` |
| Game's own pause menu (owner's setting) | none | |
| Item details | none outside shops (the modern screen already drops Price / Sell lines; the native details have none) | |

`42b988`, which `mouse-menus.md` calls the "gold count", calls the health orb draw `3ac008`: it is the inventory's health
orb, not gold. `4330b0` has a second caller, `447a70`, the shop screen's section draw.

## Own key or `campaign_treasure`?

Its own key, `campaign_gold_counter = false`. With drops off a campaign can still give Rynn gold: a Conversation
Handler's "Gold to give when complete" (level-format.md) and shops (editor-guide.md "add a shop") work without any level
gold, so a custom campaign may want no drops but a real economy. Order of the Flame has neither, so the converter writes
both keys together, and the importer's drops box (`--treasure 1`) keeps both. Missing key = gold shown, so Drakan 2,
other mods and the current dist pack are unchanged.

## What changed

| Where | What |
| --- | --- |
| Manifest | `hle::mods::Manifest::campaignGoldCounter` (same spellings as `campaign_spells`), checks in `mod_test`. |
| Runtime | `drakan_game::Campaign::goldCounter`, `goldCounterShown()`, set in `select()` with the spells and drops (log `[game] gold counter hidden (ootf)`); `campaignGoldHidden()` in `drakan_input_glue.cpp` (PvP arenas keep the disc screens, co-op follows the campaign). |
| Modern screens | `gameui::Snapshot::goldShown` (set by `drakan_tab_screen.inl` and `drakan_pause_screen.inl`), `gameui::goldVisible()` = shown or a shop is open: no header gold, no Character Gold line (the Skills block moves up), no pause Gold row. Checks `testScreenGoldHiddenInCampaign`, `testNoGoldWithoutCounter`. |
| Native Equipment page | Only while the drawn screen (`42fd18`) has five sections, so a shop keeps its gold: `42ae00` called from `4330b0` gets an empty string, and the bag record is placed but its `42b308` draw skipped (`drakan_inventory_views.inl`). |
| Editor | Project Settings > Campaign > Gameplay > **Gold** switch under Drops ("Show Rynn's gold" / "No gold amount on the screens"), `ProjectCampaign.goldCounter`, `campaignMeta` writes `campaignGoldCounter: false`, `buildManifestToml` writes the key. OotF converter (app `ootfconvert.ts`, `scripts/ootf-convert.ts --campaign 1`) writes it unless the drops are kept. Test in `campaign.test.ts`. |

A first version skipped the whole of `4330b0`. That hid the gold but the item details lost their last, indented line
("Equips to right hand"), and skipping the whole `4316c8` did the same; running the text box with an empty string and
skipping only the bag's `42b308` draw keeps the details intact (`runs/ootf-after-native`, `-native2`, `-native3`).

Reference: `docs/reference/modding.md`, "Campaigns without a gold counter". Decompiled functions used: in
`build/review/gold-counter/ghidra/` (not tracked).

## Evidence

Play folder `build/review/gold-counter/play` (copy of `baseline-play`: the owner's `drakan.ini` with modern inventory and
the game's own pause and title menus, HD textures, DLSS; private ELF copy). "Before" exe: the dist exe
(`keep/drakan_before.exe`) with the dist pack. "After" exe: current main built incrementally under the lock into the play
folder. "After" pack: the dist `ootf-levels.zip` with `campaign_gold_counter = false` added to its `mod.toml`
(`keep/ootf-levels-gold.zip`; the levels are byte-identical). Runs: `play_run.py` (New Game on a fresh card, keys through
the game's input path), recipes in `build/review/gold-counter/recipes/`, frames in `runs/<run>/`, sheets in `evidence/`.
Nothing was placed; every screen was opened with key presses (Tab, Enter on the help box, E for the next page, PgDn on the
native screen, Esc for the pause menu).

| Run | Exe, pack, settings | Seen |
| --- | --- | --- |
| `ootf-before-modern` | before, dist pack, owner's ini | header "0 Gold" with the bag; Character page Gold 0; the game's own pause menu without gold. |
| `ootf-default-pack` | after, dist pack (no key), owner's ini | same as before: the default keeps the gold. |
| `ootf-after-modern`, `ootf-after-modern-final` | after, gold pack, owner's ini | log `[game] gold counter hidden (ootf)`; no header gold; Character Attributes: Level, Experience, Health, then Skills (no Gold line). |
| `ootf-before-native` | before, dist pack, `DRAKAN_INVENTORY_SCREEN=native`, `DRAKAN_PAUSE_MENU=modern` (not the owner's settings) | Equipment: bag in its ring and "0"; modern pause: Gold 0 with bag. |
| `ootf-after-native3` | after (final), gold pack, same settings | Equipment: no bag, empty amount box, details complete; a round dark socket stays where the ring was (page art); modern pause: Health only. |
| `d2-after-modern` | after (final), gold pack installed, `DRAKAN_GAME=d2`, `DRAKAN_DEBUG_GOLD=345`, owner's ini | Drakan 2 unchanged: header "345 Gold", Character Gold 345, Spells tab and mana. |
| `d2-after-native` | same with native screen and modern pause | Equipment bag and "345", pause Gold 345. |

Sheets: `evidence/sheet_ootf_modern_before_after.png` (left before, right after), `sheet_ootf_native_before_after.png`,
`sheet_d2_after.png`; editor: `evidence/editor-gold-on.png`, `editor-gold-off.png` (browser editor, new project, no disc).

## Tests and builds as they ran

* `ctest --preset runtime-tests -R "gameui|mod_test|settings|inventory_views|menu_mouse|policy"` in a private worktree
  with the library changes: 44 / 44 passed (`gameui_ram_test` skipped, data-dependent). The full suite was not run (it
  was started and stopped in its GPU tests to save time).
* Editor: `vitest run packages/editor-core packages/app` 698 / 698 passed; `tsc --noEmit` for editor-core and app clean.
* Game: three incremental builds under the lock into the play folder (exit 0; one more attempt failed on another
  agent's unfinished save-page edits in the tree, not this change). The builds compiled the main tree, which held other
  agents' uncommitted work at the time.
* clang-format clean on the changed C++ files.

## Not done or not verified

* The native Equipment page keeps the dark round socket of the bag's ring (page art). Drawing the ring without the bag,
  or covering the socket, was not attempted.
* A shop in a campaign without a gold counter (gold kept by design, code reading only: no converted level has a shop).
* Co-op and PvP arenas (code reading), switching games on the title card in one session (follows `select()` like the
  spells), save and load.
* The I strip and M map views already hid the gold; not rerun.
* The current dist pack has no key: the owner needs a rebuilt exe and a reconverted (or keyed) pack.
* No owner play.
