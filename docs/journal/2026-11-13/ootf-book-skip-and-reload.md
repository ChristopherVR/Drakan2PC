# OotF church Book scene: skipping it with Escape, and subtitles after a load

Owner report (build `6538fffd`, pack of formats `21128e2` with subtitles), Order of the Flame, Ruined Village church:
"the book cinematic scene after walking down the staircase and saving: the scene plays, but if you try to skip it by
pressing Escape, Rynn spawns in an empty world; it doesn't actually skip the scene properly. Reloading also breaks
subtitles."

Private runs, packs, cards and frames: `build/review/ootf-book-skip/` (driver `drive.py`, batches `batch.sh`, frames
shrunk to 1920 x 1080 JPEG per run in `runs/<run>/`, contact sheets in `evidence/`). Nothing from either game is in this
note.

## 1. Setup

* Play folder `build/review/ootf-book-skip/play`: a copy of `build/review/baseline-play` with the owner's current
  `drakan.ini` from `dist/DrakanPC` (only `elf =` changed, to a private ELF copy), the owner's `drakan.exe` and DLLs
  (`d9d538c1...`, the `dist/DrakanPC` exe) and pack (`ootf-levels.zip` `a4cca837...`). Borderless 3840 x 2160, DLSS
  quality, HD textures, audio on (`--audio`), `DRAKAN_SUB_LOG=1`, god mode, a fresh card per run.
* The owner's own `dist/DrakanPC/drakan.log` of that session (read only) shows what he did: the walk-down, the save at
  the exit (slot 1, a save of the Book level), the Book loaded at 378.6 s; at about 110 s into the Book Rynn's weapon
  comes back and the exit touch is ignored (the skip), the pause menu opens and closes, she walks off her arrival spot;
  then Load Game, slot 1, the Book again, a second skip about 54 s in, then Quit.

What a run plays, by real input and by placement:

| Step | How |
| --- | --- |
| New Game, Intro, Ruined Village | the harness (Start / Cross taps until control) |
| The book | **placed** onto the book's spot on the priest's table (the Priest's Key and the locked door are skipped); the pickup itself is the game's (touch, message box dismissed with Cross, inventory checked: 2 items) |
| To the church | **placed** on the grass outside the door, then walked by key input through both doors to the altar |
| Book on the altar, "Book 1A", the floor | real: Tab, Enter, Right, Enter, Tab, E |
| Onto the stair, the walk-down, the save box, the save screen, the Book, the hand-back | real input (closed loop walk; Cross on the save box and the save screen); the scenes are the game's |
| Escape, Enter, Cross, pause menu, Load Game | real key / pad input at the chosen moment |

## 2. How the engine skips a scene

Read from the PAL executable (`analysis/scripts/gfx_dis.py`):

* `0x1a3458` walks the running scenes (list at `+0x234` of the scene manager) and marks each one whose **sequence record
  has flag `0x40`** (`+0x20` of the sequence, the PC sequence's own flags word, copied by the converter) and whose
  instance is not protected (`+8` bit `0x2000`): it sets `0x40` on the instance (`0xc0` when the caller asks for the
  end). A scene without the flag is not skipped; the Start press then opens the pause menu.
* The scene tick `0x1a4c50`, on an instance with `0x40`, stops the scene's running sounds (`0x1b82a8`, the sound stop
  with a fade time named in `2026-10-17/crash-listener.md`, over the list at `+0x4c`; the logs agree: the clip's instance
  loses its voice within a frame of the skip), moves the scene clock to the next "hold" action (type 10) or, with `0x80` or when there is none, to the time
  of the last action, and then **runs every action up to that time in order**: transforms, run / stop, show / hide and
  messages (type 7 sends its message to the actor's own object through `0x171150`). Converted OotF scenes have no type 10
  actions, so a skip always runs the scene to its end at once.
* So a skip ends a scene exactly as watching it does, as far as the scene's own actions go. What it cannot do is shorten
  anything outside the scene that was timed to the scene's length.

PC flags of the scenes involved (`seqflags.ts` over the install, 110 sequences, 43 with `0x40`): "backstory" (the Book)
`0x7c`, skippable; "Book 1A", "Book 1B" (walk-down), "Entrance Stomp" `0x3c`, not skippable; the Volcanic exit scene
"9.9" not skippable; the Intro scenes, the Ruined Village opening 1.1 / 1.2a / 1.3, "arokh awakening", 9.1 to 9.6c,
10.1a skippable.

## 3. Before: what each Escape did (owner's exe and pack)

One run per row, a fresh New Game each (`runs/b-esc-*`, contact sheets `evidence/before/`). "Book t" is seconds after
the Book's loading screen went.

| Stage | Run | What happened |
| --- | --- | --- |
| Walk-down ("Book 1B", 5 s in) | `b-esc-walk5` | The pause menu opens over the stairwell (Save Game, Load Game, Options, Resume, Quit). The scene is not skipped: its PC flags have no `0x40`. The game stays paused for the 40 s watched |
| Save box (the level-change message box, only OK) | `b-esc-savebox` | Nothing. The box stays for the 25 s watched |
| Save screen at the exit | `b-esc-savescreen` | The screen closes (Back is native here: the modern screen sends the native back) and Rynn is in Ruined Village on the pedestal at the foot of the stair with the HUD and control. No level change. The relay is spent and the exit is a point that cannot be touched, so the Book never comes (see section 6) |
| Book, 5 s | `b-esc-book5` | The scene ends at once (the narration stops); Rynn stands in the grey void of the Book with the HUD, 4:3 pillar box, control back (W walks her); the exit touch at her arrival spot is ignored by the arrival guard; the level stays until the level Timer fires at 250.7 s |
| Book, 40 s (twice, `b-esc-book40`, `b-reload-native`) | | Same. The subtitle page that was up stays on screen for 5 to 6 s over the empty level (audio clock 162.3 to 168.1 s in `b-esc-book40`): the pages ran on to their own end although the voice had stopped |
| Book, 245 s | `b-esc-book245` | Rynn in the void for 3.2 s, then the level Timer takes the exit and Ruined Village comes back (pedestal, HUD, control) |
| Hand-back loading screen | `b-esc-handback` | Nothing visible; Ruined Village comes up with control as after a watched scene |
| Book watched to the end | `b-esc-bookload` (the stage was missed, so the run watched the whole scene) | 19 clips with pages, the level Timer takes the exit, back on the pedestal |

The owner's "empty world" is the Book level itself: a grey void with Rynn, released by the skip, for the rest of the 250 s.

## 4. Cause of A

The Book's exit was fired by a level Timer of the scene's length (250.7 s, started at the load), not by the scene. A skip
runs the scene to its end (section 2): the release of Rynn and the camera at 250.5 s (formats `48f6c92`) happens at once,
so she gets control in a level that has nothing but the paintings' boards, and the Timer, which knows nothing of the
skip, still waits for the rest of its 250.7 s. Native Drakan 2 scenes survive a skip because whatever follows them is
sent by the scene itself (its message actions) or by the player walking on; nothing of theirs is timed in parallel.

The walk-down is not affected: "Book 1B" and "Entrance Stomp" are not skippable (PC flags), so Start / Escape opens the
pause menu there, which is the same rule as for any other unskippable scene. That stays as it is, deliberately.

## 5. Cause of B

Run `b-reload-native` (owner's exe and pack, native Load screen, see below): the Book, skipped at 40 s, Load Game, slot 1
(the save made at the exit, which is a save of the Book level), and Yes on the confirmation. The Book loads again and its
scene plays from the start with voice, but **the first nine clips get no subtitle at all** (the log has no `[subtitles]
clip` line for 302ea to 302f2); pages come back only with clip 302f3, 115 s into the scene. Frames:
`evidence/before/b-reload-native.png` (no text until the painting of the hooded man and the bat-winged dragon).

`drakan_clip_subtitles` starts a clip's pages once per play, by the instance's play handle, and remembers the last 64
handles it has looked at (every playing instance, texted or not). Nothing cleared that ring at a load. After the load the
sound system starts over and the new plays come with handles that are already in the ring, so they are taken as "already
started" and never get pages, until enough new handles have pushed the old ones out. The same holds for any load that
puts a voiced scene soon after another one (a quick load, Continue, a death reload).

A second, smaller fault showed in the skip runs: a clip's pages ran on after its sound was stopped (the page over the
empty Book for 5 to 6 s after the skip), and a load during a page would have left it up across the loading screen for
the rest of its time.

## 6. Fixes

**A (converter, formats `ootfseq.ts`, `ootfpipeline.ts`).** The Book's own scene now ends the level: `convertSequence`
takes `endMessages`, and for the auto-play level (the Book) the pipeline adds one actor, "Level Exit", whose `ref` is the
Book's Level Transition (PC 2420) and whose only action is a message at the time of the scene's last action (250.5 s):
`0x1005`, the message the level Timer sent (its Trigger Message 5). It comes after the releases of Rynn and the camera
at the same time. Watched, the exit is now taken at 250.5 s instead of 250.7 s; skipped, the skip runs the scene to that
action at once, so the exit is taken at once. The runtime already takes a non-touch message to the Book's exit as the
Book's end (`drakan_levelchange.cpp`, "got its touch message ... taking it"). The level Timer stays as it was: it can only
matter if the scene never runs. In the full pack only the Book's SSD entry changes (one actor more, `slots` 17 to 18);
every other file is byte-identical to the owner's pack apart from `mod.toml`'s build id.

The walk-down is left unskippable on purpose (the PC's own flags; Escape opens the pause menu and the scene waits). The
save box has only OK; Escape does nothing there, as in every level change of the game.

**B (runtime, `drakan_clip_subtitles.cpp`, `drakan_subtitles.cpp`).**

* At every level load and save restore (lifecycle `BeforeLevelLoad`, `BeforeSaveRestore`) the handles already looked at
  are forgotten and a clip's page that is up is taken down (`stopClip`; the intro / outro narration is left alone).
* A clip's pages also end when its sound stops: the HUD layer looks each frame whether the instance still holds the
  play's handle with a voice, and ends the pages once it has not for 120 ms (`ClipWatch`). A skipped scene's line no
  longer stays over the next picture, and a page cannot outlive its voice.
* `DRAKAN_SUB_LOG=1` now logs the instance and handle of each clip and why pages ended.

Tests: `drakan_subtitle_toggle_test` has a new case (a stopped sound ends the page only after 120 ms gone; a load ends a
clip's page and leaves the narration; handles are forgotten); `ootfseq.test.ts` has "sends the end messages at the last
action, after the releases, so a skipped scene sends them too".

## 7. Cancelled save at the exit, and a save made during the walk-down (found here, not fixed)

Escape (Back) on the save screen at the stair's exit returns to Ruined Village without the level change, as on any exit
of the game. On a disc level the player would walk into the exit again; here the exit is a point that cannot be touched
(the relay shrinks it so the walk-down does not touch it) and the relay has played, so the Book is not offered again by
walking. Stepping on the stair-top detector again (PC 3161, which started the relay) does nothing (section 8). The
campaign goes on from the foot of the stair, which is where the hand-back would have put her, so nothing is blocked; the
backstory is missed for that game.

The pause menu offers Save Game during the walk-down. Loading such a save (run `a-walksave`: saved 5 s into the
walk-down with the native Save screen, then the chain, the Book, the hand-back, then Load Game of that save): Ruined
Village loads with Rynn at the stair top, the rest of the walk-down plays (she walks to the book, the pages, the fade),
then she has control at the foot of the stair with the HUD, and neither the save box nor the Book comes in the 47 s
watched. Same outcome as the cancelled save: the relay's exit Timer is not running after the restore.

Both would need the exit to be offered again: for example a detector at the foot of the stair, switched on by the end of
the relayed scenes and linked to the exit, so that a player standing there after a cancelled save or such a load walks
into the save box again (as at any disc exit). Not built here.

## 8. After: the same moments on the rebuilt exe and pack

Exe: `Build-LocalGame.ps1 -Incremental` of `0fb03d2d` plus the runtime change (committed as `2653a797`), into the
private play folder (`8c45db35...`); it is newer than the owner's exe (`6538fffd`): it also has the renderer commits
`dc6d3fa4` to `0fb03d2d`. Pack: formats `3231732` converted with the same subtitle files (`--subtitles
"english2.json;corrections.json"`), plus the 12 preview pictures `extras.py` adds, taken from the owner's pack
(`121adb39...`). Same settings, same chain, one fresh New Game per row (`runs/a-*`, `evidence/after/`).

| Stage | Run | What happened |
| --- | --- | --- |
| Book, Escape at 5 s / 40 s / 245 s | `a-esc-book5`, `a-esc-book40`, `a-esc-book245` | The scene ends, the Book's exit is taken by the scene's message ("got its touch message ... taking it"), the loading screen within 0.3 s (1.5 s at 245 s), Ruined Village with control 0.5 to 5.9 s after the key: Rynn on the pedestal at the foot of the stair (279392, 14672, 152908), HUD, 16:9, W walks her off into the passage. Rynn never has control in the Book (position trace). The line on screen goes with the voice ("clip pages ended: the clip's sound stopped") |
| Book, Cross at 150 s, Enter at 100 s, pad Start at 200 s | `a-cross-book150`, `a-enter-book100`, `a-start-book200` | The same as Escape |
| Escape as the Book appears (its loading screen lasts a fraction of a second; the key landed 0.3 s into the scene) | `a-esc-bookload` | The same |
| Hand-back loading screen, Escape | `a-esc-handback` | Nothing visible; the watched scene's exit (taken 250.1 s after the Book appeared, by the scene's message) and Ruined Village with control, as before |
| Book watched to the end | `a-watched` | 19 clips with pages, the exit taken by the scene's message at its end, back on the pedestal |
| Walk-down, Escape at 5 s, Escape again 4.6 s later | `a-esc-walk5r` | Pause menu, then Resume; the walk-down goes on where it was (Rynn walks to the book, its pages, the fade), the save box comes 4.7 s later than without the pause (the relay's Timer waits with the game), then the save and the Book as usual |
| Save screen at the exit, Escape | `a-esc-savescreen` | As before: Ruined Village at the foot of the stair, no level change. Then Rynn placed on the stair-top detector: nothing happens in 25 s. The Book is not offered again (section 7) |

Subtitles after a load, new exe:

* `x-reload-native-oldpack` (new exe, the owner's old pack, so the skip still leaves Rynn in the Book, which gives the
  owner's exact path): skipped at 40 s, Load Game, slot 1 (the exit save, the Book). Before the load the clips had
  handles `80000281` to `80000284`; after it the same clips came back with `80000281`, `80000282`, ... (the counter is in
  the save). All of them now got their pages; every clip of the replayed scene to the end of the watch (302ea to 302f4)
  shows text. In normal play every clip's last page ended on its own cue before its sound's end was seen, so ending a
  page with its sound cuts nothing short.
* `a-reload-native` (new exe, fixed pack): after the skip and the hand-back, Load Game, slot 1 now loads Ruined Village
  at the foot of the stair, because the hand-back writes the slot as it does after a watched scene. The page that was up
  was taken down at the restore ("clip pages ended: a save is loaded").

More rows of the "after" runs:

| Case | Run | What happened |
| --- | --- | --- |
| Save during the walk-down, loaded after the hand-back | `a-walksave` (`evidence/after/a-walksave-load.png`) | Section 7 |
| Back on the save screen, then Rynn placed on the stair-top detector | `a-esc-savescreen` | Section 7 |
| F5 at the altar, F8 30 s into the Book | `a-quickload-book` | Not a test: the scripted F5 / F8 never reached the game's quick save / quick load (no "quick save requested" line, which the owner's real key gives). Later Cross taps of that run skipped the Book and handed back as above |

## 9. Other scenes, and Drakan 2

What can strand a player after a skip is something outside a scene that is timed to it. In the converted campaign the
converter makes such Timers in three places: the Book (fixed here), the Intro hand-over (41.4 s; the Intro movie
covers the Intro, and Escape on the movie fires the Intro's exit itself, `2026-11-09/ootf-intro-movie.md`) and the exit
relay, which is only built for exit scenes that are not skippable (the walk-down; Volcanic's "9.9" is excluded and is
not skippable either). Every other skippable scene (the Ruined Village opening 1.1 / 1.2a / 1.3, "arokh awakening", the
Volcanic 9.x scenes, the Rift World 10.1a, the Islands, Alwarren and Succubus Lair scenes) runs on the PC level's own
logic: what follows them is sent by the scene's messages, which a skip still sends.

Played here: the Intro and the Ruined Village opening, skipped by the harness's Start taps in every run of this note
(about 40 New Games), each time ending in Ruined Village with control. Not played: "arokh awakening" and the way to
Wartok Canyons, the Volcanic ship scenes, the Rift World ending.

Drakan 2, new exe (`d2.py`, `runs/d2-*`, `evidence/after/d2-*`):

* Level 1A Surdana's opening scene (voice, the engine's own subtitle bar): Escape 20 s in, the scene
  ends, Rynn under control with the HUD in the meadow; one subtitle line was shown before the skip.
* Level 0, the Intro (Arokh's flight, with the intro narration track): Escape 20 s in, the loading screen 0.8 s later,
  Level 1A loads and its opening plays with its subtitle bar; no text of the Intro is left over.
* A third run (`d2-opening`) pressed Escape after the Surdana opening was already over and only opened the pause menu;
  it is not a skip test.

## 10. Tests and builds, as they ran

* formats (`editor`): `vitest run packages/formats`: 61 files, 620 tests passed (with the new case); `tsc -p` of
  `packages/formats` clean; prettier clean on `ootfseq.ts` and the test (`ootfpipeline.ts` was not prettier-clean before
  this change and was left as it was).
* Runtime: `drakan_subtitle_toggle_test` and `subtitle_render_test` built in a private `runtime-tests` tree
  (`build/review/ootf-book-skip/rt`, those two targets only) and pass. The full `ctest --preset runtime-tests` was not run.
  clang-format 19 clean on the six touched C++ files.
* `python scripts/run_tests.py`: 561 tests OK (2 skipped). `scripts/repository_guard.py`: 0 prohibited.
* Game: `Build-LocalGame.ps1 -Incremental -Jobs 8 -NoWarnings` into the private play folder, exit 0.
* Pack: full 23-level conversion of the formats working tree (now `3231732`) with the subtitle files, exit 0.

## 11. Not verified, or only inferred

* Placed, not played: the Priest's Key and the priest's door (Rynn is placed onto the book on the priest's table; the
  pickup is the game's touch), and the way from the priest's house to the church door (placed outside the door).
* Quick save and quick load (F5 / F8): the scripted keys did not reach them, so a quick save before the stair, a quick
  load during the scene and a quick load while a line shows were not tested. A load while a line shows was tested with
  Load Game (`x-reload-native-oldpack`, the page went at the restore).
* The modern Load screen does not take the harness's keys; the reload runs used the native Load screen
  (`DRAKAN_SAVE_MENU=native`). The restore underneath is the same engine call, so the subtitle reset applies to both, but
  the owner's exact screen was not driven.
* Quit to the title and Continue, and Alt+F4 and a restart, were not tested.
* "arokh awakening" (to Wartok Canyons), the Volcanic ship scenes and the Rift World ending were not played; that a skip
  cannot strand the player there is read from the data (no converter-made Timer beside a skippable scene) only.
* A save made during the walk-down and Back on the exit's save screen both lose the Book for that game (section 7);
  not fixed.
* That the handle counter comes back with the save is inferred from the logged handles (the same values before and
  after the load); the save format was not read.
* One baseline run (`b-esc-walk5r`) crashed in the NVIDIA driver (`nvwgf2umx.dll`, read of address 0x28) 45 s in, during
  the Ruined Village opening, before any step of this test; the same case on the new exe ran through. Not examined
  (renderer).
* The 4:3 picture after a skip was checked in frames only for the Book (pillar box until the exit) and Ruined Village
  (16:9 again); other picture levels have none.
