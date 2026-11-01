# Built dist, 13-level re-run, campaign end to end (2026-11-01)

Follows [ootf-full-level-runs.md](ootf-full-level-runs.md) and [game-select-ootf-boot.md](game-select-ootf-boot.md).

## Build

`Build-LocalGame.ps1 -Incremental` on `build/final-game` compiled without errors (the game selector, OotF plaque, MP / co-op menu split
and co-op start level were all in the tree) and refilled `dist/DrakanPC`. The first attempt failed only at the link step because another
session's `build/final-game/bin/drakan.exe` was still running (LNK1104); it succeeded when that process had exited. Rebuilt once more
after the plaque change below.

## All 13 levels, full class pipeline (`full.sh`, `recipe_long.txt`, `PS2X_WATCHDOG=60`)

Four runs of the table, first on the working tree of the formats submodule, then on a `git archive` of a given formats commit (a node
loader hook in `build/ootf-run/fmt-*/` redirects `@drakan/formats`, so another session's uncommitted formats edits cannot leak into a
run). "Control" = `[in] autostart: player under control` in the log.

| # | Level | Entities | working tree (g) | formats ff7118b (h) | formats HEAD 70288c6 (z) |
| --- | --- | --- | --- | --- | --- |
| 0 | Intro | 115 | control | control | control |
| 1 | Ruined Village | 2320 | control | control | control |
| 2 | Ruined Village Book | 24 | no player (cut scene) | same | same |
| 3 | Wartok Canyons | 1990 | control | slow load, control at frame 14734, rerun PASS | control |
| 4 | Grimstone Mines | 1593 | control | control | control |
| 5 | Grotto | 537 | control | control | control |
| 6 | Islands | 2614 | control | control | control |
| 7 | Alwarren | 1865 | **hang** (allocator loop) | control | control |
| 8 | Rift Crystal | 2885 | **missing-target JALR** | control | control |
| 9 | Islands Night | 713 | control | control | control |
| 10 | Succubus Lair | 1026 | control | control | control |
| 11 | Volcanic | 1462 | control | **hang** (pc 0x4ac960) | **hang** (pc 0x4ac960) |
| 12 | Rift World | 164 | control | control | control |

(Entity counts are the last column's; they grow as the formats session lands prop fixes.) No level crashed in the last column; Volcano
hangs after the load (alive, never reaches control).

### Rift Crystal regression (found and fixed)

Rift Crystal passed before the template flag filter and crashed after it (reproduced 5 times, different `JALR` targets each time: heap
corruption). Bisect on the converter side: pack diff (14 classes got other templates), then `DRAKAN_DBG_DROPNAME` halving (13 classes:
fail; 7: fail; 7: pass; 4: fail; 3: pass; **Crimson Knight alone: pass**). The filter had moved the Crimson Knight (a creature, engine
class 68) from a flag 32 template to the flag 33 "Savage Leader" template. Fix (formats `ootfclass.ts`): the filter applies only to
non-creature classes (`kind !== 'creature'`); creature templates are chosen as before. Committed in the formats repo as ff7118b (skinned
classes only) and refined in 1724a64 (swept into another session's commit, `kind` test), both fixed Rift Crystal and kept Alwarren.

The "Heuge Sailor" skinned model of a generic object class (rfl 17, clone of "Nichol's Tome Stomp", flag 32) is still filtered, which is
why the `kind` test is better than "skinned": it is exactly the Alwarren Left Turn pattern.

### Volcano (open)

At the cap commit (6ef175a, filter on every class) Volcano reached control; with creatures exempt it hangs deterministically (4 runs,
same pc / ra). Only three class templates differ in Volcano between the two variants: Crimson Knight (Savage Leader -> Invisible
Wartock), Ebon Knight (ToadFist -> Savage Leader), Scavenger (Scavenger -> ScavengerTest). Dropping the 1 Scavenger object: still hangs;
dropping the 103 Ebon Knight objects: passes. So Ebon Knight on Savage Leader hangs, while Rift Crystal's Crimson Knight on the same
template crashes and Volcano's Crimson Knight on it passes: the outcome depends on (PC model, template) pairs, not on the template alone.
Most likely cause: the class data's channel fields (Head / Neck / Upper Body ... joint ids) copied from a template whose rig has other
joint numbers; the formats WIP `copyChannels` (copy the PC joint ids, uncommitted when this was written) targets exactly that. A rule
that picks a template for a creature class by matching joint counts would fix both levels; not done here. Neither filter variant passes
Rift Crystal and Volcano together.

### Source data note: "duplicate object id 77 at index 129" (Alwarren)

Comes from the source data, not the converter: the PC level lists two records with the same class (3249) and the same position
(219136 50000 419840) as object id 77, at index 24 and 129, and object 1389 links to 77. The parser only warns. Grimstone Mines has the
same warning (ids 112, 286). Not checked how the converter's renumbering resolves the link between the two copies; two identical
objects stacked on one spot, so visually harmless.

## Campaign pack end to end (dist build, `DRAKAN_MODS` -> `build/ootf-run/camp/mods`, harness `build/ootf-run/e2e/run.sh`)

Pack: `ootf-convert.ts --campaign 1` with all 13 levels (8654 files, `campaign = "ootf"`, start `OotF 00 - Intro`). Screenshots in
`build/ootf-run/e2e/menu2/` (after the plaque fix) and `build/ootf-run/e2e/exit0/`.

* Game card: `m1_card.png` "Choose your game": Drakan / Drakan: Order of the Flame (log `[game] campaign 'ootf' ... New Game opens
  'OotF 00 - Intro'`, `[game] playing d2 (setting 'ask')` until the choice, then `[game] switched to d1`).
* Order of the Flame main menu `m3_main.png`: plaque with "DRAKAN / Order of the Flame" over the logo, Load Game greyed (empty D1 card).
  **Fixed**: the plaque was 95% opaque and the native logo ghosted through; now opaque (`TitleScreen.cpp`, alpha 255). Position and size
  are fine (it covers the logo and clears the menu panel by 30 px).
* Multiplayer page `m4_multiplayer.png`: Host Game, Join Game, mplayer.com; no Co-op anywhere on the page.
* New Game `m6_newgame.png`: Solo / Co-op card.
* Solo: the first `beforeLevelLoad` is `OotF 00 - Intro` through the redirect of `Level 0 - Intro`; no Drakan 2 intro level or movie.
  The level is the dark cinematic intro (the owl picture, HUD gems); `autostart` reaches control in the Intro level at frame ~15900.
* Level exit: `DRAKAN_GAME=d1 DRAKAN_LEVELCHANGE=0` fires the Intro's exit (dest string 27001), `exit taken in 'Level 0 - Intro'`, then
  `afterLevelLoad 'OotF 01 - Ruined Village'` 1.2 s later with the player put on the arrival marker (220424 19248 176605); the new
  level's exit list names OotF 00 / 02 / 03 (strings 27000 / 27002 / 27003). The picture after arrival (`x3_later.png`) is a dark rock
  silhouette over a flat sky: the camera is outside the village terrain at the arrival marker. Not investigated (a walked exit was not
  tried; this is the host-fired exit, as for the disc levels).

Not run: co-op hosting on the OotF start level in game (needs a second instance).
