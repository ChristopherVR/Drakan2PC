# Equipped weapon and tutorial HUD mapping

## Rotating compass edge follow-up

The corner classifier previously required the entire compass quad to stay
inside the native screen. Diagonal headings can extend below its bottom edge,
which disabled the HUD transform and made the ring jump away from its orb.
Corner admission now checks the mesh centre and bounded dimensions while
retaining the post-world, perspective and lower-corner gates. Native clipping
continues after transformation.

The 360-heading regression fails with the previous rule (exit 35) and passes
with the correction. The complete HUD CPU suite and Release game build pass.
Private scripted mouse sweeps record clipped compass quads changing from
`mappedR=0` to `mappedR=2`, with intact compass artwork in renderer images.
Evidence is under `build/orb-motion/`. These sweeps use the native renderer;
the exact user scene and SR/NR motion were not separately reproduced.

The gameplay corner path used to partition every non-sprite draw into
individual triangles. Phase closure was sufficient to admit fogged triangles
over either lower corner, even when they belonged to late world geometry.
In a private sword capture at renderer frame 4500, the weapon shine draw
arrived with `world=0`, `flags=0x037`, `TEST=0x5002d`, and perspective
Q values around 0.0007. The orb ring following it used Q=1.

The backend now excludes textured perspective triangle lists from gameplay
HUD classification, including the permissive corner path. Inventory and
explicit menu maps retain their existing behavior. Single mesh submissions
retain one transform; triangle partitioning remains available for a batch
spanning both bottom corners.

Top-band tutorial sprites use a shared center/top transform, independent of
the previous frame's clusters. This preserves spacing between disconnected
words and uses one HUD size for the text and the full-width band's height.
The band keeps its full width.

Validation: the HUD CPU suite passes, including shared-word spacing at four
aspect ratios and three sizes, captured world/UI Q values, rigid-mesh versus
two-corner partitioning, and existing inventory/menu policies. Private native
and SR sword/equipment runs completed with exit 0. A further HD-texture
SR+NR run initialized the NR feature, displayed the sword intact and exited 0.
Direct renderer images
show an intact equipped sword; desktop BMPs obscured by another application
were excluded. The exact scene and tutorial trigger from the user's screenshot
were not reproduced, so these checks do not certify that exact scene.

Build logs, private scripts, captures and completion receipts are under
`build/hud-layout-fix/`; private game files are not part of this change.
