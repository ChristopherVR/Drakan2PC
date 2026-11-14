# Order of the Flame: subtitles on barks, and a sound survey (14 November)

Owner: "Do also investigate sound throughout the game, and we are getting subtitles for passive voicing 'Woah' which
seems incorrect." Scratch, runs and frames: `build/review/ootf-sound/` (nothing from either game is in this note).

## 1. Subtitles on barks

### What decides which sound gets a subtitle

* **PC game:** no subtitle text at all (`ootf-church-cinematic.md` section 4).
* **Drakan 2 disc** (English, counted with a scratch script over every SDB): NPCS.SDB has 392 voices with the speech flag
  `0x2000` and 1 with text; SYSTEM.SDB 136 and 0; FX, RESOURES, SOUND, music 0 and 0. The level SDBs hold the dialogue
  (LEVEL1ON 605 flagged, 474 with text). So the disc stores creature voices such as `Wartok_Idle_*`, `*_HitRxn_*`,
  `*_Taunt_*` and `*Death*` as speech, but with an empty text: Drakan 2 subtitles dialogue and narration only.
* **Converter:** the subtitle file holds one line per clip that a PC sequence plays (`listSequenceSounds`), made by speech
  recognition. `SubtitleTable` wrote the text of every line into the clip's converted descriptor.
* **Runtime:** `drakan_clip_subtitles` shows the pages of any sound instance that plays a marked clip, whoever plays it.

So a clip that a scene plays **and** a creature or object also plays by itself carried its text into gameplay. Of the 451
sequence clips, 66 are also named by a class default or a level object (export of this run). 65 of them had no line
(effects the speech filter dropped). One had a line: `common/npc/npcs` #313 "Orc_01Ambient Mumble1", transcribed
"Woah, woah.", played in the Volcanic scene 9.1. The same clip is in the "Ambient Sounds" list of the classes
"Orc Leather (purple)" and "Navy Hueges (purple)". It is also the "Sound to Play" of four Orc Leather waypoints in
Ruined Village (objects 2592, 2621, 3308, 3309). In the pack it is RESOURES sound 564 (game id `0x30234`), and its
descriptor had the text. That is the owner's "Woah".

A second kind of wrong line plays only in scenes: speech recognition wrote words for a vocal sound. Seen in the game
(run `after-open`, scene 1.2a, a kneeling villager): "Oh, no way! Oh, heh, heh, heh!" (`common/fx/fx` #891, a moan; its
two sibling clips had already been dropped as noise). By name and data the same holds for `grimstone mines` #299
"WK_BeatdownL" ("Oh my god!", the Wartok effect family). No rule on use can catch these, because they play only in
scenes.

### The change (data, not a word list)

* A subtitle line has a `kind`: `dialogue` (the default), `narration`, `bark` or `effect`. Only dialogue and narration
  get text (`SubtitleTable`; the others are counted as withheld).
* `listObjectSounds(src)` lists every sound that a PC class default or level object parameter names, of any sound field.
  `classifySubtitleLines` marks a line as a bark when its clip is in that list, unless the line already has a kind.
  `ootf-convert.ts --subtitles` runs it and logs each bark and the count.
* A file can set the kind of any line. A line with `kind` and no `cues` only sets the kind and keeps the earlier file's
  text. A later text correction without a kind keeps the earlier kind. So the two scene effects are withheld by a
  two-line list, `build/review/ootf-sound/subtitles/kinds.json` (private, game names only), passed last.
* The subtitle tool: `ootf-speech.ts` exports `played_by` per clip; `ootf_subtitles.py` writes `kind: "bark"` for those
  clips (`clip_kind`).
* No runtime change, so Drakan 2 is untouched. Reference: `reference/modding.md` "Subtitles of a pack's voice clips",
  `reference/subtitles.md` section 10.

Commits: formats `071d205`, main `e25661c3` (code); this note and the reference docs in a separate docs commit.

### Evidence

Packs, all full 23-level conversions with `DRAKAN_TERRAIN_CHUNKS=wide` and the same subtitle inputs as
`update_dist4.sh`:
* `packs/dist`: the shipped pack, before.
* `packs/after`: the bark rule only. Convert log: "297 line(s) shown, 1 withheld"; descriptor 564 has no page marks;
  249 (#891) and 732 (#299) still have them.
* `packs/after2`: the bark rule plus `kinds.json`. "295 shown, 3 withheld".

Runs: play folder `build/review/ootf-sound/play` (a copy of `baseline-play` with the 15:07 dist exe, the owner's ini, a
private ELF), New Game in the Order of the Flame campaign, `DRAKAN_SUB_LOG=1`, `DRAKAN_SND_DBG=1`, audio dump on, god
mode.

| Run | Pack | Played or placed | Result |
| --- | --- | --- | --- |
| `before-wp`, `before-wp2`, `before-wp3` | dist | Opening skipped. Rynn placed beside the mumble waypoints 2621 and 2592 and the orcs that patrol them, then 90 to 160 s of standing, walking and swinging | Clip `0x30234` never played (0 `[play]` calls). The "Woah" was **not reproduced in the game**: the waypoint sound plays when a Wartok reaches that waypoint, and in these runs the orcs stood still. The only subtitle was the opening line. |
| `after-open` | after | Opening and scene 1.2a watched in full (no skip), about 220 s | 13 dialogue clips subtitled: Delon, the villagers, Atimar. The moan #891 was subtitled (`evidence/moan_subtitled_bark-rule-only.png`). A conversation frame: `evidence/conversation_still_subtitled.png`. |
| `after2-open` | after2 | Same scene | The moan clip `0x300f9` played at 148.4 s and drew no page (no `[subtitles] clip 300f9`; frame `evidence/moan_no_subtitle_with-kinds.png`). Dialogue as before (7 cues in the shorter run). |
| `combat` | after2 | Opening skipped, placed by Wartok 2531, walk and attack (Rynn unarmed: the inventory keys did not equip a weapon) | A Wartok attacked. Its swings and hits played (`0x301be`, `0x301c7`, `0x3018c`..). No subtitle in 100 s. |
| `lvl-islands` | after2 | Direct start (`DRAKAN_START_LEVEL`), the succubi greeting scene | The 5 dialogue lines were subtitled, in the scene. |

What was heard: nothing. I cannot listen. Everything here comes from logs, frames and the audio dumps (kept in each run
folder).

## 2. Sound survey

Played: Ruined Village (the opening and 1.2a scenes, outdoors at the waypoints, a Wartok fight), Islands (start scene,
walking), Grimstone Mines (cave, 75 s walking). Not played: a dragon flight (the Islands start runs a scene, then walking),
Volcanic, music checks. Static: a scratch script compares every sound named by the campaign's classes, objects and
sequences with the descriptors of the shipped pack (`build/review/ootf-sound/coverage_dist.txt`).

### Found

1. **Class-default sounds are not converted (systemic, the main defect).** The converter converts only the sounds that
   object parameters and sequences name (`ootfpipeline.ts` sounds stage). `mergeDefaults` (`ootfclass.ts`) skips sound
   fields, because they are not value fields. So a cloned class keeps its template's sound value, or gets none. Seen in
   the game: every Torch Flame and SconceFlame of Ruined Village and Grimstone asks for sound **id 0**, three times at its
   position (`[play] id=0 ra=1b8388`, matched to the PC object positions; 142 calls in 160 s). The PC "Torch" loop is
   never heard. Classes used by the 13 campaign levels with no converted sound in a family, by the static comparison:
   Death Sounds (43 refs), Got Hit Sounds (27), the New Quest map chime (24), door Bang Hinges (8), rope Swing Sounds
   (12), Arokh's remarks (Separated, Meet At Cave, Killed Enemy, Call Response, Get Out of My Way, Crash: 32), Rynn's
   Dragon Call (4), ambient "Sounds" loops (128 missing, 36 present: birds, bats, small fires), weapon pickup, equip and
   drop (about 180). Whether each of these is silent or plays a Drakan 2 template sound depends on the template and was
   not checked one by one. The torches are silent.
2. **Drakan 2 creature voices on the Drakan 2 Wartok models.** The Wartoks use the disc's own models and clips. Their
   animation events (`ra=10d1c0`) play NPCS sounds `0x503ee`..`0x503f6`, which are Drakan 2 "Grull_NEWidle_*", about
   twice a second from a few standing Wartoks (up to 77 times from one of them in 160 s). That is how Drakan 2 drives
   those clips; whether it is wanted for Order of the Flame is a choice that comes with the model swap. Not changed.
3. Footsteps play from Drakan 2's SYSTEM set (`0x20275`..`0x20278`, the material footsteps) when Rynn walks. Expected
   from the engine; not judged by ear.
4. 442 of the 451 sequence clips are in the pack. The other 9 were not traced.
5. No clipped or looping clip, wrong rate or voice-limit drop was found in the logs. `DRAKAN_SND_LOG` voice-count runs
   were not made. The status note's peak of 17 of 48 voices stands.

### Not fixed, and why

* Item 1 is the clear fix: convert the class-default sound refs of the classes the plan clones (add them to the sounds
  stage's wanted set) and write them into the synthesized records after that stage (`setSynthesizedDefaults` takes
  scalar fields; arrays need a new writer). It touches the shared `ootfpipeline.ts` sounds stage and `ootfclass.ts`. The
  torch and sconce classes belong to the flames agent of this round. It changes what dozens of classes play, and each
  family needs a play check. Not landed in this round; proposed to the coordinator as its own task.
* Item 2: a design choice about the Drakan 2 Wartok models, for the owner.

## 3. Not verified

* The "Woah" itself in the game: the waypoint mumble never fired in three runs (above). The cause is shown from the data
  and the pack's descriptor; the fix is shown by the converter log and the after-pack's descriptor.
* Weapon combat with Rynn armed; a dragon flight; Volcanic scene 9.1 (the other place the mumble plays); every other
  scene's lines (only the opening, 1.2a and the succubi greeting were watched).
* Listening: no sound was judged by ear.
* `kinds.json` lists two clips chosen by eye and name. Other low-confidence lines in `review.txt` may also be sounds
  rather than words.
