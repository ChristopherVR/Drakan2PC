# Order of the Flame, "arokh awakening": Arokh's scene clips, the stone Arokh, the bonding lightning (16 November)

Owner report, the Arokh awakening and bonding in the Ruined Village lair: (1) Arokh's animations do not play in scenes,
he only floats; (2) the stoned Arokh and its breaking do not really show; (3) the lightning when Rynn bonds with Arokh
does not show at all. Scratch, tools, packs, runs, frames and decompiler output: `build/review/ootf-arokh-scenes/`
(not tracked; private play folder `play` copied from `dist/DrakanPC`, its own ELF copy). Nothing from either game is
quoted here beyond object, class, clip and field names.

## The scene (PC `Ruined Village.ssd` 201, "arokh awakening", 148.5 s)

Started by Stomp Player 1625 when Rynn stands on the Soul Crystal (`ootf-ruined-village-crystal.md`). Actors that matter:

| Actor | PC object | What the PC scene does |
| --- | --- | --- |
| Arokh | 1011, Dragon (engine 28) | hidden at 0, shown at 5.11 s; 34 clip actions: the dragon class's own clips (Ready, Fly, Hover, Landing, Mount, Walk) and 26 scene clips (`1_6ready.rea`, `1_6i` to `1_6z2`, `5_2b`), on channels 0 (body), 1 (wings) and 4 (neck) |
| Blow up dragon | 2855, "Blow Up Dragon Generator" (engine 63, model `StoneDragon2`, Run Type 1, Object Selection 2, 15 stone pieces) | shown at 0, Blow Up (4259) at 5.06 s, hidden at 5.11 s |
| Particle 1 / 2 | 2472 / 2474, Soul Flare generators | message 25 at 108.57 s / 3.71 s |
| Rynn Lightning, Aroch Lighting, Lightning 3 to 5 | 2475, 2476, 2859 to 2861, Soul Lightning generators (spawn "Soul Lightning 1 / 2", engine 58) | message 0 at 108.23 s (Lightning 5 at 108.56 s) |

## Causes

**(1) Arokh's clips: the wrong rig, in the wrong ADB.** The converted Arokh is the disc's Dragon class and model
(SYSTEM.ODB 2656, SYSTEM.MOD 3889: 71 joints, 28 channels, clips in SYSTEM.ADB). The converter mapped his scene clips
through the converted PC dragon model (`mapAnimRef`): PC-rig clips of 50 tracks in NPCS.ADB / RESOURES.ADB (ids 5994 to
6113). A scene clip is resolved in the actor's own ADB (Rynn's retargeted clips, which do play, live in SYSTEM.ADB), so
Arokh found nothing there. In RAM (`runs/base1`, `arokh.jsonl`, the dragon's model animation every 0.1 s) his clip stays
`0x2063f` (the disc landing clip he was placed with) for the whole scene while his scene keys move him. His channels were
not mapped either (`dragonSceneChannels` fell back to the saddle only, because `ps2/data` has no SYSTEM.MOD), so a neck
clip on PC channel 4 would have gone to the disc's `tail_b`.

The two rigs share three joint names (`jaw`, `Tail`, `saddle`); the trees differ (the PC wings, neck, arms and saddle hang
under an `UpperBody` joint, the disc's under the root; disc neck 8 joints against 6, tail 8 against 5), but the bind
poses are aligned: same size (5,655 / 5,633 units), head to -z, tail to +z, wings along x.

**(2) The stone Arokh: the PS2 generator never draws its model.** The converter clones a generator from a model-less PS2
generator, so the statue was dropped; the RAM census of the lair (`tools/genprobe.py`, run `probe1`) found the generator
object with no visible model. Keeping the model on the clone (statue model 6870 on the generator record, model flags 0,
then 1, and a fade time of 0, then 4: packs `fix2`, `fix2f`, `fix2g`) still drew nothing in game, while the same runs
showed the pieces flying. The PS2 handler (`0x2ce120`, decompiled in `dec/`) also takes Blow Up as "finish": it calls
`0x2cde90` directly, the call the tick makes when a generator is done, so the generator never released its 15 pieces
(trace `runs/base1`: no spawn `0x2cfb28` from that generator).

**(3) The lightning: message 0 never reaches a generator.** The PS2 generator with On Trigger 0 (every PC scene generator)
starts on any message it receives (`0x2ce120`: field 19 = 0 sets the running bit), and Particle 1 / 2 (message 25) did
run in `base1`. The five lightning generators got message 0, and no call of `0x2ce120` follows at 108.2 s: a sequence
message with the number 0 is not delivered. Disc scenes start their lightning generators with On (4097) and end them with
Blow Up (scenes 4.2, 4.7c, 5.051). Census of every PC scene message to a generator (`tools/pcgenmsgs.ts`): 0 is also sent
to Rimril's phase-in / phase-out generators (Islands: "No Tasks Done", "partial completion", "tasks complete", "Rimril first
contact") and to Volcanic's "Solve Magic generator" (9.6a); Blow Up only to the stone Arokh; 15, 25, 2469 and On elsewhere.

## Fixes (converter, formats `e35b471` and the end idle below)

* **Arokh's clips** (`ootfdragon.ts`, used by `playerClipBuilder.addDragon` in `ootfpipeline.ts`, only for the actor that
  is the level's class-28 dragon):
  * A PC clip that the PC Dragon class names in an animation field (Ready, Walk, Run, Fly, Fly-->Hover, Hover, Landing,
    Mount, Dismount, Soar, strafes, turns, Bite) plays the disc clip of the same field of the disc Dragon class
    (`nativeDragonClips`). In the awakening: 11 of 34 clip actions.
  * Every other scene clip is retargeted onto the disc rig as a new SYSTEM.ADB clip (`retargetDragonClip`): at each
    source key time every disc joint takes the model-space rotation change of one PC joint from a fixed table
    (`DRAGON_JOINTS`, 51 of 71 disc joints; chains with more disc joints share a partner), a joint without one keeps its
    bind rotation under its parent, the root follows the PC root's translation. Offline check (`tools/dragoncheck.ts`):
    head, feet, tail tip and hands point within 5 to 12 degrees of the PC pose from the root; folded wings land up to 45
    degrees off because the disc wing bones have other lengths (the fold looks folded, the tip sits elsewhere).
  * Channels follow the same table (`dragonChannelMap`): PC 1 wings -> 27, 4 neck -> 15, 19 saddle -> 14 (as before).
    The full conversion logs no unmapped dragon channel.
  * When a scene gives Arokh back to the game he gets the disc Dragon class's Ready clip as an end idle, as the player gets
    hers (`dragonReadyClip`, `endIdleForActor`): with his acting clips now playing, four scenes ("6_5", "6-2 Meeting
    Tuiri", "No Tasks Done", "Rimril first contact") would otherwise release him in a retargeted acting clip.
  * The Intro's "Aroch" is a class 17 prop on the PC dragon model and keeps its converted clips.
* **The stone Arokh** (`ootfblowup.ts`): a PC generator with a model and Run Type 1 gets a static prop (a PC class copied
  from a generic prop of the level, engine 17, with the generator's model, placed at the generator), planned like every
  class; each scene actor of the generator gets a twin for the prop with its show / hide actions (a scene that starts the
  generator without hiding it would hide the prop at the start). Only "Blow Up Dragon Generator" qualifies in the 23
  levels (log `blow-up generator ...: 2855 shows its model through prop 3357`). The generator itself stays model-less.
* **Generator messages** (`ootfseq.ts`, `SequenceConvertOptions.generators`): a scene message 0 or Blow Up to a generator
  becomes On; 15, 25, 2469, On and Off are kept. Also changes the Islands Rimril phase scenes and Volcanic 9.6a (not played).
* Tests: `ootfdragon.test.ts` (pairing, channel map, native clips, world-delta retarget on synthetic rigs),
  `ootfblowup.test.ts` (prop class / object, scene twins), `ootfseq.test.ts` (generator messages).

## Proof (in game)

Driver `tools/awaken.py`: direct start of Ruined Village, Esc on each opening scene, Rynn placed on the Soul Crystal
(487424, 22300, 200704), shots every 1 to 2 s (every 0.2 s in chosen scene windows), Arokh's model clip every 0.1 s.
Exe: the `dist/DrakanPC` build (SHA-256 prefix `bcb5a431`), unchanged in every run.

| Run | Pack | Arokh's clip | Stone Arokh | Lightning |
| --- | --- | --- | --- | --- |
| `base1` | the dist pack | `0x2063f` the whole scene | absent (empty pedestal), no pieces | none (`base1_lightning.jpg`) |
| `fix1`, `fix2` | Ruined Village only, clips and messages | changes with the scene: retargeted (`0x20000` + 2364..) and disc clips (1607 Ready, 1635 Hover, 1595 Fly, 1599 Landing, 1606 Mount, 1613 Walk) | pieces fly at 5 s, statue still absent | bolts around the crystal from 108 s; trace: On to all five generators, each finishes after its life time |
| `fix3` | Ruined Village only, final converter | as above | **statue on the pedestal**, then swapped for the living Arokh as the pieces fly (`fix3s_start.jpg`) | as above (`fix3_lightning.jpg`) |
| `full1` | all 23 levels, converter `e35b471` | 20 different clips over the scene | as fix3 (`full1_statue.jpg`) | as fix3 (`full1_lightning.jpg`) |

Run `fix4` (Ruined Village only, with the end idle) plays the whole scene to 148.5 s: the end idle 1607 is the clip at the
release, Arokh and Rynn stand together at the cave mouth (149879, 25210, 211978), no error in the log.

Frames: `build/review/ootf-arokh-scenes/evidence/` (`base1_start.jpg`, `base1_4s.jpg`, `base1_lightning.jpg` before;
`fix3s_start.jpg`, `fix3_lightning.jpg`, `full1_statue.jpg`, `full1_lightning.jpg` after). In the after frames Arokh rears
with his wings spread out of the broken statue, talks with neck and head movement, bows and spreads his wings during
the bond, flies off with Rynn.

Campaign-wide (`tools/arokhcheck.ts`, pack `full1`): 25 scenes with an Arokh actor; 98 clip actions now play retargeted
SYSTEM.ADB clips, 82 disc clips; the only clip outside SYSTEM.ADB is the Intro 3 prop's (by design). The converter's
unmapped-clip warnings are unchanged (29, other actors).

## Needs

* A regenerated pack (the pack command in `docs/journal/2026-11-15/ootf-opening-scenes.md`). No executable change.

## Not verified

* Only "arokh awakening" was played. The other 24 Arokh scenes (Islands Rimril / Tuiri scenes, 5.1, 8.1, the Grotto
  fly-throughs and "41", Volcanic 9.2 to 9.9, the Rift World end sequence and 10.2a) are checked offline only: their clips
  resolve in SYSTEM.ADB. How the retargeted acting clips look there was not seen.
* The Rimril phase and Volcanic 9.6a generators now start (message 0 -> On); not played.
* The end of the awakening (save prompt, mounted exit to Wartok Canyons) was not run to the end in these runs (the driver
  presses nothing after the scene); the scene's actions up to 148 s played.
* The statue prop and the generator's pieces: the living Arokh can appear a frame or two before the pieces leave (the
  generator releases them on its next tick). Compared with the PC by data only, not side by side.
* After a release: whether the dragon's game controller replaces a scene clip by itself, and whether neck or wing overlay
  clips still running at the release (channels 15, 21, 23, 27 in 7 scenes, `tools/arokhend.ts`) clear, was not seen in
  game; the awakening ends with the level exit.
* The lair before the scene: the PC places the living Arokh hidden (object 1011 flags 0) and only the statue shows. The
  PS2 dragon object was registered without its hidden bit in the RAM census before the scene (`probe1`), so the living
  Arokh may stand beside (inside) the statue during gameplay; one look from the lair floor did not frame the pedestal.
  Not settled.
* Saves: the change adds a class and a level object to Ruined Village; old saves against the new pack were not checked
  (see `save-compat.md` for the pack build check).
* Folded-wing poses of retargeted clips differ from the PC pose (bone lengths); not compared with the PC renderer.
