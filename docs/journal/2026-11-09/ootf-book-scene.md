# OotF Ruined Village Book: playing the 250 s backstory (on by default since the strip fix below)

Follow-up to [the black-screen fix](ootf-book-black-screen.md). The PC Book plays one sequence, "backstory" (250.5 s: nine paintings
shown and hidden in turn, a camera path, narration, a fade-in at the start and a fade to black at the end), which the PC engine starts
by itself. The level has no Stomp Player, so the converter never converted it.

## What is implemented (formats; first opt-in with `OOTF_BOOK_SCENE=1`, on by default since the fix below, `=0` turns it off)

* `scan()` names the Book's own sequences as if a Stomp Player listed them, so they convert like the Intro's (sounds included).
* `addAutoPlayStomp` (ootfpuzzle.ts) adds a Stomp Player (Initial State On, List Play Order 0) at the camera object.
* The exit timer is 250.7 s (`BOOK_SCENE_SECONDS`) instead of 28 s. Both faders now belong to a played sequence and stay.

## What was verified (private 3-level and 2-level packs, `build/ootf-run/bk2/book*`)

* The sequence runs: `DRAKAN_STOMP_PROBE` ticks `elapsed` from 0 to the end, the paintings change, the camera moves.
* The exit fires at the timer (about 250 s after the load) and `OotF 01 - Ruined Village` loads again (run `book1`, 23 shots).
* The hand-back lands Rynn on the temple platform of Ruined Village (shot `s254` of `book1`). That frame shows a dark silhouette and
  no HUD, which was not examined further.

## What was wrong (first pass; solved below in "Root cause of the strip")

While the sequence plays, the picture shows the scene in a thin strip at the bottom of the frame (about 14 % tall, 76 % wide: x 118..844
of 960, y 462..540) and the rest of the 3D area keeps the previous level's frame (the rocks and sea of the exit prompt), dimmed to
near black (amplified in `build/review/book/book_scene_strip.png`; the top region never changes while the strip follows the camera). It
happens with the Intro in the pack and without it. So the world is drawn into a small rectangle: a scissor or viewport left at the strip,
or a camera setup the renderer reads that way; not a fader (all faders dropped, a bounded hold, Starting / Stomp Fader zeroed, the
`painting back` board dropped: no change).

Without the sequence the same level shows a normal view (grey void, Rynn, HUD), see the earlier note. Until the strip is understood
the default stays at the 28 s void: a 250 s mostly black scene is worse than a short empty one.

## Tried, no effect on the strip

* Faders dropped (`DRAKAN_DBG_DROPNAME='^fader object'`), Start Mode 0 on faders (makes the whole frame black), finite "Faded Time",
  Level Properties `Starting Fader` / `Stomp Fader` = 0.
* Dropping `painting back` (the black board): the strip then shows sky around the painting, the black above stays.
* Treating the Book as a cinematic level for shell trimming and culling.
* The sequence's camera keys equal the PC keys (checked against the converted SSD), FOV 80.

## Next steps (of the first pass; both done below)

* Compare the GS state (scissor, XYOFFSET, display rectangle) in the Book against the Intro while a sequence plays; the Intro's
  camera action is the same (type 9, 1.0, 80), so a Book-only difference (a real player parked beside the exit, the held Rynn actor,
  the camera object class) is the place to look. The hair agent's finding (a held player keeping the 0x80 attach bit) is a candidate.
* Narration audio and subtitles were not checked (the harness runs with audio off).

## Root cause of the strip: the runtime's loading-screen hold, kept by a stale scene camera (fixed, runtime)

Not the level, the camera or the GS. The guest renderer object (`*0x5565c8`, viewport fields `+0x110..0x11c`) reads 0, 0, 512, 512
in Ruined Village and at 4, 16 and 26 s into the Book (RAM dumps, `build/ootf-run/bkfix/d1`). The FPS counter's "World 0.0 fps"
during the scene was the clue: the renderer never saw a world camera in the Book.

* The scene-camera hook (`drakan_scene_camera.cpp`) publishes a world camera only for a draw whose matrix equals the engine's
  world-to-view (terrain and other world-space geometry). The Book has no terrain (`1 layers, 0 cells`) and its paintings are
  objects with their own transforms, so the Book publishes none (`DRAKAN_CAMERA_RESET_DIAG=1`: no world packet after the load).
* The renderer's `sceneCamera` is replaced only by real camera packets; the world-phase markers (`0x139010`) and EndWorld leave it
  alone. So for the whole Book it stayed the loading screen's GUI packet (`0x43d340`, screen 14).
* While that packet is current, `renderDisplay` (d3d11_present.cpp, and wgpu_present.cpp) draws the last HD menu frame over the
  top 85.5 % of the display, because the loading bar lives in the band below. The held frame was the dimmed exit prompt, the band
  was the live Book: exactly the strip (y 1847 of 2160 = 0.855). It also kept `latchedGameWorld` false (the 4:3 pillarbox at a
  960 x 540 window). The Intro has terrain, so its first world camera replaced the packet. Why the Book without the sequence looked
  normal before the fix was not re-checked (some draw of the controlled player or the player camera may match the world camera).

Fix (runtime, `gsr`): the begin of an engine world phase drops a current loading-screen packet (`gsr::isLoadingScreenPacket`, both
backends). The hold still covers the loading screen itself, which draws no world. `docs/reference/widescreen.md` has the rule.

## Second defect at the hand-back: held player (fixed, formats)

With the picture full, the hand-back to Ruined Village came up with no HUD, no control (`gameplay=0` for good) and Rynn a dark
silhouette (`build/review/book/handback-before-release.png`, run `full1`; after: `scene-fixed-handback.png`). The backstory holds the camera and Rynn with run/stop
0 at t = 0 and never gives them back (the PC level ends there); the PS2 engine carries the held player into the restored Ruined
Village, where no scene releases her (the Intro's hold is released by scene 1.1, the Book's is not). `convertSequence` now takes
`releaseHeldAtEnd` (set by the pipeline for the auto-play Book only): every actor whose last run/stop holds it gets run/stop 1 at
the scene's last action (250.5 s, under the closing fade, 0.2 s before the exit timer). Test: `ootfseq.test.ts` "releases the
actors a level-ending scene still holds". Only the Book's SSD entry changes in the full pack.

## Verified (full 23-level pack of the committed converter, no env, private copy `build/ootf-run/play-bookfix`)

Real path: New Game, Intro, Ruined Village, `place` at the staircase exit, the prompt, the modern save, the Book.

* Book frames from 4 to 245 s fill the screen: nine paintings, the camera moves between them (`scene-fixed-sheet.png`,
  `scene-fixed-s35.png`, `-s110`, `-s190`, `-s245`); the 2-level pack shows the same (`scene-fixed-2level-s16.png`).
* The exit fires 250.9 s after the Book's load; Ruined Village loads from its save with `gameplay=1`, HUD orbs drawn, Rynn lit by the
  torch, the mouse turns the camera (`scene-fixed-handback.png`, run `full2`). Run `full3`: turned with the mouse, W walks her off the
  temple platform into the dark stair passage and S brings her back into view (`scene-fixed-handback-move.png`).
* Audio: `DRAKAN_AUDIO_DUMP` records sound through the whole scene (no silent stretch; `full1.wav`). That narration is the right
  speech was not checked by ear.
