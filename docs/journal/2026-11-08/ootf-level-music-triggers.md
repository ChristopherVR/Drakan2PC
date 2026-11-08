# Order of the Flame in-level music changes (2026-11-08)

Follow-up of [ootf-main-music.md](ootf-main-music.md): the PC Music Trigger objects were converted but never fired, so the
music never changed inside a level. They now become Trigger Detectors that switch one Triggered Music Loop per PC music id.

## PC: how a Music Trigger works

Read from the user's install (`build/ootf/cab_disc`, counts and ids only):

* Engine class 129, called "Music Detector" in `Dragon.rfl`'s string table. 37 objects in the campaign levels (classes
  "Music Trigger", "TempleMusic", "SpiderCaveMusic", "LairMusic", "FlameFortress", "Demo Tavern Music", "Grottoes Music 1").
  Fields `Music 1 ID (0=none)`, `Music 2 ID (0=none)`, a fade out / fade in time per slot (f32 s) and `Initial State`.
  No object links to a trigger and triggers link to nothing: the engine watches them itself.
* Geometry: the class model is a detector box of `Common/System/System.mod` (3725 / 3726 / 3727, vertices +-0.5, the same
  unit cube as the Ambient Transition's 3724 of `FX.mod`); the object's scale is the size in model units (2048 world units),
  thin along local z (0.2 to 1). It is a two-sided plane, like the Ambient Transition.
* Which side is which: Wartok's eight cave triggers each sit next to an Ambient Transition (PC class 131, Front / Back
  Sound). With the same rotation the cave music (`Music 1`: 2 or 7 or 5) is on the transition's front sound (159) side and
  Wartok's level music (`Music 2` = 1, its `Initial Music ID`) on the back sound (153) side; where the transition is turned
  180 degrees, the sounds swap sides with it (2134 / 1872, 2136 / 1873, 2665 / 1865). Islands' fortress pair (6021 / 6022,
  6023 / 5232) agrees. So `Music 1` = front, `Music 2` = back, in the same frame the converter already uses for the
  Ambient Transitions.
* 0 means stop, not "no change": Succubus Lair 3625 overrides the class defaults 2 / 10 with 0 / 0 at a doorway, and
  Alwarren (no start music) has three 0 / x triggers that start and stop its music.
* Per level (front / back): Ruined Village 8/2 (tavern), 3/0, 0/8, 0/3, 0/5 (temple); Wartok 2/1, 2/1, 7/1, 7/1, 6/6, 5/1,
  2/0, 6/0; Mines 9/0, 8/0; Grotto 6/1; Islands 5/3, 3/3 (twice, crossed), 9/3, 3/9, 0/3; Islands Night 0/8, 3/0, 0/8,
  3/10; Alwarren 0/10, 0/5, 0/6; Succubus Lair 2/10, 0/10, 0/10, 6/0, 0/0; Volcano 9/9, 6/10, 9/0. Intro, Book, Rift
  Crystal and Rift World have none.

## Disc: how Surdana switches music

LEVEL1NA's placed objects (template extraction in `build/odbx/data`, `analysis/scripts/lvl_objects.py`):

* Four Triggered Music Loops (PS2MUSIC class 20) 17648 / 17649 / 17650 / 17651 play 89, 91, 89, 91 as a ring: each one's
  `Message To Send when Sound Ends` (19 or 1) is the next one's `Message To Send To Trigger On`, and they link in a circle.
  Only 17650 listens for On (1); the others listen for 19.
* 14 of the 15 FX "AmbientTransition" objects (class 3158, engine 131) have `Send Message On Exit ?` = 1; nine of them link
  all four loops (plus ambient sounds) with `Front Exit Message` / `Back Exit Message` 0 (Off) and 1 (On): leaving on one
  side starts 17650 (and so the ring), leaving on the other stops every loop. Their sound slots switch the ambience at the
  same time.
* Overlay music is separate: a Trigger Detector (17661) starts loop 17662 (179, overlay), and three "NPC Music Off
  Trigger" counters (class 14) handle combat music.
* PS2MUSIC.ODB also has a "Music Ambient Trans" class (10, engine 131) that no disc level places.

## What does not work on the PS2 engine (measured)

Private packs `build/ootf-run/mtrig*/`, Ruined Village as the campaign start (`DRAKAN_GAME=d1`), audio on,
`DRAKAN_SND_DBG=1 DRAKAN_SND_LOG=1 DRAKAN_HLE_LOG`, `DRAKAN_AUDIO_DUMP`, `DRAKAN_PLAYTEST_GOD=1`, the input script `place`s
Rynn next to the temple trigger 3203 (rot 0, scale 1.3 x 1 x 0.3, front 0 / back 3) and walks her through it with `log on 20`
for positions. The dump is matched against the converter's own tracks (normalised cross-correlation, 2 s windows).

* The PS2 front of a box is local +z, world `(cos ry, 0, sin ry)` for a yaw-only rotation (`level-format.md` section 3, the
  -90 degree yaw offset): for rot 0 the thin axis is world x. Walking along z through the 2662-unit long side does nothing
  useful (first runs); walking +x exits the front, walking -x the back.
* A first version made each trigger three Ambient Transitions on the same box (off on front, off on back, on), since one
  transition sends one message per side. Only the **first** transition on a box ever fired: with the off transition first,
  walking +x stopped 8 (fade 3 s, the dump's 8 correlation falls from 0.8 to 0.06) but walking -x never started 3; with
  the on transition first (`build/ootf-run/mtrig3`), -x started 3 (`0302/3`, `0304/3 +0`, dump matches track 3 from its
  start) and 8 kept playing, +x did nothing. The engine follows one ambient transition at a time.
* Starting a non-overlay loop does not stop the playing one (8 and 3 played together in that run), so the old music must be
  stopped explicitly.

## Fix

`editor/packages/formats/src/ootfmusic.ts` `applyLevelMusic` (called by `ootfpipeline.ts`, replaces `applyMusicTriggers`
and the separate start music object):

* One Triggered Music Loop per PC music id used by the level (its `Initial Music ID` first, on from the start, then the
  trigger ids), at Rynn's start, not overlay, the fades of the first trigger slot that plays it, `Message To Send To Trigger
  On` from `MUSIC_ON_MESSAGES` (1, 19, 20, 21, 10 ...), Off 0. A level without triggers keeps the single start loop as before.
* Each trigger box becomes two halves along its local z (`triggerHalves`: same rotation and width / height, half the
  thickness, at least 0.25 model units, centred a quarter box either side of the plane). Each half holds a Trigger
  Detector (Task 6 "Trigger Only", `Trigger Message` 0, not once) linked to every loop but its music, and one sending that
  music's On message to its loop (none for 0). Entering the front half switches to `Music 1`, the back half to `Music 2`,
  so the switch happens at the mid plane in both directions. Detectors are independent of each other, the converted trigger
  object is reused for the first one and a 0 / 0 trigger in a level without music is removed.
* The class resolver maps the PC trigger classes to the template's Trigger Detector (FX), so no class is appended
  (FX and PS2MUSIC are LEVEL1NA dependencies). `ootf-convert.ts` lost the `--music-start-on` test aid (the start loop is on
  anyway).
* Converted Ruined Village: `music: 4 loop(s) (8->8, 2->2, 3->3, 5->5), 16 detector(s)`; Wartok 5 loops, 23.

## Verification

* `build/ootf-run/mtrig4` run d1 (recipe `place` then walk +x, -x, +x through 3203):
  * +x: the back half asks for 3 (`0302/3`), the front half stops everything at once (3 never audible), 8 fades: dump 8
    correlation 0.72 then 0.06.
  * -x: `0302/3`, `0304/3 +0` ... `+327680`; the dump follows track 3 from its start (ncc 0.6 to 0.8 at a constant offset)
    for 22 s.
  * +x again while 3 plays: the back half's On reaches the playing loop without a new descriptor request and the track
    continues (offset 22.8 s, no restart); the front half stops it (0.46, then 0.08).
* Tavern 1790 (8 front / 2 back, pitched 37 degrees, run tav): walking in from the street side requests 2 (`0302/2`) and
  8 stops; Rynn then drops through the floor area into the front half, which stops 2 and restarts 8 from its start
  (`0304/8 +0`, dump offset 2.3 s): a loop turned on again starts over.
* The start music is unchanged: `0302/8`, `0304/8 +0` after the level load, the dump follows 8 from second 18.
* Wartok placements far from Rynn's start were unreliable (`place` gave up, or placed after 20 tries and the fall killed
  her without `DRAKAN_PLAYTEST_GOD`), so the cave triggers were not walked in game; their conversion is the same code.
* `npm test` (editor): 1164 passed, with new cases for the half boxes, the loops, the per-half detectors and their links,
  a missing box and a template without a detector class.

## Open

* Pitch and roll are ignored when placing the two halves (the offset is a quarter of the thickness, about 100 to 500
  units); the halves keep the full rotation.
* The trigger's `Initial State` is not read (all 37 are 0 and nothing links to them).
* Cut scene music (sequence action 8) plays by id outside these loops: Ruined Village scene 381 starts 6 over whatever
  loop plays.
* A loop that is turned on again starts its track from the beginning; how the PC restarts a segment was not compared.
