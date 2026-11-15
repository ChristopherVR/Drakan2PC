# Order of the Flame, Ruined Village opening: body jump, scene music, old man's door (15 November)

Owner's report from play of the opening:

* (4) When Rynn is struck down in the opening scene, her body changes position once it is down, as if it jumped a bit
  downward. When the scene looks over (every NPC gone), the shot of her body stays on screen for a moment.
* (7) The music in the scenes does not match the original, above all while Rynn talks to the old man.
* (7a) When Rynn walks up to the old man, his door opens; it should open only after the scene.
* (4b, added later) In the scene just before Rynn meets the man pinned by a spear, a Scavenger slides across the shot
  without moving its legs.

Scratch, packs, runs, frames and decompiler output: `build/review/ootf-opening/` (private play folders `play` and
`play2` with their own ELF copy; nothing from either game is quoted here beyond object, class, clip and field names).

## 1. The scenes (PC `Ruined Village.ssd`)

Dumped with a private script (`tools/seqdump.ts`, on `parsePcSequence` and `loadPcLevel`):

| PC seq | Name | Length | What matters here |
| --- | --- | --- | --- |
| 381 | 1.1 | 61.2 s | Rynn and Delon in the ambush. Camera music action at 11.52 s: id 6 ("Dark Union"), fade in 7 s, fade out 3 s. Rynn's fall: clip 0x2a0 from 54.92 s (one key at 54.92 s), lying clip 0x3ea from 56.29 s, key at 59.98 s. Fader 2460 gets message 25 at 57.15 s. Every actor goes to storage at about 60.3 s, Stomp Player 2223 starts 1.2a at 61.19 s |
| 964 | 1.2a | 52.8 s | The villagers' scene. Fader 2458 gets 253 at 0 s, Fader 2457 gets 25 at 47.96 s |
| 390 | 1.3 | 90.8 s | Rynn wakes and walks to the old man. Fader 2461 gets 0 at 0 s. Camera music action at 4.58 s: id 0 (stop), fade out 9 s. The old man is actor "Julian", object 1185 of class "fake julian"; the subtitles of the scene call him Atimar ("Atimar, my god, Atimar, what happened?") |

The old man's house door is object 1617 (class "Door", 217174, 18999, 170555); Rynn's last keys of 1.3 are about 1,870
units from it. No object links to that door and no scene names it: on the PC it opens only when pushed. The faders are
model-less (screen faders), class defaults Fade In 2 s, Faded -1 (wait for a message), Fade Out 2 s, Start Mode 1 (on a
message); 2460 and 2457 set Faded 4 s, Fade Out 0, colour black.

So on the PC: 1.1 fades to black from 57.15 s (black at 59.15 s, held 4 s), the NPCs leave under the black, 1.2a opens from
black; at 1.3 + 4.58 s the music fades out over 9 s and the whole talk with the old man has no music; no music plays after
1.3 until Rynn walks through a Music Trigger. Rynn does not push the door during the scene.

## 2. Reproduction (real play, audio on)

Driver `tools/opening.py` (on `scripts/harness/live_drive.py`): New Game with `DRAKAN_GAME=d1`, autopress taps only until
the Intro loads (`DRAKAN_AUTOPRESS_STOP_AFTER_LOAD=1`), then no input: the Intro, Ruined Village, 1.1, 1.2a, 1.3 play as
the game runs them. Shots every second (every 0.2 s around the fall in the after run), the controlled actor's position five
times a second, `DRAKAN_SND_LOG=1`, `DRAKAN_HLE_LOG`, `DRAKAN_SND_DBG=1`, `DRAKAN_STOMP_PROBE=1`, `DRAKAN_AUDIO_DUMP`.
Windowed 1280 x 960, DLSS and HD textures off, otherwise the shipped `drakan.ini`. Which track is audible is measured
by `tools/musiccorr.py`: best-lag normalised correlation of 4 s windows of the dump against the pack's own decoded
tracks (ncc 0.4 to 0.9 when a track plays, about 0.1 otherwise).

Run `base1` (exe `fb558b16...` = `dist`, installed pack `629adfaa...`):

* **Music:** track 8 (the level's start loop) from the level load, track 6 from about 1.1 + 13 s, then **track 6 without a
  break to the end of the run**, through 1.2a, the whole of 1.3 (old man) and the gameplay after it, wrapping at its end
  (`0304/6 +0` again 242 s after it started). The stop at 1.3 + 4.58 s changed nothing.
* **Door:** in the 1.3 frames the door beside the old man stands open from the moment Rynn reaches him (`f0130`).
* **Fall:** the controlled actor's trace: at 1.1 + 54.9 s Rynn's origin follows the fall clip's root motion 660 units
  sideways and 500 down onto the grass (267117, 20546, 205611 -> 267672, 20050, 205977), then at 1.1 + 56.3 s it is set back
  to (267117, 20100, 205611) in one frame: the lying body jumps back to the spot of the last key.
* **End of 1.1:** the frames from 57 to 61 s stay fully lit; at about 60.3 s every NPC vanishes and Rynn's body lies alone
  on the grass (`f0061`) until 1.2a starts.

## 3. Causes

**(4a) The body jump: a converter key.** `settlePlayerLying` (`ootfseq.ts`, 2026-11-09) rests a lying pose on the ground and
adds a key where a lying clip starts, at the last key's position lowered to the ground. 1.1 has no key between the fall
(0x2a0) and the lying clip (0x3ea), so the added key took the 54.92 s position. The PS2 engine carries a scene clip's root
motion into the actor's position (trace above), so at 56.3 s she already lay on the ground where the fall left her; the
added key moved her back by 660 units. The key at 59.98 s also moves her (by about 470 units), but it is in the PC data and
on the PC the screen is black then.

**(4b) The shot of her body after the NPCs leave: fader messages.** The PC scenes start their screen faders with numbers
(25, 253, 0); the PC fader ignores the value (any message runs it). The converter passed the numbers through, and the PS2
fader starts only on On (4097), as every disc scene starts its faders (disc: `Fader` 4097, `Fade 2 Black` 4097 then 4259
...). None of the opening's four fades ran: no fade to black at the end of 1.1 and of 1.2a, no fade in at the start of 1.2a
and 1.3; so the NPCs' removal at 60.3 s and Rynn's lying body showed.

**(7) The music: the PS2 scene music action has no stop.** The PS2 handler of sequence action 8 (inside `0x1a4c50`,
decompiled in `build/review/ootf-opening/decomp/`, not quoted) reads `ref, keep, fadeIn, fadeOut, level, ramp, play`: with
`play` set it stores the category fades (`0x1b6bd0`) and calls PlaySound with `ref` in the music category (2), `keep` clears
the scene flag that hands the music back at the scene's end, then `0x1b6930` sets the music category's level to `level`
over `ramp` seconds. The converted stop was `ref 0, keep 1, play 1`: PlaySound(0) does nothing and the level stays, so
nothing stopped (the `[play] id=0 flags=2 ra=1a5894` line at 1.3 + 4.58 s). The engine's own category stop (`0x1b6ac8`)
is reached only from a branch taken when a word of the running scene (+0x1c, meaning not established) is set. Calling
that stop at the action did not silence the cue either (run `fix1`, below); why is not established. Two observations:
the `[play]` log (PlaySound `0x1b7f30`) never shows the cue's id (`e0006`), and the track's descriptor is read when the
scene preloads the action (`0302/6` at 1.1 + 7.1 s, 4 s ahead), while the track becomes audible only after the cue time
(ncc 0.16 at 1.1 + 12 s, 0.38 at + 16 s).

**(4b) The sliding Scavenger: a dropped clip.** 1.3's actor 5 "Scavenger" (object 885) runs past the camera from 3.66 to
9.87 s: clip 0x617 ("run", speed 0.77) at 0 s and two keys 12,000 units apart (3.71 s, 8.42 s, glide). The Scavenger class
plays on the Drakan 2 Scavenger model with its own clips (the default model choice, `ootfps2model.ts`), so no PC clip has
a converted counterpart and the converter dropped the run ("animation 0x617: no compatible resource mapping"): the
Scavenger glided along the keys in its bind pose. `base1` `f0118` shows it.

**(7a) The door: the PS2 auto-open.** PC doors have no "Auto-Open?" field; the converter's PS2 door classes open within
2 lu of the player (the stand-in for the PC push, `ootfdoors.ts`). That check runs for the player while a scene moves her,
so the door swung open when 1.3 brought Rynn to the old man.

## 4. Fixes

Converter (formats submodule):

* `settlePlayerLying` adds no key at a lying clip's start when the clip before it (with no authored key after its start)
  ends lying flat far below the origin (`carriedDown`: lowest joint at least 200 below the origin and the pose spanning less
  than 500 units): its root motion already took her down. `PlayerGround` gets an optional `highestJoint`; the pipeline
  computes both from the same pose (`jointSpan`).
* `SequenceConvertOptions.screenFaders` (PC ids of model-less faders whose Faded Time is not -1, filled by the pipeline): a
  message below 4096 sent to such an actor becomes On (4097). Engine messages (Off, Blow Up ...) are kept. A fader with
  Faded Time -1 holds the black until a second message on the PC; how the PS2 fader treats that was not tested, so those
  keep their numbers (Intro 2 "scene fade", the Rift World ending's "End Fade", Volcanic 9.9's "Fader").
* Creatures shown with their Drakan 2 model and clips (`enemy === 'drakan2'`): their scene clips map by name to the disc
  model's clips (`clipKey`: lower case, a dotted suffix dropped, so the disc's "run.rea" matches the PC "run"). Scavenger 6
  clips; the Wartok and orc classes 17 to 29 each.
* Music stop: `convertAction` writes `ref 0, keep 1, fadeIn, fadeOut, level MUSIC_STOP_LEVEL (-4096), ramp = PC fade out,
  play 1`. No disc scene, music loop or scene start / end uses a negative level.
* `sceneAutoOpenDoors` (`ootfdoors.ts`) names the doors whose auto-open is on and whose auto-open distance plus 1,024 units
  reaches one of the player's keys in a scene; `holdSceneDoors` (pipeline, after the ambush doors) adds to each such scene
  one actor per door (`addSceneDoorHolds`, `ootfseq.ts`) that sends 0x11 (auto-open off) at 0 s and 0x12 (on) at the
  scene's last action (also sent on a skip, as `addEndMessages`). The door messages are the PS2 door handler's
  (`door-proof.md`). Converter log: Ruined Village 5 holds (1.3: door 1617; The Book and Book 1B: the inner temple doors),
  Wartok Canyons 3 (the Magic Farmer scenes).

Runtime:

* `drakan_overrides.cpp`, `sceneMusicStopHook` on `0x1b6930` (installed for every game; acts only on the level
  -4096): calls the engine's category stop `0x1b6ac8` with the ramp as fade (which alone did not end the music), and has
  the campaign music ducker (`hle::CampaignMusicDucker::stopAll`, new) serve every streaming track silent until a
  track's descriptor is read again (a cue, or a music loop started for the first time). That is what ends the music, when
  the buffered chunks run out. Log `[music] scene stop: ...`. A pack converted with these changes needs this exe: an
  older exe would set the music level to -4096.

## 5. Proof

Pack `packs/fix1` (`d7b52431...`): a full 23-level conversion of the working tree (formats with these changes and other
sessions' uncommitted work) with the subtitle files named in the brief. Exe: `Build-LocalGame.ps1 -Incremental` of the
working tree (also other sessions' uncommitted runtime work).

Converted 1.1 / 1.3 (`tools/ps2seq.ts`): 1.1 has no key at 56.29 s; the four fader messages are 4097; 1.3 has the
"Door Hold" actor on 1617 (17 at 0 s, 18 at 90.76 s) and the stop `0, 1, 3, 9, -4096, 9, 1`.

Run `fix1` (exe `ba2e499a...`, the category stop only, no ducker stop yet), same driver, dense shots around the fall:

* **Fall:** the trace follows the fall to (267672, 20050, 205977) and stays there until 59.98 s; frames `f0071` to `f0087`
  show her falling and lying still, no jump. **Seen fixed in game.**
* **End of 1.1:** from 1.1 + 57.5 s the picture darkens, black from about 59.6 s to the start of 1.2a, which opens from
  black (`sheet_11end.png`); the NPCs leave and the 59.98 s key acts under the black. **Seen fixed in game.**
* **Door:** the door beside the old man stays shut through 1.3 (`f0178` to `f0180` against `base1` `f0130`) and stands open
  when control returns (`f0253`). **Seen fixed in game.** (`DRAKAN_PLAYTEST_WATCH class=10013` did not list this door, so
  the evidence is the frames.)
* **Music:** `[music] scene stop: category 2, fade 9.00 s (ra=1a58cc)` at 1.3 + 4.4 s, but the dump still follows track 6
  (ncc 0.4 to 0.8) to the end: the cue is a stream, not a category instance (section 3). Hence the ducker stop.

Run `fix2` (exe `d5c8f189...`, with the ducker stop): see section 6.

## 6. Music after the fix

Run `fix2` (exe `d5c8f189...`, pack `fix1`), shots every 2 s, then Rynn is placed beside the tavern Music Trigger 1790 and
walked through it (a harness placement, only for this check):

| Dump time | Track audible (ncc) | What the game logged |
| --- | --- | --- |
| level load to about 1.1 + 13 s | 8 | `0302/8` |
| from about 1.1 + 13 s, through 1.2a | 6 (0.4 to 0.9) | `0302/6` at the preload, `track 6 replaces 8` |
| 1.3 + 4.4 s (probe 170.6) | 6 | `[music] scene stop: category 2, fade 9.00 s`, `1 streamed track(s) silenced` |
| from about 178 s (1.3 + 12 s), the rest of 1.3 and the gameplay after it | none (all tracks 0.06 to 0.13; speech and ambience only) | |

So the old man's talk now has no music, as on the PC; the music ends about 8 s after the stop (the PC fades out over 9 s,
here it stops when the buffered chunks run out). **Seen fixed in game (audio dump and log).**

At the tavern trigger the game reads `0302/2` and plays `e0002` and `e0008`, but the dump stays silent. The control run
`base2` (shipped exe and pack, same driver) does the same: `track 2 replaces 6`, then silence; so the silence at the tavern
is older than this change (open, section 8).

## 6b. The Scavenger and the campaign-wide effect

Pack `packs/fix3` (fader rule limited, Scavenger clips by name). Byte comparison of every converted scene (`tools/seqdiff.ts`,
`pack0` = installed pack, `pack3`), changes of this work only (other sessions' scale and placement edits in the tree also
show up and are not listed):

| Scene | Change | Rule |
| --- | --- | --- |
| 1.1 | Rynn's key at 56.29 s gone; Fader 2460 25 -> On | lying key, fader |
| 1.2a | Fader 2457 25 -> On, Fader 2 2458 253 -> On; Scavenger plays "run.rea" at 34.6 s | fader, clips by name |
| 1.3 | Fade In 2461 0 -> On; stop -> level -4096; door hold on 1617; Scavenger plays "run.rea" at 0 s | fader, music, door, clips |
| Intro 3 | Scene Fade In 2851 0 -> On (Faded 0, fade out 3.5 s) | fader |
| 8.1 (Succubus Lair) | Fader Mirror 1, Fader Transition, Fader Mirror 2: 0 -> On (1.5 / 1 / 1.5 s) | fader |
| MagicFarmer 1 to 3 (Wartok) | Fader 667..669 -> On (1 / 2 / 1 s); door hold on one door | fader, door |
| end sequence (Rift World) | Fader 470: 669 -> On (1 / 4 / 1 s) | fader |
| The Book, Book 1B | door holds on the inner temple doors 916 / 917 | door |
| backstory (Book level end), 9.1 (Volcanic) | music stop -> level -4096 (fade 3 s, 4 s) | music |
| Interrogation (Grimstone Mines), "41" (Grotto) | Troll1 / Troll2 and three Wartoks get clips that were dropped before | clips by name |

No other scene lost a lying key. The music stop in 9.1 now silences Volcanic's music until a track's descriptor is read
again; a music loop that is already on when a trigger turns it on again reads none and stays silent (a limit of the
existing ducker, which every cue already had). Only the Ruined Village scenes were played.

Run `fix3` (exe `d5c8f189...`, pack `packs/fix3`), shots every 0.2 s over 1.3's first seconds: the Scavenger crosses the
shot in changing running poses (`runs/fix3/scav_strip.png`, frames `f0091` to `f0096`); in `base1` it slid past in one
standing pose (`runs/base1/scav_strip.png`). **Seen fixed in game.**

Run `skip1` (same exe and pack): Esc at 1.1 + 15 s, 1.2a + 5 s, 1.3 + 2 s (before the music stop). The skip runs the passed
actions: `[music] scene stop` and `1 streamed track(s) silenced` are logged at the 1.3 skip, the dump loses track 6 about
6 s later and stays without music; control returns with the picture and the HUD 1 s after the last skip (`f0023` on), so
nothing holds the screen black. The fade of 1.1's end runs at its skip, so about 6 s of the skipped stretch are black
(`f0017` to `f0022`).

## 7. Tests

* formats: `npx vitest run` 69 files, 722 tests pass on the working tree (other sessions' tests included) (new: clip names (`ootfclipkey.test.ts`), fall carried down / not, screen fader messages, music cue and
  stop fields, door holds and key positions in `ootfseq.test.ts`; scene door choice in `ootfdoors.test.ts`; synthetic
  fixtures only). `npx prettier --check` is clean on every file this work touched (`prettier --write` on `ootfpipeline.ts` also re-wrapped one line of another session's terrain edit); the package-wide check still lists 11 untouched files that fail at HEAD or carry other sessions' edits.
* runtime: `override_test` (private tree `build/review/ootf-opening/rt`): 97 checks pass, with the new `stopAll` case.
* `clang-format` (VS 2022 LLVM) clean on the touched C++ files.

## 8. Open

* The 59.98 s key of 1.1 still moves Rynn by about 470 units; it acts under the restored fade.
* The cue starts when the scene preloads it, so the campaign ducker cuts the level loop at the preload (1.1 + 7 s), not at
  11.5 s, and without the PC's 3 s fade out / 7 s fade in (the cue's stream is audible from about 13 s because of the
  buffered chunks). Not changed.
* The ducker's silence works per streamed chunk, so the stop is not a real 9 s fade: the music ends after the chunks already
  buffered (several seconds).
* After 1.3 the tavern Music Trigger starts loops 2 and 8 (`[play] id=e0002 / e0008 flags=4`, positional, `0302/2`) but
  nothing is audible in the 25 s after, in the shipped build too (`base2`). Not examined; the in-level music triggers were
  last verified on 2026-11-08 without the opening scenes.
* Door holds also apply to the church scenes and the Wartok Magic Farmer scenes; not played.

## Play pack rebuilt with every 15 November fix (12:42)

`extras.py ootf` passes no `--subtitles`, and `build/ootf/convert-state.json` is older than the code, so a plain rerun
would rebuild the pack without subtitles. The owner's pack was converted by hand, then the HD step and the build:

```sh
node --experimental-transform-types --no-warnings editor/scripts/ootf-convert.ts --ootf build/ootf/cab_disc \
  --ps2 build/ootf/ps2/data --template LEVEL1NA --campaign 1 --id ootf-levels --iso "<iso>" \
  --out build/ootf/mods/ootf-levels.zip --cover scripts/buildtool/kit/art/ootf-cover.png --hd --hd-sources-only 1 \
  --subtitles "build/review/ootf-church/subtitles/english2.json;build/review/ootf-church/subtitles/corrections.json"
python scripts/buildtool/extras.py hd --iso "<iso>" --play dist/DrakanPC --work build/hd --ootf-pack build/ootf/mods/ootf-levels.zip
pwsh scripts/buildtool/Build-LocalGame.ps1 -IsoPath "<iso>" -WorkDir build/final-game -Incremental -Jobs 8 -ModDirectory build/ootf/mods
```

Log: 23 levels, 6 trees lowered, `drakan 2 ground: 103673 cells`, 297 subtitle lines; the pack holds no "Erokh" or
"Riff Crystal" (69 "Arokh", 50 "Rift Crystal"). HD step: 71 new textures. The morning pack is kept in
`build/review/pack-1115/ootf-levels.before.zip`.

## 9. Music after a scene stop, and the tavern trigger (afternoon)

Private play folder `build/review/ootf-music/play` (copy of `build/final-game/bin`, the dist pack `9f1ad4f6...`, the
owner's ini with its own ELF copy, textures hard-linked). Driver `tools/probe.py`: New Game (or `--level`), optional Esc
skips, `--plan` placements, `--tavern --stage` (Rynn placed inside the tavern by trigger 1790 and moved out in short
steps until the back half turns loop 2 on, 25 s in the doorway, then out through the front half into the street).
A thread reads the sound manager's categories from guest RAM (`music.jsonl`, 4 to 50 times a second). Audio:
`ootf-opening/tools/musiccorr.py` (ncc per 4 s window) and `tools/amp.py` (gain of one track in the dump: the
projection on the best-lag window of the track, divided by that window's norm).

### Causes

* **The converted tracks were not music.** The u32 at +16 after the name of a PS2 `0x302` descriptor is the sound's
  category in the sound manager: every PS2MUSIC.SDB track on the disc has 2 (music), SYSTEM.SDB has 0 (effects) and 4,
  INTERFCE.SDB 3 and 4. `writePs2Descriptor` wrote 0, so every OotF track played in the effects category (run `p1`,
  shipped exe: `e0008`, `e0006` in category 0, category 2 empty). Consequences: tracks did not replace each other (the
  reason the chunk ducker was added on 9 November), the engine's music stop `0x1b6ac8` on category 2 found nothing (why
  run `fix1` kept playing), and neither the Music Volume option (category 2's gain, `music_volume / 100`) nor the scene
  music levels reached the campaign's music.
* **The old tavern silence** (`base2`, `ootf-final` `tavern1`) came from how those runs crossed the trigger: placed at
  186400 and walked straight through, Rynn crosses both halves within half a second (back half: 2 on, the other loops
  off; front half: 8 on, the others off) and ends in the street. Trace of `base3` (shipped exe): loop 2 starts, the
  front half turns it off (`0x1b82a8` from the loop handler) and turns 8 on again; 8 restarts its track at offset 0
  without a descriptor read and without a pause in its chunk requests, and the ducker serves a track restarted that way
  silent ("a wrapping loop with no gap stays silent"). So the street was silent after the tavern; after the 1.3 stop
  the same, with 2 getting one chunk. A player who stops in the doorway does hear 2 with the shipped exe: `base4`
  (shipped exe, the staged walk below) has 2 at ncc 0.80 to 0.96 in the doorway and 8 at 0.80 to 0.95 in the street.
* **With the category fixed (first runs `a1`, `a3`)** the engine's own hand-over showed four more faults:
  1. A new track waits in the category until the old one has faded out (`0x1b7758` holds it; the old one fades with the
     category's fade out). The Triggered Music Loop emitter (`0x4030c8`) asks whether its instance plays (instance flag
     0x4: a positional sound not voiced yet) and stops a held instance at once (`0x1b82a8` -> `0x1b7cf8`, 20 ms after
     the sort, trace `a4`). At the tavern 8 was stopped while 2 faded out: silence again.
  2. When the faded-out track ends, the engine starts the held track, also one that already plays: its fade in starts
     again from silence (`d1`: `e0006` back at attenuation 0.99 when 8 ended, 3 s into its 7 s fade in).
  3. A scene with a music action pauses the playing music at its start (`0x1b57f0` from `0x1a3d48`) and at its end fades
     out the music it started and resumes the paused one (`0x1b58e0` from `0x1a4418`), unless the action's `keep` is set.
     Dark Union ended with 1.1 and, in the next try, was cut at the start of 1.3 (paused) instead of fading at 4.6 s.
  4. A scene lowers the music category to half at its start and ramps back to the level it saved; chained scenes saved
     the half level of the previous one, and after 1.2a and 1.3 the music stayed at half (`c2`).

### Fixes (runtime; the installed pack works without a reconversion)

* `hle::MusicCategoryOverride` (`ResourceOverride.h/.cpp`, installed for every game in `drakan_overrides.cpp`): a
  PS2MUSIC.SDB descriptor with category 0 is served with 2 (log `music: 0302/8 served in the music category`). Disc
  tracks already have 2, so Drakan 2 is unchanged. `DRAKAN_MUSIC_CATEGORY_FIX=0` turns it off.
* `drakan_overrides.cpp`, only while a campaign with its own music is the current game (`CampaignMusicRedirect`), PAL
  addresses only, `DRAKAN_MUSIC_HANDOVER=0` turns it off:
  * `musicSortHook` on `0x1b7758`: a new music track the engine would hold is started at once with its fade in (the
    engine's own start, `0x1b15e0`; a cue that is already voiced keeps its own fade in); the old one keeps fading out
    (with its own time when a loop's Off already started one); a third track in a row fades the one below too. When the
    faded-out track is silent or gone, the engine's second start of the new track is skipped and only its bookkeeping
    done (no track fading out, the category's default fades). Log `[music] track e0008 starts now (fade in 3.00 s),
    e0002 fades out (2.99 s left)`. A call into the original that is suspended (the guest thread switches inside it) is
    left alone; the next update of the instance hands over instead.
  * `soundStopHook` on `0x1b7cf8`: the same bookkeeping when the faded-out track is stopped.
  * `musicPauseHook` / `musicPopHook`: the pause at a scene's start and the fade-out / resume at its end are skipped
    (`[music] scene start: the playing music goes on`, `scene end: the scene's music plays on`).
  * `sceneMusicStopHook` (on `0x1b6930`) also drops a scene's own half level and its restore, a choice: the PC scenes
    have no such level, but how loud the PC plays music under a scene was not measured. For the level -4096 it now calls
    only the category stop, which fades every music track out over the ramp and ends it: a real fade.
* `hle::CampaignMusicDucker` and `ResourceService::setChunkDuck` are removed: the engine now replaces the music itself.
* Converter (formats, outside the `ootfseq.ts` named for this task: `ootfsound.ts`, `ootfdmusic.ts`): `Ps2Sound.category`
  (written at +16), `OOTF_MUSIC_CATEGORY = 2` for the rendered tracks, so a new pack is right without the override. Not
  reconverted here.

**Volume:** the campaign's music now follows the Music Volume option, like the disc's music; before, OotF music played
at full volume whatever the option said. The owner's `drakan_native_options.ini` has `music_volume = 13` (default 25),
so at that setting the music will sound much quieter than before.

### Proof (exe `3a9695cf...`, pack `9f1ad4f6...`)

Run `d6` (New Game, no skip, `music_volume` 100 for clean correlations; after the scenes three quick placements back
half, front half, back half; then the staged tavern walk), dump seconds, ncc of tracks 2 / 6 / 8:

| Dump time | Audible | RAM / log |
| --- | --- | --- |
| 48 to 56 | 8 (0.72 to 0.78) | `e0008` alone |
| 60 | 6 coming (0.43), 8 going (0.20) | `track e0006 starts now (fade in 7.00 s), e0008 fades out (3.00 s left)`; 6 at attenuation 0.98, 0.56, 0.23, 0 at +0, 3, 5, 7 s |
| 64 to 160 (1.1, 1.2a, 1.3 start) | 6 (0.29 to 0.87) | `scene end: plays on` after 1.1, `scene start: goes on` at 1.2a and 1.3 |
| 164 to 172 | 6 fading: gain 0.090, 0.090, 0.075, 0.066, 0.058, 0.043, 0.032, 0.020, then noise (1 s steps; 0.10 before) | `scene stop: category 2, fade 9.00 s` |
| 172 to 262 (the talk with the old man, control) | none (0.07 to 0.12) | category 2 empty |
| 264 to 296 (placements 2, 8, 2 within 1.6 s) | 8 (0.26 to 0.86) | see Open |
| 304 to 328 (Rynn in the tavern doorway) | **2 (0.65 to 0.98)** | `track e0002 starts now`, 8 fades out over 3 s |
| 332 to 356 (street) | **8 (0.67 to 0.94)** | `track e0008 starts now`, 2 fades out over 3 s |

Run `d2` (the same without placements, at the owner's `music_volume` 13, exe `1a38e674`): 2 at 0.48 to 0.78 in the
doorway, 8 at 0.70 to 0.91 in the street; the cue under the scenes' speech reads only 0.2 to 0.5 at that volume.

Before, shipped exe (`base4`, same driver and settings as `d6`): 8 then 6 as above, the 1.3 stop cuts 6 when its
buffered chunks run out (0.37 to 0.52 until about 172, then none), the quick placements leave silence (0.05 to 0.15 from
264 to 296), the doorway plays 2 and the street 8 (ducker: each with a new descriptor read).

**Volcanic** (`v3`, `--level "OotF 11 - Volcanic"`, `music_volume` 100): 9.1's cue 7 plays (ncc 0.54 to 0.96 from 8 s),
the stop at 31.8 s fades it over 4 s (none from 40 s), then nothing, as on the PC, until Rynn walks through trigger 2890:
`0302/9`, `e0009` fades in over 3 s and plays (ncc 0.17 to 0.50 under combat noise). Volcanic has no start loop.

**Surdana** (Drakan 2, `s_base` shipped exe / `s_new3`, `--game d2 --level "Level 1A - Surdana"`, 120 s): the same
music requests (`0302/89`, 18 chunks of 89), the same category-2 timeline in RAM (the opening scene lowers the music to
half and its cue `0x70059` = track 89 plays), no `[music]` line in the new run, and the dump loudness per 10 s within
1 % (166 / 166 ... 403 / 408). Drakan 2 is unaffected.

Runs `a1` to `d5` are on intermediate builds (category only, then the hand-over without the second-start and suspend
handling) and are kept only for the causes above.

### Tests

* `ctest --preset runtime-tests`: 200 of 200 (the usual data-dependent skips); `override_test` replaces the ducker case
  with `MusicCategoryOverride` (category 0 -> 2, 2 and 3 left alone, short records, other files and types). The later
  hook changes are in `drakan_overrides.cpp` only (not in the test tree).
* formats `npx vitest run`: 69 files, 724 tests (new expectation: category 2 in the rendered track's descriptor);
  `prettier --check` clean on the touched files. `clang-format` clean on the touched C++.

### Open

* Quick toggling inside the fade time: back half, front half, back half within 1.6 s ends with 8 although Rynn stands in
  the back half. The third "2 on" reaches loop 2 while its own instance is still fading out, and the loop ignores an On
  while it plays. The shipped exe ends silent there. Walking through once in either direction is right.
* A converted cue still has `keep` 0 and the scenes still carry the engine's music flags; the runtime makes them behave
  as on the PC only while the campaign is the current game. A pack converted with category 2 still needs this exe for
  the hand-over and the stop.
* Not compared with the PC by ear: the music level under scenes (now the music actions' own, full), Esc skips (a skip
  runs the passed actions, so an Esc at 1.1 + 15 s keeps Dark Union playing), and Ruined Village's loop 8 not coming
  back after the scenes until a trigger (as the PC data says: 1.3 stops all music).
* In the harness walk Rynn drops off the tavern step into the street (y 16498 below the floor at 19248); a placement
  detail, not the music.
