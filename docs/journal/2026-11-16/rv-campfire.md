# OotF Ruined Village: the empty orc campfire and the flame beside the push tree

Owner reports (Order of the Flame campaign, Ruined Village): "textures missing ... just after getting Heron's crystal and
going up in the elevator. There is a spot where there is fire but the roast pit, the log laying where the axe is and the
three wartoks / grunts are not there", and "The tree that we can cut that kills the wartok / grunt there: there is a weird
flame next to it and the flame even changes once the tree is destroyed". Both are converter faults, fixed in formats
`09f6550` (`src/ootfzonecull.ts`, `src/ootftree.ts`, one block in `src/ootfpipeline.ts`); the runtime is unchanged.
Scratch, packs, runs and frames: `build/review/rv-campfire/` (not tracked; private play folder `play` with its own ELF
copy, HD textures copied in). Nothing from either game is quoted here beyond object, class and field names.

## 1. The camp (PC `Ruined Village.lvl` ids)

| Object | PC class (engine) | PC visible flag |
| --- | --- | --- |
| 700 Fire Pit, 698 Log, 2437 Spit Holder, 701 Meat, 819 / 234 / 699 / 235 crates and barrels, 813 / 814 torch | 17, 13, 20 | clear (hidden) |
| 702 Burning Flame (Wide), 1754 FireSmall_1 (sound), 2825 Battle Axe | 3035, sound, weapon | set |
| 226, 2593, 2594 Orc Leather (purple), 818 Orc Plate (yellow) | 49 | clear |
| 2684 Visibility Toggler, Trigger Mode 1 (Depends on Message), Show 1, Hide 0 (class default) | 121 | links the 13 hidden props and 4 visible far props (archway 44, 229, 2501, 2502) |
| 2685 / 2683 "Creature" detectors (61), boxes 3 x 1 x 1 cells on the pass north of the camp | 61 | 2685 sends On, 2683 Off, to 2684 |
| 2716 detector, on the pass west of the camp | 61 | sends On to the four grunts, orc 109 and 2684 |
| 2597 detector south of the camp | 61 | sends Move to WayPoint (7) to the grunts |

So by the PC data the camp is zone culling: the props start hidden, 2685 or 2716 shows them, 2683 hides them again when
Rynn walks back north, and only 2716 shows the grunts. The fire and the axe are not on the toggler, so they are there from
the start: exactly "fire, the axe, no log, no pit, no grunts". The converted records (`ootf-levels.zip` of 15 November
evening and the morning backup `pack-1115/ootf-levels.before.zip`) are identical for every object involved: **not a
regression** of the 15 / 16 November work (models, far land, tree grounding), not models, LOD or the model cache (the
objects draw as soon as they are shown, with and without HD textures), not save / load (shown props and grunts survive a
quicksave / quickload, run `save1`).

What was established in game (dist exe `bcb5a431...`, dist pack `0214d9e7...`, `tools/wdrive.py` placements and walks):

* Level load, view of the camp: fire and axe only (`ev/before_camp_load.png`, run `gate2`).
* Walking through 2716 from the west: props and grunts appear (`base1`).
* Walking from the ledge south through 2683 then 2685: props appear, **no grunts** (`gate2`, `ev/ev_gate2_post.jpg`): only
  2716 wakes them, as authored.
* Box orientation (`orient1`): a box with rot 270 and scale 3 x 1 x 1 is long along world x in the PS2 engine (+2,000 on x
  still fires, +2,500 on z does not), i.e. a gate across the north-south pass as the PC level means it.
* The ground under the south half of 2685 (about 23,020 to 23,060) lies below the box floor (23,266), and the west half of
  2683 too; the west pass box 2716 only covers the valley floor (x 315,000 to 318,500), its ends are on slopes above it.
  Detector 2715 at the lift exit wakes orcs 812 and 108 on the way west of the camp, so the lift route arrives from the west.
* The owner's exact route was **not reproduced**: a flood fill over the converted heights finds paths into the camp valley
  that cross no Show box at the box's height, but the one scripted walk along such a path (south of 2716) stuck at a river
  bank (`south1`). The before frames are therefore the load state and `gate2`.

### Fix: zone culling togglers (`ootfzonecull.ts`)

The converted game draws far and does not need the PC culling. A PC Visibility Toggler counts as zone culling when it is in
Depends on Message mode, has at least 4 links, every link source is a detector or a timer, at least one detector sends its
Show and one its Hide message, and every link that starts hidden is static scenery (engine 17, 13 or 20). Then the hidden
links start visible, the Hide sources no longer link to the toggler, and the hidden creatures a Show detector of that
toggler wakes are shown too when they stand within 6,144 units (x / z) of one of its hidden links. Log line per toggler.

Campaign census of the 74 PC togglers (`tools/cullsurvey.ts`, `tools/zonecensus.ts`): 8 match. Ruined Village 2196, 2199,
2238, 2337, 2684, 2731, Alwarren 2546 (plants and falls, all visible: only its hides are cut) and Wartok Canyons 2854 (the
spider pit props, all visible: only its hides are cut). Grotto 808 does not (a hidden Boulder, engine 16); Volcano 2322 does
not (one link); puzzle togglers (one source, Movers, plates, locks, waypoints) do not. Single-level conversion of Ruined Village: "zone culling: 6 toggler(s), 53 object(s) shown":
2684 shows 13 props and 4 grunts (orc 109, 13,000 units away, still waits for 2716), 2337 shows the cemetery's 36 graves
and crypt pieces (on the dist pack the crypt entrance is a see-through hole until its detector 2335 fires,
`ev/ev_crypt_dist.jpg`; now it stands from the start, `ev/ev_crypt_fix.jpg`); the others were visible from the start and
are no longer hidden when Rynn leaves (village interiors, trees, houses).

Prototype first (`lvlpatch` pack `proto1`): with the props and grunts shown at load and Rynn away for 150 s, the grunts
were still at the fire (`ev/ev_proto1.jpg`), so waking them early does not send them off their waypoints.

## 2. The flame beside the push tree

Bisection with probe packs (objects dropped or hidden in the converted LVL, `tools/lvlpatch.ts`): the flat square of flame
about 1,000 units east of the push tree stays with the tree, its stump, the Triggered Sound 2455, the boulders, river
currents and detectors removed (`ev/ev_tv.jpg`), and goes only with the hidden falling-tree **Mover** (3374 in the campaign
pack; `ev/ev_bis2.jpg`). The Mover shown with the tree removed still has it; the tree hidden without the Mover has not
(`ev/ev_m12.jpg`). Cause: `fallingTreeMoverClass` names 18 Mover fields; the planner cloned the PS2 Mover closest to them,
RESOURES "!! Flaming Bolt  test!!", and kept its other defaults, among them "Special Effect" 14891 (a flame class). The
Mover carries that effect whether hidden or not and turns it with the trunk when it falls, hence "changes once the tree is
destroyed". Fix: `placeFallingTrees` sets "Special Effect" 0 on every tree Mover (and follower Mover). Not a regression of
15 / 16 November either (the tree Mover exists since 14 November).

## 3. Proof (converter-built pack)

`tools/conv.sh fix1`: Ruined Village alone, dist converter flags (wide chunks, subtitles), working tree of formats with this
change (and other sessions' uncommitted formats work), pack `packs/fix1/ootf-levels.zip` (`00a8b9f4...`), same dist exe.
Run `fix1`: the camp at level load has fire pit, spit, log, crates, barrels and the grunts around the fire
(`ev/after_camp_load.png`); the push tree has no flame (`ev/after_tree.png`). Run `fall_fix2` (the same pack with a test
link from detector 2622 to the tree's toggler and Mover, to fell it without a weapon): the trunk lies over the gap, no flame
(`ev/after_tree_fallen.png`; the standing tree also stays there because the test link does not destroy it).

The owner needs the rebuilt campaign pack (`extras.py ootf` / the converter run of `ootf-opening-scenes.md`, then the HD step
and the incremental build); the exe does not change. Saves made on the old pack keep the old objects of a level already
entered; Ruined Village entered fresh (or a save made before it) gets the camp.

## 4. Not verified

* The owner's own route from the lift to the camp (see section 1); the effect of a missed Show box is shown only by the
  load-state view.
* Felling the tree with Atimar on the new pack (the felled state came from a test link); the grunt the tree lands on.
* The other matched togglers in game beyond the cemetery view: Ruined Village 2196 / 2199 / 2238 / 2731, Alwarren 2546 and
  Wartok Canyons 2854 now never hide (only Ruined Village was converted and played here).
* The full 23-level pack, the German tree, sound.
