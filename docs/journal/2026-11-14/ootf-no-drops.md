# Order of the Flame without gold and trinket drops: a campaign switch

The owner: "Can we also remove trinkets + gold being dropped in the ootf campaign (make this configurable in the editor,
similar to Spells to disable as it's not required or used in ootf)." Order of the Flame has no shops and no gold, but in
the converted campaign killed orcs (and broken barrels) scattered Drakan 2's gold sacks and trinkets.

## Where the drops come from

Not from the OotF data and not from anything the converter places: the converted levels hold no treasure object (class
`0x1006d`) and no chest (`0x100c5`), in all 23 levels of the 14 November pack. The drops are the Drakan 2 engine's level
gold:

* The PS2 Level Properties class has a "Level Gold Distribution" group: `Total amount of gold to distribute`, allocation
  sizes and `Types of Treasure Available`, a list of treasure classes. The converter clones the template level's Level
  Properties object (`ensureSingletons`, template `LEVEL1NA`, Surdana), which overrides those to 2000 gold and twelve
  `RESOURES.ODB` classes: Scepter, Bracelet, Chalice, Crown, Crystal, Diamond, Emerald, Goblet, Necklace, Ring, Ruby and
  SackoGold (35 to 225 gold each). Every converted level carries that object.
* NPC classes (field 170 `Gold Generation Weight`, 171 `Number of Gold Slots`), breakables and chests (fields 0 and 1) say
  how much of the level's gold an object gets. The converted OotF creatures and props use the Drakan 2 classes, so they
  inherit the disc weights: in Ruined Village 43 orcs (weight 20), 3 slaves (50) and 34 barrels and crates (50).
* At level load the gold is dealt into the gold generator's table (`0x4fbee0`, entries `{actor, gold, ?}`). One function
  hands an object its share: `2eeb98(generator, actor, slots)` removes the actor's entry, splits the gold into up to
  `slots` pieces, makes a treasure object for each (`2ef558`) and lists them as the actor's children. Its callers are the
  creature death `2d7120` (it reads field 171), the breakable's destruction `214488` and the chest's spew `227ec8` (field 1);
  each throws the new children out only when it returned 1. A class with no slots takes the entry out and gets 0, and the
  caller carries on as for an object without gold (several disc barrels have 0 slots).
* Not the same thing: a creature's `Death Items Discarded` (the converter fills it from the OotF data: red and blue
  potions, an invisibility potion) are made and thrown by `2d7120` itself before `2eeb98`, and Rynn's own gold is dropped
  at her death by `25f668` through `2ef728`. Neither goes through the gate.

Decompiled functions used for this stay in `build/review/no-drops/ghidra/` (not tracked).

## What changed

Same pattern as the spells switch (`ootf-no-spells.md`):

| Where | What |
| --- | --- |
| Manifest | `campaign_treasure = false` (TOML word or JSON false; `0`, `no`, `off` also count; missing = drops as on the disc): `hle::mods::Manifest::campaignTreasure`, test in `mod_test`. |
| Runtime | `drakan_game::Campaign::treasure`, `drakan_game::treasureEnabled()`, set in `select()` with the spells (log `[game] gold and trinket drops off (ootf)`). `drakan_campaign_treasure.inl` hooks `2eeb98`: while drops are off and no PvP arena is running it calls the original with `slots` = 0, the disc's own no-gold path (log `[treasure] actor ...: no gold or trinkets (4 slots, caller ...)`). With drops on it logs the first shares that made treasure (`gold share made`). PAL address through `drakan_region::guestAddress`. Nothing is stored in a save. Co-op follows the campaign on both sides; PvP arenas keep the disc rules. |
| Editor | Project Settings > Campaign > Gameplay > **Drops** switch under Spells (`ProjectCampaign.treasure`; `campaignMeta` writes `campaignTreasure: false`; `buildManifestToml` writes the key). The Order of the Flame importer has a "Gold and trinket drops" box, off by default (`ConvertRequest.treasure`). `editor/scripts/ootf-convert.ts --campaign 1` writes the key; `--treasure 1` leaves it out. Test in `campaign.test.ts`. |

The converter does not touch the level data: the Level Properties gold and the class weights stay as they are, so the
switch alone decides and turning it back on brings the drops back without a new conversion of the levels. Drakan 2 and
other mods (no key) are unchanged.

Commits: `edc2567b` (editor), `d1345a08` (game), docs `52afcc1a` and a follow-up. No change in the formats submodule.

## Evidence

Play folder `build/review/no-drops/play` (a copy of `baseline-play`: the owner's `drakan.ini`, HD textures, DLSS quality;
private ELF copy). "Before" exe: the dist exe of 14 November 15:30 (`play/drakan_before.exe`) with the dist pack
(`play/mods`, no key). "After" exe: current main built incrementally under the lock into the play folder, with a full
23-level pack converted from current main (`DRAKAN_TERRAIN_CHUNKS=wide`, `--campaign 1`, subtitles;
`build/review/no-drops/packs/after/ootf-levels.zip`, manifest has `campaign_treasure = false`), installed from
`build/review/no-drops/mods-after`. Runs: `tools/run.sh` / `tools/batch.sh` around `rv-physics`'s `push.py` (live_drive):
the owner's ini in a window, god mode (`DRAKAN_PLAYTEST_GOD=1`), fresh card. Fights are live_drive's fight policy: real
input (mouse turn, W, left mouse strikes, or Arokh's fire in Wartok Canyons). Placements bring Rynn to the creatures.
After each kill the run counts every registry object within 3000 units of the corpse by class (treasure `0x1006d`,
potion `0x1000a`, weapon `0x1000e`) and takes shots; logs and frames in `build/review/no-drops/runs/<run>/`.

| Run | Exe, pack | Played / placed | Seen |
| --- | --- | --- | --- |
| `rv-before`, `rv-before3` | before, dist | Ruined Village New Game (harness taps through the opening), Tab / Enter / Enter / Esc equips Atimar, F5; **placed** into detector 2606 (wakes the orc camp) and 9000 units north of the camp; the orcs fought with left mouse strikes | `rv-before`: two orcs die (one by Rynn, one by an exploding barrel), no treasure that time (the shares are random). `rv-before3`: an orc killed (40 to -1 hit points), two treasure objects appear beside it (RAM tally 0 -> 2). |
| `rv-on` | after exe, dist pack (no key) | as above | Two orcs killed, log `gold share made (4 slots, caller 2d7844)` twice, three treasure objects at the corpses; the Scepter with its sparkle is in frame `k0_2` (bottom left). Same behaviour as the owner's exe. |
| `rv-after`, `rv-after2` | after, after pack (key) | as above | `[game] gold and trinket drops off (ootf)`; three barrels (caller `214c84`) and two or three orcs (caller `2d7844`) each log `no gold or trinkets`; no treasure object anywhere near (tally 0 -> 0); the orcs' weapons lie about as before. |
| `wc-on` | after exe, dist pack | Wartok Canyons direct start (on Arokh), **placed** near the orcs north of the start, Arokh's fire | An orc burnt (38 to -1), `gold share made`, two treasure objects at it. |
| `wc-after` | after, after pack | as above | The orc Arokh fought was left at 7 hit points; another orc (actor `00ac2700`) died nearby and logged `no gold or trinkets (caller 2d7844)`; no treasure object; a red potion lies 1,100 units from the fought orc, which by position is that orc's OotF death item (not tied to the actor in the log). |
| `wc-before`, `wc-before2`, `wc-after2` | before / after | as above | The fight policy did not finish an orc from Arokh within the time (hit points 40 -> 36 and the like); no result. |
| `d2-after2` | after, after pack | Drakan 2 `Level 1B - Kragmor` direct start (on Arokh), **placed** 8000 units from the patrolling Purple Orc and Flanker, Arokh's fire | `[game] playing d2`, `gold share made` twice (two creature deaths), one kill recorded by the run (Arokh chased the orc 54,000 units south) with two treasure objects at it (tally 0 -> 2): Drakan 2 keeps its drops with the new exe and the new pack installed. |
| `d2-after` | after | Surdana, the Wartoks east of the town | They never took damage (asleep until the quest); no result. |
| `gr-before` | before | Grotto, two Wartoks | No damage dealt (the fight policy never reached them); no result. |

Sheet: `build/review/no-drops/evidence/sheet_drops.png` (the frames show corpses; the treasure objects mostly lie out of
the camera's view, so the RAM tallies and the log lines carry the comparison; `rv-on` shows a Scepter). Editor:
`evidence/editor-drops-on.png`, `editor-drops-off.png` (Project Settings > Campaign > Gameplay > Drops) and
`editor-import-drops.png` (the importer's box). Converter toggle: `packs/toggle/rv-treasure0.zip` and `rv-treasure1.zip`
(Ruined Village alone, without and with `--treasure 1`): the manifests differ only in `campaign_treasure = false`, the
level files are identical (same content hash in `build`).

Tests and builds as they ran: `ctest --preset runtime-tests` 198 / 198 passed (7 skipped as data-dependent) in a private
worktree with the same code, including `mod_test` 245 checks; `python scripts/run_tests.py` 561 tests passed; editor
`vitest run` 1346 of 1347 passed, the one failure is `packages/app/test/ootfps2model.test.ts` ("ticks pairs by their default
and the player choice": it expects the Scavenger pair off by default; the Drakan 2 models work owns that table, this change
does not touch it); `tsc` for
editor-core and app clean; one incremental game build under the lock into the play folder (exit 0); clang-format clean on
the changed C++ files.

## Not done or not verified

* Played kills cover Ruined Village (on foot) and Wartok Canyons (from Arokh); a Wartok Canyons "before" with the owner's
  exe did not produce a kill, so its "before" is the new exe with the old pack (the gate is off without the key, which is
  the owner's behaviour; `rv-on` and `rv-before3` agree).
* Rynn died to the camp's exploding barrels in several Ruined Village runs despite the harness god mode; the kills and
  tallies happened before that. Owner's ini in a window (not full screen), sound off.
* Chests: no converted level has one, so the chest path (227ec8) was not played; Drakan 2 chests are untouched (no key).
* Not played: co-op (the gate applies on both sides by code reading), a PvP arena (keeps the drops by code reading),
  switching games on the title card in one session (`select()` sets it with the spells, code reading), save and load.
* The inventory still shows a gold counter (always 0 in the campaign) and the Conversation Handler can still give gold if
  a custom campaign sets "Gold to give when complete" (the converted OotF levels leave it at 0).
* USA disc: PAL address through `drakan_region` (no USA entry: the hook stays off there).
* The editor switch was shown in the browser editor without a disc opened; exporting a custom campaign with it off was
  covered by the unit test, not by an exported pack played in the game.
