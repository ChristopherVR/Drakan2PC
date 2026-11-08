# OotF Ruined Village: the sea ends short of the horizon (2026-11-09)

Reports: the sky over the sea looks wrong in the distance, and there seems to be less water than on the PC.

## Findings

* Water surfaces were not lost in conversion. Ruined Village has 102 water layers on both sides; water cells with a texture
  7855 (PC) against 7646 (converted: a cell with a hole in one triangle becomes a hole, 209 cells); every water cell maps to
  one native water texture.
* Cause: the engine draws, from the layer the camera is in, only the layers of that layer's `visible` list. The PC lists were
  authored for the PC draw distance (Level Properties fog depth 12 / 30 lu; the converter keeps the longer PS2 template distance).
  The village layer under the first screenshot spot listed 4 of the 102 sea layers, 29 of 188 floor layers listed none. The
  rest of the sea was not drawn, so the sea stopped in a diagonal edge against the sky, a hard seam instead of fog.
* The Procedural Sky itself (cloned from the template) shows no seams; the flat colour comes from the template's night cycle.

## Fix (formats)

`linkFarWater` (convert.ts): every layer's `visible` list gets the water layers whose bounding box is within 96 cells
(`WATER_VISIBLE_CELLS`). The village layer now lists 100 of 102 sea layers. Test in `pc.test.ts`.

## Verification

Private game copy, Ruined Village alone as a campaign pack, `place` on the headland above the village: the diagonal sea edge is
gone and the horizon is a smooth hazy line (`build/ootf-run/sky1` before, `sky2` after). Four back-to-back runs of one spot gave
129 / 140 / 44 / 133 fps (before / after / before / after, other games running), so no clear cost. Islands and Wartok load and play
the start recipe (PASS).

## Open

* A thin dark line near the rock off the village headland (a degenerate sliver, probably a water hole cell) remains.
* Not compared against a PC capture; hole cells that are half water on the PC (209) still turn to holes.
* Frame cost of the far water layers not measured on a quiet machine; `WATER_VISIBLE_CELLS` is the knob.

## Follow-up: sheets over the valley, half-water cells, waterfalls

* The stair-stepped sheet beside the church is not new with the far links: the same sheet draws without them (`sheet_original_no_far_links.png`),
  it is a pool (Temple / Coastal Water layers at 11000) the PC lists from there, seen from the hill with no bank around its cell
  edges. The diagonal cut of its stair steps is the 418 shore cells that are water in one triangle only; they were converted to holes.
  Water layers now keep the water triangle's texture (`convertLayer`), which gives clean diagonal edges
  (`pool_edge_seaonly_vs_halfwater.png`) and brings back the shore water.
* The far link is narrowed to the sea (`linkFarWater`): only water layers with 80 % of their water vertices within 64 of the level's
  most common water height are added to the lists. Rivers and lakes above the sea (the 19500 rivers, Coastal 07 slopes) are left
  to the PC lists, since linked from the temple they were more sheets over valleys.
* Waterfalls: for all 18 Ruined Village waterfall objects the water layers within 8 cells are in the lists of the floor layers
  there (audit `fall.ts`, not kept), so lists were not what hid the pools; the half cells were. Not audited in the other levels.
* Not reproduced exactly: the user's quicksave cannot load into a private pack (a play card with saves opens the load menu, and
  the save records the pack build), so shots use `place` near the church. The sheets still seen from mid-air at the church are
  PC pools with no bank; flattening or dropping those is not done.

## Follow-up 2: the user's quicksave spot (church path)

* Reproduced exactly: the user's slot 33 loads in a private copy with the installed pack (build 1a8d4afc503be6a1; the card slot copied
  to slot 1 so the harness's autopress Continue picks it, `.mods` record untouched). The "after" run swaps only the terrain entry
  (LVL type 1) of that pack's Ruined Village level for the new converter's (`writeSrsc`), so the save still restores (`fromSave=1 ok=1`)
  and the camera is the user's. Images: `build/review/water/user_spot.png` (before left, after right).
* The sheet belongs to the layers Temple Water 06 (origin cell 139,77; 9x7), 04 and 03 at height 11000. They are authored water
  (cells touching the ground, W0 to W3 in the audit) on a ledge; the cells beyond the lip are holes over a drop of 6000 to 9000, so the
  edge is the layer's own cell outline. The PC lists those layers from the church layers (`inPcVisible` true), so the PC draws the same
  sheet. The sheet is not floating water from the converter: no per-cell visibility or depth test was found that the converter drops.
* Tried: turning water-layer hole cells with a vertex over 512 above the ground into invisible cells; the picture did not change,
  so those hole cells are not what draws there. Reverted.
* After the fix the left horizon shows the sea (before: void) and a dark slab over the valley is gone; the sheet is larger because
  the shore half cells are water now.
