# Arokh's awakening and fireball effects

## Awakening

The 20:40 Ruined Village quicksave and the older `da1e598e3d3adfdd` Slot 33 backup were played through the awakening on
pack `493b53de681b8791`. Both runs used copies of the save and sampled the scene every two seconds through the save
prompt. The backup logged the revision 0 to 1 upgrade and the addition of statue object 3353. In both runs the statue
appears on the pedestal, live Arokh moves his jaw and wings, the bonding effects play, and Arokh flies out of the lair.
The model issue was not visible in the sampled frames. The test recordings are in `build/harness/awakening-current-active`
and `build/harness/awakening-current-old`.

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
