# OotF: the swinging blades had no ropes, the maces no heads (cause, fixes, evidence)

Two reports from the owner's Order of the Flame play: "the swinging axes have no ropes" and "maces from OotF are not
rendering the mace top point with spikes; only the hilt is shown". Both are one class of fault (a PC class shown with a
disc model that is not the whole PC thing) with two different mechanisms. A third fault showed up once the ropes were
drawn: the engine's rope update lets the carried blade trail the rope, which is a runtime matter.

Private runs and scratch under `build/review/ootf-axes/` (`runs/`, `packs/`, `evidence/`, `scratch/`, `dec/`); no
original data in this note.

## 1. The traps in the PC data

A swinging blade is two placed objects: a **Rope** (engine class 73, "Rope / Cable": `Starting Length`, `Swing Period`,
`Maximum Swing`, ...) linked to a **Blade** (engine class 35, the mover / mechanism class; it carries the damage). The
rope object is the pivot; the engine swings it and carries its linked objects along.

| Level | Rope objects (PC id) | Carried | Swing Period (s) | Notes |
| --- | --- | --- | --- | --- |
| Ruined Village | 1437, 1445, 1447, 1449, 1451 | Blade 1438, 1446, 1448, 1450, 1452 | 1.1, 0.8, 2.5, 1.25, 1.5 | one corridor over a pit (x about 275,456 to 276,522, z 164,885 to 173,056, pivots at y 10,626, blades 2,244 below); rope scale (0.5, 0.13, 0.5), turned 180 degrees to hang down; Maximum Swing 0.375 |
| Wartok Canyons | 1273, 1274 | Blade 1518, 1517 | 1.2 | the break-away brick floor room (x about 242,500 to 243,100, z 128,409 and 131,977); unscaled; start on trigger (Detector 1375) |

The other blade traps of the campaign have no rope in the PC data either: Ruined Village's six `Swing Blade 02` turn out
of wall slots (`Swing Blade Slots`), the Islands have the `Spin Blade Trap` on its column, Volcanic the
`drillshaftblade`s.

**How the PC draws the rope:** the Rope class has its own model, `CableA01` (Mountain World Common): one three-sided
prism 8.1 model units long and 0.03 thick (6 vertices, 3 quads, one tiling texture). No alpha, no second material, no
bone chain, no engine primitive: a plain mesh, scaled by the placement. The engine keeps a private copy of the mesh and
moves its far-end vertices so that the rope is `Starting Length` long (decompiled rope init `3e3378` / `3e30d0` /
`3e4310` of the PS2 engine, which reads the same fields; the PC executable was not run). Render of the PC meshes at the
Ruined Village placement: `evidence/trap_render.png` (left), the rope ends in the blade's top knob.

## 2. Why the pack lost the rope

The class planner maps a PC class onto a disc class of the same name and engine class and the placed objects then show
the **disc class's model**. The disc has classes named `Rope`, `Cable` and `Static Rope` in Mountain World Common, with
other meshes:

| PC class (engine class) | PC model | Disc class of that name | Its model | Objects |
| --- | --- | --- | --- | --- |
| Rope (73) | CableA01, 8.1 long, +y | Rope | `BRopeOnly`: 0.198 long, 0.007 thick (a rope bridge stub) | 7 (the traps above) |
| Cable (73) | CableA01 | Cable | `rope`: 5.06 long, pointing -y | 1 (Grimstone Mines: the cable of the GrimStone Crystal) |
| Static Rope (35) | CableA01 | Static Rope | `rope`: 5.06 long, pointing -y | 1 (Wartok Canyons, moved by a Switch) |

So the traps' ropes were a 7 unit wide stub (drawn at the object's 0.5 x scale) in a dark cave, and the two
`rope`-model objects pointed the wrong way (the PC placement turns the +y mesh by 180 degrees; the disc mesh already
points down, so it ended up pointing into the ceiling). Nothing was dropped: no sub-mesh, no texture, no object. The
blades swung because the disc Rope class is the same engine class and carried them.

In game before the fix (`runs/axeA-before`, `axeB-before`, `wartok2-before`): blades swinging in the air, nothing above
them.

## 3. Fix 1 (converter): ropes keep the PC rope mesh

Formats `src/ootfownmodel.ts` (`ownModelClasses`): every rope class (engine class 73) with a model, and every other
class placed with the same mesh as one of them (the `Static Rope` mover), is kept out of the name mapping
(`PlanOptions.keepOwnModel`) and cloned with its converted PC model, as the Islands' `Swing rope` and Grimstone's
`lairswing` (other names, so never name-mapped) already were. `ootfpipeline.ts` calls it in the class stage. Tests:
`test/ootfownmodel.test.ts`.

## 4. Fix 2 (runtime): the blade stays on the rope

With the rope drawn, the blade did not hang on it: the rope's end and the blade's top were up to 16 degrees of swing
apart (600 units at the 2,244 unit radius), a constant offset per trap (`runs/axeA-fix1`, converter fix on the old
executable). Read from RAM over 40 dumps per run (`scratch/ropelag.py`): rope angle minus blade angle was 12.6, 15.7,
6.3, 11.4 and 9.8 degrees for the five traps at `world_rate = uncapped`, and 2.1, 2.9, 0.9, 1.9 and 1.6 at
`DRAKAN_WORLD_RATE=30` (2.3 degrees divided by the period). The rope itself swung off-centre too (one trap -60 to +25
degrees instead of +-43), so the blades did as well: that part is older than this work and changes the trap.

Cause, from the decompiled rope class (update `3e3610`, attach group `3ab1f8` / `3aafd8`; `dec/`):

* the objects a rope carries are bound to it lazily, at the end of the rope's **first** update, after the rope has
  already turned by its first step; the carried object is then rigid to the rope, but that first step behind it for
  good. On the console that is 2.3 degrees / period; with a long first tick (a level that has just loaded) it is much more.
* each update turns the rope by a small-angle step (`y' = normalize(y + z * velocity * tick)`), which turns by
  `atan(step)` instead of `step`: one long tick (a load hitch, a slow frame) leaves the swing off-centre for good.

Both assume the console's steady tick; the port's variable tick makes them visible. `drakan_frame_rate.h`
(`ropeUpdateHook`, PAL only) now runs the rope update with a 0 s tick until the carried objects are bound (it binds them
where the level placed them), and otherwise in equal steps of at most 1/30 s (a tick over 8 such steps is cut short).
The rule is `ropeTicks` in `drakan_frame_policy.h`, tested in `scripts/tests/frame_cadence_test.cpp`;
`DRAKAN_ROPE_TICK=0` gives the native update. It applies to every rope of both games (Drakan 2's cables carry lamps on
slow, narrow swings, where the difference is fractions of a degree).

Measured on the fixed executable and pack (RAM, 45 dumps over 29 s, `scratch/period.py`, `period2.py`):

| | rope minus blade (max, degrees) | rope swing range of the five traps (degrees) | rope time / wall time |
| --- | --- | --- | --- |
| `DRAKAN_ROPE_TICK=0` (native) | 12.6, 15.7, 6.3, 11.4, 9.8 | -49.5..37.1, -48.1..39.0, -45.1..40.9, -48.0..39.3, -46.6..39.2 (blades -62.1..24.5, -63.8..23.3, ...) | 0.994 |
| default (hook) | 0.0 for all five | -44.2..41.5, -44.8..42.6, -43.8..41.9, -44.2..42.1, -44.2..41.0 (blades the same) | 0.940 |

The blade's distance from the pivot is 2,244 units in every dump of both. The period is unchanged: the rope's own time
accumulator advances with the engine tick as before; the 6 % lost in the hook run is the 45 RAM dumps themselves (each a
frame longer than the 8 step limit; the native update loses 0.6 % to the engine's own tick cap). Not measured without
dumps.

## 5. The maces

**PC:** `Mace` (engine class 14, Resources) has one mesh of 96 triangles, handle and spiked head together. `Magic Mace`
and `Death Mace` likewise.

**Why the head was missing:** since 2026-11-08 plain weapons take the disc weapon's model (`ootfps2model.ts`): the
clone of the PC class names the model of the disc class `Steel Mace`. But a Drakan 2 weapon is not one model: the disc
class's model is `SteelMace_Handle`, and the class record names a **child part** class (engine class 203, here
`Steel Mace Head` with the model `SteelMace_Head`) in its auxiliary class reference (record body +10). The class planner
clears that reference on every clone, on purpose (a template's companion is unrelated to the PC class). So the swapped
weapon drew the handle alone, in every state: on the ground, in either hand, in the modern and the native inventory
(`runs/mace3-before`, `mace4-before`). Render: `evidence/mace_render.png`.

**Affected, from an audit of all 33 PC weapon classes placed in the campaign** (`weapon-audit-head.txt` before,
`weapon-audit-fix1.txt` after; PC triangles and textures against the pack's model per class):

| PC weapon | Disc model shown | Child part that was missing | What was lost |
| --- | --- | --- | --- |
| Mace | SteelMace_Handle | Steel Mace Head | the head |
| Battle Hammer | Steel_HammerHandle | Steel Hammer_Head | the head |
| Battle Axe | S_BA_Enviro (70 triangles, the sheen layer) | Part:S_BA_Child (181 triangles) | the axe itself |
| Short Sword, Long Sword, 2-Handed, Scimitar, Speed Scimitar | the `_Main` sword model | the `_Enviro` part | the blade's sheen layer; the sword was drawn |

Not affected: Troll Axe, club, Goblin Club, the three bows and the arrow quiver (their disc classes have no child part),
and the 19 weapons converted from their PC model (Atimar Blade, Magic / Death Mace, Runeblade, the magic swords, axes,
hammers, quivers, Energy Bow, slave pick): each keeps every PC triangle and texture (for example Death Mace 137 of 137
triangles, 1 texture). No flail, morning star or staff is placed in the campaign.

**Fix (converter):** `PlanOptions.companionFor` lets the caller name the reference a clone carries;
`ootfpipeline.ts` gives a swapped weapon the child part of the disc weapon whose model it shows
(`classCompanionRef`, `ootfps2model.ts`). All eight rows above now carry their part. Tests in
`test/ootfownmodel.test.ts`. Unrelated to the rope cause except in kind.

**Hit volume:** unchanged by the fix (it changes four bytes of the class record; model and bounds are the same). The
swapped mace hits with the disc handle model's own sphere chain, which includes the head (a sphere of radius 0.05 at
0.166 along the handle, reach 0.216 model units), against the PC mace's 0.164: the swapped mace has reached 0.05 units
(about 100 world units) further than the PC one since the model swap. Not measured in combat here.

## 6. The class of fault across the campaign

Audit of every placed PC class with a model that is name-mapped to a disc class with a model (`scratch/namemap.ts`;
145 classes, most of them invisible detector and sound boxes):

* **Restored by the rope rule:** the 7 trap ropes, Grimstone's crystal `Cable` (was pointing up), Wartok's
  `Static Rope` (was 2,590 units pointing down, is the PC mesh pointing up 4,147).
* **Helped by the runtime rule:** everything a rope carries: the Islands' `Cage` and three `swinging platform`s,
  Grimstone's two wrecking balls (`lairswing`), counterweights, pulley platform and crystal. Not looked at in game.
* **Deliberate disc counterparts, unchanged:** the creatures (Wartoks, orcs, giants, dragons, spider, Scavenger, Death
  Mage, Rynn, Arokh), Ballista and its base, potions, Barrel, Crate, Brick, Boulder, Prison Gate, the club and spears.
* **Name-mapped with a visibly different disc model, unchanged and not checked in game:** `Shield` (the Wartok shield
  is shown as the disc Shaman Shield, 1 object in Wartok Canyons), `Dragon Armor` (the PC rack model is shown as the
  disc dragon armour, 1 in Volcanic), and classes whose disc model lives in a container not read here (`Skeleton02`,
  `SusBridge01` / `03`, `Solid`, `Troll Crate`, `Switch`, `TallLadder`, `Trap Door`, `Prison Door`, `Sky`).
* No hanging lamp, chandelier, flag or drawbridge chain class is name-mapped; `Chain`, `Chain Small`, `Bridge Rope`,
  `ropes` and `Hanging Flame` are ordinary converted props.

## 7. Proof runs

Own play folder `build/review/ootf-axes/play` (copy of `baseline-play`: the owner's `drakan.ini`, HD textures, DLSS
quality, 3840 x 2160 borderless, `world_rate = uncapped`; `elf =` a private copy). "Before": the baseline executable
(`drakan_base.exe`) with the owner's installed pack. "After": the rebuilt executable with the rebuilt full 23-level
pack (`packs/fix1`). Differences from the owner's play in every run: `play_run.py` (audio off, not activated), a fresh
card with a direct start of the level (`DRAKAN_START_LEVEL`; the owner's saves cannot load on a rebuilt pack, as with
every pack rebuild), Rynn placed on the trap's walkway, god mode except in the damage runs. Other agents kept the GPU at
99 % throughout, so the frame rate was 2 to 80 fps and uneven; each sequence is consecutive rendered frames.

| What | Before | After |
| --- | --- | --- |
| Ruined Village corridor, 120 frames, looking along it | `runs/axeA-before` | `runs/axeA-after` |
| the same, looking up at the pivots | `runs/axeB-before` | `runs/axeB-after` |
| Wartok Canyons room, 20 frames before and 150 after the trigger | `runs/wartok2-before` | `runs/wartok2-after` |
| blade damage without god mode (Rynn steps into trap 1448) | `runs/axedmg2-before` | `runs/axedmg2-after` |
| mace: ground, pickup, modern inventory, idle, swing, native inventory | `runs/mace3-before` | `runs/mace3-after` |
| mace in the off hand (`dual_wield = 1`, Atimar's Blade right) | `runs/mace4-before` | `runs/mace4-after` |

Contact sheets (brightened copies for the dark cave; the run folders hold the frames as rendered): `evidence/`.

What the frames show (looked at frame by frame in the first 24 of each sequence and across the 120 frame sheets):

* **Ruined Village, before:** three to four blades of the corridor swing with nothing above them.
* **Ruined Village, after:** each blade hangs on a striped rope. In `axeA-after` the rope of the near blade (0.8 s)
  ends in the blade's top knob in every one of the first 24 frames, through more than one full swing, and the far
  blades' ropes likewise; in `axeB-after` the upper end stays on one fixed spot under the ceiling while the lower end
  follows the blade. `evidence/axeA_converter_fix_only_old_exe_bright.png` is the intermediate state (rope drawn, blade
  beside it) that the runtime rule removes.
* **Wartok Canyons, after:** the two blades hang on ropes before the trigger (`s00`) and swing on them after it; they
  swing towards the entrance where Rynn stands, so they leave the picture for half of each swing and the upper rope
  ends are never in view. The health orb flashes as they pass through her (god mode).
* **Damage:** without god mode, Rynn placed in the plane of blade 1448 is dead within 7 (before) and 13 (after) shots
  of the placement (`evidence/blade_damage_health_orb_before_top_after_bottom.png`); 710 units beside the plane she
  keeps full health for the whole run (`runs/axedmg2-after`).
* **Mace, before:** a handle with a knob, no head, on the ground, in the right hand (idle and swing), in the off hand,
  in the modern inventory (grid icon and detail picture) and in the native inventory (grid icon and preview).
* **Mace, after:** the spiked head is on the handle in all of those, through the whole swing.

The frame rate during the captures was 3 to 9 frames per second (five game instances and two other GPU jobs ran at the
same time, and every frame was written as a 3840 x 2160 picture), so "consecutive frames" are 0.1 to 0.3 s apart: 5 to
16 frames per rope cycle, not the dense sequence asked for. The RAM series of section 4 covers what the pictures
cannot: rope and blade angle agree to 0.0 degrees in all 45 dumps.

Tests as run: formats `npx vitest run` 57 files, 587 tests passed (before the commit; the 5 new tests are among them),
`tsc` clean, prettier clean on the changed files except `ootfpipeline.ts`, which already failed the check before this
change (other lines); `frame_cadence_test` passes (asserts on); `scripts/repository_guard.py` clean; clang-format 19
clean on the three C++ files. `python scripts/run_tests.py` and the full `runtime-tests` ctest were not run.

Needs: a regenerated pack and the rebuilt executable. The rope's texture is new in the pack's Mountain World Common
texture container, so the HD step has to be re-run to get an HD image for it (the runs here show the converted
texture, there is no HD image for it yet); the mace parts are disc models and already have theirs. Saves made on the
installed pack do not load on a rebuilt one, as with every pack rebuild.

## 8. Not verified, or inferred

* The PC game was not run: how it draws the rope is read from the PC data (the mesh, the placement) and from the PS2
  engine's rope code, which reads the same class fields. That the PC stretches the mesh the same way is inferred.
* Whether the PC blades swing as the fixed PS2 ones do (centred, +-43 degrees for Maximum Swing 0.375, a full cycle of
  twice the Swing Period) is not known; the amplitude and cycle are the PS2 engine's.
* The swing period was not measured free of the dumps' own hitches; that the rule leaves it alone at 30 fps and above
  follows from the code (the tick is only split, never changed, below 8 steps), not from a measurement.
* No sequence here is dense in time (3 to 9 fps). No run at a normal frame rate, none from a walked approach, none from
  the owner's save (it cannot load on the rebuilt pack), none with a hand on the controls.
* The damage runs hold Rynn in the blade's plane after a placement; a walked pass through a trap was not made, and the
  number of hit points per hit was not read. The first shots of both runs already show less than full health; why was
  not looked at.
* Wartok Canyons: the upper rope ends and the ceiling were never in view; the rope there is drawn `Starting Length`
  (5,120 units) long against a blade 4,566 below, so its end should sit inside the blade (from the code, not seen).
* Not looked at in game: Grimstone's crystal cable, Wartok's Static Rope, the Islands' cage and platforms, Grimstone's
  wrecking balls, and every swapped weapon other than the mace (hammer, axe and the five swords get their part by the
  same rule and show it in the pack audit only).
* Drakan 2's own ropes with the runtime rule: not run. The rule is PAL only; the USA executable keeps the native update.
* Sheathing or a weapon on Rynn's back: no such state was found (no sheath binding in the input settings; unequipping
  through the inventory removes the weapon from the hand) and none was shown; not investigated further. None of the four
  placed Mace objects is linked to a creature, so no enemy carries one by placement; drops were not traced.
* The mace's reach against the PC mace (section 5) is from the two models' sphere chains, not from combat.
* The 16 degree offsets came from long first ticks under load; how large they are in the owner's normal play was not
  measured (the console value, 2.3 degrees / period, is the floor).
* The hook itself landed in main as part of another agent's commit of the same file (`31a6faf7`); `fe65d9b9` adds the
  rule and its test.
