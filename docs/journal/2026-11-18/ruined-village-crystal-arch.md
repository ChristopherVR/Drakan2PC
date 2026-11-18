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

## Detecting the overlap during conversion

The converter now finds the cave-side face from the model geometry. The rock quad shares an edge with a
near-coplanar, oppositely wound patch that covers most of it. The two sides use different opaque materials. A scan
of 3,122 source models found that case once, and the geometry it produces matches the installed arch fix.

The second report showed the passage into the crystal room. The doorway and the tomb mesh meet at the same
vertical plane and each draws overlapping triangles that face away from the other. The level converter checks
touching static models that have a single placement and finds the overlapping faces. The scan across the supplied
campaign found one matching pair, at this doorway. The six doorway faces move by 16 world units along their
authored front; the original indices and materials remain available to the rest of the mesh.

The complete pack changed only the doorway's vertex, polygon and draw-list records. The existing rock-cap model
and the save build match the already deployed pack byte-for-byte. In-game tests walked toward both arches from
slot 3 and viewed the doorway from inside the room. Captures are under `build/crystal-arch-room`.
