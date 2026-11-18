# River appearance in Wartok Canyons

The pale river in the report is below Ballista 171, the barred relief door and the waterfall beside it.
Direct-starting Wartok Canyons and placing mounted Arokh near `(142000, 23200, 264000)` reproduces the view.
The latest manual save starts in the entrance cave, where the water appears as a narrow strip on the right.

The original Wartok river uses the PC Water material with ripple textures 388 and 389 and opposing scroll directions.
Drakan 2's plain Water uses the same texture pair and directions, with slower scrolling. The conversion substituted
Surdana Water. It uses a different texture pair and moves both layers in the same direction, which looks pale and
nearly uniform from the canyon at flight height.

Wartok Canyons now prefers the plain Water material. A private pack test compared all 441 terrain layers and changed
only the material references on 2,423 water cells. The plain material showed distinct moving ripples at the cliff and
along the river without moving its shoreline. Both packs continued to load a copy of the entrance save.

The stationary captures and 48 samples during a short flight towards the door show continuous water in both versions.
Ballista projectiles and flashes cross the view during the flight. An F5 and F8 replay from the same spot captured 22
gameplay images per material during the first second after loading. The water remained visible through the scene fade.
A dark patch in the sampled area came from Arokh's wing passing in front of the river. These runs did not reproduce
a separate water visibility flicker. Evidence is under `build/ravine-water-new/` in `exact.jpg`, `move-current.jpg`,
`move-plain.jpg`, and `quick-water.jpg`.

The ice-shaped object in the screenshot did not recur in 32 captured frames from the same location while Arokh
hovered and moved. All six projectile factory calls in that interval used the stock Flaming Bolt class, which the
PC ballista also names. The visible bolts in the replay were orange. The capture did not identify the blue object.
