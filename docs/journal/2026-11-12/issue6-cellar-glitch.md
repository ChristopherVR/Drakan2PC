# Issue #6 item 1: Rynn stuck and the picture breaking in the Surdana spider cellar

Player report (installer v0.1.10, still in v0.1.11, no mods, HD textures or ray tracing): "During the early quest in
Surdana where you have to clear the cellar of spiders, moving across certain seams in the floor or stairs seems to
randomly trigger a severe glitch. Rynn gets completely stuck, and at the same time the entire rendering bugs out."

This note replaces the first version of the same day (which had a stair-head fault and a fix that held Rynn at layer
edges); the second half explains why that fix was replaced.

## The video

25 s; 1 frame per second plus 8 fps around the event (`build/review/issue6-1/video/`, evidence
`report_onset.png`, `onset_zoom.png`). Rynn comes down a flight of stairs and runs along a narrow corridor towards the
barrel room (big wine kegs ahead on the right, a spider on the floor of the room, a torch on the back wall). Between two
frames 1/8 s apart the cellar is gone: sky-blue background, outdoor terrain and a house on the horizon, then the outdoor
terrain overhead seen from below (grass, cliffs, sea); Rynn a dark silhouette in changing poses. It stays like that for
the remaining 11 s, also after the inventory map is opened and closed.

Matched against the level data: the stairs are those from layer 453 (cell 4, the room at the stair head) down into the
corridor 452 (cell 5, x 460800 to 462848, z 460800 to 464896), which opens into the barrel room 448 (cell 9) at
z = 464896; the kegs stand along x = 459000 to 460700, z 465200 to 470700.

## How the engine keeps actors and the view "in" a layer

The cellar floors are 13 small terrain layers (LEVEL1NA 0 to 11 and 205); walls, ceilings and some stairs are tile
models. Outdoor layers lie above it: 195 (x 395264 to 481280, z 411648 to 464896), 217 (x 436224 to 464896, z 464896
to 483328) and 272 (x >= 464896).

* **Actor layer.** Every actor stores its layer at `+0x7c`. When it moves, the refile `0x1496a0` asks the cell lookup
  `0x14b6b8(x, y, z, world, old layer, flags 5)` at the actor's bounding-sphere centre (`+0xe8`).
* **View layer.** `0x112488` (each frame, from `0x139010`) does the same lookup from the view layer `0x4dade4` at a
  point 32 units behind the camera. The terrain draw `0x152330` draws that layer's `visible` list.
* The lookup only searches the old layer's `visible` list (Python model `tools/lookup_model.py`, agrees with 27 of 30
  logged changes). When no listed layer holds the point the result is -1.
* After a -1 the engine refiles "from no layer": `0x1499c8` for actors (a lookup from a hint layer, then from -1), the
  next `0x112488` call for the view (old layer -1). A lookup from -1 searches all 483 layers and returns the outdoor layer
  whose rectangle holds the point (the point is below its surface; it is the only layer there). The outdoor layers do
  not list the cellar layers, so from then on the view keeps drawing the outdoor world, or Rynn is moved onto the
  outdoor terrain.

## Step 1: every cellar edge, from the data and in the game

`tools/edges.py` walks every edge of the 13 cellar layers in 64-unit steps and runs the lookup model for a point 8
units outside, at the sphere-centre height: on every outer edge it finds no listed layer (that is normal; walls stand
there). Two adjoining layers that do not list each other exist once: 447 (cell 10) and 449 (cell 8) along z = 471040,
x 464896 to 468992 (a wall stands on it). The model geometry could not tell reliably which edges a player can lean over
(it missed walls the game has), so every edge was then driven in the game: `tools/make_probes.py` places Rynn 600 units
inside the edge facing out (straight and +/- 0.5 rad) and runs for 3.5 s, logging layer, view layer and positions every
10 ms (`runs/probes-base`, 132 baseline drives; `runs/probes-fix`, `runs/probes2-fix-*` on the first fix build; table
`build/review/issue6-1/edge_reachability.json`).

Edges where the sphere centre does get over no listed layer (baseline -1, or the first fix build holding her):

| layer (cell) | edge | along | baseline drives: count / -1 seen / outdoor layer | first fix build: drives / reached the edge |
| --- | --- | --- | --- | --- |
| 456 (0) | E x = 468992 | z 448512..450560 | 9 / 9 / 0 | 12 / 9 |
| 456 (0) | W x = 462848 | z 448512..450560 | 9 / 7 / 0 | 11 / 8 |
| 454 (3) | E x = 471040 | z 450560..452608 | 0 / 0 / 0 | 2 / 1 |
| 454 (3) | W x = 468992 | z 454656..456704 | 6 / 0 / 0 | 9 / 1 |
| 453 (4) | E x = 462848 | z 452608..454656 | 9 / 6 / 0 | 12 / 9 |
| 453 (4) | E x = 462848 | z 456704..458752 | 9 / 9 / 0 | 12 / 9 |
| 453 (4) | S z = 460800 | x 456704..458752 | 9 / 0 / 0 | 12 / 11 |
| 453 (4) | W x = 456704 | z 458752..460800 | 9 / 8 / 0 | 11 / 9 |
| 452 (5) | E x = 462848 | z 460800..462848 | 9 / 0 / 0 | 12 / 3 |
| 452 (5) | W x = 460800 | z 460800..462848 | 9 / 6 / 1 | 12 / 5 |
| 451 (6) | E x = 475136 | z 456704..458752 | 9 / 8 / 0 | 10 / 9 |
| 451 (6) | E x = 475136 | z 460800..462848 | 9 / 8 / 0 | 12 / 8 |
| 451 (6) | N z = 456704 | x 473088..475136 | 9 / 8 / 0 | 12 / 9 |
| 451 (6) | S z = 462848 | x 473088..475136 | 9 / 8 / 1 | 12 / 11 |
| 450 (7) | E x = 473088 | z 462848..464896 | 3 / 3 / 0 | 6 / 6 |
| 450 (7) | E x = 473088 | z 464896..466944 | 0 / 0 / 0 | 9 / 9 |
| 450 (7) | W x = 466944 | z 462848..464896 | 3 / 3 / 0 | 6 / 6 |
| 450 (7) | W x = 466944 | z 464896..466944 | 6 / 6 / 0 | 9 / 9 |
| 449 (8) | E x = 473088 | z 466944..468992 | 3 / 3 / 0 | 6 / 6 |
| 449 (8) | E x = 473088 | z 468992..471040 | 6 / 6 / 0 | 9 / 9 |
| 449 (8) | S z = 471040 | x 464896..466944 | 6 / 6 / 0 | 9 / 1 |
| 449 (8) | S z = 471040 | x 466944..468992 | 3 / 3 / 0 | 6 / 3 |
| 449 (8) | S z = 471040 | x 468992..471040 | 3 / 3 / 0 | 6 / 3 |
| 449 (8) | S z = 471040 | x 471040..473088 | 3 / 3 / 0 | 6 / 3 |
| 448 (9) | S z = 473088 | x 458752..460800 | 9 / 8 / 0 | 3 / 0 |
| 448 (9) | W x = 458752 | z 471040..473088 | 9 / 8 / 0 | 9 / 6 |
| 447 (10) | N z = 471040 | x 466944..468992 | 1 / 1 / 1 | 2 / 2 |
| 446 (11) | S z = 477184 | x 466944..468992 | 3 / 2 / 2 | 0 / 0 |

Listed as reachable when a baseline drive saw -1 or the first fix build (which held Rynn at the edge) fired there. 0
baseline drives means the baseline set had no probe on that stretch. Not reachable in any drive: the other stretches of
all outer edges (walls inside the layer), and the corridor 452 between z 462848 and 464896 on both sides.

So the "seams" are the walls on the layers' outer edges: the sphere centre (about 11 units off Rynn's origin) or the
camera point leans a few units past them. The engine's own answer to the actor's -1 acts like a wall there: with the
port's change switched off, Rynn reaches 20 to 190 units past the 453 south edge and is put back
(`runs/ab2-l4s-fix0`). The bad case is only the search from -1 picking an outdoor layer.

## Step 2: the corridor and the barrel corner

`ISSUE6_SCENARIO=barrel`: Rynn placed in the corridor 452 facing the barrel room, running, running with the sword drawn
(attack taps), strafing right into the wall (D), turning right while running, or jumping; 4 s each.

* Actor: 80 baseline drives (`runs/barrel-base-s11`, `-s12`) never lost the layer in the corridor itself: the
  corridor walls stop the centre before the edge.
* View: the camera does lean out. In `runs/viewonly-base-s12` (baseline, stop at the first time the view is outdoors
  while Rynn is in a cellar layer, at least 0.3 s into a drive) drive 21 (a jump) put the camera 4 units east of the
  453 east edge at the stair head, (462852, 158805, 458075), with Rynn on the stairs at (462772, 158382, 458774) in
  layer 4: the view went -1, then 195. The picture (`evidence/baseline_view_break_corridor_run.png`, left) is the video's:
  sky-blue, the outdoor terrain overhead seen from below. 60 samples later the camera was back over 453 and the
  picture with it. 120 drives (`viewonly-base-s11` 98, `-s12` 22) gave this one break.
* Earlier drives (`runs/barrel-base-s11`, `-s12`) logged view breaks with the camera 130 to 230 units into the wall
  west of the corridor, (460570..460750, z 464000..464870), the view going -1 then 195 or 217 and in two cases staying
  outdoors for the rest of the run (up to 60 s). Those runs did not separate harness placements from the drives, so
  some of them may be caused by the camera flying along with a placement.
* Rynn stuck and the picture broken together, persisting: the end of `runs/base-pass` (baseline). After a harness
  placement from cell 10 to cell 4 the camera flew through the walls, the view became 195 and stayed; in the following
  drives Rynn (layer 4 or 5, 600 units inside) did not move at all (pen -570 to -584 in every one of 15 drives, camera
  frozen at (461524, 158558, 461009)). The same freeze came back on a rejected fix build (v6, below) whose view stuck
  at 195: 36 of 36 stair-head drives without a step. A frozen Rynn together with a view reset every frame was later
  also seen with the view kept in the cellar (section "Pinned at the west wall"), so the freeze is not caused by the
  view layer alone; what stops her was not traced.
* Second corridor wave (`runs/base-barrel2-s11`, `-s12`, 84 and 85 drives, stop disabled, screenshots): in both runs
  Rynn ended in 195 at drive 21 and 29 and stayed there for the rest of the run (12177 and 10469 of about 15000
  samples outdoors, y 149000 to 177000). Both starts were harness teleports (from the house layer 214 back into the
  corridor, and a 3450-unit jump in one frame), not walking, so they count as "a teleport near the cellar ends in 195",
  not as a natural repro.

Which layer the search picks and why Rynn ends up below the terrain: every point just outside the cellar lies only in
an outdoor layer (195 north of z = 464896, 217 or 272 south of it); she, and the camera, are below that layer's surface
(about 176000 to 180000 against 157816 to 159164), so the search returns it as the only candidate. The view drawn from
195 shows its terrain from below, which is the video. Whether Rynn herself is moved onto the outdoor surface (the
stair-head hits, `evidence/baseline_hit_s5.png`) or stays below it was not established for the reporter's run; the
video looks like the view-only case (her silhouette stays near the camera, below the terrain).

Plain statement: yes, the video's spot is reproduced on the baseline exe in natural movement. Along the right-hand
(west) wall at the stair foot of 452 Rynn's centre crossed the layer edge, the search from -1 handed her to 195, she
walked out through the wall under the outdoor terrain and the picture became sky, for the rest of the run (section
"The video's spot" below; once in 55 wall drives in that run). The view-only variant (camera at the stair-head wall)
came once in 120 corridor drives. The video's exact frame (Rynn by the kegs) was not matched pixel for pixel.

## A second way into the same state: the view arriving at -1

On fix build v7 (below) one corridor run (`runs/v7-barrel-s11`) still broke the picture: after a harness placement 37
units inside the 452 east edge, (462811, 463668), the camera swung 150 to 360 units into the wall east of the corridor
and the view went to 195; the sampled view then alternated -1 / 195 for about 2700 samples with Rynn in layer 5
(`evidence/v7_gap_view_reset_corridor.png`: the video's picture). The view search found the layer at -1 without a miss
of its own being logged. `0x1435c8` (it stores -1 into `0x4dade4`) is not what does it: with its return address logged
(`DRAKAN_LAYER_SEAM_TRACE=1`, `runs/v13-pin-*`, `runs/pin-fix-*`) it runs once per level load, from `0x218700`, with
the view at 217, and never during play. The store that puts -1 there in play was not found (the candidates in the view
struct copy `0x134450` / `0x133498` do not write `+0x8c`). From v8 such a search is judged against the controlled
actor's layer, and the trace build counts terrain draws (`0x152330`) that still find -1: 2 to 4 per 10-minute
west-wall run, single frames.

## The video's spot: the right-hand (west) wall at the stair foot

`ISSUE6_SCENARIO=westwall`: Rynn placed 30 to 250 units east of the west wall of 452 (x = 460800) at the stair foot,
z 460850 to 461350, turned 0.3 to 1.2 rad towards the wall, then running, running with the sword drawn, strafing
right, sliding along it with the mouse, or jumping, 4 s each.

* Baseline, natural movement (`runs/base-westwall-cf0-s1`, `[mouse] camera_follow = 0`, drive 6 of 55, running with
  the sword drawn): the origin reached x 460784, the sphere centre x 460800.0 over the edge, the refile found no listed
  layer and the search from -1 returned 195. From then on she walked west through the wall, under the outdoor terrain
  (x 460736 a few frames later), and the picture was only sky (`evidence/baseline_westwall_natural.png`: before, at
  the hand-over, 5 s later). She stayed in 195 for the rest of the run (4797 of 5825 samples), the placements of the
  following drives could not bring her back. This is the report: at a seam of the stair foot, stuck (outside the
  cellar, under the ground) and the picture broken, from walking.
* With `camera_follow = 5` (`runs/base-westwall-s1`) the first hand-over came after a placement, so the PC camera
  follow is not needed for it.

The port's fix (below) keeps her in the cellar there; with input held she walks away from the wall (next section).

## "Pinned at the west wall": the harness, not the game

The stretches of 300 to 2000 frames without movement reported in the first follow-up were made by the harness. The
`jumps` mission counts a lost layer (-1) as a hit: it releases every key and waits 6 s for screenshots. In
`runs/v8-barrel-s11` the input log shows the keys going to `mv=0/0/0` at frame 67750, the hit screenshots at 68043 and
the stretch ending at 68373 (595 frames, the 6 s wait); `[playtest]` shows no placement in it, so none was "ignored";
the turning was the mouse slide already queued. Since the fix leaves her at -1 at that wall more often, it made more
such waits, not more pins.

Measured instead with input held (`ISSUE6_MISSION=pintest`, `tools/issue6_drive.py`): placed 30 to 250 units from the
west wall at the stair foot, a 3 s drive into it (run, run with the sword, strafe, slide), then 1.5 s of S, a turn of
about 180 degrees and 1.5 s of W; a drive whose escape moves her less than 50 units is a pin. The keys stay down when
she loses her layer. Five runs of 48 drives each per exe:

| exe | runs x drives | pins (escape under 50 units) | drives that lost the layer | runs that went outdoors | rewinds over 1000 units, outside placements |
| --- | --- | --- | --- | --- | --- |
| baseline v0.1.11 (`pin-base-s1..5`) | 5 x 48 | 3: two at the stair-top snag (layer 4), one right after a placement | 0, 3, 1, 2, 6 | 5 of 5 (drives 4, 9, 0, 1, 21), and stayed there | not traced |
| current tree, fix off (`pin-fix0-s1`) | 1 x 48 | 2: stair-top snag, right after a placement | 14 | 0 | 0 (6 during placements) |
| fix on, v8 design (`pin-fix-s1..3`) | 3 x 48 | 0 | 15, 1, 19 | 1 (drive 5: a 12554-unit rewind into 195) | 2, 0, 2 |
| v12 (rewind clamp to the last refile position) | 3 x 48 | 1, 0, 2: stair-top snag | 21, 11, 21 | 0 | 0 |
| v13, final (`v13-pin-s1..5`) | 5 x 48 | 0 | 19, 14, 14, 17, 16 | 0 | 0 (12 to 38 rewinds per run refused) |

The pins that remain are the same on both: at the top of the stairs from 453 down to 452, origin (460840 to 461060,
z 460720 to 460770), layer 4 with a valid floor (`y_d4 = 159164`), no layer loss and no hook action. In
`runs/pin-fix0-s1` (fix off) the input log shows her stepping forward 15 to 25 units and being put back every frame for
over 100 frames with W held. That is a native snag of the stair-head tiles, not the layer handling; it also releases
after a turn or a jump.

What having no layer does (traced in the recompiled code):

* The refile `0x1496a0` returns at once for an actor at -1 (see `netcode.md`, "Ghost collision cells"), and the floor
  sample of the player (`+0xd4`, `y_d4` in the input log) reads 0 while she has no layer.
* The player update `0x321258` checks the actor's layer at `0x321480`. At -1 it puts her at the position saved at the
  end of the last frame (player `+0xbe4`, written at `0x3216a8` together with her layer at `+0xb08`) minus a step from
  `0x325d30`, and files her again through the world's vtable (`0x1499c8`, return address `0x321548`). That is the
  engine's own "wall" for an actor that ran off her layer, and it is why she does not stay stuck at -1 with input held.

## The long moves: the player update's rewind

The one-frame moves of 1000 to 12500 units are that rewind. With the move logged at each (re)file
(`DRAKAN_LAYER_SEAM_TRACE=1`, `[layer-seam] actor moved`), every one of them in the west-wall runs was "seen by file
0x1499c8 called from 321548": the step from `0x325d30` was large, and she landed at the stair head (453, 2000 to 3100
units south), elsewhere in the corridor, or once (`runs/pin-fix-s2`) 12554 units west, filed into 195. The same
rewinds happen with the fix off (`runs/pin-fix0-s1`: 6 over 1000 units in 48 drives; the baseline exe shows the same
one-sample jumps, for example `runs/pin-base-s1` (460851, 460864) to (462691, 458959) into 453); the fix made them more
common because it leaves her at -1 where the engine would have handed her outdoors.

They are not a fall rescue, a collision push-out or a script: the writer is the player update itself. The guard
window of rule 1 cannot cover them (they are already a filing from -1 that lands in a cellar layer, or far away), so
v13 adds a rule for the rewind itself (rule 4 below).

Most of them are made by the harness. Split by whether a placement was active (`tools/rewinds.py`: between
`[playtest] script place` and `released`, plus about 120 frames): with the v8 design 4 of 23 rewinds over 1000 units in
three west-wall runs came outside placements (the natural ones), all the others during or right after one: the
placement writes her to a point thousands of units away while the player update is rewinding her, and the next step
is computed from that jump. On v13 there were none outside placements in ten runs (5 west wall, 5 corridor); the clamp
refused 1 to 38 rewinds per run outside placements. The one run of the ten that went outdoors (`v13-barrel-s25`,
frame 19558) did so in a placement: x went 461003, 463293, 465299 in three frames, past the 2048-unit window, and the
search from -1 gave 195.

## Fix (superseded: the guard is off by default)

The owner and the coordinator stopped the guard approach after three revisions in one day (v13 undid placements,
v14 let the west-wall hand-over back in, v15 needed a hook on the player update to tell a teleport from the engine's
own rewind): every rule had to separate cases the engine does not separate. The guard below is now off unless
`DRAKAN_LAYER_SEAM_FIX=1`, so the committed game behaves like the original; the work moved to the cause, the first
lookup miss at the wall (section "Root cause" when it is written). What follows documents the guard as it was.


`ps2recomp/ps2xRuntime/src/lib/drakan_layer_seam.h` (policy, unit tested) and `drakan_layer_seam_hook.h` (hooks on
`0x14b6b8`, `0x1496a0`, `0x1499c8`), installed from `drakan_overrides.cpp` (PAL only). Only the controlled actor's
refiles and the view layer are touched; the only write to the actor is rule 4.

1. **Actor: no hand-over to a layer that is not adjacent** (`foreignLayer`). When the search from -1 in `0x1499c8`
   (filing the controlled actor after she lost her layer) returns a layer that neither lists her last layer nor is
   listed by it, while the point is within 2048 units of that layer's listed area, the result is refused. She stays
   without a layer for that tick, exactly as the engine already leaves her at walls, and is refiled from the next
   lookup once her point is over a listed layer again. After a teleport (she moved more than 2048 units, 3D, since the
   view update of the last frame: a harness or script placement, a level arrival, a cut-scene; `teleported`) and on a
   new level the engine's search stands. Where the search returns an adjacent layer (every walkable passage) the
   engine's handling stands.
2. **View kept at walls.** A view lookup from a known layer that finds nothing keeps that layer while the camera point
   is within 384 units of that layer or one it lists (the logged breaks were 4 to 360 units out).
3. **View: no hand-over to a foreign layer.** A view search from -1 is judged against the layer the view just lost
   (beyond the 384 units), or, when the view arrived at -1 without a miss of its own, against the controlled actor's
   layer (`viewReferenceLayer`). A foreign result is replaced by a lookup from that layer, or by that layer.
4. **No long rewind.** When the player update rewinds the controlled actor without a layer (the filing called from
   `0x321548`) and the rewound position is more than 512 units from the position it saved at the end of the last frame
   (player `+0xbe4`, the caller's `s17` is the player), she is put at that saved position instead
   (`rewindTooFar`). Not in a frame in which she teleported (see below): then the engine's rewind stands. A first try (v12) used the position of her last refile with a layer; the refile does not run every
   frame, so that position was stale (it was the placement point) and was dropped.

Rejected on the way (each one built and driven, runs under `build/review/issue6-1/runs/`):

* v1, holding Rynn at the last position with a layer (the first version of this note): layer edges became invisible
  walls. At the stair foot of 452 she could no longer cut the corner onto the stairs (the engine lets her:
  `runs/ab-l4s-fix0`, three drives end in 453), and at the 453 south edge she got 260 units further than the engine
  allows.
* v2/v3, keeping the actor's old layer within 384 units (`runs/v2-*`): she walked 380 units past edges the engine
  treats as walls.
* v4, refusal plus "one more ring" (retrying a miss from each listed layer): after a harness camera cut the view still
  went to 195 and stayed.
* v5, remembering the last view layer across cuts: the ring retry from 205 via 214 handed the view to 272, the
  remembered layer became outdoor and then every cellar layer was refused. The ring retry was removed.
* v6, keeping the last view layer when lost: it latched 195 during the entry into the cellar; the view stayed at 195
  and Rynn was frozen in every drive.
* v7, the view refusal only right after the view's own miss: misses the engine's reset (above). The final design is
  v7 plus the actor's layer as the reference for the view (v8).
* v9 (a "fell through the floor" exception that never fired, because an actor falling under the floor stays filed in
  the cellar layer above her), v10 and v11 (above).

`DRAKAN_LAYER_SEAM_FIX=0` turns it off; `DRAKAN_LAYER_SEAM_TRACE=1` logs native "no layer" lookups (the first 2000)
and every change, `=2` with `DRAKAN_LAYER_SEAM_BOX` every layer change in a box with its caller
(`docs/reference/env-flags.md`).

## Teleports: the guard undid far placements (v13), fixed in v14

Another session reported that `place` no longer took on builds of the current tree. Checked with
`ISSUE6_MISSION=teleports` (18 placements: cellar, Surdana level start, straight above the cellar, house, outdoors by
the house, cellar 205):

| target | baseline (`tp-base`) | v13, fix off (`tp-v13-fix0`) | v13 (`tp-v13`) | v14 (`tp-v14`) |
| --- | --- | --- | --- | --- |
| cellar, house 259, cellar 205, outdoors by the house (272, 1900 units off: the terrain) | taken | taken | taken | taken |
| Surdana level start from the cellar (3 times) | flung 24000 to 395000 units | flung 259000 to 437000 units | **stays in the cellar, 275000 to 289000 units away** | flung 8000 to 645000 units |
| straight above the cellar onto 195 | lands in 5 | lands on 195 | lands in 5 | lands in 5 |
| hook actions | - | - | refusals and refused rewinds | none |

On v13 a far placement left Rynn where she was: the placement moves her, the refile from her old layer finds nothing,
the player update rewinds her by a step computed from the jump, and rule 4 replaced that rewind with the saved
position, her old spot. Rule 1 could also refuse the search after a placement whose target lay within 2048 units of
the listed area of her last layer, which builds since 11:15 had. The level-start target does not take natively either
(the engine flings her 24000 to 436000 units), so it shows the difference, not a working placement. v14 exempts
teleports from rule 1 and runs rule 4 only after a rule 1 refusal, so after any teleport the engine's own handling is
unchanged.

## Fixed build results

v13 (SHA-256 `2cf9a56d0258b28daac5b56b474af66e8a414087a70d8f497bc72996cece5781`, the design above before the teleport
exemption of v14, with the trace diagnostics; built with `Build-LocalGame.ps1 -Incremental` into `build/review/issue6-1/play`). Baseline: the
v0.1.11 exe in `build/review/baseline-play`. Counts are samples after the cellar entry; "outdoor" means layer 195, 217
or 272. The corridor recipe is `ISSUE6_SCENARIO=barrel` (`jumps`, 700 s, with its 6 s idle after a lost layer); the
west-wall recipe is `ISSUE6_MISSION=pintest` (above).

| recipe | exe | runs | drives | runs with an outdoor view / outdoor layer | outdoor-view samples per run | pins with input | rewinds over 1000 outside placements |
| --- | --- | --- | --- | --- | --- | --- | --- |
| west wall | baseline | 5 | 240 | 5 / 5 | 10783, 8696, 10689, 9335, 4748 | 3 (2 stair-top snag, 1 after a placement) | not traced |
| west wall | v13 | 5 | 240 | 0 / 0 | 0, 0, 0, 0, 0 | 0 | 0 |
| corridor | baseline | 5 | 332 | 5 / 4 | 7878, 6519, 5967, 9509, 665 | - | not traced |
| corridor | v13 | 5 | 371 | 2 / 1 | 0, 0, 0, 3, 6034 | - | 0 |

The two v13 corridor runs with an outdoor view: `v13-barrel-s24` had 3 samples (a placement camera cut), and
`v13-barrel-s25` went outdoors in a harness placement (above). The terrain draw found the view layer at -1 in 1 to 14
single frames per v13 run (not measurable on the baseline exe, which has no hook).

Legitimate passages, each probe three times (`tools/probes_pass_x3.json`; `runs/pass3-base` baseline,
`runs/pass3-v13` v13, trace on):

Every passage that crossed on the baseline crossed on v13 in every try: the stairs 453 / 452 both ways (`L4 -> 5`,
`L5 -> 4`, 3/3 and 2/2), the barrel-room doorway both ways (`L5 -> 9`, `L9 -> 5`), 447 / 446 / 205 (`L10 -> 11`,
`L11 -> 10`, `L11 -> 205`, `L205 -> 11`, 3/3 each) and the rest of the 37 probe points. The probes that stop on the
baseline stop on v13 too (pillars and the 449 / 447 wall: `L0 -> 2` at x 463872 and 467968, `L6 -> 7` at x 467968 and
472064, `L8 -> 7` at 467968, `L8 -> 9` at z 470016, `L11 -> 205` at x 463872); `L4 -> 1` crossed 3/3 on v13 and 0/1
on the baseline. (The 3600 s limit ended the baseline run after 87 of 138 probes and the v13 run after 104; the
counts are per probe point.) No actor refusal was logged at a passage: they were at probe placements dropped outside
the layers (x 459500 / z 475000 west of 446, x 460000 / z 450000 west of 455, and the drop under the 447 floor at
x 468500 to 469000).

The house door, walked (`ISSUE6_MISSION=door`: placed in 205 below it, 5 s of W up the stairs, then a turn and 4 s
back, three times; `runs/door-base-2`, `runs/door-v13`): on both exes Rynn runs up the stairs of 205 and stops in the
doorway at z 480817, in front of a stone wall (`evidence/housedoor_baseline.png`, `evidence/housedoor_v13.png`; the way
into the house is closed at this point of the quest), and walks back down into 446 (layers 205 then 11) every time.
Same result on both; no hook action at the door.

Unit test `layer_seam_policy` (`ps2recomp/ps2xRuntime/tests/layer_seam_policy_tests.cpp`, in
`scripts/tests/CMakeLists.txt`): passes.

## Evidence

`build/review/issue6-1/evidence/`: `report_onset.png`, `report_after.png` (video); `baseline_view_break_corridor_run.png`
(baseline: the view handed to 195 at the stair head, then back); `baseline_hit_s5.png`, `baseline_hit_plain.png`
(baseline: Rynn moved onto the outdoor terrain from the stair head, owner settings and plain);
`v7_gap_view_reset_corridor.png` (v7 after the view reset, the video's picture); `baseline_westwall_natural.png`
(baseline, the west wall at the stair foot: before, at the hand-over, 5 s later); `westwall_baseline_vs_final.png`
(the same spot, baseline after the hand-over and the final exe with Rynn without a layer at the wall, the cellar still
drawn); `corridor_v7_vs_v8.png`; `fixed_v8_stairhead_plain.png` (v8, plain settings, Rynn at the stair-head wall
without a layer, then 300 frames later, the camera beyond the wall shows the clear colour as on the baseline). Per-sample logs
(`cells.jsonl`: layer, view layer, position, sphere centre, camera every 10 ms) and screenshots are in the run folders
under `build/review/issue6-1/runs/`.

## Not established

* The store that puts the view layer at -1 in play (not `0x1435c8`).
* The stair-top snag at the south edge of 453 (both exes): a collision put-back of the stair-head tiles, not traced.
* Whether rule 4 ever changes a legitimate move: it acts only on the player update's rewind of an actor without a
  layer after a rule 1 refusal (v14). On v13, where it was not gated yet, it refused 117 rewinds in the passage run, a
  run made of placements; the passage counts above are from v13, and v14 was checked on the teleport mission, three
  west-wall and two corridor runs only.
* Whether the PS2 shows the fault. With `camera_follow = 0` the baseline still handed her outdoors at the west wall,
  so the PC camera follow is not required for it.
* Other levels and other interiors: the change is general (any interior under an outdoor layer), only Surdana's cellar
  was driven.
* NPCs and spiders keep the engine's behaviour.
* All runs used the harness: god mode, windowed 1280x720 (the copied `drakan.ini` otherwise unchanged), audio as in
  the ini. The harness placements cause most of the long rewinds and camera cuts seen; a player without placements was
  not measured.
