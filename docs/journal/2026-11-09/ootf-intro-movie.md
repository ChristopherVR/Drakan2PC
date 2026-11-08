# OotF intro: why it stutters and runs fast, and the pre-rendered movie (2026-11-09)

Report: the Order of the Flame opening ("Psygnosis Presents", the owl, Intro 1 / 2 / 3) stutters and plays at the wrong speed.
Question: "can't we just play the intro videos normally in the PS2 engine?" The PC install has no video file (see
[intro music](ootf-intro-music-and-title-redirect.md)), so there is nothing to feed the movie player with: the opening is the
engine-rendered level `OotF 00 - Intro`.

All runs: private copy of the play folder (`build/ootf-run/play-introvid`), the `mus2` pack, `DRAKAN_AUTOPRESS_STOP_AFTER_LOAD=1`,
no pad taps, `DRAKAN_FRAME_AUDIT=2` (per tick `U` and per flip `F` on one clock), `DRAKAN_STOMP_PROBE=1` (scene elapsed time),
window 960x540, scale 1. The machine was shared: one to two other game instances and 40 to 62 % CPU load during the runs.

## Root causes (measured)

1. **Wrong speed: the scenes ran 6.5 % fast with vsync on.** The scene loop runs two engine ticks per 33.3 ms: one of about 31 ms
   and one of 1 to 3 ms (`[ft] U`: `33.335, 2.0, 33.335, 2.0 ...` against one flip per 16.7 ms). `tickTimeHook` snaps every tick to
   whole refreshes and carries the rounding difference, but a tick shorter than half a refresh "passes through and drops the
   carry". The 31 ms tick was rounded up to 33.335 (+2.3 ms), the 2 ms tick was passed on unchanged and the debt was forgotten:
   35.3 ms of world time per 33.3 ms of wall time. Scene elapsed time (`DRAKAN_STOMP_PROBE`) against wall time: 12.74 s per
   12.13 s (Intro 1) and 8.54 s per 8.08 s (Intro 2). Fixed in `snapTickToRefresh`: a short tick repays a negative carry
   (never below 0.5 ms), a positive carry is still dropped (a catch-up after a stall). `frame_cadence_test` covers it (the cut
   scene pattern now keeps the clock's pace within 0.5 %, it was +7 %). After the fix: 0.98 to 1.02 s of engine time per wall second.
2. **Stutter with the player's settings (vsync 0, `world_rate = uncapped`, the ini defaults).** The tick log of the Intro shows
   bursts: a flip and a 2 ms tick every 2 ms for about 70 ms (hundreds of ticks per second), then one 65 to 100 ms frame
   (`U 65, 29, 77, 87 ms` between runs of `U 2 ms`). Over 43 s: 10308 flips, 342 flip gaps over 25 ms, 222 over 40 ms, tick `dt`
   up to 100 ms. The same level with vsync on is 60 flips per second all through, so this is the unthrottled completion-driven
   cadence (the guest runs ahead of the display in bursts), not the renderer's cost. Not fixed in the live path: the fix is not
   a one-liner and the movie below does not depend on it.
3. **Scene changes stall the engine, then the clock catches up.** With vsync on, engine time per wall second is flat at 0.98 to 1.02 s
   except at the three scene boundaries: 1.364 s (26 s), 1.580 s (36 s), 1.507 s (52 s) of engine time in one wall second. The
   scene preloads (`DRAKAN_STOMP_PROBE`: `resource pending ...` lists at each scene start) hold the loop for 0.4 to 0.6 s, the
   next tick carries that dt and the world jumps. The live Intro therefore ends 0.4 to 0.9 s before the 44.02 s of the movie's
   engine time. The texture cache (8 MiB campaign budget) did not wait in these runs.
4. **PAL 50 Hz vs 60 Hz** is not a factor: the display refresh is 59.997 Hz, the engine ticks on the clock, and the scenes
   carry no 25 Hz assumption (a fixed 60 Hz step renders them at the same speed).

Variants run to rule things out (vsync 0, 14 to 36 s window, `dt` sum per wall second): `DRAKAN_FRAME_LIMIT=60` 1.025,
`DRAKAN_WORLD_RATE=60` 1.026, both with the 100 ms catch-up ticks included; ticks at 60 Hz with `WORLD_RATE=60`, bursts only with
`uncapped`.

## Approach: render once, play the file

A fix of the live pacing cannot remove cause 3 (the engine's own load stalls) and a player's machine differs, so the opening is
rendered once into a movie and played through the host's movie layer (`reference/modding.md`, "Intro movie"). Why not the IPU
movie player: that path needs a VDB movie in a level with a Movie Player object; the OotF Intro level is a converted scene level
and the movie would have to be encoded as PS2 IPU macroblocks. A host layer over the live level needs no format work and keeps
the hand-over (inventory carry through the synthetic Intro player, music, level transition) in the engine.

Deterministic render (`DRAKAN_INTRO_RENDER`): fixed 1/60 s tick, vsync on (one tick per displayed frame), every completed frame
read back and piped to ffmpeg, the SPU2 stepped 800 samples per frame by the capture hook (the wall-clock audio thread idles). A
first attempt let the audio run on the wall clock: at scale 3 with the encoder in line the render fell behind real time (46.09 s
of sound for 44.03 s of engine time, 1057 ms worst picture/sound offset), which is why the audio is now in lockstep.

Result (`ootf_intro_movie.py --scale 3`, 65 s): 2641 frames, `60.000 fps on the audio clock (44.02 s of engine time, 44.02 s of
sound); worst picture/sound offset 0 ms`; `ootf_intro.mkv` 1920x1440 h264 (4:3, the PS2 frame is a square 1:1 buffer shown at 4:3)
+ aac, 30.8 MB, ffprobe last packet ends: video 43.999 s, audio 44.010 s. The sound has the whole opening's sounds, not only
music 4 (RMS 0.01 to 0.07 through the 44 s).

Playback verification (`play-introvid`, DRAKAN_AUDIO_DUMP of the playing game, correlated with the file's sound):

* offset constant, correlation 1.0000, gain 1.000, residual 0.00001: the mix is the movie's sound only; before a fix of
  `setExternalSource` (the silence flag was computed after the std::function was moved from) the residual equalled the movie's own
  level: the live level's sound was audible under it.
* vsync 60 Hz, quiet machine: 2665 presents, 1 later than 25 ms, 0 repeated and 0 skipped pictures during the 44 s (the 25
  repeats listed are the held last picture of the fade); vsync off (430 presents per second): 0 skipped pictures. With other
  games running the display itself missed refreshes (gaps of 33 ms, average present 18 to 22 ms) and the playback skipped or
  repeated pictures with it; the movie follows the display.
* the movie is 0.4 s longer than the live level: the cover stays through the next level's load start and goes with the movie's end
  (`cover removed (movie over, next level loading) after 44.5 s`); a movie that ends before the level fires the level's exit.
* Esc at 16.6 s of the movie: the Intro's exit fired, `[levelchange] exit taken in 'OotF 00 - Intro'`, Ruined Village began
  loading 1 s later and was up 2.3 s after the key press, with the cover gone. START / CROSS taps do not skip the level: scenes skip individually and the
  44 s timer remains (an earlier version tapped START and opened the pause menu in a loop).

## Not verified

* HD textures in the render (the test copy had no `textures/`), fullscreen windows, DLSS and ray-traced water on the live level
  under the cover, 120 / 144 Hz displays (free mode, by the audio clock), the USA profile.
* Subjective smoothness on an idle machine and the look of the 4:3 picture against the PC game (no PC capture).
* The hand-over's inventory carry after a skipped Intro (the exit is the game's own trigger, as in the natural hand-over).
