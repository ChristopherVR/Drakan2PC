# Order of the Flame without spells: a campaign switch

The owner: "Spells are also not a thing in OotF, so we may need to allow the editor to let campaigns disable the spells
tab and functionality." Rynn has no spells in Order of the Flame (her magic comes from items and from Arokh), but a
converted OotF campaign played in this engine showed the Drakan 2 Spells tab in both inventory screens, the mana orb on
the HUD and in the native inventory, a mana bar on the modern Character page and in the modern pause menu, and Circle
with empty hands started a spell (summon state `player+0x1040` went to 1).

## What changed

A campaign pack's manifest can say `campaign_spells = false` (reference: `docs/reference/modding.md`, "Campaigns without
spells"). The runtime reads it per campaign and follows the current game (`drakan_game::spellsEnabled()`, switched in
`select()` together with the redirect, card folder and skin); nothing is written to a save, so saves of both games load as
before and Drakan 2 saves are never touched. Multiplayer keeps spells.

| Where | How (all PAL addresses) |
| --- | --- |
| Native Tab screen | Section key steps (R1 / L1, PgDn / PgUp, wheel, tab clicks; they all reach `4309d8`) that would land on section 1 are preset to it so the same step lands beyond it, as multiplayer already did for the Journal (`drakan_views::skipSectionPreset`, now one rule for both); a screen sitting on a disabled section leaves towards Equipment; the stars tab icon is not drawn (`42b308` for record `screen+0x19c + 0x24`); the menu mouse refuses a click on tab 1. A shop's Spells pane (section 1 of 3) is skipped the same way. |
| Modern Tab screen | `gameui::Snapshot::spellsEnabled`: no Spells page in `visiblePages`, `pageEnabled` false, no mana bar on the Character page. |
| HUD | The right orb of `3ac540` shows mana only when it has nothing else to show. Its mana branch alone calls the stats getter `3d5838` (return `3adf9c`); the hook puts the orb's slide offset `hud+0xc` back to -90 there, so the orb's draws land outside the picture. Arrows and item charges still slide it in. Nothing is drawn in its place: the OotF PC HUD has no mana. |
| Pause menus | Modern: no mana bar in the status panel. Original: it shows the HUD, which has no mana orb. |
| Casting | `38f370` ("can a summon start", called only by the Circle command `0x4007` in `23ce18`) answers no, so Circle with empty hands parries; `3895c0` and `38bd48` (the inventory's Summon, the debug console) start nothing. Log `[spells] Circle: no cast`. |
| Controls page | The Circle action reads "Parry" (`dsettings::actionLabel`, `setGameHasSpells`). |
| Editor | Project Settings > Campaign > Gameplay > **Spells** switch (`ProjectCampaign.spells`, `campaignMeta` writes `campaignSpells: false`, `buildManifestToml` writes `campaign_spells = false`). The OotF converter (app `ootfconvert.ts` and `editor/scripts/ootf-convert.ts --campaign 1`) writes it for the OotF campaign. |

Code: `drakan_campaign_spells.inl` (new), `drakan_inventory_views.inl`, `drakan_inventory_views_policy.h`,
`drakan_game.cpp`, `runtime/hle` ModPack, `runtime/gameui` Screen / PauseScreen, `runtime/settings` Bindings,
`runtime/input` MenuMouse; editor-core `mod.ts`, `project.ts`, app `project/campaign.ts`. Decompiled functions used for the
analysis stay in `build/review/ootf-spells/ghidra/` (not tracked).

Can anything still give Rynn a spell? The converted OotF levels have no spell scroll and no shop (the converter maps no
class to the scroll class `0x100b2` and writes no vendor). A spell scroll that a custom campaign places is an ordinary item:
reading it would teach a spell that can never be seen or cast. The converter's stand-in for OotF's Magic Crystal is the
mana power-up (171): it still fills the hidden mana, which nothing uses. OotF Rynn starts with 100 / 100 mana (read from the
runs below), also unseen.

## Evidence

Build: `Build-LocalGame.ps1 -Incremental` into `build/review/ootf-spells/play` (copy of `baseline-play`, the owner's
`drakan.ini`: modern inventory, Original pause and title, HD textures, DLSS; private ELF copy). Pack: the current
`dist/DrakanPC/mods/ootf-levels.zip` with `campaign_spells = false` added to its `mod.toml` ("after"), and unchanged
("before", same exe). Runs: `play_run.py` (New Game on a fresh card, scripted keys and mouse through the game's own input
path), recipes in `build/review/ootf-spells/recipes/`, frames in `build/review/ootf-spells/runs/<run>/`, contact sheets in
`build/review/ootf-spells/evidence/`.

| Run | Seen |
| --- | --- |
| `ootf-before-modern`, `ootf-before-native` | Mana orb at the bottom right of the HUD and in the native inventory; Spells tab and Spells page with its help box in both screens; mana bar on Character and in the modern pause; Circle: `player+0x1040` = 1. |
| `ootf-after-modern2` (owner's settings) | HUD with the health orb only; tabs Inventory, Character, Skills, Journal, Map; Character page without Mana; Q from Skills reaches Character, the native step went from 2 straight to 0 in one tap (no Spells help box); Original pause; F5 / F8 quick save and load, then the same. Circle: `0x1040` stays 0, flags `0x90` gets `0x20` (the parry flag), log `[spells] Circle: no cast`. |
| `ootf-after-native` (`DRAKAN_INVENTORY_SCREEN=native`, `DRAKAN_PAUSE_MENU=modern`) | Native tab row without the stars (a gap between the chest and the face), PgDn Equipment -> Skills, PgUp Skills -> Equipment, no mana orb in the inventory; modern pause shows Health and Gold only; after F8 the same. |
| `d2-after-modern` (`DRAKAN_GAME=d2`, same exe) | Drakan 2 unchanged: mana orb, Spells tab and page, Mana bar, Circle `0x1040` = 1; after F8 the same. |

Sheets: `sheet_ootf_after_vs_before_modern.png`, `sheet_ootf_native_before_vs_after.png`, `sheet_d2_after_modern.png`,
`sheet_saveload.png`, `editor-spells-on.png`, `editor-spells-off.png`.

Tests: `ctest --preset runtime-tests` 190 / 190 passed (7 skipped as data-dependent), including the new checks in
`mod_test` (manifest key, TOML and JSON), `gameui_test` (`testScreenSpellsOffInCampaign`), `gameui_pause_test`
(`testNoManaWithoutSpells`), `settings_test` (Circle label) and `inventory_views_policy` (section skip with spells off,
with the Journal off, in a shop). Editor: `vitest packages/editor-core` 367 passed (round trip of the switch through the
project file and the manifest), `tsc` for editor-core and app clean.

## Switching games

`drakan_game::select()` (the title card at start-up and Switch Campaigns use it) now sets the spells state with the
redirect, card folder and skin, and every hook reads it per call, so a switch takes effect at once. The runs above start
each game in its own process (`DRAKAN_GAME=d2` / `ootf`); a run that plays Drakan 2, quits to the title, switches to Order
of the Flame and plays it in one process was attempted three times (`runs/switch`) and did not get past the Original pause
menu's Quit with scripted input (the injected arrow keys and pad taps did not move its selection; a mouse click on Quit
left the title only after a long stall). So switching inside one session is shown by code reading only.

## Not done or not verified

* Switching from Drakan 2 to Order of the Flame and back inside one session (see above).

* The native tab row keeps a gap where the stars were (the icons are 3D records placed by the screen; moving them was not
  attempted).
* The Skills page (both screens) still lists Magic; points can be spent on it to no effect.
* The game's own help texts that mention spells (the Skills help box: "more powerful weaponry and spells") are unchanged.
* Circle while holding an item of class `0x1001c` or a `0x10090` item, a bow drawn (the orb then shows arrows), riding
  Arokh and a spell scroll in a custom campaign were not played.
* The arena / co-op exception (multiplayer keeps spells) is by code reading only.
* USA disc: the new hooks use PAL addresses like the inventory view hooks they sit beside. They act only while a campaign
  without spells is the current game, and the converted packs target the PAL profile; on the USA disc they were not run.
