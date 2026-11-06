# Issue 5: OotF progression, part 2 (Islands rift, Rift Crystal whirlpool, Volcanic)

Follows [ootf-progression.md](ootf-progression.md). Same method: `DRAKAN_START_LEVEL` direct starts, `DRAKAN_PLAYTEST_POS`
placement, then the real trigger, native message box, save screen and load, god mode on. Runs and helpers are private under
`build/harness/ootf-progress2/` (`run.py`, `pair.py`, recipes, `column.mts` / `layers.mts` terrain columns, `ps2objs.mts`
converted objects, `handles.py` live registry handles near a point in a RAM dump, `waits.py` STOMP waits and heap). Pack
`pack6` is the full 23-level conversion with the fix below (and the indexed-texture work of the same day), regenerated from
the working tree; `mods7` is `pack6` with one experimental parameter edit (below).

## Rift Crystal: the exit box was never placed (fixed)

`r08-09c` had Rynn slide over the whirlpool rim and, so it seemed, fall into the void. The position log says otherwise: she
stands at (229497, 205684, 128856) on the basin's invisible floor (PC layer 46, texture `0xfffffffe`, PS2 cell flags
`0xff3c`: invisible, collision kept), inside the PC "Creature Detector" box 307 (scale 2 x 0.5 x 2 at height 206024). The blue
"void" is the undrawn invisible floor. `basin-a` (placed over the centre) shows the same: standing on the floor, in the box,
no touch.

The live registry has no handle at the box (`handles.py`): neither 307 nor its two walk-in copies exist in the world, while
the Islands rift box and its copies (refs 958, 2376, 2377, model `0x20e8f`) and the PS2MUSIC counters of the same level do. The
converter had cloned the class from PS2MUSIC.ODB's "trigger" (`0x1003d`), which has no model. A Detector is the FX box model
(3727) scaled by the object, and the engine does not place an object of a model-less detector class. Detectors are a
`logic` kind, so their template pool is every PS2 class of the engine id; with a template reused only when no fresh one is
left, Rift Crystal's class (late in the order) got the model-less one. Every other campaign detector came from a boxed
SYSTEM / FX template.

Fix (formats `BOXED_TEMPLATE_RFL`, engine 61): detector classes clone only templates that carry a model. In `pack6` the
class is FX 12185 with model 3727. The code went into the formats commit `4a48f67` together with the quest-marker work
(the two sessions shared the working tree); the regression test is `13cddf8` ("clones every detector from a template with
the box model", fails without the rule).

`r08-09d` (`pack6`): placed on the WhirlpoolSteps, W with two jumps, Rynn slides into the basin: touch `1001`, message box,
save screen, exit taken, Islands Night loads at the Exit Whirlpool (596992, 54570, 457728). W, F5, S 2,769 away, F8: back at
(597067, 54570, 459629) exactly; D after the load. Clean scripted quit.

## Islands: the blank screen is the camera in the rock

The bell tower (Bell Tower Interior, Glass Floor, Whirl Doors 01-04, Whirl Bell Lock at 656384, 77832, 474112) stands on
terrain layer 65 (floor 74712 to 77832, 14 x 16 cells) inside the cave system (layers 42 to 90, floors and ceilings). The
rift box 4131 lies under the whirl doors, its floor at 74712. Outside layer 65 there is no terrain at that height at all.
`i06-08` put mounted Rynn at (656384, 76500, 469112): under the platform's south edge, inside the rock, with the chase
camera 4,200 units behind her, beyond every layer. `tower-a` and `tower-b` reproduce the blank screen (World 0.0 fps, no draw
calls in the world phase) with the camera beyond the layer; `tower-c` (camera over layer 65) draws the tower at 312 fps. So
nothing stops: the camera simply has nothing it may draw. The dragon not moving under W is a direct-start trait (`isl-ctl`,
the same at the level start), not the tower's.

The box itself works: `tower-d` (mounted, in the box) and `i06-08c` / `i06-08d` give touch `1001`, message box, save screen and
"exit taken" every time. The real route (the bell, its cut scene, the whirl doors opening) was not played; the Islands
`Islands.ssd` sequences are dropped by the converter ("4 sequence(s) dropped").

**New blocker:** both runs then crash right after Rift Crystal loads: `missing-target` JALR at `0x23d924` (inside `0x23ce18`)
calling `*(*(rider + 0x57c) + 0x104)` through a null pointer, then the input stall. The player arrived on Arokh, and Rift
Crystal has no dragon (the PC level has a Rynn record only). `mods7` sets the Islands exit's Maintain Player Position to 0
(the box is 75,700 units above the exit object, so the kept offset is huge): `i06-08e` crashes the same way, so the offset is
not the cause. Rift Crystal on foot works (direct starts), and Rift Crystal to Islands Night (on foot) works. Not tried: an on
foot entry into the Islands box (a direct Islands start is mounted, and holding Cross in the tower did not dismount, `i06-08d`).

## Volcanic: the Runeblade and Arokh

Test aid: the input script now takes `place x y z [yaw]` (commit `499fa675`), so one run can stop at the Runeblade, Arokh and
the rift room. A direct start plays the opening sequence 80045 for about 82 s first (no wait at action 0); a placement during
it is undone, so the recipes place after 130 s.

* The Runeblade (2239) sits on the sanctum platform under an "ice cover" (2241) that the fire-arrow chain removes (Arrow Detect
  Object 2366, flame generator 2320, "vis" togglers). Placed beside it, W and E took nothing (`v11-12b`): the puzzle has to be
  solved first.
* Arokh (Dragon 40) stands by the start. Rynn placed beside him, E (L1) held, Space, Enter, a walk into him and pad Triangle /
  Circle did not mount her (`v11-mount`; the controlled actor stayed Rynn, state `0x60036`). A real mount is still unproven in
  this port (2026-11-01 coop-dragons note); it may need a level state the direct start lacks.
* Gate: Rynn placed on foot in the rift room walks into box 2318 and its copy (two handles, model `0x20e8f`) with no exit
  message (`v11-12b`, `v11-nogate`): the Runeblade gate and the mount-only rule hold.

## Scenes and heap with the 8 MiB campaign texture budget

`DRAKAN_STOMP_PROBE` on every run of this pass. No sequence waited at action 0:

| Level | Sequences | Wait at action 0 | Guest heap in use | Heap top |
| --- | --- | --- | --- | --- |
| Grimstone Mines (`idle-04`) | 80114 (41 s), 80125 (61 s), 80147 (43 s), back to back | 0 s each | 9.7 to 9.8 MiB | 0xf92000 |
| Volcanic (`v11-12b`) | 80045 (82 s) | 0 s | 14.7 to 17.1 MiB | 0x16d6000 |
| Rift Crystal, Islands Night (`r08-09d`) | none | | 10.1, 12.2 MiB | 0x1326000 |
| Rift World (`idle-12`) | none at the start | | 12.1 MiB | 0x11d3000 |

The highest heap top (0x16d6000, Volcanic with the rift room loaded) stays well under the main stack (0x1fbfdf0) and under the
Ruined Village peak of the scene-timing pass (0x1914000). Volcanic's later "resource pending" probe lines (225 to 253 s, texture
manager `0x4db280`) are preload calls of the finished sequence's actors, with no sequence running.

## Next

1. Mounted arrival in a level without a dragon: find what `0x23ce18` expects at `rider + 0x57c` after `0x362310` clones the
   rider record, and either dismount the rider at the transition or clear the link; or establish that the PC path into the
   Rift Crystal is on foot (the bell tower route) and give the Islands box only the on-foot copy.
2. Volcanic: the fire-arrow puzzle for the Runeblade, then a real mount (decompile the mount condition of `0x4009`).
3. The Islands bell (Bell Hammer Detector 4128 is a lock class: "Lock Code") and its dropped sequences.
