# OotF terrain texture cache pressure

The normal New Game route intermittently stalls at the Ruined Village opening and at scene 1.1 to 1.2.
Native cache allocation checks current bytes plus requested bytes plus 200 against the manager budget.
The texture manager has a 5,662,310-byte budget. Baseline RAM samples have only 8,806 bytes free, with
converted 128 by 128 RGBA terrain records needing 65,600 bytes each. Eviction scans eligible unpinned,
unreferenced entries; repeated pending terrain resources acquire fresh descriptors. Semaphore snapshots
do not show a mutex deadlock. A pending Interface texture alone was not evidence of a missing campaign asset.

## Controlled comparison

The private candidate `build/scratch/ootf-indexed/mods/ootf-levels.zip` starts from the exact baseline pack.
Only `files/ENGLISH/WORLDCON/PROCTEES/PROCTEES.TXD` changes. Native entries are retained, and every other
ZIP entry is byte-identical. All 879 converted terrain textures retain their IDs, ordering and dimensions.
The existing formats indexed encoder reduces their combined payload from 53,731,392 to 14,905,008 bytes.
Decoded pixel RMS difference averages 1.124 and is at most 1.886 on the 0-255 channel scale; 72 records
remain pixel-exact. Detailed audit and candidate generator are private under `build/scratch/`.

Candidate SHA256: `03374b5e329f2fd468bf4ec2350b19deacf2b939104cd7a1c2fcfb27926c7e5e`.
Both runs use executable SHA256 `74D4D64C58BE3AF9AD9EB367C9BCD5DF8B1D02C9557CC1CC75E6AEA7CFEADB84`.
The baseline starts at the normal title and reaches RV but remains at scene 1.1 elapsed zero in RV+15 and
RV+75 RAM snapshots. The indexed run follows the same route without a cinematic skip or forced exit:
RV+15 reaches elapsed 13.93 seconds with 4,054,744 texture bytes resident; RV+75 reaches scene 1.2 elapsed
13.14 seconds. Scene 1.2 continues to advance at normal speed. Full opening completion and control restoration
are still being observed; neither these intermediate results nor the pack audit establishes campaign completion.

## Formats change

`TerrainTextureBuilder` defaults to indexed encoding, retaining the existing dimensions and source texture
mapping. Explicit truecolor options remain available for controlled comparisons. Regression fixtures verify
that a texture with more than 256 colors uses bounded indexed storage without reducing dimensions, while an
explicit truecolor request remains effective. The terrain/texture suite passes 21 tests and formats typecheck.
## Complete opening result and remaining blocker

The indexed comparison finishes all three original opening scenes naturally at about 277 seconds. The player
then responds to three seconds of W: position changes from (219033.109, 19248.502, 170358.344) to
(218709.641, 19248.494, 170022.328), roughly 466 world units. Camera follows, scripted mode is zero, GUI is zero,
and the screenshot shows Rynn and the normal HUD. No cinematic skip, forced exit or player teleport was used.

This validates the startup improvement, not campaign stability. At about 311.9 seconds a watchdog records a
native heap/free loop at PC 0x4acdd0, return address 0x4ac784, following cleanup calls. The candidate is not promoted
to the normal play folder while that stall is investigated. Establish whether indexed texture cleanup exposes a
format defect or a separate native lifetime/allocator issue before proceeding with normal traversal.
