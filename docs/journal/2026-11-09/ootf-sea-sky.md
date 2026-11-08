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
