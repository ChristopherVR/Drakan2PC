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

## Snowy mountain faces

From the same bridge view, the distant peaks sit roughly 111,000 to 135,000 world units away. Their authored detail
passes fade out at 61,440 units. The remaining four-texel-per-cell map is projected from above, stretching its
surface detail vertically on the steep faces. At four times the texture distance, adding side projection restored
some detail but repeated the snow borders in a grid across the cliffs.

The Direct3D 11 renderer now shades distant steep faces in Order of the Flame with world-space relief. It uses
the same surface shading on the far map and the individual texture passes, so the painted snow borders stay in
place as the texture passes fade. The comparison at normal texture distance shows detail on both the left cliffs
and the distant peaks. The original mountain silhouettes still reflect the coarse terrain mesh.

The matched bridge comparison and the movement samples are under `build/snow-mountains-20261119/` as
`wartok-before-after.jpg` and `bridge-motion.jpg`.

## Snow accumulation on the bridge range

The relief pass gave the rock detail, but the high faces still read as bare rock. The view from the first rope
bridge puts the distant ridge at about 37,000 to 55,000 world units above the map origin. The lower faces and
nearby hills sit below that band. The rendered snow now builds from thin, broken cover on the shoulders to
continuous caps at the highest elevations. Ledges hold snow lower down than steep slopes. The snow receives
the ground's lighting and fog, and thick cover subdues the rough cliff shading underneath.

The level load supplies the Wartok scene flag for New Game and save restore. Within the level, the snow fades out
past the bridge range. Terrain samples found walkable floors under roofs at snow-line elevations farther east.
The first named cave floor in that direction starts around world X 198,600. The profile reaches zero by X 170,000
and excludes the visible undersides of ceilings. The in-game captures are in `build/snow-cover-20261119/`.

## Drakan 2 Blue Snow on the other Order of the Flame ranges

The converted campaign's Blue Snow tile is byte-identical to the Drakan 2 tile in `PROCTEES.TXD` entry 563. Its
decoded mean RGB is 224, 240, 240. The new terrain ranges use that color with low-contrast variation, level lighting
and fog. The first rope bridge range retains the approved palette. Coverage from overlapping ranges merges smoothly.

The installed Order of the Flame terrain audit found outdoor snowy mountains in Wartok, Grimstone Mines and Grotto,
plus the air versions of Arena of Death and Grotto of Death. The profile table records the observed footprints and
local snow lines for those areas. The terrain audit sampled ground under ceilings to keep the lowest deposits above
cave floors. At the first test position in Grimstone, the broad 100,000-unit line produced little snow on
the exposed face. Its local lower line covers that face, and the sampled covered ground in that footprint reaches
only 54,950 units. The direct-start replays and geometry samples are under `build/snow-ranges-20261119/`.

## Mounted cave turn blackout

The original Wartok manual save and the isolated quick-slot copy have the same save data. Arokh starts grounded.
During a mouse turn the old camera carry swung the eye through the entrance ceiling on layer 394. In the two
revolutions, the eye crossed 136 units into the ceiling with original water and 123 with Modern Water. The world
briefly went dark while the HUD remained visible. Setting camera carry to zero removed the crossings and blackouts.

On the ground, mouse input now carries only the view orientation and the native camera controls its position.
The same two revolutions stayed at least 117 and 78 units below the ceiling in the sampled original and Modern
Water runs. Seven blackout frames were detected in each baseline turn capture and zero in either patched capture.
The air path still carries both the position and orientation.

The mounted forward captures did not isolate an independent river flicker. Modern Water passed coverage and
depth checks across the visible river. Turning on the separately controlled ray-traced reflections also reflected
the bright cave opening. The texture cache kept its HD textures through the scripted turns, with zero evictions
and rebuilds. Captures and the roof-clearance analysis are in `build/cave-flicker-20261119/`.

## One-frame water and bank overlap

Walking from the same mounted save and turning under the lower roof produced diagonal flashes along the riverbank
in both water modes. They also occurred with Arokh stationary against the bank. Three consecutive captures in
`build/cave-walk-turn-20261119/pixel-pick-live-captures/` (1298–1300) show the same water draw at a shoreline pixel.
The water depth went from 8 GS units in front of the floor to 2 behind it and then 2 in front again. The pixel
went blue, dark, blue. The depth-tested water draw kept the same opacity throughout.

The renderer now tags the native water draw scope in both modes. During a blended water draw that tests depth
without writing it, the renderer scales the depth toward the far plane by roughly two parts per million.
Clipped ripple packets can arrive after the scope closes. Those packets use the same tie break based on the native
ripple texture and draw state. The depth buffer, water geometry and ray inputs use their original depths.

The lower-cave replay with consecutive presented frames found 21 coherent flashes in 70 baseline triplets with
steady camera and actor positions. The corrected runs found none in 31 such triplets with original water and
30 with Modern Water. Open water remains visible. Run logs and the screenshot comparison are in
`build/cave-walk-turn-20261119/`.

The installed executable was checked on a separate replay that turns while Arokh is moving under the lower roof.
The 264 presented frames showed continuous camera movement through the turns and no full-view blackouts. The
test used the byte-identical quick-slot copy and left the player's save intact.
