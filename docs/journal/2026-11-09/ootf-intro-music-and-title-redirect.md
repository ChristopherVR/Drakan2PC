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

## Root cause 2 (fixed in the runtime): scene 381 plays its cue over the level loop

Ruined Village starts its loop 8 ("nitetime1", `Initial Music ID`) and its opening scene 381 plays PC cue 6 ("Dark Union") at
11.5 s (fade in 7 s, fade out 3 s); scene 390 stops music at 4.6 s (fade out 9 s). The PC replaces the playing music with each cue.
The PS2 plays a cue on top of the level's Triggered Music Loops, so both tracks streamed together from the cue on
(`0304/8` and `0304/6` chunks alternated for the rest of the run; the intro hand-over itself is clean).

A converter-side fix failed: a sequence music stop only affects cue music, and sequence messages (Off = 0 or 4098, to actors on
the loop objects, ids checked) do not reach a Triggered Music Loop. Reverted.

Fix (`hle::CampaignMusicDucker`, `runtime/hle/src/ResourceOverride.cpp`, hooked in `ResourceService::process`): while a campaign
with its own title music is the current game, a track starts when its descriptor (0x302) is read, and every other track still
streaming is then served silent chunks (ADPCM blocks with predictor, shift and data zeroed, loop flags kept). A silenced track
starts again with its descriptor, or with a chunk at offset 0 after a gap of more than 8 s (a looping stream wraps to offset 0
with no gap). Drakan 2 does nothing (the ducker needs the campaign). The PC does not resume the level loop after the cue
(scene 390 stops everything), so the loop stays silent. There is no crossfade: the old track cuts at the cue.

Verified (`build/ootf-run/mus2`, Intro + Ruined Village, HLE log + 48 kHz dump, best-lag normalised correlation of 4 s windows
against the pack's decoded tracks): log `music: track 6 replaces 8`; the dump follows track 8 (ncc 0.41 to 0.74) until the cue and
track 6 (0.35 to 0.52) after it, track 8 at 0.07 to 0.10 (no longer audible). Unit test in `override_test` (ducker).

## Title music at New Game after 60 s idle

`build/ootf-run/mus1` (`DRAKAN_AUTOPRESS=62000`): the title streams the redirected theme (`0304/196` chunks from +0 to
+737280, about 58 s) and requests no more after New Game; the Intro's cue starts a fresh track (`0302/4`, `0304/4 +0`). The dump
level dips at the level load. No overlap of the two.
