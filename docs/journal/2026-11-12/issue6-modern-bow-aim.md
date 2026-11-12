# Issue #6 item 4: Modern Aiming with the bow

Player reports. v0.1.10: "the crosshair is positioned directly over Rynn's character model". v0.1.11: "significantly
worse": (a) the crosshair still overlaps Rynn, (b) the aiming animation is consistently glitched with an arrow
floating in the air behind her, (c) sometimes the animation gets stuck after a shot and no further arrow can be fired
until the bow is unequipped and re-equipped, (d) rarely the game freezes when moving around while holding the left
mouse button. The attached picture (v0.1.10) shows Rynn from behind in the bow stance with the gold crosshair on the
back of her neck.

"Modern Aiming" is F1 > Gameplay > Combat > Bow combat > "Modern (hold to aim)", i.e. `[mouse] bow_aim = modern` in
`drakan_input.ini`. The owner's own file has `bow_aim = native`. Since v0.1.11 (`d99020e7`) the default `ootf` style
uses the same hold-to-aim machinery (zoom, shoulder offset, held draw, crosshair), so everything below except the
first-person sight applies to the default as well.

## Test setup

`build/review/issue6-4/tools/bowrun.py` runs `scripts/harness/play_run.py` with copies of the owner's
`drakan.ini` / `drakan_input.ini` from `build/review/baseline-play`. Differences from the owner's files, all of them:
`fullscreen = 0`, `window_mode = windowed`, `window_width/height = 1600x900` (16:9, the owner's monitor shape; one run
at the owner's own 1280x960 window, 4:3), and `bow_aim = modern` (the owner has `native`; the native and `ootf` runs
use `native` / `ootf`). FOV 99, HD textures, DLSS quality, uncapped world rate, D3D11 as in the owner's file. The
harness adds `DRAKAN_BOW_LOG=1`, `DRAKAN_PLAYTEST_HOLD=60`, audio off. Rynn gets Surdana's Short Wood Bow (`place` on
it, E, Tab screen, equip) and is placed on the archery range (424900, 160800, 444500, facing the target). Input is the
script's mouse button 1 and WASD, as a player would press them. Renderer `shot` frames include the host crosshair.

Recipes (`build/review/issue6-4/tools/`): `basic` (1 s hold, 2 s hold with pitch up and down, 8 quick shots 0.5 s
apart, 4 slow shots, walk / strafe / back with the button held and released, final shot), `stress` (3 min of random
WASD, mouse turns, jumps, holds of random length and quick re-presses, each 4 s block followed by a standing check shot),
`move` (6 min with the button held almost all the time while moving, short releases, back on the range every 25 s),
`mix` (`move` plus two standing check shots every 25 s), `modes` (hold shot, RMB sight, shot in the sight, close),
`wall2` (rock face beside and behind her). Baseline exe: `build/review/baseline-play/drakan.exe` (about v0.1.11).
Fixed exe: incremental build of the working tree into `build/review/issue6-4/play` (it also contains the other
sessions' uncommitted changes; the baseline does not).

## (a) Crosshair over Rynn

Cause: the shoulder offset never held. Modern aim wrote 200 units along the camera's right axis into the camera's
target offset `cam+0x10c` / `+0x114` (read by the follow tick `0x41f848`). Per-frame camera positions on baseline
(`runs/base-basic1`, Rynn facing +z, camera 1060 behind): the lateral camera offset went -26, -109, -111, -34, -11,
-3, 0 over frames 415 to 465 while the button stayed held until 480, so the view eased out and straight back behind
her within about half a second. With `kBowShoulder` unchanged since the port's first commit (`3eb29c5d`), this is
v0.1.10 behaviour (the player's picture), not a regression; `d99020e7` (v0.1.11) put the default `ootf` style on the
same path. What pulls the camera back was not established (the probe saw `cam+0x10c` still at -200 right after the
native tick while the per-frame camera position was back on her; the target formula in `0x41f848` adds `cam+0x10c` to
`cam+0x1f8` and an anchor).

Fix (`BowAim.cpp` `afterCameraFollow`, glue `hookCameraFollow`): after each native follow tick the presented camera
moves 210 units along its own right axis and 40 along its up axis, eased at 8/s, and shrunk with the camera's distance
to Rynn when it is nearer than 700 units (the native collision pulled it in). `beforeCameraFollow` takes the shift back
out before the next native tick, only if the camera is still where the shift put it (a sight view, cut scene or
teleport that moved it keeps its own place), so the native easing, collision and the mouse orbit never see it. The
crosshair and the arrow's aim ray (`beforeBowLaunch`) use the shifted view. Values chosen from the frames: Rynn sits at
about 38 to 42 % of the image width with the crosshair clear of her right shoulder.

Evidence: `evidence/base_h1_crop.png` vs `evidence/fix3_h1_crop.png` (draw, hold, release, standing), `base_h2_crop.png`
vs `fix3_h2_crop.png` (hold while pitching up to the castle and down to the grass), `fix3_m_crop.png` (top: fixed,
bottom: baseline, running and strafing with the button held), `fix3_43_crop.png` (4:3 window), `fix3_wall2_detail.png`
(rock face on her right; backing into the slope). In all held frames after the ease-in the crosshair is beside her.
Unit test `testShoulderView`.

## (b) Glitched draw, arrow floating behind her

Regressed by `62d6074e` (held draw, `bow_draw = 1` default) and kept by `43f92462` (hold at 0.20 s, snap after the
launch). The hold put the archer clip `0x20896` on the torso channel, froze it at 0.20 s and muted the launch hook while
held. Probe (`runs/probe-stress1`, `runs/probe-cam1`, the full build of the tree as it was): one frame after the draw
started, the nocked arrow's `arrow+4` bit 0x400000 cleared, and from then on the launch `0x20e6c8` was called every
frame from the player's weapon update `0x303d18` (return `0x303ea8`), muted each time (682 muted calls in one basic run).
So the archer clip's mere presence on the torso takes the arrow off the string and requests the launch; the loose arrow
stayed where it was let go of, at her hip, pointing back (`evidence/base_h1_00452_zoom.png`), and the shot left on the
release from wherever it hung, before the release tap's attack had even started. Held at 0.20 s the pose is also the
follow-through (right arm up, empty hand), not a drawn bow. Holding the clip at its first frame (tried first, build
`fix1`) did not help: the arrow still came off the string at the start of every draw (`evidence/fix1_h1_crop.png`,
arrowhead at her hip). A second finding: Rynn's torso channel 2 maps to animation record 8, but the freeze only scanned
records 0 to 7, so the torso record was never frozen (rate 0.01 kept running).

Fix (`BowAim.cpp` `bowDrawFrame` / `runBowDrawAction`): the hold plays the bow's own stand motion (native cached Stand
`player+0xabc` mapped by `0x325650`; `0x20895` here), Rynn's aiming stance with the arrow drawn to her chin, on the
torso channel over the legs' clip, restarted when native locomotion replaces it. The archer clip only plays when the
release tap's native attack starts it. No launch is muted any more (`suppressBowLaunch`, `afterBowLaunch` and the
learned archer clip id are gone). A refused tap (12 frames without the attack bit) or an aim cancelled without a shot
hands the torso back to the base channel's clip at its phase. Probe on the fixed build (`runs/fix3-basic`): the arrow
keeps bit 0x400000 through the whole hold, no muted launches, the release launches it from the attack (attack bit set,
archer clip at 0.0, `aimed shot`, reload at 0.43 s). Evidence: `fix3_h1_crop.png`, `fix3_h2_crop.png`,
`fix3_slow_crop.png` vs `base_slow_crop.png` (two slow draws: drawn stance, crosshair clear, loose, next draw).
Unit test `testHeldDraw`.

## (c) Stuck after a shot

Not reproduced as a lasting "no more arrows until re-equip" state on the baseline. What was measured: standing check
shots (button held 50 samples, a launch within 60 samples counts) failed on baseline 3 of 28 (`stress`) and 2 of 37
(`mix`, checks while swimming or dying after a harness teleport excluded), never twice in a row; the probe shows the
`stress` failures were dry shots: no arrow on the string (`bow+0x158` = 0) because the draw had started before the
previous shot's reload event (archer clip at about 0.4 s) and the muted per-frame launch had already taken the arrow.
Fixed build: 0 of 28 (`stress`) and 0 of 14 (`mix`, the run ended after about 10900 samples when Rynn swam off, was teleported back,
fell and died; not bow related). The likely mechanism for the player's report is that chain (the held draw taking the
arrow off the string and muting launches, so a shot can leave without the native attack and the next one can be dry),
plus the old refusal path (`bowDrawQuiet_`: the frozen clip given back its rate with launches muted for 60 frames). Both
are gone with (b). Inferred, not demonstrated.

## (d) Freeze while moving with the button held

Reproduced on baseline: `runs/base-move-s1` froze 2363 input samples into the `move` recipe (about 40 s),
`runs/base-move-s2` after 19964 samples (about 5.5 min). In both the runtime's stall detector reported the EE thread live at `pc=0x20e6c8 ra=0x303ea8` (the hooked
launch, called from the weapon update) for the rest of the run; rendering went on. cdb stacks (`hang_*.txt`; the
watcher then attached to every process of the baseline exe, so the files include other sessions' games): no thread
waits on a lock; one process's `GameThread` sits in the EE scheduler loop with a shallow stack.

Cause (from the code, consistent with the above): `hookBowLaunch` returned on the muted path without setting
`c->pc = $ra`. Called from generated code (`PS2_JALR`) that is harmless, because a pc still equal to the target counts
as returned. But when the scheduler's preemption checkpoint lands on that call (`cpTick` after `pc = target`), the
thread is later resumed by `EeScheduler` with `pc = 0x20e6c8`, which calls the function at pc and expects it to move
pc. The muted hook left pc unchanged, so the scheduler called it again, forever; with the guest stuck, the input frame
never ran, the button stayed "held" and the mute stayed on. Rare because the checkpoint has to fall on that call, and
only while the draw is held (the call happens every frame then). Regressed by `62d6074e`.

Fix: the muted path is gone (see (b)); `hookBowLaunch` always runs the native launch. Separately, the held draw's
motion setter calls (`0x32cf30`, the model root-motion callbacks, the stand mapper `0x325650`) now run after the input
module's lock is released (`updateBowAim` -> `updateBowAimLocked` + `runBowDrawAction`): the setter is not a leaf
function, and a hook reached from it that takes the lock again would deadlock (in the unit test the same re-entry
aborts the process with 0xC0000409 when the call is made under the lock). This second point is a hardening, not the
observed freeze. Counts with the `move` recipe (button held almost throughout, moving, 21600 samples, about 6 min): baseline 2 of 2 froze (seeds 21, 22);
fixed build 3 of 3 ran to the scripted quit (`fix3-move-s1/-s2/-s3`, seeds 21 to 23; the intermediate `fix2` build,
which still had the muted path, also finished seed 21 once), plus `fix3-stress1` (3 min) and
`fix3-mix1` (about 10900 samples, until Rynn died) without a freeze.

## Classic and native modes, first-person sight

`modes` recipe, baseline vs fixed: `native` (owner's setting) gives the same launches on both (press fires, RMB sight,
shot in the sight; no crosshair or shoulder shift in third person). `ootf`: the RMB sight opens with the crosshair and
fires with the sight shot basis on both; on baseline the first hold launched an arrow while still held (sample 472,
release at 480), on the fixed build the arrow left on the release. The sight camera path (`0x32a608`) does not run the
follow tick, so the shoulder shift is not applied there; a stale shift is dropped when the sight moved the camera.

## Commits

| Fault | Regressed by | Notes |
| --- | --- | --- |
| (a) | none (port's first commit `3eb29c5d`) | `d99020e7` extended it to the default `ootf` |
| (b) | `62d6074e`, `43f92462` | held draw on the archer clip, launches muted |
| (c) | `62d6074e`, `43f92462` (inferred) | dry and early shots of the muted held draw |
| (d) | `62d6074e` | muted launch hook returned without moving pc |

## Not established

- (c) was not reproduced as a lasting stuck state; the mechanism is inferred from probe traces of short failures.
- Which mechanism pulls the camera back from the native target offset in `0x41f848`.
- Wall cases: one rock face beside and behind Rynn in Surdana; no narrow indoor corridor or doorway was tested, and
  the shift has no ray test of its own (only the distance scaling).
- The arrow still converges on the crosshair ray 8000 units ahead; with the camera 210 units to the side a target at
  2000 units is hit about 150 units left of the crosshair. Not measured.
- Fullscreen at the owner's 2560x1440 was not run (a 1600x900 window of the same shape was); DLSS was active.
- The freeze mechanism is from the code and the stall trace; the cdb stacks could not be tied to the hung process
  with certainty (no baseline symbols or map).
- Crossbows (holder mode 4) were not exercised in game.
- A release while the native attack is refused (seen while Rynn lands after a harness teleport) loses that shot: the
  tap is refused natively and the stance is handed back after 12 frames. The baseline lost it too (muted instead).
- The ease-in takes a few frames: in the first 2 to 4 frames after the press the crosshair can still touch her
  shoulder (`fix3_slow_crop.png`, `s_01124`).
- The final build without the probes (`runs/final-basic`, `runs/final-modes-ootf`) was checked with the basic and
  modes recipes only; the stress counts above are from the build `fix3`, whose bow code differs only by the probe
  logging (inactive without `DRAKAN_BOW_PROBE`).
