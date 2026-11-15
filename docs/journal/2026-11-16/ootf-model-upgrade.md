# Order of the Flame: a model upgrade for the characters that keep their PC model, and a moving jaw (2026-11-16)

Owner request: the 1999 PC characters look dated next to the Drakan 2 models; Delon in particular is low-poly and his
mouth does not move when he speaks. Scratch, tools, packs, runs and frames are under `build/review/ootf-models2/` (not
tracked). Nothing from either game is quoted here, only model, class and field names and counts.

## Summary

* New convert-time stage (formats `src/ootfskinupgrade.ts`, hook in `OotfAssetBuilder.addModel`): the PC human models of
  `DEFAULT_SKIN_UPGRADES` (24 rows; 18 of them are used by the 23 converted levels) get one interpolating curved subdivision step (526 to 2,104 triangles for Delon, about
  the 1,900 to 2,300 of the disc's humans at LOD 0) with their skin weights carried over; Delon and Julian also get a
  synthetic jaw joint. The other scene speakers' jaws are added on request (`DRAKAN_MODEL_UPGRADE=<regex of PC model
  names>`, the rest of the default stays): their placement was not looked at in game.
* Converted scenes start a jaw clip, made from the line's own samples, on the jaw channel with each line a speaker with
  such a jaw says (`SequenceMaps.jawForActor`, formats `src/ootfseq.ts`). This is not the engine's lip sync (see the jaw
  section): the scene Delon is a class 17 prop that has no `Jaw Channel`.
* Seen in game in the Ruined Village opening: Delon's head and body are round where they were faceted, no tears, and his
  jaw opens and closes while he speaks (first proved with an exaggerated test clip).
* Switch: `DRAKAN_MODEL_UPGRADE=off` (or `--model-upgrade 0`, or `OotfPipelineOptions.modelUpgrade: false`) converts
  every character as before.
* This is not the engine's lip sync: the lip-sync bit (0x20) and the 0x0305 binding stay player-only; the converter
  writes the jaw motion as clips.

## 1. Inventory

Characters a converted level shows with their PC model (no Drakan 2 counterpart; Rynn, Arokh, Wartoks, orcs, the spider
and the Scavenger are already the disc's, `ootfps2model.ts`). Scene lines = sound actions with the PC speech bit 0x10 in
the level's scenes (`tools/speakers.ts`); triangles are LOD 0 (`tools/skinstats.ts`, `tools/dryrun.ts`).

| Rank | PC model | Who / where | Scene lines | Triangles PC -> upgraded | Jaw |
| --- | --- | --- | --- | --- | --- |
| 1 | `Delon(h)` | Delon, Ruined Village 1.1 | 2 | 526 -> 2,104 | yes |
| 2 | `julian (h)` | Julian, Ruined Village 1.3 | 8 | 456 -> 1,824 | yes |
| 3 | `farmer` | the magic farmer, Wartok Canyons | 19 | 504 -> 2,016 | on request (35 vertices) |
| 4 | `warrior2` | the warrior, Wartok Canyons | 4 | 500 -> 2,000 | on request (32) |
| 5 | `tuiri (h)`, `tuiri(h)` (the second unused) | Tuiri, Alwarren | 12 | 493 / 547 -> 1,972 / 2,188 | on request (22) |
| 6 | `Rimril` | Rimril, Islands, Islands Night | 20 | 503 -> 2,012 | on request (8) |
| 7 | `hider` | the hiding slave, Grimstone Mines | 7 | 496 -> 1,984 | on request (18) |
| 8 | `the queen(shaun)` | Shillah, Succubus Lair, Volcanic | 13 | 433 -> 1,732 | on request (24) |
| 9 | `Succubus` | the succubi, Islands, Volcanic | about 88 | 506 -> 2,024 | on request (15) |
| 10 | `mvillager`, `mvillage2`, `fvillager`, `fvillage2` | villagers, Ruined Village scenes | 0 | 496 / 489 -> 1,984 / 1,956 | no |
| 11 | `slaveM (h)`, `slaveM 02 (h)`, `slaveM 03 (h)`, `slaveM (brown shirt ...` (unused) | mine slaves | 0 | 496 -> 1,984 | no |
| 12 | `VillageDude`, `VillageChick` | Wartok Canyons villagers (no converted class uses them) | 0 | 310 -> 1,240 | no |
| 13 | `rackboy` | Grimstone Mines | 5 | 496 -> 1,984 | no (see below) |
| 14 | `wounded wizard`, `rackvictim`, `TGyroVictim01` | props (only the wizard is used) | 0 | 503 / 290 / 290 -> x4 | no |
| - | `Dr Evil` (`drevil(h)`, has jaw joints), death mages, war giants, goblin, Ebon / Crimson Knight, enemy dragons, harpy-like fliers, owl, ticks, range beasts | creatures | few | 181 to 557 | not in the table |

Comparable disc models (NPCS.MOD, LOD 0): villagers `MediumGuy` 2,048, `SkinnyGuy01` 2,046, `BigBlueGuy` 1,928, `ThinChick`
2,178, `Blacksmith` 984, `Hermit` 2,991, `Alchemist` 3,488; 57 to 65 joints, a `Jaw` joint (node 6 or 9) and up to 4
influences per vertex. The PC humans have 30 to 43 joints, up to 2 influences and no jaw.

Props and buildings: the frequently seen ones were handled in the 14 and 15 November rounds (Drakan 2 models for barrels,
crates, trees, torches, crystals, skeletons, bodies and bridges; convert-time remakes for palms, pillars and corridor
pieces, `ootfps2model.ts` props rows). This round adds nothing there.

## 2. Approach

* **In the TS converter, not Blender.** The stage runs where the conversion already runs (also the editor in the browser),
  from the player's own data, with no extra install; the Loop / PN code of the prop remakes was the starting point.
* **Interpolating PN step, not Loop.** Loop moves the original vertices and shrinks a 500-triangle body (thin fingers,
  noses, ears). `subdivideSkin` keeps every original vertex and puts each new edge point on the cubic PN curve of the
  smooth normals (sign independent, capped at 20 % of the edge length off the straight midpoint). Edges whose faces meet
  at more than 55 degrees, open borders and every edge of an alpha card (hair fringes, wings: the texture list entries
  whose converted texture has alpha) split straight, so nothing cracks and cards stay flat. UVs split linearly. A new
  vertex takes the mean of its edge's weights, capped at 4 joints (the disc maximum) and renormalised.
* **One step.** It lands at the disc's LOD 0 density. The converter writes one LOD (the second-LOD tail of 0x0211 is not
  decoded), so crowds pay full detail at any distance; vertices stay under 4,000 per model, and a mesh whose display list
  does not build falls back to the PC mesh (`noUpgrade`, a warning).
* **Swap where Drakan 2 has the creature** (unchanged); no new body swaps: no disc model is Delon, Julian, Tuiri or the
  farmer, and a different rig would need every clip retargeted.

## 3. The jaw

### Can the engine's lip sync drive it? Not for these actors

* The scene sound action with bit 0x20 sends message 0x31 to the actor (`0x1a4c50`); a class with a `Jaw Channel` binds the
  0x0305 track. The jaw tick `0x404e08` is called only from `0x4051e0`, and that from 7 call sites in class code
  (`0x271af4`, `0x271bf8`, `0x31087c`, `0x3117cc`, `0x3f408c`, `0x3f5bf0`, `0x483ec0`); the scene Delon, Julian and the
  other speakers are class 17 generic props, whose class has no `Jaw Channel` field on the disc. Making them talk through
  the engine would mean another engine class (the class 157 talkers) for a scene prop, with its own AI. Not done.
* **What the converter does instead:** scenes already play clips on skeleton channels (Delon's head clips on channel 9; on the disc 129 of 1,539 scene
  animation actions play on a channel other than 0, with flags 0x3f). A synthetic jaw joint gets its own
  channel, and each speech action of such an actor is followed, at the same time, by an animation action on that channel
  that plays a clip of the line's length: every joint at rest, the jaw opened by the line's mouth openings (the peak of the
  first half of each 1/25 s frame, as the disc's 0x0305 tracks, scaled so the 90th percentile opens 80 % of 14 degrees,
  quiet frames shut), once, closing at the end. The player keeps the engine's lip sync.
* Proven in game with a test pack whose jaw clips alternate 24 to 36 degrees open and shut every 0.2 s (`jawtest`,
  `jawtest2`): Delon's chin drops and returns with the clip in scene 1.1 (`runs/jawtest2/mouth.jpg`). A class 17 prop does
  layer a channel clip.

### The joint (`addJaw`)

* Appended as the last joint (a child of `head`, the head's rotation, origin at the hinge), its node `jaw` last in the node
  list and a new last channel; every converted clip of the model gets a resting jaw track (`withJawTrack`), since clips
  pair with models by joint count.
* Chosen from the bind pose, no index lists: forward is where the feet point (foot vertices against their ankle joints);
  the nose tip is the front-most head vertex in the middle band, the chin the lowest vertex of the front quarter; the
  mouth line lies 40 % of the way from the nose tip to the chin; the hinge 45 % of the head depth behind the front, a
  tenth of the head height above the mouth line. Head vertices below the mouth line and ahead of the hinge move to the
  jaw, fading over a band so the cheeks stretch. Delon: 24 vertices of the subdivided head.
* First try put the mouth line at a fixed 62 % of the head height: in game the nose's underside opened with the jaw
  (`runs/jawtest`); fixed by the nose / chin rule (`runs/jawtest2`).
* The mouth is painted on: the jaw stretches the lower lip and chin, there is no mouth interior. The PC humans have no lip
  geometry to split.

## 4. In game

Play folder `build/review/ootf-models2/play` (a copy of `dist/DrakanPC` with its own `elf =` copy; `mods` holds the pack
under test and the character-motion pack). Packs from `tools/conv.sh` (Ruined Village only, the dist converter flags,
the formats tree with other sessions' uncommitted work): `off5` / `off6` (`DRAKAN_MODEL_UPGRADE=off`), `upg2` (Delon),
`jawtest`, `jawtest2` (test clips, not shipped), `fin3` (the default table), `all2` (every row with its jaw). Driver
`tools/opening.py` (New Game, the scenes play with no input, a shot every second and every 0.1 s around chosen scene
times; the watchdog line of `stdout.log` marks a frozen guest).

### A freeze, and its cause

With the jaw joint written as `firstChild = nextSibling = -1`, the opening froze: frame counter stopped, sound running,
`[watchdog]` after 8 s. With Julian's jaw in the pack it froze in 6 of 8 runs (full table `upg3_a`, `upg3_b`, `upg5`;
Delon + Julian `fin1` and 2 of 3 repeats; Delon + Julian `upg4j` ran through), at scene times 0 to 29 s; the off pack
ran 4 of 4, Delon alone (also -1) 6 of 6. The live thread sat in `0x104678`, a recursive walk over the joint tree that
samples each joint's keys. The data ends child and sibling lists with 0 (no joint links to the root) and never uses -1;
that the walk follows -1 into memory before the joint table is inferred (its handling of -1 was not read in the
decompile), but written with 0 (`addJaw`) the freeze is gone: 0 of 8 runs (`fin2` and `all2`, every row with its jaw,
3 each; `full_fin3`; the 23-level pack through the Intro into Ruined Village).

### Frames

Runs `full_fin3` (default table) and `full_off6` (upgrade off), the whole opening (scenes 1.1, 1.2a, 1.3, 230 s), no
watchdog in either. Comparison sheets in `build/review/ootf-models2/evidence/` (upgraded top or first, off bottom or
second):

* `delon_face_fin3_top_off6_bottom.jpg` (1.1 at 7.4 to 9.6 s, his first line): the hexagonal outline of his hair and
  head is round, the chin and cheeks smooth; in the upgraded row the lower lip drops below the painted mouth on the
  loud syllables, in the off row the face never changes.
* `julian_mouth_fin3_top_off6_bottom.jpg` (1.3 at 21 to 24.5 s, the "Despicable Wartoks" close-up): with the upgrade
  his mouth opens under the moustache on the line's peaks (first three frames clearly), off it stays shut.
* `rv_11_full_*.jpg`, `rv_12a_full_*.jpg` (same scene times): Delon, the villagers and the mine slaves are rounder
  (shoulders, knees, heads); no torn or stretched polygons in the frames looked at (about 60, as contact sheets; not every frame of the runs).
* `jawtest2_delon_mouth.jpg`: the exaggerated test clip, open and shut every 0.2 s.

Shipped jaw clips read back from the pack (`tools/packmodel.ts`): Delon's two lines 4.0 and 8.2 s, Julian's eight 2.2
to 5.8 s, 39 to 144 jaw keys each, peak 14 degrees (7.5 on Julian's quietest line), every clip with the model's own
track count (38, 31) and channel count. The bounding spheres of both models are identical to the off pack's.

### Frame rate

Flips per second while each scene runs (`[frame-uncapped]` lines of `stdout.log`, uncapped, no screenshots, two runs
each, `runs/fps_*`):

| Pack | 1.1 | 1.2a (village, crowd) | 1.3 |
| --- | --- | --- | --- |
| `fin3` (upgrade) | 70.3 / 86.1 | 61.5 / 72.7 | 121.3 / 106.6 |
| `off6` | 100.3 / 71.1 | 94.6 / 66.0 | 77.6 / 81.9 |

Runs of one pack differ by up to 30 %, so this shows no clear cost; 1.2a, where most upgraded characters are in view,
is the scene to watch (means 67 against 80). Every run stayed above 60 flips per second.

## Limits

* Class 17 scene speakers move their jaw only in converted scenes; outside scenes (barks, gameplay) the mouth stays still.
  Converted characters with an engine class that has a `Jaw Channel` would need it rewritten to the new joint before the
  lip-sync bit is set (template inheritance, as with the trog claw); none of the table is such a class today.
* No LOD: the upgraded model draws at full detail at every distance (frame rate above).
* The scene orcs (Drakan 2 orc rig, `lowjaw` joint, class 17) and the Wartok scene speakers do not get jaw clips (their
  clips come from the disc rig, `tockOf`).
* Jaws of the other speakers are built on request only: their placement was not looked at in game. `rackboy` and the death
  mages are left without one: the geometric rule placed their face on the other side (lying or bent bind poses).
* Outside Ruined Village the upgraded characters were only loaded, not looked at: direct starts of Wartok Canyons,
  Grimstone Mines, Islands, Alwarren and Succubus Lair on the 23-level private pack (`packs/full`, 18 models upgraded, no
  upgrade warning) ran 60 s each with no watchdog (`runs/lvl_03` ... `lvl_10`). Volcanic and Islands Night were not
  started.
* Graded alpha of hair fringes stays cut at 50 % (skinned textures, since the Intro owl).

## Files

* formats: `src/ootfskinupgrade.ts` (new), `src/ootfassets.ts` (hook, `addClip`, `jawRigOf`, report `upgraded`),
  `src/ootfseq.ts` (`jawForActor`), `src/ootfpipeline.ts` (`modelUpgrade`, jaw clips per line), tests
  `test/ootfskinupgrade.test.ts` (new, synthetic icosahedron and stick figure) and `test/ootfseq.test.ts`.
* main: `editor/scripts/ootf-convert.ts` (`--model-upgrade`, report line), this note, `docs/status/ootf-port.md`.
