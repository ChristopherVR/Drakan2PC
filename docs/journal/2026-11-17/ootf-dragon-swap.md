# Order of the Flame dragons played by the Drakan 2 dragons (2026-11-17)

Step 1 of [the new-models plan](../../proposals/ootf-new-models.md): the converted OotF levels drew the Crimson, Blade and
Ice Dragons with their 1999 meshes although the disc ships remakes, because the class names differ. Probes, packs and
frames are under `build/dragon-alias/` and `build/ootf-run/dragon-*` (not tracked). Only class and field names and
numbers are quoted here.

## What the classes are

PC `NPCs.odb` against disc `NPCS.ODB` (engine class 30 on both sides, 69 shared fields):

| OotF class | Disc class | Same ids | Values that differ |
| --- | --- | --- | --- |
| Crimson Dragon | Crimson Dragon (Bat) | class 3040, model 3671 | health 175 / 200, attack radius, accuracy, suicide run % |
| Blade Dragon | Blade Dragon (Crow) | class 2676, model 3252 | health 150 / 125, attack radius, turn acceleration |
| Ice Dragon | Ice Dragon 1 | no (PC 3063 / 3682, disc 5604 / 5146) | health 400 / 175, detect radius 25 / 40, field of view 210 / 359, speeds, sounds |
| Boss Blade Dragon | (Blade Dragon (Crow)) | PC model 3932 is the Blade Dragon mesh with a green skin | health 200 / 125 |
| Ebon Dragon | (Crimson Dragon (Bat)) | own PC model 3676 | health 225 / 200, sight, retaliation |

## What changed (formats)

* `PC_CLASS_ALIASES`: Crimson Dragon, Blade Dragon, Ice Dragon to the disc classes above.
* `DEFAULT_PS2_MODEL_SUBSTITUTIONS`: three enemy rows (disc model and clips). New row field `pcFields`: plain value fields
  each placed creature takes from its OotF object or class, written as object parameters (the Scavenger's health-until-flee
  block in `ootfpipeline.ts` now does both). The dragons keep `Initial Health`; the Ice Dragon also `Detect Radius (lu)`
  and `Field of Vision`, so it wakes where the original did. Movement stays the disc's (it is tuned to the disc clips).
* The Blade and Crimson Dragon AnimWay Points are name-mapped onto disc way point classes that use the same disc models.
  Before, the way point retargeted the PC clips onto the disc model and overwrote its clips in `NPCS.ADB`, so a swapped
  dragon would have flown with the 1999 motion. A class whose disc model is played by a `drakan2` enemy row now maps its
  scene clips by name like the enemy itself.
* Not swapped: Boss Blade Dragon (the disc model has no green skin) and Ebon Dragon (own mesh); both wait for a recolour.

## In game (`ootf_level_run.py --campaign`, harness window settings, test aid `DRAKAN_DBG_PULL`)

| Run | What the frames show |
| --- | --- |
| `dragon-wc` (Wartok Canyons, four Blade Dragons pulled) | the disc crow-style Blade Dragons flying and breathing fire at Rynn on Arokh (`build/dragon-alias/wc_zoom.png`) |
| `dragon-grotto2` (Grotto, Ice Dragon pulled and shown with `DRAKAN_DBG_PULLFLAGS=1`: it starts hidden until its trigger) | the disc Ice Dragon flying in the cave and firing its ice beam, shadow on the wall (`ice_zoom.png`) |
| `dragon-grotto3` (Grotto, Crimson Dragon pulled) | the disc bat-winged Crimson Dragon with its ring breath (`crimson_zoom.png`) |

Conversion of Grotto, Islands and Wartok Canyons: "swapped creatures kept Order of the Flame values: Crimson Dragon x12"
(Islands), "Ice Dragon x1, Blade Dragon x8, Crimson Dragon x1" (Grotto), "Blade Dragon x9" (Wartok Canyons).

Not checked: a full fight to the kill (hit points were set at convert time and read back in the log, not counted in game),
and the Islands dragons in their own setting (the level opens in the arrival cave). The runs used the harness window
settings, not the player's `drakan.ini`.
