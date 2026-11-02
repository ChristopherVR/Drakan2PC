# Level copies and OotF levels: Play straight into the level (2026-11-02)

Follow-up to `editor-play-direct.md`. Since the level-path rewriter (`drakan_lifecycle::setLevelPathRewriter`, armed by
`DRAKAN_START_LEVEL` + `DRAKAN_START_LEVEL_FROM=Level 0 - Intro` in `drakan_playtest.cpp`) the intro's slot opens
**any** level by its path, not only disc levels. The 2026-10-31 failure (`ootf-ingame.md`: "the intro slot does not open
a new level's files") was the file redirect alone; it no longer applies. The editor's Play now sends every level
through the intro slot.

## How the copy pack was made (headless, the editor's own code)

`build/scratch/copydirect/make_copy.ts` (scratch, not in the repo), run from `editor/` with
`npx vite-node <script> <out mods dir> "<source title>" "<copy title>"`: it imports the real `worker.ts` behind a fake
`self` (so `loadLevel` / `readLevelFile` / `readStoryFile` are the worker's), opens the ISO with `openAsBlob`, and calls
`buildProjectPack` (`project/export.ts`) on a one-level project `{ title: <copy>, copyOf: <source> }`. Output folder
plus `mods.ini`; run with `DRAKAN_MODS=<absolute folder>`.

Packs: `build/copydirect/mods` (`Surdana Copy`, folder `SURDANPY`, `LEVEL1TS.*` + `SURDANTS.*` splats) and
`build/copydirect/mods-city` (`City Copy`, `CITYCOPY`).

## In-game runs (dist build, `play_run.py`, empty card, `DRAKAN_GAME=d2`, `_FROM=Level 0 - Intro`)

| Run | Result |
| --- | --- |
| `build/harness/cd-surdana-copy` (`START_LEVEL=Surdana Copy`) | `New Game opens 'Surdana Copy' in place of 'Level 0 - Intro'`, `level load '.../Level 0 - Intro/Level 0 - Intro.lvl' -> '.../Surdana Copy/Surdana Copy.lvl'`, `level 'Surdana Copy' loaded (playable)` at 18.4 s, player under control, quest text and HUD up (`shot1_300.png`). 24 s run, no intro. |
| `build/harness/cd-city-copy` (`City Copy`, a non-Surdana source: the case that crashed on splat names on 2026-11-01) | same lines for `City Copy`, loaded at 18.2 s, Rynn on the arrival teleporter (`shot1_300.png`). |
| `build/harness/cd-ootf-isl` (converted OotF pack `build/ootf-run/hdv-isl-before/mods`, `campaign = "ootf"` pack, `START_LEVEL=OotF - Islands`, still `DRAKAN_GAME=d2`) | `level load ... -> '.../OotF - Islands/OotF - Islands.lvl'`, loaded at 21.4 s, on Arokh in the cave mouth, terrain textured as on the old route (`shot1_300.png`). |
| `build/harness/cd-ootf-intro` (`OotF - intro level`, `build/ootf-run/a1-intro/mods`, `DRAKAN_HLE_LOG=1000000`) | loads and is playable; the screen is black with the HUD, exactly as that pack's old-route run (`build/ootf-run/a1-intro/run/s_walk1.png`): the conversion, not the route. HLE log: the engine opens `OotF - intro level_splats.db/.txd` -> `OOTFINTS.*` (the pack's own name). |
| `build/harness/cdcopy2-walk-735232_772096` (`level_change_walk.py --exit 735232,772096 --dist 5000 --y 153000`, Surdana Copy) | placed after 1 try, walk-in fires: `exit taken in 'Surdana Copy' at 729088 152750 772096`, the level-change save screen, `Level 1B - Kragmor` loaded. **Arrival 353585 174500 263193**, not the disc route's 342162 172679 230735 (see below); the position stays put for 800+ frames at ground height. The recipe's after-load `sync play` never matched, so no `arrived` shot and no clean quit (the 420 s timeout); `pie2-walk` (the disc-level control) ended the same way, and the verdict's "loaded after the walk: nothing" is the harness skipping the first two loads. |

## Exits out of a copy arrive at the fallback point

The arrival lookup (`0x362a60`, `docs/reference/world-model.md`) picks the destination's marker whose own destination text
equals the *old level's name*. The engine now knows the copy by its real name ("Surdana Copy"), Kragmor's way back
names "Level 1A - Surdana", so no marker matches and the player lands at the fallback point (the same 353585 174500 263193
as the broken 2026-11-02 `pie-walk` run, where the engine called the level "Level 0 - Intro"). This is not specific to
the play test: a copy entered from the campaign behaves the same. The old route (copy in Surdana's slot) only hid it
for Surdana copies; for any other copy it looked markers up as "Level 1A - Surdana", which is just as wrong.

Possible runtime fix (not made; runtime C++ was being edited by the lead): in `drakan_levelchange.cpp`, wrap the
arrival lookup `0x362a60` (or the transition `0x362d38`, `transitionEntry`, before it calls the lookup) so that when the
old level is a registered copy (`g_copies`, filled by `registerLevelCopy` from the pack's `level_copies = ["Copy =
Source"]`) and the new level has no marker for the copy's name, the lookup is retried with the source's name (the
copy's exits are the source's exits). The way *into* a copy already works (the copy carries the source's exit back,
whose text equals the old level's name, `level-copy-campaign.md`).

## Editor change

* `launch.ts`: `LaunchExtras.direct` and the `ootf` exclusion are gone; every `startLevel` level except the intro itself
  gets `DRAKAN_START_LEVEL_FROM=Level 0 - Intro`. `playInGame` (`modio.ts`) lost its `extras` parameter; callers
  (`playtest.ts` `playTestDoc`, `editor/panels.ts` Mod panel Play, `project/install.ts` project Play) no longer decide.
* `launch.test.ts`: disc level, copy and OotF title all go direct; the intro and `startLevel: false` do not.
* `docs/reference/env-flags.md` row for `DRAKAN_START_LEVEL` updated.

Not done: the desktop Play button on a copy was not clicked (the env it sends is the one the harness used); the
arrival fix above.
