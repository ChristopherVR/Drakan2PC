# All-map performance sweep

Tested all 13 playable Ancients' Gates campaign maps from the player's PAL disc.
The first pass ran from 22:03 to 22:21 Brisbane time on 4 November 2026, followed
by four supplemental runs. Every requested playable map was confirmed by the
runtime's loaded-level log, not just by its launch arguments. There were 17 game
launches in total, all with a clean scripted process exit and no logged
missing-target, ray-pass-failure, watchdog or device-removal error.

Twelve maps completed both fixed-view samples and the movement smoke check.
Andrellian Isles returned to the title screen partway through its second view;
its second sample is partial and its movement check did not run in gameplay.
This is an open result, not a full pass.

## Build, settings and evidence

- All 17 launches used the same executable, SHA-256
  `dd6697f4a7b73169c90cfbfce78820e700ac34c907ddac038f8f6262cba3f328`.
  The executable was hashed before and after each run; no within-run change was
  observed. This is a later executable than the earlier single-scene review;
  the two investigations must not be treated as an isolated optimization A/B.
- Launched only from `dist/DrakanPC`, through `scripts/harness/play_run.py`.
  Installed graphics settings were frozen in a private INI copy: D3D11, scale 4,
  borderless output (renderer screenshots 3840x2160), DLSS Quality, modern water,
  ray reflections and ray shadows on, neural rendering off, uncapped, vsync off.
  Audio was enabled. English resources were selected consistently for the harness.
- Frozen graphics INI SHA-256:
  `977d7059b401db16583daeedcb9f4140283e647cdf28e32e696fb62a001f5e04`.
  Fresh private cards and private native-options files isolate the player's saves
  and preferences. No game/runtime source or installed settings were changed by
  this sweep.
- Instrumentation: `DRAKAN_FRAME_AUDIT=2`, `GSR_PROFILE=2`,
  `DRAKAN_LIFECYCLE_LOG=1`. Each run has six renderer screenshots.
- Session receipts, copied configurations, recipes, JSON summaries and contact
  sheets: `build/perf-map-sweep/20261104-220355/`. Game logs/screenshots/cards:
  `build/harness/map-sweep-20261104-220355-*`. These remain ignored; no game assets,
  saves or captures were added to tracked files.

The user explicitly requested that their other game session remain running.
Processes were sampled every five seconds. A `yes` in the competition column
means another Drakan process was observed at some point during that run; it does
not establish precisely how much GPU time it used during each measured phase.
Other GPU applications are not inventoried. These are observed diagnostic rates,
not clean standalone benchmarks or proof of a regression between maps.

## Results

FPS counts unique nonzero world-present serials, not confirmed display scanouts.
Each ordinary initial view was held for 20 seconds and the turned view for 12
seconds. Analysis excludes the first second and final quarter second of each
phase. Boundaries use the present immediately after the screenshot-completion
log, so boundaries are approximate to a frame. The table uses the selected
supplemental run where noted; original runs remain in `results.json`.

| Map | Initial FPS | Turned FPS | Initial p99 ms | Turned p99 ms | Other game observed | Result |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| Surdana | 48.1 | 43.8 | 41.41 | 44.19 | yes | Complete |
| Kragmor | 115.5 | 69.8 | 9.93 | 23.07 | no | Complete |
| Catacombs | 50.2 | 59.8 | 32.16 | 18.39 | yes | Complete, supplemental |
| City of the Damned | 62.9 | 71.3 | 17.14 | 14.97 | no | Complete |
| Inquisitor's Lair | 87.8 | 112.7 | 13.10 | 9.93 | no | Complete |
| Shadowmire | 68.0 | 66.4 | 26.10 | 26.62 | yes | Complete |
| Andrellian Isles | 37.9 | 39.9* | 46.28 | 42.06* | yes | Returned to title, supplemental |
| Crystal Mine | 63.1 | 60.4 | 27.18 | 29.64 | yes | Complete, supplemental with invulnerability |
| Ravenshold | 147.7 | 148.5 | 9.10 | 7.67 | no | Complete |
| Northern Tundra | 54.9 | 63.2 | 32.26 | 17.68 | no | Complete |
| Shiverbane | 75.4 | 75.5 | 23.65 | 23.63 | no | Complete |
| Valley of the Fallen | 55.0 | 54.8 | 32.13 | 32.50 | yes | Complete, supplemental |
| Stratos | 84.6 | 83.9 | 12.97 | 13.56 | yes | Complete |

`*` Isles has only 7.42 seconds of measured unique-world-present intervals in its
second view before returning to the title. The last screenshot shows the title
menu, not gameplay. The initial fixed view has 18.73 measured seconds of gameplay.

`Level 6 - Transition to Desert` is listed by the old harness but its `.lvl` is
absent from this disc (confirmed through ISO directory lookup), consistent with
the existing world-model reference. It was not launched or counted as a playable
map failure. Intro/outro cinematics and the installed Order of the Flame campaign
pack are outside this Ancients' Gates map sweep.

## Sampling corrections and findings

1. Catacombs: walking backwards in the original recipe touched a level-exit
   trigger and opened the game's save-required message. Its initial view was
   valid (30.3 FPS, p99 58.40 ms), but the second-view sample was blocked. The
   supplemental recipe measures both views before moving forwards. It completed
   both views and movement without that modal interruption. Different background
   load prevents attributing the 30.3 versus 50.2 FPS difference to code.
2. Isles and Valley: the initial screenshot pairs showed moving entry cinematics
   without a gameplay HUD, despite the generic `sync play` barrier releasing.
   Those original FPS values are excluded from the gameplay table. Supplemental
   runs tap Start once during the entry sequence and allow a longer settle time;
   their initial screenshots show controllable Arokh with the HUD. Valley completed
   normally. Isles changed from GUI screen -1 to screen 8 at input time 62.61 s,
   before `turned_end` at 66.18 s, with no scripted quit/menu command at that time.
   The cause of that return is not established. It needs investigation through a
   normal campaign arrival or a verified save; this redirected-New-Game test alone
   does not prove an ordinary campaign transition is broken.
3. Crystal Mine: an NPC killed Rynn during the unattended original run. The death
   screen invalidated its later sample and movement check. Its supplemental run
   uses the existing `DRAKAN_PLAYTEST_GOD=1` aid to keep Rynn's health at maximum;
   the enemy and rendering remain active. This completed both views and movement.
   It is a performance fixture, not evidence of ordinary combat balance/survival.
4. The performance issue remains broader than Surdana. `readbackMap` dominates
   the stationary renderer profiles in Catacombs, Northern Tundra and Shiverbane,
   and is also the largest measured stage in the Lair, Shadowmire, Isles, Crystal
   Mine and Valley views. In the selected roughly 20-second profile sections,
   Catacombs accounts for 10.70 s in this stage, Tundra 13.54 s and Shiverbane
   14.20 s. Readback waits serialize the renderer with GPU work. These measurements
   identify a priority for further work; they do not prove every readback is
   redundant or safe to remove.
5. Surdana, Kragmor, City, Ravenshold and Stratos instead show `presentCall` and
   ray frame-slot waiting as their largest measured stages. Background GPU load
   and the installed ray effects can affect these waits substantially. Times on
   different threads overlap and must not be added together as a frame duration.

Coverage is limited to each map's native start/entrance, two camera views and a
short movement sequence, including mounted starts where the game supplies them.
This is not a complete playthrough of every room, encounter, quest or exit.
The sweep establishes that all 13 maps load and records remaining performance
and harness/state problems; it does not establish that the FPS-drop issue is fixed.
