# OotF intro: "Psygnosis Presents" and music overlap (2026-11-09)

Two reports about the opening of Order of the Flame: the "Psygnosis Presents" intro "video" has playback issues, and the
music in the intro overlaps.

## "Psygnosis Presents" is not a video

The PC install has no movie file (no `.avi`, `.bik`, `.smk`, `.mpg`; the 14 `.wav` files are sounds). "Psygnosis Presents" is the
first scene (Intro 1) of the converted `OotF 00 - Intro` level, rendered by the engine, so frame rate, colour and decode
problems of a video cannot exist. Its graphics problems (mirrored logos, black start) were fixed on 2026-11-08, see
[Intro graphics](../2026-11-08/ootf-intro-graphics.md) and [Intro black start](../2026-11-08/ootf-intro-black-start.md).
Runs on the current converter (private packs, `build/ootf-run/mus1`, `mus2`, shots every 2 s): the owl and plate show from the
first frame, the scenes tick at 1 s of scene time per second (`DRAKAN_STOMP_PROBE`), Intro 1 (14.5 s), Intro 2 (10.2 s) and
Intro 3 follow each other. Nothing new was found in this part; I did not review the shots against the PC game (no PC capture).
A timing pitfall for any intro test: the default `DRAKAN_AUTOPRESS` taps Start + Cross every few seconds and skips Intro 1
(use `DRAKAN_AUTOPRESS_UNTIL=11000`).

## Music: what plays

The Intro has exactly one music cue: scene 452 (Intro 3) starts PC music 4 ("gui") at 0.185 s, fade in 0.6 s, fade out 3 s.
The level has `Initial Music ID` 0. In game (HLE log, `PS2MUSIC.SDB` requests) the Intro streams only track 4 after its load, and
stops it when the level changes: no overlap inside the Intro itself.

## Root cause 1 (fixed): the title menu played the Drakan 2 track

With `game = d1` the title menu should play the converted main theme (`campaign_title_music`, redirect of PS2MUSIC.SDB 196 to the
pack's id 4). The log said `music: title track 196 -> 4 (campaign)` and then `redirect 0302/196 -> 4 not found, serving the
original`: the disc's SeaHigh GUI played. `ResourceService::process` refused any redirect target "too short for the request"
by `offset + length > size`, and the guest asks for a descriptor (type 0x302) with `length = 0xFFFFFFFF` (the whole entry), so
every redirect failed. Since the Intro then starts the theme at 0.185 s of Intro 3, the player heard the Drakan 2 title track
and then the OotF main theme: two different main tracks one after the other. Fix (`runtime/hle/src/ResourceService.cpp`): a
length of 0xFFFFFFFF is "whole entry" and never "too long". Verified: `0302/196/-1 +0 len 60` is now served from id 4 (60 bytes)
and `0304/196 +0` follows.

## Root cause 2 (found, not fixed): scene 381 plays its cue over the level loop

Ruined Village starts its loop 8 ("nitetime1", `Initial Music ID`) and its opening scene 381 plays PC cue 6 ("Dark Union") at
11.5 s (fade in 7 s, fade out 3 s); scene 390 stops music at 4.6 s (fade out 9 s). The PC replaces the playing music with each cue.
The PS2 plays a cue on top of the level's Triggered Music Loops, so in game both tracks stream together from the cue on
(`0304/8` and `0304/6` chunks alternate for the rest of the run; the intro hand-over itself is clean: 4 stops before 8 starts).

Tried, in game, without effect (reverted, nothing committed):

* a sequence music stop (cue id 0) in front of each cue: the PS2 stop only affects cue music, loop 8 kept streaming;
* extra sequence actors on the level's loop objects (ids reserved before the scenes are converted, then given to
  `applyLevelMusic`) sending the loops' Off message at each cue's time, with Off = 0 and with Off = 4098: loop 8 kept streaming.
  The ids matched the placed loops, so a sequence message does not reach a Triggered Music Loop (it probably only listens to
  messages of linked triggers, as the detector halves send them).

Ideas left: let a linked Trigger Detector be fired by the scene (or a Timer at the cue time) that sends Off to the loops, or
play the cue as a loop (one loop per cue id with the On message) and fire both from the same linked object.


