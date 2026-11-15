# Order of the Flame, final acceptance run of the 15 November fixes

This is one combined check of the final exe and pack after today's fixes: `ootf-opening-scenes.md`,
`ootf-ground-placement.md`, `ootf-character-models.md`, `ootf-ground-textures.md`, `ootf-distant-sea.md` and
`esc-menu-stretch.md`. Scratch, play folder, tools, runs, packs and pictures are in `build/review/ootf-final/`
(private, not tracked). Nothing from either game is quoted here.

## Set-up

* Exe `build/final-game/bin/drakan.exe`, md5 `480f68cc...`, the same file as `dist/DrakanPC/drakan.exe`, built at 12:47
  from HEAD `7b17d21c`. Pack `dist/DrakanPC/mods/ootf-levels.zip`, md5 `9f1ad4f6...`. The character motion pack and
  `mods.ini` are copied from dist.
* Play folder `build/review/ootf-final/play`: a copy of `build/final-game/bin`. `textures/` and `nr/` are hard links to
  the dist folders (as is `nvngx_dlss.dll`). The owner's `drakan.ini`, `drakan_input.ini` and
  `drakan_native_options.ini` are copied, and `elf =` points to a private ELF copy (`build/review/ootf-final/elf`).
* The display settings differ from the owner's in the main run: windowed 1920 x 1080 instead of borderless 3840 x 2160,
  to keep the shots small and the frame 16:9 for the Esc check. Everything else is the owner's: HD textures, DLSS
  quality, texture upscale `auto`, field of view 99, uncapped, `pause_menu = native`, audio on. The sea run used the
  owner's display settings unchanged. The black-patch runs used 1280 x 960 windowed with HD textures and DLSS switched
  per run (`tools/treerun.sh` switches the private ini for one run and restores it afterwards).
* Nothing in `dist/DrakanPC` changed during the runs (`find -newer` on a marker file). Only one game ran at a time.

## Runs

| Run | What |
| --- | --- |
| `runs/main1` | Driver `tools/final.py`. One real New Game in the campaign (`DRAKAN_GAME=d1`; autopress taps stop once the Intro has loaded). Then no input through the Intro, Ruined Village and the scenes 1.1, 1.2a and 1.3, with no placement. Shots every 1.5 s in the Intro and every 1 s in the scenes, and every 0.2 s around Rynn's fall, the 1.1 to 1.2a cut and the Scavenger run. The ponytail cloth state is read from RAM every 0.1 s (`hair.csv`, the logic of `ootf-models/tools/hairmon.py`), with the player trace (`pos.jsonl`), the audio dump (`audio.wav`), `GSR_PAUSE_HUD_LOG=1` and the stomp probe. Control returned at about 260 s; the driver left the game idle until 413 s. Then: a steered walk towards the tavern Music Trigger 1790, which stopped against a house wall with a Wartok attacking (god mode on); 24 quick Esc open / close cycles with shots 1 and 2 samples after each open; and placed views of miner 2529, trees 60, 1845 and 2661 (the poses of `ootf-placement/tools/poses_trees.json`) and the shore. Exit 0. |
| `runs/sea4k` | Direct start of Ruined Village, the owner's display settings unchanged (3840 x 2160 borderless, DLSS quality 2560 x 1440 -> 3840 x 2160), shore placement at ground level, 45,000 and 80,000. Audio off. |
| `runs/tavern1` | Music only. A New Game with the scenes skipped by Esc (1.1 at 15 s, 1.2a at 5 s, 1.3 at 4 s), which runs the passed actions including the music stop. Then Rynn is placed at the tavern Music Trigger 1790 and walked through it, out and back, listening for 50 s. The placement is a harness aid, not play. |
| `runs/trees_hdoff`, `tr_hd0_dlss`, `tr_hd1_nodlss` | Tree views by direct start with the dist pack: HD off and DLSS off, HD off and DLSS on, HD on and DLSS off. |
| `runs/tr_rvt1_hd0`, `tr_rvt0_hd0`, `tr_rvt1op_hd0` | The same views with HD and DLSS off, using single-level Ruined Village packs: `--ps2-terrain 1`, `--ps2-terrain 0`, and the terrain 1 pack with Proc texture 621 made opaque (`packs/`, below). |

## Results

| # | Check | Result | Evidence |
| --- | --- | --- | --- |
| 1a | Intro ground (Drakan 2 ground on by default) | **Pass.** The owl cliff, grass and water show no stitched squares and no wrong textures. HD textures were on (see 9b). | `ev/intro_a.jpg` (frames `i0021` to `i0031`) |
| 1b | Ponytail after the hand-over from Arokh | **Pass.** From the level change to control return, the cloth "attached" bit 0x80 stays clear and the state is 0x1 (running) in 2,026 of 2,027 samples (one 0x11 at a scene cut). It is the same in gameplay: 2,674 of 2,674 samples to 526 s. It moves: the angle from vertical is 14 / 18 / 80 degrees (10th percentile, median, 90th percentile) in the scenes and 8 / 15 / 41 in gameplay. The fastest particle has a median of 265 and a 90th percentile of 418 in the scenes, and 173 and 616 in gameplay. In the 1.1 frames the ponytail hangs behind her. | `runs/main1/hair.csv`; `runs/main1/shots/f0010*`, `f0025*` |
| 2 | Scene 1.1: the two big Wartoks at 1.5 scale stand on the grass | **Pass.** In the final pack, every transform key of the two scene Wartoks carries scale 1.5 (`packs/seq11_final.txt`, actors "Wartok Chain 1" 3031 and "Wartok Plate 1" 3030). In the game they are drawn 1.5 times as large as the small Wartoks. Where their feet are in view (`f0025`), the feet rest on the grass with their shadow under them, not in the air as in the before frame of `ootf-placement/runs/pair_056.png`. | `runs/main1/shots/f0022*`, `f0025*`; `ev/f0025_ground_crop.jpg`; `ev/s11_overview.jpg` |
| 3a | No jump of Rynn's body once she is down | **Pass.** The trace follows the fall to (267672, 20050, 205977) at 108.8 s and holds there to the unit until 112.97 s. Then the 59.98 s key moves her about 550 units, while the picture is fully black (from 112.3 s). | `runs/main1/pos.jsonl`; `ev/s11_fall_end.jpg` |
| 3b | The fade to black covers the NPC despawn; 1.2a opens from black | **Pass.** The mean frame brightness is 69 at 110.2 s, falls to 0.1 at 112.3 s and stays black until 114.2 s; the NPCs leave under the black. 1.2a starts from black but is near full brightness within 0.4 s (30, then 51), so the fade-in is a quick cut-in rather than the class default 2 s. 1.3 does the same (black, then 30 -> 48 within about 0.6 s). | `ev/s11_fall_end.jpg`, `ev/s12a_open.jpg` |
| 4 | The Scavenger before the old man runs (animated) | **Pass.** It crosses the shot in changing running and leaping poses (1.3 + 3 to 4.6 s). | `ev/s13_scav_strip.jpg` (frames `f0173` to `f0177`) |
| 5 | The old man's door stays shut during 1.3 and opens when control returns | **Pass.** It is shut in the 1.3 frames where it is in view (1.3 + 17.7 s and + 18.9 s, `f0216`, `f0218`, "Atimar, my god..."; later 1.3 shots do not show it). In gameplay it is swinging open at `f0291` and open at `f0300`. | `ev/door_13_and_control.jpg` |
| 6 | Music | **The stop passes. Afterwards the level stays silent for as long as it was played.** See the next section. | `runs/main1/stdout.log` line 8355; `runs/main1/music.tsv`; `runs/tavern1/music.tsv` |
| 7 | A subtitle with "Arokh" is spelled Arokh | **Pass.** The old man's line in 1.3 shows "Find Arokh." (the morning pack has "Find Erokh"). | `ev/s13_arokh_subtitle.png` (frame `f0279`) |
| 8 | The miner with a pickaxe has no claw or spikes on his shoulder | **Pass.** Slave 2529 in three views shows the pickaxe only and plain shoulders. | `ev/slave.jpg`, `ev/slave_crop.jpg`, `ev/slave_c_shoulders.jpg` |
| 9a | Tree 60 trunk meets the ground | **Pass.** | `ev/trees.jpg` (`t60_b`, `t60_c`) |
| 9b | Black shadow patches on the ground | **Fail with HD textures off; not visible with HD textures on (the owner's setting).** Cause found: the Drakan 2 ground swap's brown rock texture. Details below. | `ev/patches_isolate.jpg`, `ev/patches_terrain.jpg`, `ev/patches_alpha621.jpg` |
| 10 | Distant sea from the shore: no striped band | **Pass** at the owner's 3840 x 2160 with DLSS, and at 1920 x 1080. The horizon is a clean fog colour, and the lower sky from 45,000 and 80,000 is flat. The straight far edge of the sea and a thin dark line on the water by the left rock are still visible; both are listed as not fixed in `ootf-distant-sea.md`. | `ev/sea_4k.jpg`, `ev/sea_4k_horizon.jpg`, `ev/sea_1080.jpg` |
| 11 | Esc quickly a few times in gameplay: no stretched pause frame | **Pass.** 24 cycles, 18 openings counted in the log: every pause page frame was drawn at hud scale 0.750 (none at 1.000), and the override took effect 13 times. Of the 73 shots, `tools/width.py` measures 64 at page width 0.297 to 0.298 and 8 with no page; none is stretched. | `runs/main1/stdout.log` (`[pause-hud]`); `ev/esc_o1.jpg`; `runs/main1/esc/` |

## The black patches (item 9b)

* **Settings.** In `main1` (HD on, DLSS on) the six tree views show no patches. The same views by direct start show the
  large black, blocky patches of `ootf-placement/runs/trees_before_after.png` on the rock slopes and the brown ground at
  1845 and 60 with HD off. They show with DLSS on or off, and they are absent with HD on and DLSS off
  (`ev/patches_isolate.jpg`, levels stretched). So HD textures are the switch; DLSS plays no part. The placement agent's
  runs had HD textures off, which is why it saw them. The patches are not a mixed-pack effect: the final dist pack has
  them too.
* **Drakan 2 ground.** The single-level packs were converted from the pinned formats `da38fdb` (clean) with
  `tools/conv.sh` (the converter command of `ootf-placement/tools/conv.sh`, Ruined Village only), once with
  `--ps2-terrain 1` and once with `--ps2-terrain 0`. They differ only in the level, its splat TXD and
  `WORLDCON/PROCTEES/PROCTEES.TXD`. With HD off, terrain 1 shows the patches at every view and terrain 0 shows none
  (`ev/patches_terrain.jpg`). The Drakan 2 ground swap causes them.
* **Which texture.** The five Proc Textures the families use (537 grass, 621 brown rock, 563 snow, 13719 dark earth, 535
  rocky paths in snow) are byte-identical in all three containers and decode normally. 621, the brown rock ground and
  slopes family, is 32 bpp with an alpha plane that is mostly empty: 59 % of its texels have alpha 0 and the mean is
  39 of 255 (`tools/alpha.ts`). 537, 563 and 13719 are fully opaque. In a copy of the terrain 1 pack whose 621 alpha is
  set to 0x80 (opaque) and nothing else changed (`tools/opaque.ts`, `packs/rv_t1_opaque`), every patch is gone with HD
  off (`ev/patches_alpha621.jpg`). So the native terrain path draws 621's transparent texels black. The HD replacement
  of 621 in `textures/` covers them. Its alpha was not read; that it is opaque is inferred from the result.
* **Who sees it.** Any player without the HD texture folder, or with `hd_textures = 0`, in every Mountain World level
  whose ground the swap maps to 621: Ruined Village here, and by the same table the Intro, Wartok Canyons, Grimstone
  Mines and Grotto (those were not run).
* **Proposed fix (converter, formats `ootfps2tex.ts` / the pack writer; not made here).** When a ground family maps to a
  Proc Texture that is not opaque, write an opaque copy of it (alpha 0x80) under a free id in the pack's `PROCTEES.TXD`,
  and point the family at that copy. Patching 621 in place would also change it for any Drakan 2 level that draws it
  while the pack is on. The other choice is an opaque Drakan 2 rock texture. Not established: how the disc's own levels
  use 621's alpha (as a ground texture or for something else), and whether 535 (alpha mean 135) does the same where
  rocky snow paths are drawn.
* The single-level packs draw some props differently from the dist pack (for example tree 60's model): `tools/conv.sh`
  passes fewer flags than the build's `extras.py ootf`. The ground comparison is not affected: terrain 1 reproduces the
  dist pack's patches exactly where they were.

## Music in detail

The audible track is measured with `ootf-opening/tools/musiccorr.py` (4 s windows, tracks 2, 3, 5, 6, 8; ncc 0.4 to
0.9 means a track plays, about 0.1 means none). Times are audio dump seconds.

`runs/main1` (real play):

| Dump time | Audible | Log |
| --- | --- | --- |
| about 48 to 64 s (level start) | track 8 (0.5 to 0.8) | `track 8 replaces 4` |
| about 66 to 176 s (1.1, 1.2a, 1.3 to + 12 s) | track 6 (0.3 to 0.9) | `track 6 replaces 8` |
| 1.3 + 4.6 s | track 6, fading as the buffered chunks run out | `[music] scene stop: category 2, fade 9.00 s`, `1 streamed track(s) silenced` |
| from about 178 s to the end of the dump (564 s) | **none** (all tracks 0.04 to 0.15) | after the stop, no music descriptor (`PS2MUSIC.SDB 0302/...`) is read again; chunks of streams 8 and 6 are still read |

So the talk with the old man has no music, as on the PC. After control returns there is no music at all. That covers
the rest of 1.3, 156 s of standing, the walk with a Wartok fight, the Esc spam and the placed views: about six and a
half minutes. The walk did not reach the tavern trigger.

`runs/tavern1` (scenes skipped, then placed at the tavern trigger): the stop is logged at the 1.3 skip and track 6 is
inaudible from about 88 s. At the trigger the game reads track 2's descriptor and its first chunk (`0302/2`, `0304/2
+0`), but no further chunk of track 2 is read. Nothing is audible in the 50 s after (ncc 0.09 to 0.14). Streams 8 and 6
keep being read, silenced. No `track 2 replaces 6` line is logged. `ootf-opening` run `fix2` (with the stop) behaved the
same way: one chunk of track 2 and no replace line. `base2` (no stop) logged `track 2 replaces 6` and was silent as
well.

**Answer:** after the 1.3 stop, Ruined Village is silent for as long as it was played here, and walking through the
tavern Music Trigger does not bring music back.

* The silence from the stop to the first trigger is new today and intended (the PC behaviour). The morning pack
  (`build/review/pack-1115/ootf-levels.before.zip`, md5 `754d5825...`) still has the old stop. 1.3's camera music
  action there is `level 0, ramp 0`, against `level -4096, ramp 9` in the final pack (`packs/seq13_morning.txt`,
  `packs/seq13_final.txt`). That is the no-op stop of `ootf-opening-scenes.md` section 3. The morning pack was not run
  here; that track 6 plays on through the talk and the gameplay after it is inferred from `ootf-opening` run `base1`
  (pack `629adfaa...`, the same no-op stop).
* The silence at the tavern trigger is older: `base2` (the shipped exe and pack, no stop) was silent there too
  (`ootf-opening-scenes.md` section 8). With the stop, the trigger no longer even starts track 2 as a replacement (the
  same in `fix2` and today).
* Not established, and a question for the next music fix: does the engine's category stop `0x1b6ac8` leave category 2
  stopped or at level 0? `sceneMusicStopHook` calls it, and the trigger's loop plays in that category. A check would be
  to read the music category's state after the stop and at the trigger, or to run once with the hook calling only
  `CampaignMusicDucker::stopAll`.

## Other observations

* 1.2a and 1.3 open from black but brighten within about 0.5 s, not over a 2 s fade (item 3b). This is cosmetic; the
  black covers the cuts.
* Each run that had audio ends with three `[hle] post(sid=0x1310 fno=0): guest handler 0x1e6208 did not return within
  20 s` lines, written after `[run] shutdown`. The process still exits with code 0. Run `fix3` of `ootf-opening` did not
  log these lines; not examined.
* Apart from those shutdown lines, `stdout.log` of `main1` has no error, failure or missing-file lines. No crash, stall,
  missing model or wrong texture was seen in the frames looked at: the contact sheets of the Intro, 1.1, the fall and
  the 1.2a cut, 1.3, the walk, the Esc shots and the placed views.
* The steered walk ran into a house wall near (221938, 180899), which is a limit of the harness, not of the game.

## Files

* `build/review/ootf-final/tools/`: `final.py` (driver); `opening.py` (a copy of the `ootf-opening` driver, used for
  `tavern1`); `wdrive.py` (a copy of the `ootf-distance` driver, used for `sea4k` and the tree runs); `treerun.sh`;
  `conv.sh`; `txdlist.ts`, `alpha.ts` and `opaque.ts` (texture container listing, alpha statistics, opaque copy);
  `cmp.py`, `width.py`, `sheet.py`.
* Packs in `build/review/ootf-final/packs/`: `rv_t1`, `rv_t0`, `rv_t1_opaque`; scene dumps `seq11_final.txt`,
  `seq13_final.txt`, `seq13_morning.txt`.
* Shots are stored as JPEG (`runs/*/shots/*.jpg`). Names carry the driver time, the scene reference (`4a` = 1.1,
  `4b` = 1.2a, `49` = 1.3) and the elapsed scene time of the last stomp probe tick, which is up to 1 s old. Frames after
  1.3 ends (about 257 s) keep the last tick's label `49_089.9` although they show gameplay.
