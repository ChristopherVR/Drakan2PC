# OotF Intro owl: brown and dark triangles over the chest

Report: the perched owl of the Intro looks broken (clipping, wrong parts). Fixed in the converter (formats `ootftex.ts`,
`ootfassets.ts`); nothing in the runtime changed.

## What was checked first

* The model (`3d_owl`, PC model 66: 249 vertices, 410 triangles, 37 joints, one texture) converts completely: the decoded
  0x0211 list has exactly the 410 source triangles at the same positions, every vertex has weights (sum 1), and the LOD / stump
  cap rule of the previous commit keeps all 410 polygons. A CPU render of the PC mesh in bind pose is an intact folded-wing owl.
* The converted texture matches the PC one (256 x 256, 8 bpp, 4 bit graded alpha kept in the palette, square).
* Culling of the Intro (back faces) does not touch the owl: it is a skinned model, not a culled class.

## Cause

The owl texture has graded alpha (feather edges), so it is written with alpha selector 8 (blend). A blended draw does not
write depth, and the owl mesh is built of overlapping feather cards (inner wing and chest layers lie within 1 % of the outer
surface in 5 % of the pixels). In strip order the cards that should be hidden behind the chest painted over it: sharp brown
and dark triangles on the chest and belly in the Intro 1 close-up, and ragged wing sides.

## Fix

Textures of skinned models (already forced 8 bpp square) now cut alpha at 50 % (`hardAlpha`): every texel is opaque or clear,
which selects the depth-writing alpha-test selector 1. `OOTF_SKIN_SOFT_ALPHA=1` restores the blend for comparison.

## Result

Private two-run comparison (same executable copy, same Intro pack except the texture flag; screenshots under
`build/introgfx/owl1` (blend) and `owl2` (cut-out)): at 16 s and 17 s of the Intro the blend run shows the triangles, the
cut-out run shows a clean chest and wings. Feather edges are slightly harder.

## Open

Other skinned classes with graded alpha also get the cut-out; only the Intro was re-shot. The owl turns its head sideways in
the clip (animation, as on the PC). Run notes: the harness needs `DRAKAN_AUTOPRESS_UNTIL=11000` and no pad taps now, or
the auto-press skips and pauses the Intro scenes.
