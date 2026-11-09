# OotF Ruined Village Book: playing the 250 s backstory (opt-in, not finished)

Follow-up to [the black-screen fix](ootf-book-black-screen.md). The PC Book plays one sequence, "backstory" (250.5 s: nine paintings
shown and hidden in turn, a camera path, narration, a fade-in at the start and a fade to black at the end), which the PC engine starts
by itself. The level has no Stomp Player, so the converter never converted it.

## What is implemented (formats, off by default: `OOTF_BOOK_SCENE=1`)

* `scan()` names the Book's own sequences as if a Stomp Player listed them, so they convert like the Intro's (sounds included).
* `addAutoPlayStomp` (ootfpuzzle.ts) adds a Stomp Player (Initial State On, List Play Order 0) at the camera object.
* The exit timer is 250.7 s (`BOOK_SCENE_SECONDS`) instead of 28 s. Both faders now belong to a played sequence and stay.

## What was verified (private 3-level and 2-level packs, `build/ootf-run/bk2/book*`)

* The sequence runs: `DRAKAN_STOMP_PROBE` ticks `elapsed` from 0 to the end, the paintings change, the camera moves.
* The exit fires at the timer (about 250 s after the load) and `OotF 01 - Ruined Village` loads again (run `book1`, 23 shots).
* The hand-back lands Rynn on the temple platform of Ruined Village (shot `s254` of `book1`). That frame shows a dark silhouette and
  no HUD, which was not examined further.

## What is wrong, and why it is off

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

## Next steps

* Compare the GS state (scissor, XYOFFSET, display rectangle) in the Book against the Intro while a sequence plays; the Intro's
  camera action is the same (type 9, 1.0, 80), so a Book-only difference (a real player parked beside the exit, the held Rynn actor,
  the camera object class) is the place to look. The hair agent's finding (a held player keeping the 0x80 attach bit) is a candidate.
* Narration audio and subtitles were not checked (the harness runs with audio off).
