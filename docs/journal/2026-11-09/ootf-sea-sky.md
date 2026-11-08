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
