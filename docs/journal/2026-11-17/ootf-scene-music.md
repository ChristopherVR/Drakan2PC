# Order of the Flame: the scene music that "is not playing" (2026-11-17)

Owner report: "The music that should be playing at certain scenes in the cinematics is not playing."

Runs, dumps and scratch tools are under `build/ootf-run/sm_*` (before) and `build/ootf-run/fx_*` (after), not tracked.
Only object, class, field and track names and numbers are quoted here.

## 1. Which PC scenes start, change or stop music

A scratch script on `parsePcSequence` read every 0x311 record of the 16 campaign `.ssd` files (`build/ootf/cab_disc`).
Sequence action 8 (music: `musicId, dlsId, fadeIn, fadeOut`) occurs in five scenes only, seven actions in all. No scene
sends a message to a music trigger (the PC Music Trigger has no links), and no other action type touches music.

| Level | PC scene (record, length) | PC music action | Converted (installed pack) |
| --- | --- | --- | --- |
| Intro | Intro 3 (452, 16.4 s) | 0.18 s: 4 "gui", fade in 0.6 s, out 3 s | `e0004`, keep 0, play 1 |
| Ruined Village | 1.1 (381, 61.2 s) | 11.52 s: 6 "Dark Union", in 7 s, out 3 s; plays on through 1.2a | `e0006` |
| Ruined Village | 1.3 (390, 90.8 s) | 4.58 s: stop, fade out 9 s | ref 0, keep 1, level -4096, ramp 9 |
| Ruined Village Book | backstory (Ruin Book 1, 250.5 s) | 0.12 s: 11 "ord-sub" (rendered as track 5), in 3 s; 246.17 s: stop, 3 s | `e0005`; stop with ramp 3 |
| Volcanic | 9.1 (Volcano 1, 82.7 s) | 0.08 s: 7 "Order Corrupted1", in 2.5 s, out 3.5 s; 31.83 s: stop, 4 s | `e0007`; stop with ramp 4 |

Level start music (PC Level Properties `Initial Music ID`): Intro, Ruined Village Book and Volcanic 0, Ruined Village 8,
Grimstone Mines 9, Grotto 1, Wartok Canyons 1, Rift Crystal 7, Succubus Lair 10, Rift World 6, the Islands levels 0.

The converter dropped nothing: a scratch reader on `parseSequence` / `parseActions` finds all seven actions in
`LEVEL1ON.SSD` of the installed pack (`dist/DrakanPC/mods/ootf-levels.zip`), with the refs above (db 14 = the template's
PS2 Music dependency), and PS2MUSIC.SDB tracks 1 to 10 are in the pack (`0302/0304_<n>_44`), served in category 2.

## 2. What the converted scenes played (before)

Shipped exe (`dist`, 17 Nov 11:50) and the installed pack, the owner's `drakan.ini` and `drakan_native_options.ini`
(Sound Volume 25, Music Volume 13), New Game with `DRAKAN_GAME=d1` and the cut scenes kept, audio dump on. The audible
track is measured by a scratch correlator (pack tracks decoded, 4 s windows of the dump at 5.5 kHz, best-lag normalised
correlation; the least-squares gain of the track at that lag gives its level in the mix; a lag that grows by 4 s per
window means one continuous play).

| Scene | Log | Dump |
| --- | --- | --- |
| Intro 3 (`sm_a`) | `scene start: the playing music goes on` at 38.5 s | track 4 audible, -7.5 dB; but that is mostly the Intro's own effect samples `30037` / `30038` (base gain 1.0); the cue `e0004` itself has base 0.116 on voice c1.v23 (RAM) |
| 1.1, 1.2a (`sm_a`) | `track e0006 starts now (fade in 7.00 s), e0008 fades out (3.00 s left)` at 67 s | track 6 one continuous play from 67 s, **-38.5 dB**; ncc only 0.25 to 0.36, and only in the pauses between lines (0.04 to 0.09 under speech) |
| 1.3 (`sm_a`) | `scene stop: category 2, fade 9.00 s` at 1.3 + 4.5 s | silence after it, as on the PC |
| backstory (`sm_book`, the Book level alone as a campaign pack) | `scene start: goes on` | track 5 one continuous play from 15 s, **-37.5 dB**, ncc 0.13 to 0.35 under the narration |
| 9.1 (`sm_volc`, Volcanic alone) | `scene stop: category 2, fade 4.00 s` at 45.5 s (31.8 s into the scene) | track 7 from 15.3 s, **-36 to -39 dB**, then silence until a trigger, as on the PC |

So every music action of the PC runs at its time and with its track. What the owner hears as "not playing" is the level:
the scene music sits 30 dB below the Intro's music and about 20 dB under the scenes' speech.

## 3. Why it is that quiet

RAM dumps of the sound manager (`0x4db4f0`, category records at `+0x48c`, 0x2c bytes each) and SPU2 parameter logs
(`DRAKAN_SND_LOG=3`):

* Sound Volume 25 is the master: `0x1b6800` with category -1 stores it at manager `+0x444` and the driver (`0x1e6370`)
  writes it to core 1 MVOL (`setParam reg=09 core1 = 0xfff`), the final mix. It scales every sound alike.
* Music Volume 13 is the music category's gain (`cat2 gain 0.130`); every music instance's base gain is
  `category gain * (1 - global level) * (1 - category level) * sound gain` (`0x1b30b8` from `0x1b7758`, `0x1b6e28`,
  `0x1b6800`): `e0006` base 0.116 = 0.13 * 0.891 (the tracks' -100 mB). Effects and speech have no such factor
  (base 0.24 to 1.0).
* At the defaults (50 / 25) this is Drakan 2's own mix: music a quarter of the sounds. Order of the Flame's music played
  at full gain until 15 November (it sat in the effects category, `ootf-opening-scenes.md` section 9); the owner's
  options file dates from 13 November, when Music Volume had no effect on the campaign. Since the 15 November fix the
  campaign's music follows the option, so at 13 it dropped by 18 dB (that journal warned of it), and under the scenes'
  speech it vanished.
* Music Volume 100 (`sm_d`) confirms the law: track 6 in 1.1 at -20.5 dB, 18 dB above the 13 run.

On the PC the music and the sound have separate volume controls, both at full by default, and the scenes' music is
heard over their speech.

## 4. Fix (runtime)

`hle::CampaignMusicLevel::baseGain` (`ResourceOverride.h/.cpp`) and `musicBaseGainHook` on `0x1b30b8`
(`drakan_overrides.cpp`, PAL addresses only): while a campaign with its own music is the current game, a music
instance (category 2, read from its descriptor `+0x10 -> +0x3c` as `0x1b3098` does, because a new instance is not in its
category yet when the sort sets its gain) takes the Music Volume relative to its default: 25 and above play at full,
below 25 the option scales as before (13 -> 0.52 instead of 0.13). The scene, global and fade levels inside the engine's
product are kept, and 0 stays silent. Drakan 2 is untouched (`campaignMusic()` false). Log
`[music] campaign music level: e0006 at 0.463 (engine 0.116, Music Volume 13)` (first 16). `DRAKAN_CAMPAIGN_MUSIC_LEVEL=0`
restores the engine's gain.

`scripts/harness/ootf_level_run.py` gains `--keep-cutscene` and `--audio` (passed to `play_run.py`).

## 5. After

Exe built from this tree (`Build-LocalGame.ps1 -WorkDir build/local-game -Incremental -Jobs 8 -NoDist`, md5
`cd58a97c...`), run from a private play folder `build/ootf-run/sm_play` (that exe, the owner's `drakan.ini`,
`drakan_input.ini` and `drakan_native_options.ini`, the dist movies and subtitles) with the same packs, harness and
correlator: `ootf_level_run.py --campaign --no-convert --keep-cutscene --audio --exe ...`.

| Scene | Before: track level, ncc | After: track level, ncc | Log after |
| --- | --- | --- | --- |
| Ruined Village start loop 8 (under 1.1's opening) | -38 dB, 0.15 to 0.17 | -26 dB, 0.36 to 0.53 | `campaign music level: e0008 at 0.463 (engine 0.116, Music Volume 13)` |
| 1.1 and 1.2a, Dark Union (`fx_open`) | -38.5 dB; 0.03 to 0.09 under speech, 0.25 to 0.36 between lines | -26.5 dB; 0.12 to 0.26 under speech, 0.43 to 0.75 between lines | `track e0006 starts now (fade in 7.00 s)` at 67.5 s, `e0006 at 0.463` |
| 1.3 stop | silence after it | the same: `scene stop: category 2, fade 9.00 s` at 173.5 s, track 6 gone by 180 s | |
| backstory, ord-sub (`fx_book`) | -37.5 dB, 0.13 to 0.35 | -25 dB, 0.26 to 0.70 for the whole 245 s, wrapping at the track's end (234.7 s) | `scene stop: category 2, fade 3.00 s` at 259.5 s (scene + 245.5 s), silence after |
| 9.1, Order Corrupted1 (`fx_volc`) | -36 to -39 dB, 0.10 to 0.27 | -25 to -27 dB, 0.20 to 0.71 | `scene stop: category 2, fade 4.00 s` at 45.5 s, gone by 48 s |
| Intro 3 | -7.5 dB (the Intro's effect samples) | unchanged, -8 dB | |

Every scene's music is now 12 dB louder at the owner's settings and holds through the speech; the stops still act at the
PC's times. The `fx_book` harness verdict is FAIL as it was before (`sm_book`): the Book alone has no Ruined Village to
hand back to, so the run ends at its time limit after the exit fires; the scene itself played to its end.

Drakan 2 (`fx_d2`, same exe, the owner's pack folder, `DRAKAN_GAME=d2`): no `campaign music level` line, the music
category's gain stays at 0.13. That run stayed in the boot movies (the cut scenes are kept), so it shows only that the
hook does not act outside the campaign, not a Drakan 2 level's music.

## 6. Tests

* `override_test` (runtime-tests preset): passes, with the new `CampaignMusicLevel` case (13 -> 0.52, the default and
  above at full, 0 silent, a scene's half level kept, NaN passed through).
* `clang-format` (VS 2022 LLVM) clean on the four touched C++ files; `ruff check` / `ruff format --check` clean on
  `ootf_level_run.py`; `test_play_run_keep_cutscene.py` and `test_builder_extras.py` pass.

## 7. Open

* A choice to confirm: above 25 the Music Volume no longer changes the campaign's music (a voice cannot go above full
  scale). The PC's defaults (music and sound at full) were not read from a PC install; they are assumed.
* The rendered Order of the Flame tracks sit about 4 to 5 dB below the disc tracks (`OOTF_MUSIC_VOLUME`, rms 0.13);
  not changed (it needs a reconversion).
* The Intro's loud "gui" theme is the level's own effect samples (base gain 1.0); the scene cue under it is quiet; not
  compared with the PC by ear.
* Harness: with `DRAKAN_GAME=d1`, `DRAKAN_START_LEVEL` loads the disc paths (`Level 0 - Intro`, then `Level 1A -
  Surdana`); the Book loaded that way hung (watchdog, live thread pc `0x177710`). The single-level campaign packs were
  used instead. Not followed up.
* `dist/DrakanPC` was not redeployed (other sessions were playing from it); `build/final-game` does not exist, the
  current WorkDir is `build/local-game`.
