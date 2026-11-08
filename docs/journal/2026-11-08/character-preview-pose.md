# Character preview pose - 2026-11-08

The multiplayer "your character" preview and lobby showed every character in the PS2 bind pose, a T-pose.

Fix, in `analysis/scripts/character_previews.py` (DCPM v1 and the C++ loader are unchanged): `read_rig` decodes the part of the 0x0208
record the pose needs (node names, joint origins = `-(R t)` of the inverse-bind matrices, first-child / next-sibling tree, per-LOD skin
weights, LOD first vertices) and `lower_arms` rotates each `Luparm` / `Ruparm` joint and its subtree about the joint origin so the arm
ends `ARM_ANGLE` (15 degrees) off straight down, outward. The vertices are then re-skinned with their original weights, so shoulders and
sleeves deform instead of tearing. Joints are looked up by name; a model without `Luparm` / `Lforearm` (dragons) or whose arm is not
sideways is returned unchanged. The script hashes itself into `source.sha256`, so old bundles regenerate.

Checked from the PAL disc on all 15 roster models (Rynn, Northmen Archer, villager/monk/guard family, Ranwulf, Atlus, Serris; no orc
or Arokh is in the current roster) with the script's `render()` front and side views: arms hang beside the legs, no gaps or stretched
triangles at shoulders, hands in front of the thighs. Test: `scripts/tests/test_character_preview_pose.py` (synthetic skeleton).
