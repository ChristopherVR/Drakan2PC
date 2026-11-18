# Arokh's awakening and fireball effects

## Awakening

The 20:40 Ruined Village quicksave and the older `da1e598e3d3adfdd` Slot 33 backup were played through the awakening on
pack `493b53de681b8791`. Both runs used copies of the save and sampled the scene every two seconds through the save
prompt. The backup logged the revision 0 to 1 upgrade and the addition of statue object 3353. In both runs the statue
appears on the pedestal, live Arokh moves his jaw and wings, the bonding effects play, and Arokh flies out of the lair.

The report identified wing clipping. The PC and Drakan 2 dragons have similar wingspans, with different segment lengths
and shoulder positions. During a fold, applying the PC dragon's rotations directly to the Drakan 2 wings could put
the elbows, wrists and tips closer to the body. In the `1_6z1` pose the PC wing tips sit about 1,040 units from the
body's centre line and the previous conversion brought them to within 250–300 units. The converter now turns each
wing segment outward when it needs clearance to match the PC fold. It keeps the native bone lengths and passes the
turn along to child joints so the membrane moves with the wing.

A skinned-mesh probe used both games' actual rigs and checked the same poses on the Drakan 2 mesh before and after.
For the opening wing overlay, intersections with distal wing triangles fell from a peak of 73 to 52, and the wing-root
count fell from 96 to 38. In `1_6n`, the first spread of the wings after Arokh comes down from the pedestal, distal
intersections fell from a peak of 137 to 62 and from an average of 25 to 5. This metric also counts folds and attachment
overlap in the authored meshes. The PC `1_6n` peaks at 176 and the native Drakan 2 Ready clip at 140 on the distal
metric. The runtime side-by-side replay of the first 22 seconds shows the wing opening farther away from the chest
while keeping the flap. Some fast folds still show a crease.

The complete scene runs are in `build/harness/awakening-current-active` and `build/harness/awakening-current-old`.
The dense original and corrected comparisons are in `build/harness/awakening-wing-dense` and
`build/harness/awakening-wing-hier`.

## Fireball

The 20:41 Wartok Canyons save starts with Arokh facing low cave terrain. With primary attack held, the large white and
orange bursts cover much of his silhouette from the mounted camera. Native Drakan 2 in Ravenshold also shows this
effect during sustained fire. Both campaigns use the disc's FireBall 3 class. Its 10-point charge cost and 0.75-second
repeat interval match the trace. The log labels the rechargeable meter as dragon health.

The exact Wartok Canyons save was then loaded with three private test mods, one at a time. Removing the projectile's
lens flare or launch smoke left the bright bursts visible. Removing only its four spawned impact effects stopped the
bursts, with four launches still traced at 0.000, 0.751, 1.508 and 2.260 seconds. Those entries produce the explosion,
ring, shrapnel and flash. A close impact fills the mounted view and overlaps Arokh's silhouette.

An open-air control used the same save and stock effects. At (120950, 50000, 453966) with Arokh aimed 40 degrees up,
the trace recorded two launches and the frames showed the projectile above his head. The large burst did not appear
during the sampled shots.

Captures are in `build/harness/awakening-user-canyon`, `build/harness/awakening-d2-native` and
`build/harness/awakening-no-spawned-explosions`, with the open-air control in `build/harness/awakening-user-canyon-sky`.
The tests used private save copies and loaded the effect overrides from separate mod folders.

### Grounded follow-up

A single primary shot on the Wartok save reproduced the flame over the grounded dragon between about 0.2 and
1.0 seconds after firing. A separate placement on an outdoor bank reproduced it too. Projectile factory traces
put the cave impact about 10,133 world units from the camera, with the projectile originating at the mouth.
Removing only the main impact particle generator cleared the bright burst in the matched tests. Removing only
the ring or flash left it visible.

The runtime skips that one visual generator for FireBall 3 impacts within six terrain cells of the mounted OotF
camera. The projectile, the hit, the ring, shrapnel and smaller flash still take their normal paths. The grounded
cave and bank runs each launched one shot and reported the close burst correction. The open-sky run launched two
shots with the stock effect path available.
