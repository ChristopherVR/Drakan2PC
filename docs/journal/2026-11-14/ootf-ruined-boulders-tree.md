# OotF Ruined Village: boulders that do nothing and the tree that will not fall (round 7)

Owner report (Order of the Flame campaign in the PS2 engine, Ruined Village): "After getting Heron's crystal and going
through the cave there is a place where you can roll down boulders. It does roll down but doesn't interact with the physics
properly to cause explosion for the explosive type barrels or even killing the npcs." and "There is also a place where you
need to chop down a tree to let it fall over for you to walk over it, doesn't seem to work." Fixed in the converter (formats
`dd830b1`); the runtime is unchanged. Scratch, runs and evidence are under `build/review/rv-physics/` (not tracked); nothing
from either game is quoted here.

## 1. The place (PC data, ids of `Ruined Village.lvl`)

Past the cave from the tomb of Heron's Crystal the path comes out on a ledge east of the village.

| Object | PC class (engine) | What it is |
| --- | --- | --- |
| 366, 2605 | Boulder (16 Rigid Body), Push Mode Roll, not waiting | on the slope at 329041, 24238, 200097 and 329830, 24196, 200886, about 4,600 units above the orc camp |
| 247, 248, 2829 | Barrel (explode) (17 Ground Object), health 5, explosion generator, "Message To Send" Blow Up | at 333260 to 333750, 19583, 203131 to 203707 |
| 236, 237 | Orc Plate (yellow) (49), hidden until Rynn crosses detector 2606 or 2057 | next to the barrels |
| 2603 / 2604 | detector (Rynn on foot) and Kill Box (1000 hp/s for 4 s) | a trap over the camp; not part of the boulders |
| 1106 | "push tree" (17), Push Over Mode 5, Fall When Dead on, health 0, links Timer 2624 and Triggered Sound 2455 | on the ledge at 321687, 24172, 197611, a 5,116 unit trunk at the edge of a 4,000 unit gap |
| 1105 | "pushtreebase" (17) | the roots and stump |
| 2624 | Timer, 1.65 s, then Gib to orc 2612 | the orc the tree lands on |

The boulder class carries "Damage (per lu/s)" 50 and "Damage Player" on (the two waiting boulders 2780 / 2790 further
east are the same kind and not part of this report). The tree's fall direction is not stored: the terrain says it, the
ledge continues 4,500 units to the west of the tree (world -x) at the tree's foot height with a drop to 18,000 between.

## 2. Boulders

### Why nothing happened

The PS2 Rigid Body (class 0x10010) is the PC class field for field plus "Detect Objects?" and "Shadow?". Its hit handler
(0x45a148, called from the body's event dispatcher 0x45eb10) sends the damage message 0x10d7 with
`Damage (per lu/s) x speed / 2048` to what it runs into, when it moves toward it, unless that is another Rigid Body. The
Ground Object handler (0x213a98, event 0x20 at 0x215030) takes 0x10d7 off its health and at 0 destroys the object
(0x214488), which spawns the explosion and sends "Message To Send" along its links. So the engine has the whole chain.

The converter maps a PC class onto a PS2 class of the same name and engine id when there is one. PC "Boulder" therefore
became the disc's RESOURES "Boulder" (6924), whose defaults are Damage 1, Damage Player off, Friction 2. The objects keep
only the fields they set themselves (Friction, Health), so a boulder rolling at 2 to 4 lu/s sent 1 to 2 damage: less than a
barrel's 5. Classes with no same-named PS2 class are cloned with the PC defaults merged, which is why "Boulder for orc"
(2780) kept its 100.

### Fix

`keepsPcDefaults` (ootfclass.ts) already gives objects of name-mapped logic classes their PC class defaults where the PS2
class defaults differ; Rigid Bodies (16) are now included. In the campaign this changes the name-mapped boulders only:
Ruined Village 366, 2605, 2790 (Damage 50, Damage Player on, Friction 0.15 / 0.2 instead of 2), Alwarren 4 and Islands
Night 1 (Damage 5, Damage Player on, Health 50 instead of immortal), Wartok Canyons 1 (Friction 0.2). The converter log
lists them ("PC class defaults kept on ...").

### What the engine does with it (found by playing)

* Barrels: a boulder rolling into them sets them off (barrels 3 to 0 in run `after1`).
* Orcs: the orcs are objects of kind 7 and take the boulder's damage directly; in run `n3` an orc went from 40 to -39 hit
  points with all three barrels still standing. When the barrels go up as well, both orcs die.
* Rynn: kind 3. The hit handler leaves any kind-3 partner but the dragon (class 0x1001c) before the damage block
  (0x45a1b8 to 0x45a23c), so "Damage Player" on does not make the hit handler hurt Rynn. She can still die at a boulder
  (section 4, last part), on the old pack as on the new one.
* The boulders creep: the PC friction 0.15 (an object parameter in Ruined Village, so the dist pack had it too) lets them
  roll a few hundred units from their placed spots onto the flat above the slope after a load.

## 3. The tree

### Why it could not be felled

The PS2 Ground Object (0x10011) has 16 fields that end at "Smoke Generator": no "Push Over Mode", "Fall When Dead" or
"Message To Send When Pushed". The converted tree was a Ground Object with the PC health 0, which the handler treats as not
damageable (0x215030 only counts a hit while health is above 0), and nothing could make it fall. What PC mode 5 means was not
established (OpenDrakan's Building class reads the field but implements no push-over).

The PS2 engine fells trees with a Mover (35): the disc's "Tree Mechanism" (LEVEL1ON, placed in Level 1NA and started by a
timer) is Type 1 with a rotation axis and a distance in turns. A Mover does not react to a blow (0x393988 handles only On /
Off), a Ground Object with health does.

### Fix (formats `src/ootftree.ts`, stage "falling trees" and the puzzles stage of `ootfpipeline.ts`)

For a PC Ground Object with Push Over Mode 5 and Fall When Dead (the campaign has one: Ruined Village's tree; Alwarren's
bricks with mode 5 do not fall when dead, the Islands and Wartok pillars use other modes):

* the fall: of the four horizontal model axes, the one where ground at the foot's height lies under the last 30 % of the
  trunk with none at half its length (`bridgeAxis`). For the tree: model -z, world -x, toward the far ledge;
* the standing tree stays the converted Ground Object with "Initial Health" 1, "Message To Send" the PC "Message To Send
  When Pushed" (On) and no explosion, smoke or rubble: one blow removes it and sends On along its links;
* a Mover class of the tree's own model (planned with the other classes, so the model gets its display list and bounds),
  Type 1, a quarter turn about model x (-0.25), 1.6 s with 1.4 s of speeding up, Collide with player on, its disc sounds
  off. Its object is placed hidden on the tree's spot;
* a Visibility Toggler of the level's own converted class (the one that shows the tomb's hidden stair Movers) shows the
  Mover on the same On. The tree's links are the PC Timer and sound, then the toggler, then the Mover.

Tried and dropped on the way: a visible Mover on the same spot takes the blow meant for the tree (run `n3`, six strikes,
the tree stands); a hidden Mover turns on On but stays unseen and lets Rynn through (run `t1`). The rotation sense was read
off the Mover's matrix in game (`t1`, `n4`): the model's up axis ends at world -x.

### What it does in game

One strike with Atimar removes the standing tree; the Mover appears and swings down over the gap in about 1.6 s and lies
from the stump to the far ledge. Rynn stands and walks on it over the gap (runs `n5`, `n7`, `after1`). The trunk is the PC
model's collision boxes, so its top is about 470 units above the ledge at the stump: Rynn gets onto it with a running jump,
as on the PC; walking into the butt end stops her. The PC trunk curves, its upper third lies up to 430 units south of the
foot's line; a straight walk along the foot's line drops off beside the crown, following the trunk works.

## 4. Played

Own play folder `build/review/rv-physics/play` (a copy of `baseline-play`: the owner's `drakan.ini` and `drakan_input.ini`,
HD textures, DLSS; exe of current main from `build/final-game/bin`, sha256 `a3b2703c...`; private ELF copy). Runs by
`scripts/harness/live_drive.py` through `tools/push.py`. Differences from the owner's settings: a 1280 x 960 window instead
of borderless full screen, `game = d1`, sound off, a fresh memory card.

Both evidence runs use the same plan (`tools/ng_tree.py` with `tools/ev.py`): New Game on an empty card, the Intro and the
Ruined Village opening skipped by the harness taps, Tab and Enter to equip Atimar, F5; one placement into detector 2606
(the orcs appear), F5; one placement 1,200 units behind boulder 366 on the line to the barrels; W (real input) in bursts
toward the boulder until it goes over the edge, then turn to the camp and watch; one placement 800 units east of the tree,
left mouse (the owner's attack binding) until the tree is gone, F5; one placement 1,300 units east of the tree; W with
the mouse steering along the trunk and Space once (a running jump) until Rynn is on the far ledge.

| Run | Pack | Boulder | Tree |
| --- | --- | --- | --- |
| `before3` | the dist pack of 14 November morning (`play/mods`, sha256 `3b1f6b95...`) | rolls down through the camp past the barrels: 3 barrels stay, both orcs stay at 40 hit points (one at 37) and fight Rynn | six sword strikes, the tree stands; no way over the gap |
| `after1` | full 23-level pack of formats `dd830b1` (`packs/final`) | barrels 3, 2, 0 within half a second as the boulder reaches them; debris, Rynn hurt by the blast (the orcs were still hidden: this run skips the detector) | the first strike fells it; on the first try a running jump onto the butt and W along the trunk take Rynn to the far ledge |

Two earlier runs on packs with the same boulder code show the creatures: `n4` (pack `p4`, after the detector, W into
boulder 366): barrels 3, 2, 0 and both orcs from 40 to -56 and -1 (`evidence/after_boulder_n4.jpg`); `n3` (pack `p3`):
an orc from 40 to -39 hit points with all three barrels still standing, the boulder alone.

Contact sheets: `evidence/before_boulder.jpg`, `before_tree.jpg`, `after_boulder.jpg`, `after_boulder_n4.jpg`,
`after_tree.jpg`; the single frames are in the run folders.

### Rynn dies when she walks into a boulder from below (not fixed, seen before the change too)

Nine times Rynn died the moment she walked into a boulder: whenever her origin was below the boulder's centre
(30 to 50 units below in `after1`'s first four tries, 3 below in `recon1` on the dist pack, where the boulder has the disc
class's Damage 1 and Damage Player off). Every push from 150 units or more above the centre worked (`recon2`, `n4`,
`before3`, `after1` fifth try). It is not the hit handler's damage (it skips Rynn, section 2) and it happens on the old
pack, so it is the engine's own rule for a body that is on top of something; the first part of the body's contact query
(0x459a30, the partner at +0x194) calls the damage functions 0x4918d0 / 0x491e28 and is the likely place. On the slope as
the level leaves the boulders Rynn stands above them; on flat ground next to one that has crept (section 2) she may not.
Not traced further; it is a candidate for its own look.

The coordinator's save made just after taking Heron's Crystal (`build/review/rv-plates/saves/after-crystal-dist`, dist pack)
was loaded for two earlier before runs (`before1`, `before2`): with the sword it shows the same defects, but Rynn has
almost no health in that save, and the placements' short drops killed her twice, so the evidence runs start from New Game.

## 5. Not verified

* The walk from the tomb through the cave to the slope was not walked; each part starts from a placement.
* Owner's full-screen settings, sound (the boulders' roll sounds stay off: the PS2 class has none), the German tree.
* The orc the tree is meant to land on (Timer 2624 gibs it on the PC): not looked at.
* Alwarren's and Islands Night's boulders with their new PC damage and health 50, and Wartok's with PC friction: not played.
* A save made on the old pack keeps the old objects: the owner needs the rebuilt pack and a new campaign game.
* Drakan 2 is not reached by the change (converter only); not played.
