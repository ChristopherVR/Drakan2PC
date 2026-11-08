# OotF Intro: black for 12 s before the first shot

User report: after New Game of Order of the Flame, `OotF 00 - Intro` loads and the screen stays black for about 11 s before
the owl face appears. Cause: a converted backdrop box. The Intro's "Black Background" is a closed box whose faces look
inward. The PC culls its near wall from outside, so the camera sees the logo inside the box. The PS2 draws model strips
without back-face culling, so that wall covered the whole view until the camera flew into the box. Fixed in the
converter (formats `fe67ab9`). A second converter defect found on the way also hid the Intro's logos: model faders were
converted as screen faders (formats `013b5e9`).

Runs: private two-level packs (Intro + Ruined Village, `--campaign 1`, template LEVEL1NA) under `build/ootf-run/blk-*`,
the user's `dist/DrakanPC/drakan.exe`, `DRAKAN_GAME=d1`, New Game by the script, `DRAKAN_STOMP_PROBE=1` for sequence
times, screenshots every 0.5 s (0.1 s around the change). `DRAKAN_AUTOPRESS=0` is needed: `play_run.py` taps Cross every
3 s by default, and since the Intro scenes hold the player those taps skip them. That made the first two runs look as if
the scenes overlapped. Contact sheets and helper scripts are private under `build/introblack/`.

## What the PC Intro is

There is no movie in the install. The level plays three sequences of `Intro Level.ssd` back to back:

| Ref (PS2) | PC id | Name | Length | Content |
| --- | --- | --- | --- | --- |
| `80006` | 450 | Intro 1 | 14.5 s | Psygnosis: the owl face flies at the camera from t = 0 (keys 0 to 3.86 s), plate, "Presents" |
| `80007` | 451 | Intro 2 | 10.2 s | Surreal Software logo |
| `80004` | 452 | Intro 3 | 16.4 s | perched owl, "DRAKAN Order of the Flame" letters, Arokh, main theme at 0.19 s |

The owl is on screen from the first frame. Intro 1's camera moves from x 46821 to 39223 by 3.8 s, holds until 11.9 s, then
closes in on the owl (30728 by 14.0 s). The scene's only fade action is a message to the model-less "scene fade" at
7.75 s of Intro 2 (fade in 2 s, out on the next message at 10.24 s). There is no black hold at the start: (a) is out.

## Measurements (before)

| Check | Result |
| --- | --- |
| Scene start (d) | Intro 1 ticks from the level load: `elapsed=0.0000` at probe 18.0 s, `afterLevelLoad` at 18.4 s (lifecycle clock), 1 s of elapsed per second |
| Readiness (b) | no wait at action 0; the preloader cursor runs ahead (`preload=37..52`), no pending line |
| Faders (c) | Level Properties `Starting Fader` = 0 and `Stomp Fader` = 0 (diagnostic packs `blk-nofader`, `blk-nostomp`): black until +12.4 s as before |
| Fog | `Min/Max Fog Depth` 160 lu (`blk-fog`): no change |
| Music (e) | the theme starts at 0.19 s of Intro 3, long after the black |
| First picture | pure black (max pixel 0 below the overlay) up to +12.31 s of Intro 1, then at +12.41 s the full plate at once (`blk-fine`, 0.1 s shots) |

A pop with no fade-in, while the camera moved, meant something opaque was in front of it. Dropping objects by class name
(`DRAKAN_DBG_DROPNAME`) narrowed it down. Without the black, grid, Surreal and fade objects the owl shows at +0.3 s
(`blk-dropA`). Without only `^black background$` the result is the same (`blk-dropBB`).

## Cause

`Black Background` (PC object 2437, Ground Object, model 19): 8 vertices, 12 triangles, a cube of +-10 cells at scale 0.5.
That is +-10240 units around (26625, 209899, 30379), so its +X wall stands at x = 36865. Every face's front side looks
inward. With the PC's winding (front = the side opposite to `(b - a) x (c - a)`), the crates, barrels and boulders of Ruined
Village all face outward and this box and the sky boxes face inward. Texture 24 is plain black with no alpha, and no
polygon has the double-sided bit. On the PC the camera outside the box sees only its far walls, a black backdrop behind the
owl. The PS2 draws strips in either orientation and culls nothing (`level-format.md` 0x0211, "the engine does not seem to
care"). So the near wall hid everything inside the box until the camera crossed x = 36865, at 12.5 s on the authored path.
That matches the measured +12.4 s. The Nov 5 capture ([issue 1 follow-up](../2026-11-05/issue-1-cliff-intro-city.md)) and
the [HUD and owl framing](ootf-intro-hud-and-owl-camera.md) runs show the same black.

Of all placed PC models, only this one and the sky boxes (whose classes become the PS2 sky) are inward shells.

The object's stored Y rotation matters. The engine turns objects by stored Y minus 90 degrees (`level-format.md` section 3),
so the wall facing the camera is the model's -Z face. A first attempt without the turn dropped the model's +X face, a side
wall, and the screen stayed black (`blk-shell`).

## Fix: inward shells (`ootfshell.ts`)

* `isInwardShell`: a closed mesh (every edge on two polygons) of single-sided polygons whose fronts all face the centroid.
* `shellWallsHiddenFrom`: places the model as the object does (position, scale, stored Y minus 90; tilted objects are left
  alone). It returns the walls the PC culls from a cut-scene camera key that lies outside the shell and within the PC draw
  distance (`Max Fog Depth`, 30 lu in the Intro). The landscape keys of Intro 3, 250000 units away, do not count.
* `planShellTrims` (pipeline stage `models`) gathers the camera keys of every sequence in the `.ssd` beside the level's
  `.odb` dependencies (actors bound to a Camera object, engine 27). When a model is placed more than once, only walls
  hidden for every placement are dropped. `OotfAssetBuilder.dropPolygonsOf` / `pcToNeutral({ dropPolygons })` leave
  those walls out of the converted model (collision lists are remapped as for dropped textures).
* Converter log: `shell Black Background: 2 of 12 walls left out (1 placed)`.

From inside the box (Intro 1 after 12.5 s, all of Intro 2) the missing wall is behind the camera.

## Also fixed: model faders (`ootfclass.ts`)

A PC Fader (engine 136) with a model fades that model. Only a model-less one draws a full-screen quad (OpenDrakan
`Fader_Cl`, Start Mode 0 = run instantly, 1 = run on a message; the message value is ignored). Every PS2 Fader class is
model-less, so the Intro's Face Ring, Psygnosis Monolith (the plate), Metal Background, Presents, D R A K A N, Sub Title,
A / Production and the Surreal text were cloned as screen faders and drew nothing. A PC fader with a model now becomes an
inert Ground Object (17) with its PC model, like the Pirate Ship stand-in: no health, damage links or explosion. The scenes'
show / hide actions keep their timing, but the alpha ramp is lost. Model-less faders ("scene fade") stay PS2 faders.
Wartok's Splash01/02, Start Splash and Intro Stomp 2 name models the install's MOD does not have. Those six objects are
now dropped with a warning; before, they were white screen faders.

## Result

| Run | Owl visible | First full picture |
| --- | --- | --- |
| `blk-before` (formats `6c154d8`) | +12.7 s after Intro 1 start | +12.7 s |
| `blk-after` (model faders only) | +12.8 s (plate now drawn) | +12.8 s |
| `blk-final` (both) | +0.3 s, small and approaching as authored | plate from +4.3 s, Metal Background from +6.8 s |

`blk-final` then plays the Surreal logo and the perched owl with the "DRAKAN Order of the Flame" title, which the old
packs never showed. It hands over to Ruined Village at 62.9 s, whose opening scene plays, and exits cleanly through the
scripted quit. Contact sheets: `build/introblack/sheet_before.png`, `sheet_after.png`, `sheet_after_rest.png`.

Tests: formats `test/ootfshell.test.ts` (shell detection, the culled wall from an outside camera, none from inside or beyond
the draw distance, the minus-90 turn, tilted placements, merging over placements, draw distance from Level Properties) and
`test/ootfclass.test.ts` (model fader to Ground Object with its model and inert fields; model-less fader stays a fader).
`npm test` in `editor/`: 1171 passed.

## Ruined Village arrival

Not the same cause: Ruined Village has no inward shell. Its normal arrival from the Intro shows the night scene from the
load on (`blk-final` 46 to 52 s). The few black seconds after a host-fired exit (status doc) were not looked at here.

## Open

* The model faders' alpha fades (Psygnosis plate fading in over 4 s, Face Ring fading out) are cut to show / hide. A PS2
  class that fades a model's alpha was not looked for.
* If a camera inside a trimmed shell turned back toward the dropped wall, it would see through it. No such shot exists in
  the Intro.
