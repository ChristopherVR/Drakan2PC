# OotF Ruined Village: the church stair "cinematic" (framing and subtitles)

Owner report from play of the Order of the Flame campaign: "we know there are issues with entering the church staircase
that plays a 'cinematic' scene in the game: the cinematic is not rendered to scale properly and no subtitles."

Private runs, packs and frames are under `build/review/ootf-church/` (nothing from either game is in this note).

## 1. Which scene

The church is the temple of Ruined Village (PC objects `Outer Temple` / `Inner Temple`, altar 873 at 279573, 20241,
151741). The PC level has four things around its stair:

| PC object | What it does |
| --- | --- |
| `altarlock` 1739 (a lock; the key is the `flamebook` 894 from the priest's house) | Used with the book: Timer 3160 plays sequence 1052 "Book 1A" (3.1 s, Rynn and the camera), the `floortemple` mover 2074 slides off the stair, the cogs turn |
| `Creature` detector 3161 on the stair's top landing (280256, 19410, 153698) | Rynn in it: Timer 2847 sends On to the level exit |
| `Level Transition` 2467 at the foot of the stair | Goes to `Ruined Village Book.lvl`; PC `ExitSequence` 1054 "Book 1B" (19.7 s, Rynn walks down, a fader) and `Entrance Sequence` 1116 |
| `Stomp Player` 2465 | Class default list (sequence 680 "The Book", 19.8 s); nothing in the level links to it |

So walking onto the stair is the walked exit into the **Book level**, whose one sequence, "backstory", is the 250 s
narrated slide show of nine paintings (the scene of the [2026-11-09 Book note](../2026-11-09/ootf-book-scene.md)). That is
the owner's "cinematic": it is the only scene on this path that plays for more than a moment in the converted pack. The
converter does not convert `ExitSequence` / `Entrance Sequence` (no code reads them), so "Book 1B" does not play; that is
a separate, still open gap (section 7).

## 2. Reproduction (owner's exe, installed pack, owner's `drakan.ini`)

Play folder `build/review/ootf-church/play`, a copy of `build/review/baseline-play` (exe sha256 `4fddfbc8...`, the same file
as `dist/DrakanPC`, installed `ootf-levels.zip` `80e71df5...`), `drakan.ini` unchanged except `elf =` (a private copy):
borderless 3840 x 2160, `aspect = auto`, `fov = 99`, `hud_aspect_correct = 0`, DLSS quality, HD textures, audio on.
Driver `build/review/ootf-church/drive.py` (on `scripts/harness/live_drive.py`): New Game by `DRAKAN_GAME=d1`, the Intro,
Ruined Village (the opening skipped by the harness's Start taps), then:

* **Real play:** Rynn is placed on the grass outside the church door (279560, 19700, 163800) and walked by closed-loop
  key input through the outer and inner doors to the altar (frames `a00` to `a04`).
* **Not real play:** the floor over the stair opens only with the book on the altar, and the book needs the Priest's Key
  from the cave under the tavern. Two attempts to take the book from a test pack that moved it outside the church
  (`OOTF_TEST_MOVE`) failed (E did not pick it up; not investigated). So from the altar Rynn is **placed at the exit object
  at the foot of the stair** (279392, 15300, 152908), as the 2026-11-09 runs did. From there everything is the game's own:
  the message box, the modern Save screen, the load of the Book, the scene, the hand-back.

Run `before1`: 132 frames at 2.5 s steps through the whole scene (`runs/before1/sheet_scene1.png`, `sheet_scene2.png`).

* **Framing.** The scene fills the 16:9 screen (the 2026-11-09 strip is gone), but the picture is wider than the paintings:
  in most frames a black area stands on the left or the right of the painting, and it changes side and width as the camera
  pans (12 % each side on the first shot, about 25 % on one side in others). The FPS counter reads "World 0.0 fps": the
  Book has no world camera, so DLSS and the player's `fov` do not act on it.
* **Subtitles.** None in any of the 132 frames. The narration is audible in the dump (17 of the 19 clips are found in the
  later run's audio dump by cross-correlation with the PC clips).

## 3. Cause of the framing

Measured, not guessed:

* The nine paintings are flat squares of 1.27 lu (about 2,600 world units) and the camera looks straight at them from 640
  to 1,880 units, panning (43 camera keys).
* The PC plays every sequence at the level's `Default Field of View`, 80 degrees horizontal, on a 4:3 screen (all 13
  levels say 80; the PC sequence format has no field of view). At 80 degrees and 4:3 the first shot shows 2,662 units of
  width at the painting: the painting (2,600) fills the frame. Over the 43 keys the 4:3 view stays within a few percent of
  the painting's edges (worst key 18 %, at a cut).
* The port renders a cut scene at its authored field of view (the player's `fov = 99` applies only to the free player
  camera, mode 6 without the scripted flag; `[fov] scale=1 mode=1` in the Book) and widens it horizontally to the window.
  At 16:9 that is 3,550 units at the first shot: 950 units of nothing beside the painting, and up to 26 % of the frame
  past an edge over the keys. In the `before1` first frame the painting spans 75 % of the width, as this predicts (73 %).

So the scene is a set of flat pictures framed for a 4:3 screen, shown with a 16:9 view. A native Drakan 2 cut scene is a
3D set, where the wider view is correct; the port's own movies and menus, which are flat 4:3 pictures, are pillarboxed.

## 4. Cause of the missing subtitles (campaign-wide)

* **The PC game has no subtitle text.** A STOMP sound action is `ref, duration, volume, flags`; the PC sound descriptors
  hold a name and samples; `Dragon.rrc` has 425 UI strings (9 longer than 80 characters, the hint boxes). No file of the
  install holds the spoken lines. So the converter had nothing to convert, for any scene.
* **The engine only subtitles speech controllers.** Test pack with a text on all 617 converted sounds: in the Ruined
  Village opening the engine's bar came up for exactly the two lines the player speaks (`[subtitle-set]` twice) and for no
  other actor; in the Book (all 19 clips are played by the camera actor) never. `SetSubtitle` is called from an actor's
  speech controller, and a scene camera or a prop has none.

Both hold for every OotF scene: 451 clips are played by the sequences of the install (16 sound banks), none had text. The
voices themselves play.

## 5. Fixes

**Framing (runtime + converter).** A pack manifest can say `level_aspect = ["Level title = 4:3"]`. While that level is
loaded the runtime renders and presents its world at that aspect when the window is wider (pillarboxed, like a movie;
`drakan_widescreen` `levelWorldAspect`, cleared when the next load begins, so loading screens are laid out as before).
The converter writes the entry for a level whose own sequence the PC engine plays at the load (the Book). No renderer
backend file changed: the existing `setAspect` path is used. See `docs/reference/widescreen.md`.

**Subtitles (tool + converter + runtime), general for the campaign.**

* `analysis/scripts/ootf_subtitles.py` exports the clips the PC sequences play (`editor/scripts/ootf-speech.ts`, new
  `listSequenceSounds`) and transcribes them on the player's machine (faster-whisper `small.en`, CPU, about 25 minutes for
  the 34 minutes of clips here). It writes an engine-neutral subtitle file: one line per clip (PC sound bank and id), timed
  cues of at most 84 characters cut at sentence ends, pauses and commas. A filter drops what a speech model writes for
  effects (repeated words, the context names read back, spelled noises, single words the clip's name does not confirm):
  344 transcripts, 298 kept.
* `ootf-convert.ts --subtitles <file>` stores each line in the clip's sound descriptor as timed pages
  (`[[0.4]]text[[3.1]]text[[6]]`) and ships the lines used as `story/subtitles.json`. In the pack built here 289 of the 617
  converted sounds carry text (19 Book, 41 Ruined Village, 73 Islands, 37 Volcanic, ...).
* The runtime (`drakan_clip_subtitles`) starts a clip's pages when its sound instance takes an SPU2 voice, whichever actor
  plays it, and draws them in the narration subtitle band on the audio clock. The engine's own bar is not given marked
  text. `[subtitles] narration` and the game's Show Subtitles option switch them. See `docs/reference/subtitles.md`
  section 10 and `docs/reference/modding.md`.

Text and timing are data in the pack; nothing is hard-coded per scene, and no game text is in the repository (the subtitle
file lives under `build/`).

## 6. Proof

Exe: an incremental `Build-LocalGame.ps1` of the working tree into the private play folder (`dae16f3d...`). It contains
other sessions' uncommitted runtime work that was in the tree (DLSS, VU1 files). Pack `packs/fix` (`91185ecc...`): a full
23-level conversion of formats `15c2899` plus this change, with the subtitle file. Same driver, same entry, owner's ini.

| Run | Display | Frames looked at | Framing | Subtitles | Hand-back |
| --- | --- | --- | --- | --- | --- |
| `before1` (owner's exe, installed pack) | 3840 x 2160 borderless | 113 scene frames at 2.5 s | 16:9, black beside the painting on one or both sides in most frames | none | Ruined Village with HUD, `gameplay=1` |
| `after-4k` | 3840 x 2160 borderless | 122 frames at 2.5 s | 4:3 picture centred (x 480 to 3360), the painting fills it; the bars do not move | 19 clips, 53 pages logged; a page is on screen in the frames between the first and last line except in the pauses between lines | exit 256 s after the scene began, 16:9 with HUD, `gameplay=1`, W moves Rynn |
| `after-43` | 1280 x 960 window | 120 frames at 2.5 s | the painting fills the window | 19 clips, 53 pages | exit after 260 s, HUD, `gameplay=1` |
| `rv-open-after` (other scene: Intro and Ruined Village opening 1.1) | 3840 x 2160 | 40 frames at 3 s | unchanged: Intro as before, opening 16:9 full screen | the dialogue lines of Rynn and Delon now show in the band (4 clips, 7 pages in the captured 45 s); the engine's own bar no longer draws for Rynn's lines | not reached (run ends in the scene) |

Evidence (private): `build/review/ootf-church/evidence/` (`before/`, `after-4k/`, `after-43/`, `rv-open/` contact sheets and
single frames, `after-4k/subtitle_log.txt`, `timing/`); every frame is in `build/review/ootf-church/runs/<run>/`.

Timing of the pages against the voice:

* On the audio clock, in a direct start of the Book with `DRAKAN_SND_LOG=3` (`runs/kon`): for each of the five clips of the
  first minute the pages start at the SPU2 frame of the clip's voice key-on (difference 0.000 s at the log's 10 ms
  resolution).
* In the `after-4k` audio dump 17 of the 19 PC clips are found by cross-correlation, in order. The dump's own timeline is 3.5
  to 5.5 s ahead of the audio clock and the gap grows through the run, so it cannot time the pages (taken to be output
  written while the game stalls; not examined further).
* A first `after-4k` attempt (`runs/after-4k-loaded`) ran while four other game instances shared the GPU: the scene took 315 s
  instead of 250 s. Scene time against wall time in 41 s direct starts on the same loaded machine: 0.979 (new exe, fixed
  pack), 0.982 (new exe, fixed pack, clip subtitles off), 0.994 (new exe, installed pack), 0.994 (owner's exe, installed
  pack), with the probe's 1 s sampling; so no slowdown is attributed to this change, but the 1.5 % difference is not
  explained either.

Tests and builds, as they ran:

* `editor`: `vitest` on `packages/formats` and `packages/editor-core` of a snapshot (HEAD plus this change): 961 passed, 2
  failed (`guides.test.ts`, `uiskin.test.ts`: they read repository files the snapshot does not hold). New:
  `ootfsubtitles.test.ts` (11), a `level_aspect` case in `campaign.test.ts`.
* Runtime tests built in a private tree (`cmake --preset runtime-tests -B build/review/ootf-church/rt`, four targets only):
  `widescreen_policy_test` (4600 checks), `drakan_subtitle_toggle_test`, `subtitle_render_test`, `mod_test` (228 checks)
  pass. The full `ctest --preset runtime-tests` was not run.
* `python scripts/run_tests.py`: 557 tests OK (2 skipped), with the new `test_ootf_subtitles.py` (9). `ruff` clean on the
  new Python files; `clang-format` 19 clean on the touched C++ files; `scripts/repository_guard.py`: 0 prohibited.
* Game: `Build-LocalGame.ps1 -Incremental` exit 0 (before the last formatting-only edit of two files).

## 7. Not verified, or only inferred

* In this first pass the stair was not entered the way a player does: the floor needs the book, so Rynn was placed at the
  exit at the foot of the stair after a real walk to the altar. Section 9 plays the altar, the floor and the stair.
* (Done later the same day, section 9.) The PC `ExitSequence` "Book 1B" (Rynn walking down the stair) and `Entrance
  Sequence` were not played by the pack of this first pass.
* How the PC original looks was computed from the camera keys, the painting models and the level's field of view; the PC
  game was not run. A few shots still show a painting's edge inside the 4:3 frame, which the keys predict for 4:3 too.
* Subtitle text is machine transcription of the English voices, not checked line by line: names can be misspelled, and a
  quiet line can be missing (298 of 451 clips have text; most of the rest are effects, but some real short lines were
  dropped by the filter, for example one-word shouts whose clip name does not say the word). German or other installs
  need `--model small` / `--language`; not run.
* Timing is proven on the audio clock (pages start with the voice key-on) and by eye in frames, not by an end to end
  measurement against the speakers: the audio dump's timeline gains seconds over the audio clock when the game stalls, so
  the dump could only show that the clips are audible.
* Only the Book and the Ruined Village opening were watched with subtitles. The other scenes get their text by the same
  path and were not played.
* The guided builder (`DrakanPC-Setup.exe`, `Build-Drakan.cmd -OotfSource`) does not run the subtitle tool yet: a pack it
  converts has the 4:3 Book but no subtitles until the tool is run and the file passed with `--subtitles`.
* The installed pack in `dist/DrakanPC` is unchanged; it needs a regenerated pack and the new exe.
* The owner's Book save (`BESCES-50006Slot1/OotF 02 - Ruined Village .rsg`) was refused by the native Load screen in one
  harness run (message box 0x105); not investigated here.

## 8. Follow-up: can the Book fill a wide screen?

Owner: "Can the converter rebuild the book frames to support wide screen so we can avoid the black pillar boxes. If not
all good." Three candidates were built on the real scene (frames of `after-4k` and `before1`, 104 frames each, and real
runs for the first), all in `build/review/ootf-church/evidence/` (`wide_crop_all.png`, `wide_ambient_all.png`,
`wide_mirror_all.png`: every 2.5 s of the scene; `wide/sNNN_compare.png`: the pillar box beside the three candidates at
3840 x 2160 for nine paintings; `wide/sNNN_<candidate>.png`: the full-size pictures).

| Candidate | What it does | What the pictures show | Verdict |
| --- | --- | --- | --- |
| 1. Fill by cutting (`level_aspect = "... = 4:3 fill"`) | The 4:3 authored width fills the window; a quarter of the height (an eighth at the top and at the bottom) is cut at 16:9 | Real runs at 16:9, 21:9, 32:9 (`wide/fill_real_169_219_329.png`). No painting edge comes into frame that the 4:3 picture does not show (the width is unchanged). Cut: the top of the rider's head on the first painting, the hood tops of the two hooded men, the top of the mage's hood and spikes, the dragon's head on the first pan; faces stay. The subtitle band lies over the lower picture | Works, costs picture. Shipped as an opt-in pack setting, not the default |
| 2. Side fill from the picture | The 4:3 picture as authored; the bars show a blurred, darkened, enlarged copy of the same frame | Mock-ups only. Nothing is cut; the bars stop being black but stay quiet (the paintings are dark, so the fill is a faint brown haze) | The best looking one, cannot be landed now: it is a present pass in the renderer backends, which are being restructured |
| 3. Mirrored painting edges | The black beside the painting in the old 16:9 view filled with the painting mirrored at its edge | Clear seams and doubled figures: two riders back to back, mirrored skulls, twin dragon heads | Worse than the pillar box. Rejected |

A fourth idea came out of the pictures: the old 16:9 view shows more of the real painting whenever the camera is close (a
second dragon head, a boar-headed knight), and black only where it runs past the edge. Widening each shot as far as its
painting allows would need the painting bounds at run time or camera keys rebuilt per aspect; not built.

Recommendation: keep the pillar box as the default. Nothing of the authored pictures is lost and the bars are steady. The
fill mode is there for players or packs that prefer a full screen (`OOTF_PICTURE_FILL=1` at conversion, or the manifest
entry by hand). The side fill would be the better default once the backend work has landed.

Hook needed for candidate 2 (not landed, no backend file touched): in the present pass of both backends, when the
presenter is given a forced aspect narrower than the window and a new flag `ambientBars` (set from `drakan_ws::hostTick`
beside `setAspect`, policy in `drakan_widescreen_policy.h`), draw the frame first as a full-window quad scaled to cover
(the existing present shader with a 1/8 resolution intermediate, a 9 tap blur and a 0.35 gain), then the normal
pillarboxed quad over it.

What landed: `detail::levelFraming` (policy, unit tested at 4:3, 3:2, 16:9, 21:9, 32:9: zoom, shown aspect, the authored
width kept, never more than a quarter cut, a 4:3 window unchanged), the `fill` word in `parseLevelAspect`, the zoom in
`hookRenderWorld`, `LEVEL_ASPECT_PATTERN` in the editor. Real captures: 16:9 full screen, 21:9 (3440 x 1440) and 32:9
(3840 x 1080) show a 16:9 picture with bars (`[ws] push ... present aspect 1.778`).

## 9. Follow-up: the walk down the stair ("Book 1B") and exit / entrance sequences in general

Every Level Transition of the 13 campaign levels was read (`pcTransitionSequences`, object parameter or class default):

| Level | Transition | Field | Sequence | Length | Now |
| --- | --- | --- | --- | --- | --- |
| Ruined Village | 2467 (to the Book) | ExitSequence | "Book 1B" | 19.7 s, no voice | plays before the exit (seen, below) |
| Ruined Village | 2467 | Entrance Sequence | "Entrance Stomp" | 0.0 s (a timer message that ends the fade, Rynn released and shown) | plays right after the exit scene, before the save (see below) |
| Ruined Village Book | 2420 (back) | ExitSequence (class default) | "backstory" | 250.5 s, 19 voice clips | already played at the load by the 2026-11-09 work; left as it is |
| Volcanic | 1237 (to Rift World) | ExitSequence | "9.9" | 31.4 s, no voice | not played: relayed, it stalled the game at the rift box (below); left out of the relay |

The other 17 transitions (Wartok Canyons 2, Grimstone Mines 3, Grotto 2, Islands 3, Alwarren 1, Rift Crystal 1, Islands
Night 1, Succubus Lair 2, Ruined Village 1077) name no sequence. The sequences themselves were already in the pack (the
resource scan sees the two fields); what was missing is anything that plays them.

Why not the PS2 exit's own `ExitSequence` / `Entrance Sequence` fields: the converted exits are of a class in SYSTEM,
whose database has no SSD and does not list the level's, so a ref cannot resolve (the converter says so if asked to), and
the PS2 engine opens its save box the moment an exit fires. So the scenes are relayed (formats `ootfexitseq.ts`
`relayExitSequences`), as detector scenes already are:

* whatever fired the exit by link (in Ruined Village: Timer 2847 behind the stair-top detector 3161, and the converter's
  own walk-in columns under the exit) now starts a Stomp Player with the exit scene and then the entrance scene, and a
  Timer of their length (plus 0.25 s) that sends the exit its On;
* the exit's touch volume is shrunk to a point: the PC transition leaves only on a message, while a PS2 exit also fires on
  touch, and the scene walks Rynn onto it (a first run without this showed the save box as soon as she reached the foot
  of the stair, with no scene);
* the entrance scene cannot wait for the way back: the PS2 saves at the exit and that save is what the Book hands back, so
  whatever the exit scene left (the fader, the held player) has to be undone before the save. On the way back the level
  is the restored save: Rynn at the foot of the stair with the HUD.

The Book level is left out of the relay (its exit scene is the scene it already plays).

The book pickup is not a bug: at its real place in the priest's house Rynn takes "Atimar's Book" by walking to it (no E),
and it is in the inventory. The 2026-11-12 attempt failed because the test pack had moved the book 370 units above the
ground outside the church, out of her reach.

**The chain, played** (run `chain-4k`: exe of the working tree, pack `packs/fix6` = formats `db1a9c3` plus this change with
the subtitle files, owner's ini, 3840 x 2160 borderless, audio on, god mode; 204 frames, a position trace five times a
second in `pos.jsonl`; contact sheets `evidence/chain/sheet_1_book_altar_walkdown.png`,
`sheet_2_transition_book_return.png`):

| Step | Played or placed | What the frames and the trace show |
| --- | --- | --- |
| New Game, Intro, Ruined Village | played by the harness (Start taps skip the opening) | control in Ruined Village |
| The book | **placed** inside the locked priest's house beside the table (the Priest's Key from the cave and the door lock were skipped); the pickup itself is real: she walks to the table and has "Atimar's Book" | `a00a`, `a00b`, the inventory frame |
| To the church | **placed** on the grass outside the door, then walked through both doors to the altar by key input | `a00` to `a03_altar` |
| Book on the altar | real: inventory (Tab, Right, Enter equips the book), E at the altar | "Book 1A" plays for about 3 s (scene camera, HUD off, the floor sliding), then control returns |
| Onto the stair | real: walked along the altar front and back onto the stair's top landing (the floor is gone; she drops onto it) | detector 3161 fires at 87.7 s; control is taken |
| Walk-down "Book 1B" | the game's own | 20.7 s: the stairwell with the pedestal, Rynn at the book, the book's pages, fade to black; framed 16:9 full screen like any 3D scene; it has no voice, so no subtitles |
| "Entrance Stomp" and the exit | the game's own | Rynn released at 108.4 s at the exit; the save message box 0.2 s later (over the still dark picture), the modern Save screen, the load |
| The Book | the game's own | 4:3 pillar box, 19 clips and 55 pages of subtitles logged, the exit 305 s after its start (the machine ran four other game instances; 250 s unloaded) |
| Back | the game's own | Ruined Village from the save: Rynn on the pedestal at the foot of the stair, HUD, `gameplay=1`; nothing re-triggers in 40 s; W walks her off into the passage below |

Earlier fixes on this path, seen again in this run: the Book full screen and on by default (`09ff9e11`), ended by its own
timer (`f63cbc34`), Rynn and the camera released at its end (formats `48f6c92`); the level-exit hold (`aca2cbfc`) does not
swallow either transition: the Ruined Village exit is fired by the relay's timer long after Rynn left her arrival spot,
and in the Book the log shows the hold ignoring one touch at the arrival (`exit ... touched before the player walked off
her arrival spot: ignored`) and the Book's own timer message taking the exit at the end (`got its touch message 317 units
from the player: taking it`).

**Volcanic.** With the relay in Volcanic too (pack `fix5`), the route harness played the level up to the remount with the
Runeblade (`runs/volc1`, every step passed) and the game then stalled right after Arokh was placed in the rift box
(`[stall] guest input boundary silent`, the main thread at `0x80000080`), where packs without the relay give the message
box. So the Volcanic exit scene "9.9" (31.4 s, Arokh into the rift with Rynn mounted) is **not converted**: the level is
excluded (`EXIT_SCENE_SKIP`, `OOTF_EXIT_SCENES=all` relays it for a test), the converter says so in its log, and the
Volcanic level file of the final pack is byte-identical to the one of a pack without any relay. Why the scene stalls was
not looked into.

Not verified in this follow-up: the Priest's Key and the priest's door (skipped by the placement); the walk from the
priest's house to the church; a player who jumps into the open stairwell instead of taking the stair (the converter's
walk-in columns under the exit then start the same relay; not played); the same chain in a 4:3 window; a save made during
the walk-down scene; a second visit of the stair after the Book (the relay's Stomp Player plays once and is gone, so the
exit should stay shut, as on the PC where the transition is one way; not played). The first seconds of "Book 1B" show
the stairwell with Rynn hard to make out in the frames; whether she is seen walking down as on the PC was not checked
against the PC game.


## 10. Follow-up: subtitle review list, corrected files, speech models

* **Review list.** The text is game text, so the list itself is private: `build/review/ootf-church/subtitles/review.txt`
  (152 lines of text: the Book's 19 clips with their 53 pages, and the 35 lines the model was unsure of). By clip (sound
  bank, id), confidence = the model's mean log probability:
  * Book (`ruin book`): ids 9 to 27, confidence -0.13 to -0.27, all to be read once against the voice. Known: id 14 has
    "bloody decision", most likely "bloody deception"; corrected in the private `corrections.json` used for the proof pack
    (by reading, not by ear).
  * Unsure (confidence below -0.45): `islands` 258, 259, 279, 304, 312, 315, 327, 354, 365, 367, 371; `grimstone mines`
    269, 280, 281, 282, 287, 289, 299; `riftworld` 10, 12, 13, 15, 42; `volcano` 26, 28, 35, 53; `alwarren` 228, 251;
    `islands night` 394; `ruined village` 334; `wartok canyons` 306; `fx` 891; `npcs` 313; `system` 331.
* **A corrected file wins.** The converter takes several files (`--subtitles "fresh.json;corrected.json"`,
  `mergeSubtitleFiles`: a later file's line replaces an earlier one's, an emptied line removes the subtitle), and the tool
  keeps the lines of an existing output file and only transcribes clips it does not hold (`--retranscribe` starts over).
  A second transcription of all 451 clips gave the same 298 lines as the first.
* **Speech models.** The earlier report did not list them. Both were already in the user's Hugging Face cache
  (`~/.cache/huggingface/hub`, folder dates 2026-10-17, from the October narration work); this task downloaded no model.
  It did install Python packages into a scratch venv without listing them first
  (`build/review/ootf-church/venv`, 292 MiB: faster-whisper 1.2.1, ctranslate2 4.9.0, onnxruntime 1.31.1, av 19.0.2,
  tokenizers 0.23.3, huggingface_hub 2.4.0, numpy 2.5.4 and their dependencies, from PyPI).

  | Model | Source | Licence | Size | Used here |
  | --- | --- | --- | --- | --- |
  | faster-whisper-small.en | `https://huggingface.co/Systran/faster-whisper-small.en` (CTranslate2 conversion of OpenAI Whisper small.en) | MIT | 464 MiB | yes: the transcription and the timing check |
  | faster-whisper-base.en | `https://huggingface.co/Systran/faster-whisper-base.en` | MIT | 141 MiB | no (present in the cache) |

  The tool now names a missing model with source, licence and size and stops; it downloads only with `--allow-download`
  (`BUILDING.md` has the table).
