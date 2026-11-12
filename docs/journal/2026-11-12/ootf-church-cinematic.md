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

* The stair was not entered the way a player does: the floor needs the book, so Rynn was placed at the exit at the foot of
  the stair after a real walk to the altar. The altar scene "Book 1A" and the floor sliding off were not played.
* The PC `ExitSequence` "Book 1B" (Rynn walking down the stair) and `Entrance Sequence` are not converted. If the owner
  meant that scene, it does not exist in the pack yet.
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
