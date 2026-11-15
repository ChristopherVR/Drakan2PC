# OotF Ruined Village: the striped band over the distant sea (2026-11-15)

Owner's report: (5) the sea in the distance in the Ruined Village shows graphical issues; (9) graphical issues when
viewing from a distance (the screenshot was not attached). Scratch, tools, runs and pictures:
`build/review/ootf-distance/` (private, not tracked). Nothing from either game is quoted here.

## What was seen

Play folder `build/review/ootf-distance/play`: a copy of `build/final-game/bin` (the same exe as `dist/DrakanPC`), the
owner's `drakan.ini` and `drakan_native_options.ini` copied unchanged except for an `elf =` line to a private ELF copy,
the `dist` HD textures (hard links), the current pack `ootf-levels.zip` (24aa9c51). Owner's settings throughout:
3840 x 2160 borderless, DLSS quality, field of view 99, HD textures, live texture upscaling `auto` (8x at this scale),
`hd_texture_mips` 1, `modern_water` 1, `view_distance` 1. Driver: round 3's `wdrive.py` (live input, renderer shots),
New Game straight into the level, god mode, audio off.

**A band of pale panels with thin pink, blue and orange stripes sits on the horizon above the far sea**, everywhere the
Ruined Village sea is seen in the distance looking out to sea (north). The panels have hard vertical seams and slightly
different shades, so the horizon looks like a fence of grey boards. Seen:

* walking from the village road down to the shore with W (played; `runs/walk_before`, sheet `ev/walk_before.jpg`):
  the band shows at the horizon in every frame with open sea;
* on the shore west of the village, eight headings (placed; `runs/views2`, sheet `runs/views2/sheet_a.jpg`, crop
  `ev/a4_horizon_left.png`): the band is there in the four headings towards the sea and absent towards the land;
* from the headland at the west end of the sea looking east, where the water runs on for 200,000 units, no band
  (`ev/west1.jpg`, first frame); towards the north it is there again;
* raised over the shore (Rynn placed at 45,000 and 80,000, shot before she falls; `runs/t_high`, `ev/t_high.jpg`): the
  sea is a rectangle with a straight far edge, and the band is a whole ring of striped panels below the horizon,
  around the camera, wherever no ground or sea is drawn.

If the owner's screenshot shows a striped or boarded strip along the horizon over the sea, this is it.

## Cause

Two things together.

1. **The sea ends inside the view.** The PC level's sea layers stop at z = 26,624 (and x 169,984 .. 399,360), about
   115,000 to 125,000 units from the shore. The PS2 view distance is 153,600 with the fog full only there, so the far
   edge and the space behind it are in view. On the PC the level's own fog (12 lu) hides that; the converted level
   keeps the PS2 template's longer distance. The PC data has nothing beyond that edge (floors and water both stop), and
   terrain layer origins are stored as unsigned cells, so the sea cannot be extended past z = 0 (13 cells) without
   finding out whether the engine takes a wrapped negative origin (not tried).
2. **What shows behind the edge is the lower half of the procedural sky, drawn with an upscaled lookup texture.** The
   Procedural Sky object (cloned from the template, World Common model 110, a sphere with one texture) samples a 64 x 64
   CT24 texture point by point in two written rows; the other rows of that texture in video memory are random bytes.
   Proof that the band is the sky: moving the sky object's `Offset` parameter (a test pack, `packs/m6000`, `p6000`,
   `ev/sky_offset_cmp.jpg`) moves the band off the horizon. Proof that it is the texture path: with the live upscale off
   (`GSR_TEXUP=1`) the lower sky is one flat fog colour and the band is gone (`ev/tex_toggle_band.jpg`,
   `ev/tex_toggle_band2.jpg`); `modern_water` 0, HD pack off, DLSS off, `hd_texture_mips` 0, anisotropy 1,
   `view_distance` 1.5 and 2 all keep it (`ev/toggles1.png`, `ev/toggles_vd.png`, `tex_toggle_band.jpg` row 3,
   `tex_toggle_band2.jpg` row 2). The 8x upscale smears the random rows into the sampled ones, so each sky panel picks up
   different colours.

The renderer already keeps such textures native (`texup::looksUninitialized`, 24 October, for the intro's sky ramp,
`2026-10-24/inventory-sky-textures.md`), but its test asks for a quarter of the rows to average more than 60 levels
between neighbours. Here the random bytes are mostly zero words: the dumped texture (`bda03ea204764b4e`, `GSR_TEXDUMP`
into a scratch folder, first tile of the second row in `ev/a0_textures.png`; the dump itself was removed) has 50 random
rows, but only 8 pass that test, so it was upscaled. It also depends on what the video memory held, so the band may come and go between runs and levels; it is not new with the view distance
or field of view changes of 14 and 15 November (it is faintly visible in round 3's water pictures of 14 November).

## Fix (renderer, `runtime/gs`)

`texup::looksUninitialized` (`gs_texup.cpp`) gains a second test for raw 24-bit colour textures (PSMCT24, passed from
`setupUpscale` in `gs_texture_cache.inl`): rows where more than a quarter of the neighbour pairs jump by more than 64
levels in some channel; half of the rows (and at least 4) like that keep the texture native. The first version applied
it to every format; a scan of every texture in the disc and pack TXDs (6,135, `tools/txdscan.ts`) then flagged 26
pieces of painted art (vertical stripes, sparkle particles, up to 81 % of the rows; `ev/fp_textures.png`), all palette
or 32-bit. Restricted to 24-bit colour, the same scan flags none of the 22 24-bit textures, and over the 1,583 textures
dumped from both games in `dist/DrakanPC/textures/dump` plus 311 from this level the new test adds exactly the three
night sky ramps (78 % such rows each) to the 83 the old test already caught. Tests (`test_texup.cpp`): a sparse random
ramp is native as 24-bit colour and not in another format, and an atlas with sharp edges in 40 % of its rows still
upscales.

## Seen in game after the fix

Exe from an incremental `build/final-game` build under `build/.game-build.lock` (`-DistDirectory` a scratch folder;
the exe copied into the play folder; the tree also held other agents' uncommitted runtime edits at the time). Same pack,
same ini. First with the rule for every format (exe `3cf9a174...`), then again with the final 24-bit rule (exe
`e56c7b4f...`).

* Walk from the village road to the shore with W (played, the same plan as before; `runs/walk_after`,
  `ev/walk_after.jpg`; before and after side by side in `ev/walk_before_after.jpg`): the horizon over the sea is clean
  fog colour in every frame; before, the striped boards were there in every frame with open sea.
* Shore spot and the two raised shots (placed; `runs/fix_high`; `ev/before_after_high.jpg`, left before, right after):
  the ring of panels under the horizon is gone; the lower sky is one flat colour.
* Final rule (`runs/fix2_high`, `runs/walk_after2`, `ev/fix2_check.jpg`): the same, the shore horizon and the lower sky
  flat, the walk clean.
* Control: the old exe (`play/drakan_before.exe`) run again afterwards at the shore spot shows the band as before
  (`ev/control_before.jpg`), so the change is what removed it.
* Frame rates in these shots are not comparable: up to three other agents' games were running during the runs.

Tests: `ctest --preset runtime-tests`, 200 of 200 passed (6 data-dependent skipped) on the first version;
200 of 200 again on the final one, `gsr_test_texup` 33 checks (the shared test tree could not relink
`gsr_test_vertex_records_webgpu` because another session had it open; that test ran from its previous build).
clang-format clean.

## Not fixed

* **The straight far edge of the sea.** With the band gone, the sea ends in a line against the flat fog colour, about
  70 % fogged from the shore (`ev/tex_toggle_band.jpg` row 2 shows it from above). It cannot be pushed out past z = 0
  (unsigned layer origins, see above), and 13 more cells would move it only from about 123,000 to 140,000 units away. A shorter
  fog for this level (the PC's own look) would hide it at the cost of the far view; not done.
* **A thin dark line across the sea** west of the village (`ev/seam_crop.png`, also in round 3's 9 November note as "a
  thin dark line near the rock"). It is there with modern water off and with the upscale off. From the shore spot it
  runs out to sea and stops where the water layers change: it fits the join of water layers 195 and 196 at x 237,568
  (z 124,928 to 143,360), which ends where layer 141 (no join at that x) begins. Not proven: a run with the software
  renderer to tell renderer from pack data produced no picture (`PS2X_GS=sw` shots are not written by the harness).
