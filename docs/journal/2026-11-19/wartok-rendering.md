# Wartok Canyons water and terrain rendering

The installed Order of the Flame quicksave was in Ruined Village. The replay used a private Wartok quick slot whose save
file matches the installed Wartok manual save byte for byte. The bridge comparison used a fresh direct start.

## River at the entrance

The visible river layers meet at the same height and use the same water material. The near triangles emitted by the
PAL engine's CPU clipping path had a texture coordinate close to `-2^31` at all three vertices. Their neighbour
triangles had the expected ripple coordinates. Changing the water's opacity, ripple images, or filtering kept the
boundary visible.

Layer 393 starts at row 2 and is 8 rows tall. The engine subtracts half the height before halving the result again
to set the ripple origin. A logical right shift of `2 - 4` turns it into a large positive value. Converting that value
to a float and negating it rounds away the variation along the river. The PAL translation config applies the signed
calculation to both axes in both water paths.

At the matched raster point, the installed build produced T/Q values 5.583, 6.000 and 6.000 where the old draw gave
approximately -2,147,483,648 at every vertex. The before and after cave images show continuous ripples in the corrected draw.
Evidence is under `build/canyon-render-20261119/` in `water-proof.txt` and `water-final.jpg`.

## Forward movement

The mounted replay held W for ten seconds and sampled 96 images. A second replay sampled 38 images at the installed
3840 × 2160 presentation settings. The captures show a nearly uniform patch as the camera enters
the low passage. Both sectors list the roof as visible. From the settled pose the upper centre of the view hits the
same roof triangle at a depth of 69 to 131 world units. Those rays cover a patch only 142 by 57 units across the dark
roof. Raising the roof in a test pack made open seams visible, so the installed pack keeps the cave geometry.
The sampled replay did not establish a separate intermittent flicker.

## Hill by the first rope bridge

The stripe on layer 405, grid cell (43, 23), has a visible triangle with a gradient of 0.892. Averaging the two
triangles gives 0.816, below the converter's 0.84 cutoff for retaining the original rock texture. The converter
now checks the largest visible triangle gradient. Both triangles follow the authored diagonal and holes carry no
weight in that decision.

The first rebuilt pack still showed a band from the native rock. Terrain textures blend in draw order,
and the rebuilt chunk moved the coarse native rock after the retained tile. Changing only the order of those
passes removed the band from the recorded bridge view. The converter now moves the native rock before the retained
tile whenever those two share a chunk. The other passes keep their relative order. The distant peaks still use the
game's coarse color maps.

The installed campaign pack reproduces the corrected bridge view in `terrain-installed.jpg`. It keeps save build
`493b53de681b8791`, revision 1. The local Wartok and Ruined Village saves loaded in private copies, and the 11 original
Order of the Flame save files matched their pre-build hashes.
