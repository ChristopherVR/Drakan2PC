# Water below the Ruined Village rope bridge

The reported view comes from the 20:35:40 OotF quicksave. Rynn is at
`(308641, 23331, 220218)`, looking from layer 187 towards the river and the rope bridge.
Loading a copy of that save in the normal menu reproduces the view.

The rock bank separates the two visible stretches. The source level puts the upper river at
19,500 world units and the downstream basin at 17,500. The waterfall below the bridge
connects those elevations farther left. Both waterfall models kept their original positions
and class types in the conversion. A game run from the downstream side shows flowing water
and splash particles, with Rynn treading water (animation `0x20637`).

An offline check cast a 640 × 360 ray grid from the loaded camera against the authored and
converted terrain. The terrain sample tests water against floor and ceiling triangles and
does not include object meshes or wave displacement. It finds the same four exposed water
layers on both sides (205, 264, 291, and 292). The converted terrain covers all 39,903
water samples from the original terrain and 10 more at the edge. Including every nearby
layer gives the same water coverage. In the game, disabling far scenery or raising view
distance to 1.01 gave the same two stretches from the quicksave.

The converted river uses the Drakan 2 Surdana Water material, which looks dark and smooth
from this distance at night. Modern Water kept the same visible coverage. A private pack
probe swapped 138 local cells to the plain Water material, which comes from the same
material family as the PC source. It gave the water darker blue color and stronger ripples
while keeping the shorelines and waterfall in the same place. The report did not establish
a lost surface or waterfall, so the campaign keeps its existing water material.

Private runs and analysis are under `build/ravine-water/`. `exact-original/exact.png`
shows the quicksave view, `waterfall/fall_west.png` shows the downstream basin, and
`plain-both.jpg` compares the two materials. The geometry comparison uses the original PC
level data. The playable captures are from the port.
