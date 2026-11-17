# Order of the Flame: the opening scene dips to black and back (2026-11-17)

Owner report: "For every scene that starts (the initial cinematic) it starts out like it's fading to black first, then
back to normal." Probes, runs and frames are under `build/review/ootf-fade/` and `build/ootf-run/fade-*` (not tracked).
Only object, class and field names and numbers are quoted here.

## Method

Single-level campaign packs (`ootf_level_run.py --campaign`, `DRAKAN_AUTOPRESS_STOP_AFTER_LOAD=1` so the opening plays),
the owner's `dist/DrakanPC/drakan.ini` with the harness overrides (960 x 540 window, DLSS off). Recipe: `sync screen -1 1`
(the first world screen after the loading screen), then a shot every 100 ms for 4 s (`f*`), or every 33 ms for 2 s
(`h*`) followed by one every 0.5 s. Each strip prints the mean brightness (0..255) of each frame. The shot files' times
confirm the spacing (100 ms within 5 ms).

## Not every level, not native Drakan 2

| Start | First 3 s | Frames |
| --- | --- | --- |
| Native New Game (the book intro) | black, one smooth fade in over about 3 s (0.6 -> 26) | `strip_native_1a.png` |
| OotF Wartok Canyons (starts in play) | one smooth fade in over about 2.6 s (9 -> 62) | `strip_wc_before.png` |
| OotF Islands (starts in play) | one smooth fade in (3.5 -> 17) | `strip_isl_before.png` |
| OotF Volcanic (starts in a scene) | one smooth fade in over about 2 s (2 -> 18) | `dense_volc_before.png` |
| **OotF Ruined Village (opening 1.1)** | **picture (22 to 29) for 0.3 s, then down to 11 at once, back to 30 over 1.2 s** | `dense_rv_before.png` |

So it is OotF only, and only where the converted level holds a PC level-start fader that the engine runs after the
scene is already on screen.

## Cause: a PC level-start fader on top of the engine's own fade

The PC levels that open on a cinematic place a model-less Fader with **Start Mode 0** (run when the level starts),
Fade In 0, Faded 0 and Fade Out 2 s: the screen is black at the start and clears over 2 s (Ruined Village 2459 "Fader",
Volcanic 2402 "Beginning Fader", the Intro 2739 "Beginning Fader"). The converter kept them as they were (field names
are the same on both platforms, the PS2 class adds Priority Level).

On the PS2 no disc level places a Start Mode 0 screen fader (all 11 Faders in the 15 disc level files on hand start on
a message, by their own parameter or the class default; `tools/ps2lvlfaders.ts`); the engine fades a level in by itself, with or without such a fader (Volcanic below). In Ruined Village the
converted 2459 ran about 0.3 s after the first frames of scene 1.1 were presented, when the picture was already at full
brightness: the screen went dark at once (Fade In 0) and cleared over the remaining fade (`h010` 11.4 -> `h046` 30.0).
That is the "fade to black and back".

The scene faders (1.2a 2458, 1.3 2461, messages at 0 s) are not involved: they start on the scene's On message after the
previous scene ended in black (`2026-11-15/ootf-opening-scenes.md`).

## Fix (formats)

`ootffade.ts` (new): `isLevelStartFadeIn` (Start Mode 0, Fade In 0, Faded Time not -1, Fade Out above 0),
`levelStartFaders(pc)` (PC ids of such model-less faders, object parameters over class defaults) and
`holdLevelStartFaders`, which sets Start Mode 1 (run on a message) on the converted copies. The fader stays in the level,
so a scene that messages it still runs it; it only no longer fires at load. `ootfpipeline.ts` calls it for every level
after the scene-fader pass (class lookup also covers synthesized classes, which Volcanic's 2402 uses). Converter log:
`<level>: N level-start fader(s) left to the engine's fade-in`.

Campaign effect: Ruined Village 2459, Volcanic 2402, Intro 2739 change to Start Mode 1. A fader whose Faded Time is -1
(holds black until a second message) or that fades to black is not touched.

## Proof (game, exe `dist/DrakanPC/drakan.exe` as shipped; the fix is converter-only)

Packs: `fade-rv-before`, `fade-rv-after`, `fade-volc-before` (converted with the hold disabled), `fade-volc-after`,
`fade-intro-after`. Pack check (`tools/packfaders.ts` on the converted LVL): 2459 Start Mode 0 before, 1 after; 2402
gains a Start Mode 1 parameter after.

| Ruined Village, 33 ms shots | before | after |
| --- | --- | --- |
| 0 to 0.1 s (`h001..h004`) | stray first frames (see Open) | the same |
| 0.13 to 0.3 s (`h005..h009`) | scene 1.1 brightens 21 -> 28 | the same, 21 -> 30 |
| 0.33 s (`h010`) | **drops to 11.4** | 29.7 |
| 0.33 to 1.5 s | climbs back 11.9 -> 30.0 | flat 29.4 to 30.0 |
| 2 to 6 s (`j*`) | 30 -> 25 (the scene's own camera) | identical |

Volcanic before and after are identical to 0.1 of a level frame by frame (2.2 -> 18.3 over 1.8 s, the camera cut at
`h055`), so the engine's fade, not 2402, gives that fade in. The Intro (after) stays black for its logo opening, no dip.
The Ruined Village 100 ms strips (`strip_rv_before.png`, `strip_rv_after.png`) show the same: before `f003` 30, `f004` 13,
`f015` 30; after flat from `f003`.

Tests: `test/ootffade.test.ts` (synthetic classes: the level-start fader and an object override are found, scene,
fade-to-black, hold-black and model faders are not; the converted copy gets Start Mode 1, a dropped object is skipped).
`npx vitest run` in `editor/packages/formats`: 79 files, 790 tests pass on the working tree (other sessions' work
included). `prettier --check` clean on the touched files.

## Follow-up: the stray first frames and the short fade (same day)

After the fix above the Ruined Village opening still showed about 0.1 s of an unprepared shot (the scene camera's
position with another view: the well, a floating rock, an unfogged sky) and then came up from about 21 to 30 within
0.2 s, while Volcanic, Grimstone Mines and Wartok Canyons start black and fade in once over 2 to 3 s.

### Why Ruined Village differs

Level Properties carries two engine faders (the disc levels and every converted level): "Starting Fader" = SYSTEM
"Start Fader" (black, held, cleared over 3 s) and "Stomp Fader" = SYSTEM "Stomp Fader" (black, held, cleared over
0.5 s). Bisection with private packs (actors of 1.1 left out, camera keys cut, fader moved, priorities; runs
`fade-rv-*` under `build/ootf-run/`, each with 33 ms shots from the first world frame):

| Pack | First 0.3 s |
| --- | --- |
| 1.1 not started at load (Stomp Player 2217 Initial State 0) | black, 3 s fade (`dense_rv_nostart.png`) |
| 1.1 with no actors (only the held player) / only Delon | black, 3 s fade |
| 1.1 with only its camera | stray frame, 0.2 s ramp (`dense_rv_camonly.png`) |
| 1.1 camera keys cut to t = 0 (a scene of length 0) | dark first frame, 0.3 s ramp |
| camera actions reordered as Volcanic's, sequence word 2 set to 0, fader 2459 moved next to the camera or the player, Priority Level 1, an On message at t = 0 | no change |
| **Level Properties Stomp Fader 0** | **black, one 3 s fade** (`dense_rv_nostomp.png`) |

So a scene that takes the camera makes the engine put up its Stomp Fader in place of the screen fader that is up, and
only one screen fader is drawn at a time (2459, started at t = 0 or at load, was drawn only after the Stomp Fader was
killed, the `kill-list flush` logged just before `h010`). 1.1 takes the camera just after the loading screen (the
input log has gameplay=1 at the first world frame; Volcanic has gameplay=0, its scene already holds the camera), so the
Stomp Fader replaced the Starting Fader a frame or two in: the stray frames are the scene camera before it is set up,
then the Stomp Fader's 0.5 s. Volcanic and Grimstone Mines take the camera during the loading screen and keep the
Starting Fader. Not established: why 1.1 is later (its actors wait for resources, `stomp-probe` pending lines, but the
probe clock does not pin the moment the camera is taken).

The same replacement broke the scene changes inside the level. 1.1 ends with Fader 2460 (fade to black, hold 4 s, then
off at once) and starts 1.2a under it; when 1.2a took the camera the Stomp Fader replaced the black: the picture came up
1.25 s early (36), dipped to 24 under 1.2a's own Fader 2458 and climbed back (`chain_rv_after_a.png`, 0.25 s shots from
1.1 + 57 s). 1.2a to 1.3 did the same (40, 20, then a ramp; `chain_rv_after_b.png`). That is the "fade to black, then
back" at every scene start the owner described.

### Fix (formats)

`ootffade.ts`: `hasScenes(pc)` (the PC level has a Stomp Player) and `dropStompFader`, which sets the converted Level
Properties' "Stomp Fader" to 0. `ootfpipeline.ts` calls it after the singletons (where Level Properties exists). Converter
log: `<level>: Level Properties Stomp Fader 0 (scenes fade with their own faders)`. The PC engine has no scene-start fade;
the OotF scenes do their fades with their own Fader objects, so their timing is now the PC's. The Starting Fader is
untouched. The level-start fader hold (above) stays.

### Proof (pack `fade-rv-final`, exe `dist/DrakanPC/drakan.exe`, no runtime change)

Pack check: Level Properties `Starting Fader=200745 | Stomp Fader=0`, 2459 Start Mode 1.

| Moment | before (`fade-rv-after`) | after (`fade-rv-final`) | Frames |
| --- | --- | --- | --- |
| Level start, 33 ms | stray 22 to 26, ramp 19 -> 30 in 0.2 s | 4, 4, 3, 3, 4 ... one smooth fade, 22 at 2 s, 27 to 29 at 2.5 to 3 s | `dense_rv_after.png`, `final_rv_h.png`, `final_rv_j.png` |
| 1.1 -> 1.2a, 0.25 s | black, 36 at once, 24, back to 46 | black until 2460's hold ends, then the shot at 45 (the PC order: 2458 has cleared under it) | `chain_rv_after_a.png`, `final_rv_a.png` |
| 1.2a -> 1.3, 0.25 s | black, 19, 40, 20, ramp | black, 24, 31, 35, 43: one ramp | `chain_rv_after_b.png`, `final_rv_b.png` |
| End of 1.3, back to play | 19 -> 13 at the handback | the same | `chain_rv_after_c.png`, `final_rv_c.png` |

In the after start the unprepared frames still exist but sit under the Starting Fader (mean 4, not visible as a
picture). The 1.2a camera cut at 1.1 + 65 s (`a032`, 14 before) is a plain cut now.

Other openings with the final converter: Volcanic (`final_volc.png`: 2 rising to 19 over 2 s, the camera cut, as
before) and Grimstone Mines (`final_mines.png`: 4 rising to 17 over 2 s, as before; `dense_mines_before.png`). Esc 15 s
after the first world frame (`final_rv_skip.png`): the picture fades to black under 2460 (run by the skip), stays black about 4 s, then
the following scene and play come back; nothing stays black. One Esc tap here passed both 1.1 and 1.2a; not compared
with a skip before this change.

Tests: `test/ootffade.test.ts` adds the Stomp Fader cases (a level with a Stomp Player gets Stomp Fader 0, the Starting
Fader keeps its class default; a level without one, or without Level Properties, is left alone). `npx vitest run`: 79
files, 798 tests pass on the working tree.

## Open

* Why Ruined Village's 1.1 takes the camera after the loading screen while Volcanic's 9.1 takes it before is not
  established; with the Stomp Fader off it no longer shows.
* Levels whose scenes start in play (Wartok Canyons' Magic Farmer scenes, Succubus Lair 8.1, the Rift World ending,
  the church scenes of Ruined Village) also lose the 0.5 s engine fade at scene start; not played. They now cut in as
  on the PC, under their own faders where the PC scene has one.
* Rift World's "Fade-In Intro" (sequence 162, Fade In 475 at 0 s) is not a level start (no Stomp Player names it; it is
  an entrance scene). Not played.
