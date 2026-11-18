# Ruined Village ceiling near Heron's Crystal

The approach reproduced from the original Ruined Village slot 3. A brown strip appeared over the far arch as Rynn
walked toward the cave. The overlap belongs to object 2092 at the end of the straight hall. The same model appears
again at object 2100.

In PC model 3875, the cave-facing rock quad and the two hall-facing plaster triangles overlap within a fraction of
one world unit. The PC back-face culling gives each side its own surface. The converted model draws both sides,
so the rock and plaster compete for the same depth.

The converter gives the rock quad four separate vertices and moves them 16 world units toward the cave. The
hall-facing surface then sits in front from the hall, and the rock sits in front from the cave. The full pack
comparison changed only the vertex, polygon and draw-list records of converted model 7519. The material
assignments, UVs and collision bounds stayed byte-for-byte, as did the pack's save build and revision.

The installed game loaded a restored copy of the original slot 3, archived it byte-for-byte, and repeated the walk
without the brown strip. Additional captures from the installed game show both sides of the model's second placement
and the cave side of the reported arch. The private recipes, comparisons and pack audit are under `build/crystal-arch`
and `build/ruined-arch-overlay`.
