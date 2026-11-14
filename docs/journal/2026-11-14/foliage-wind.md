# Living foliage: wind on the leaves, not on whole trees (2026-11-14)

Owner's report: "The foliage logic to move trees isn't right. The point was to add effects to the actual leaves of trees
and grass depending on wind direction. It currently moves the whole tree, which is incorrect."

## What was wrong and why

The first version (`2026-10-31/living-foliage.md`) moved every vegetation object by shearing its 3x3 for the duration of
its render call. That is right for a grass clump (the base stays, the tip moves), but for a tree the shear is linear in
height, so the trunk leans with the crown: the whole tree rocked. Its own notes listed the per-vertex bend as "next".
It also only knew the Drakan 2 table of 39 resource ids, so the palms of Surdana and of the converted Ruined Village did
not move at all.

## What the engine does with a tree's vertices (probes of this session, build/review/foliage-wind/runs/probe*)

* Two draw paths of a static model, both reached from `0x189b38`: the inline groups `0x1ed8f0` (rigid path, a1 = the
  prepared mesh; each group of up to 256 vertices has its own copy of the positions at group `+8`) and the indexed path
  `0x1ec160` / `0x1ec8f0` (renderer `0x4e9ac0` `+0xc` = the model's own position array `+0x24`).
* Moving the upper half of a tree in place by 1000 units during the call and restoring it afterwards changed nothing on
  screen, in both arrays (`probe2`, `probe4`). A permanent change did show (`probe5`). The part draws put DMA references to
  the arrays in the chain, and the arrays are read when the chain is sent, after the render call returns.
* Materials tell leaves from trunks cleanly. Surdana tree: trunk texture 0 % transparent texels, crown texture 60 %;
  `b0658`: 0 % / 49 % / 67 %; palm `806ca` (Surdana, and the converted Ruined Village uses the same model): trunk 0 %,
  fronds 77 %. Grass textures have no transparent texels (16 x 16) and are moved whole as before.
* The engine has a level wind: engine `+0x40c..0x414`, the eased gust direction of `0x141d80` (the one that blows snow
  and particles); a unit vector that veers in Surdana and the Ruined Village.

## What changed

`[video] foliage_wind` is now `off | grass | all | classic` (F1 > Display > Vegetation: Off, Grass, Grass and leaves,
Whole trees). An older ini with `1` / `on` reads as `all`, so the owner's ini (`foliage_wind = 1`) gets the new
behaviour; `classic` is the old whole-tree lean, one setting away.

* Grass, flowers, ferns: unchanged (the shear, base anchored, bends away from the player), in grass, all and classic.
* Trees in `all`: trunks and branches never move. A material is a leaf material when at least 10 % of 32 x 32 texel
  samples of its texture are transparent; a leaf vertex that a solid face shares is an anchor. The offset of a leaf vertex
  grows with its height above the model's base and its distance from the trunk axis (`leafWeight`), and is the crown's
  sway (downwind lean 0.8 to 2.4 % of the height with the gust plus a 0.45 Hz sway) plus two flutter waves (1.7 and
  2.6 Hz, up to about 1 % of the height) whose phase follows the vertex position, so clumps flutter together. Each tree
  has its own phase (from its handle), so a forest does not move in step; the gust field is shared with its neighbours.
  Direction: the level's wind when it has one, else the global veering wind; strength scales it all.
* How: every tree gets its own copy of its leaf arrays in one block of the game's heap (6 MB with `heap_extend`, else
  1 MB; Surdana uses about 120 KiB), refilled every frame with the leaves moved; during the render call the leaf groups'
  `+8` pointers and the indexed path's renderer `+0xc` point at the copy and are put back after. The model's own arrays,
  the object transform, collision, saves and the ray tracing export never see a change.
* Which trees: the vegetation table's trees, plus (in `all`) any static prop of the prop classes whose model looks like
  a tree: at least 1500 tall, a solid material from the base, cut-out materials in the upper half with at least 15 % of
  the faces (`looksLikeTree`; a house with cut-out windows, fences, small props and the all-cut-out bare palm `806cb`
  fail it). This is what moves the palms in Surdana and in the Ruined Village.
* CPU, not a vertex shader: the GS sees screen-space vertices only, and the GPU vertex path (D3D11 only, default `cpu`,
  being changed by the gpu-finish agent) would need the same per-object data. Displacing the model-space positions
  before VU1 makes the `cpu` and `gpu` vertex modes draw the same picture.

Files: `ps2recomp/ps2xRuntime/src/lib/drakan_foliage.{h,cpp}`, `drakan_config.cpp`, `drakan_settings_common.h`,
`runtime/world/include/world/Foliage.h` (leaf rules and wind, `world_foliage_test`), `runtime/settings` (Types, Store,
Schema, `settings_test`), `scripts/harness/setting_shots.py`. No renderer backend file was touched.

## Evidence (owner's ini: 3840 x 2160, DLSS quality, HD textures; New Game on an empty card)

Before = the dist exe of 35e00f99 with `foliage_wind = 1`; after = this build, same ini (reads as `all`). Same recipe:
standing still after the start, frames a0..a7 every 6 input samples, then two camera turns (b, c). Placed, not played
to (play_run starts each level directly); nobody walks, the camera only turns. In `build/review/foliage-wind/runs/`:

| Scene | Sheet (same crop, before row, after row) | Motion map (abs difference of two frames, x4) | What it shows |
| --- | --- | --- | --- |
| Surdana start (trees) | `surdana_sheet.png` | `surdana_motion.png` | before: trunks and branches outlined (they moved); after: only the crowns and grass light up, trunks dark |
| Surdana, turned to the tower (one tree, grass) | `surdanac_sheet.png` | `surdanac_motion.png` | after: the crown moves, trunk dark |
| OotF Ruined Village (palms) | `ootf_sheet.png` | `ootf_motion.png` | before: palms still (not in the table); after: fronds move, trunks dark |

Drakan 2 guard scenes: Ravenshold (mounted, `ab-ravenshold-*`) and Shadowmire (`ab-shadowmire-*`) ran to their scripted
quit with no foliage in view; pictures look as before.

DLSS: the object motion tracker's counts with `DRAKAN_OBJECT_MOTION_DIAG` in the Surdana start over 480 frames: parts
eligible for model motion 50,511 (off) and 51,072 (all), rejected 23,713 and 23,229; the per-object copies keep a stable
address per tree, so foliage parts stay eligible. The DLSS frames of moving leaves above show no smearing.

## Frame time (scene_ab_bench turn lock, owner's ini, quiet machine: 0 other games, replays or compilers in every run)

`--cycle video.foliage_wind=off,grass,all,classic`, 3 runs per scene, the mode order rotated per run (12 windows each):

| Scene | off | grass | all | classic |
| --- | --- | --- | --- | --- |
| Surdana start, frame ms (median, range) | 6.02 (5.83 to 6.09) | 5.82 (5.79 to 6.05) | 5.95 (5.80 to 6.03) | 5.84 (5.82 to 6.03) |
| Surdana, EE cpu ms per world frame | 5.73 | 5.55 | 5.60 | 5.42 |
| OotF Ruined Village, frame ms | 10.64 (10.38 to 10.76) | 10.46 (10.41 to 10.60) | 10.63 (10.61 to 10.64) | 10.60 (10.59 to 10.76) |

All differences are inside the range between repeats (the first window of a run is the slow one in Surdana, whatever
its mode): no cost was measured. `DRAKAN_FOLIAGE_LOG` puts the leaf draws (render call included) at 0.05 to 0.1 ms per
frame in Surdana and about 0.25 ms in the Ruined Village.

## Tests

`world_foliage_test` (new: leaf classification on tree, palm, house, prop, hut and all-cut-out models; weights; anchors
never move; motion downwind, bounded, per-object phase; scale and rotation of the object undone; level wind steering),
`settings_test` 592 checks (old `1` / `0` values, the four modes, round trip), `settings_schema_test` 1204,
`gameui_preview_test` 57 and the rest of the settings / gameui tests built standalone: all pass.

## Not verified

* Wartok Canyons: the direct start went back to the title (the known terrain memory crash of converted levels in view of
  much terrain, `status/ootf-port.md` known gaps), so its trees were not seen. Packs converted after `ecdf69d7` /
  `ee6b270b` draw OotF pines and fan palms with Drakan 2 models: not seen with this build (the shipped pack predates them).
* A real walk through a forest, and long sessions (the leaf block starts over when full; not reached here).
* Per-pixel motion vector correctness of leaves under DLSS beyond "eligible parts unchanged and no visible smear".
* The CPU clipping draw path `0x18bb90` (objects cut by the near plane) keeps its leaves still.
* Ray-traced shadows of trees do not move (scene export reads the true arrays), as before.
