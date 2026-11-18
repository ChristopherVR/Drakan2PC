# Order of the Flame enemy dragon movement

The converted Blade Dragons in Wartok Canyons can appear still when they hover. On the current build, the crow near
the western end of the upper canyon held its horizontal position while the hover animation advanced through several
loops. It then flew, turned and fired its Crow Fire Breath during the fight.

Both runs began from a copy of the player's Wartok Canyons save in Slot 2. The test script placed Arokh near the
encounter and aimed him at the nearest dragon. The keyboard and mouse run used forward input and the left mouse
button. The controller run used the left stick and Square, with stick movement and breath requests visible in the
game log.

| Wartok Canyons dragon | Input | Greatest distance from its first sampled position | Health |
| --- | --- | ---: | --- |
| Blade Dragon 2739 | Keyboard and mouse | 12,421 world units | 150 to 101 |
| Blade Dragon 2738 near Ballista 252 | Controller | 3,265 world units | 150 to 104 |
| Waypoint-linked Blade Dragon 1091 | Controller | 18,735 world units | 150 throughout |
| Waypoint-linked Blade Dragon 1092 | Controller | 32,619 world units | 150 to 101 |

The Grotto test started the installed campaign at that level. The Ice Dragon lifted from its valley and moved 22,597
units from its first sampled position. Its health stayed at 400 because the test's shots missed. The Crimson Dragon
turned, moved 4,422 units and lost health from 175 to 128 during the controller attack.

The test artifacts are in `build/harness/ootf-hostile-wartok`, `ootf-hostile-wartok-pad`, `ootf-hostile-driveby`
and `ootf-hostile-grotto`. The Wartok capture sets contain a live Crow Fire Breath projectile and show the hover
advancing while Arokh waits without attacking. The tests cover the listed encounters and did not reproduce an AI
freeze.
