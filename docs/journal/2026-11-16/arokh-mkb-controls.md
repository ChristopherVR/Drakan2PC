# Arokh on mouse and keyboard: the mouse steers him directly (2026-11-16)

Owner report: Arokh's controls do not feel native on mouse and keyboard. Riding him felt like an emulated pad. Fixed
in `runtime/input` (`input/DragonSteer.h`, `InputModule::steerDragon` / `carryDragonCamera` in `DrakanInput.cpp`,
config and the controls v3 upgrade in `Config.cpp`) and in the F1 Controls page (`runtime/settings`). Reference:
[input.md, "Arokh on mouse and keyboard"](../../reference/input.md#arokh-on-mouse-and-keyboard-2026-11-16). Probes,
runs and scripts: `build/review/arokh-controls/` (not tracked).

## The old scheme

- `[mouse] dragon_mode = stick` (the written default): in flight the mouse counts per tick were turned into a right
  stick deflection (`PadSynth`: `dx / stick_full_counts`, clamped to +-1, sqrt pre-curve, 8-bit, jitter dither). The
  game then squares it, maps it to axis 0 / 1 (air table `0x511f08`) and its flight model `FUN_003a5210` eases the
  yaw and pitch rates towards it (`rate += min(1, dt x k) x (gain x input - rate)`). Controller A banks him and turns
  him partly through the bank. So a turn built up over about a second, coasted after the mouse stopped, and saturated
  on any flick.
- `direct` existed but never steered the flight. It wrote angles in the `Camera_UpdateLook` after-hook, and controllers
  A / B call that only in camera mode 6. In flight the camera is mode 1 (trace `cmode=1`). The value was also the camera
  look pitch, not his attitude.
- A / D turned him (the strafe remap was switched off on the dragon). W / S forward / back, Space (Triangle) climb,
  Ctrl / C (Cross) dive.
- Mouse up put the nose down (pad stick sense).

## The new scheme and defaults

- `dragon_mode = mouse`. Before his controller runs, the PadMgr hook turns his world transform about the world up axis
  by `-dx x sensitivity_x x dragon_sensitivity` (default 0.002 rad per count, as on foot). It pitches him about his own
  right axis by `-dy x sensitivity_y x dragon_sensitivity`, clamped to `dragon_pitch_up` / `_down` = 0.75 / 1.0 rad,
  the hard limits controller A applies while he is slow. The mouse is no longer fed into the right stick. Mouse up =
  nose up; `invert_y` / `invert_x` reverse it.
- `dragon_camera_carry = 1`: the flight camera's position is turned round him, and its axes by the same rotation, so
  the view follows the mouse in the same tick. The native follow then only covers his travel.
- `dragon_smoothing = 0` ms (optional exponential ease; every count still arrives).
- `[keyboard] dragon_strafe = 1`: A / D slide him sideways (left stick X to axis 3, which the flight model takes as
  its lateral input), as on foot. W / S, Space and Ctrl / C are unchanged.
- Gates: no steering while the dragon's `+0x94` bit 0 is set (controller A then reads no player input), no mouse
  pitch while bit 3 is set (its branch that does not pitch him itself). Both bits come from the decompile; neither
  was set in any flight tick traced here (`f94=0` throughout).
- Pad path unchanged: a physical right stick or an `RSTICK` binding stops the mouse steering, and the left stick keeps
  its native map. On foot nothing changed: the new code runs only while the controlled actor is the dragon class, and
  the camera look hooks only lost their `direct`-on-dragon branch.
- Existing `drakan_input.ini` files are upgraded in place (`controls_version` 3; `dragon_mode = stick` / `direct`
  becomes `mouse`; nothing else is touched). `DRAKAN_DRAGON_MODE`, `_SENS`, `_SMOOTHING`, `_STRAFE` override for one
  run; `DRAKAN_DRAGON_TRACE=1` logs every tick.
- F1 > Controls > Arokh: steering (Mouse / Right stick emulation), sensitivity, smoothing, camera follows the mouse,
  A / D strafe.

## Evidence

Private build: `build/review/arokh-controls/gb2`. The source is HEAD plus this change, because another session's
uncommitted `hle` edit did not compile in the shared tree. It links a copy of `build/final-game`'s `ps2_generated.lib`.
The play folder is a copy of `dist/DrakanPC` without textures, with `elf =` set to a private ELF copy. Runs used
`play_run.py --level 4a` (Ravenshold: Rynn starts on Arokh) and the recipe `tools/flight_measure.txt`. The same exe
was used for both modes (`DRAKAN_DRAGON_MODE=stick` reproduces the old path), and `tools/analyse.py` / `keys.py`
read the `[dragon]` trace.

Heading response, 30 Hz world (`runs/stick30`, `runs/mouse30c`). t90 = world ticks to 90 % of the final change.

| Probe | old stick | new mouse |
| --- | --- | --- |
| 30 counts, 1 tick | -0.027 rad (0.90 mrad/count), t90 28 ticks (0.93 s), all of it after the input | -0.060 rad (2.00), t90 0 |
| 360 counts, 3 ticks | -0.080 rad (0.22, clamped), coast -0.036 | -0.720 rad (2.00), t90 2, coast 0 |
| 10 counts x 30 ticks | -0.446 rad (1.49), coast -0.016 | -0.600 rad (2.00), coast 0 |
| 1 count x 60 ticks | -0.114 rad (1.90) | -0.120 rad (2.00) |
| -400 counts, 1 tick | -0.367 rad, wrong sign (the flick overlapped the next probes) | +0.800 rad (2.00), t90 0 |
| 200 counts up, 20 ticks | pitch -0.316 (nose down), coast -0.078 | pitch +0.400, coast 0 |
| view (camera) t90, 30-count impulse | 38 ticks | 0 (19 without the camera carry, `runs/mouse30`) |

The trace line of a tick is written after the mouse turn, before the game's controller, so "t90 0" is the turn
itself. The next lines show the controller and the camera tick do not undo it. 30-count impulse, mouse: heading
3.08159 on ticks 860, 861, 862 and 863, view 3.08158 on each. Stick: tick 863 carries the counts, 864 shows the first
-0.011, and the turn still runs on. -400 counts in one tick, stick: +0.0175 rad (clamped at full deflection; the
-0.367 in the table is mostly the later probes).

Uncapped world (`runs/stickunc` 11.5 ms median tick, `runs/mouseunc` 23.6 ms): the old path's gain varied from
0.04 to 1.47 mrad per count and its 30-count impulse took 52 ticks to t90. The new path gives exactly 2.00 mrad per
count with t90 0 in every probe. `DRAKAN_DRAGON_SMOOTHING=40` (`runs/smooth30b`): the totals are unchanged (2.00 per
count), t90 is 2 ticks for an impulse, and nothing is left over once the ease ends.

Keys, 30 Hz, from hover (`keys.py`):

| Segment | old | new |
| --- | --- | --- |
| W, 40 ticks | forward +14,354 | forward +14,386 |
| D, 30 ticks | heading -1.605 rad, no travel | right +12,474, heading 0 |
| A, 30 ticks | heading +1.605 rad | right -12,457, heading 0 |
| Ctrl, 40 ticks | up -14,266 | up -14,695 |
| Space, 40 ticks | up +17,109 | up +16,464 |

He flies where the mouse points his nose (`runs/pitch30`, limits then +-1.0). Nose up 0.30 rad then W: +4,700 up
while moving forward. Nose down 0.37 rad then W: -8,600. At hover without keys the game does not level a pitch inside
about +-0.5 (the stick path leaves it where the stick stopped, too). Beyond that, controller A's slow-speed correction
eases it back: +1.0 was down to 0.54 and -1.0 up to -0.49 within about 40 ticks, still moving when the probe ended.

Flying forward (`runs/ground30`, W held, defaults 0.75 / 1.0):
- 10 counts x 30 ticks turned the heading by -0.600 rad, exactly 0.020 per tick, with zero coast. The track turned
  with it: x drifted from 0 to -3,400 over the turn while z kept rising, and the view followed within 0.00001 rad.
- Nose up to the 0.75 limit with W held: 0.7500 held for 80 ticks (0.74999), climbing about 280 units per tick.
  Nose down to -1.0: -1.0000 held, diving about 560 per tick.
- So the defaults hold while he flies. At hover the game's correction pulls them back towards about +-0.5, as above.

Holding Ctrl for 900 ticks took him down to a floor at y = 129,024 (`+0x90` stayed 0x40100 / 0x40000; bit 0x4000,
controller A's landing branch, was never set). That floor is the sea: the run's last frame (`runs/ground30/end.png`)
shows him in flight just above the water with his wings spread. He did not land, and no recipe here reached solid
ground. At that floor:
- a 60-count turn applied in one tick (-0.12 rad, the view with it);
- D slid him sideways;
- W moved him, and the game levelled a -0.18 nose on its own while he moved;
- a -0.79 nose was eased by the game to -0.48 and stayed there while he hovered, the same as in flight.

The game moved him back and forth by about 1,000 units while he hovered there without input (terrain contact,
seen with Ctrl released too). That is not the mouse steering, which applied nothing on those ticks.

Tests: `dragon_steer_test` (new: angles per count, rate independence, smoothing conservation, rotations,
orthonormality, pitch clamp, ini keys, v3 upgrade, env overrides). `input_test` gained module tests: mouse turns and
pitches the transform in one tick, no right stick, no drift, the clamp, no double apply from the camera look, the
exclusions (on foot, menu, stick mode, physical right stick), the `+0x94` bit 0 / bit 3 gates (blocked counts are
dropped, not replayed), the camera carry on and off (position and axes turned round him), invert / sensitivity, and
A / D strafe on the mount by mode. `disc_profile_test` (PAL / USA) now checks the native map comes back on the mount
with `dragon_strafe = 0`. `disc_combat_check.py` and `spell_gesture_check.py` pin `DRAKAN_DRAGON_MODE=stick`.
Their ini patch writes `stick`, but the v3 upgrade would turn it to `mouse` on load, and their flight assertions were
recorded with the stick path. Older expectations were updated where the behaviour changed on purpose: the camera look on the dragon, strafe
on the mount, and the v2 upgrade now ending at v3.

Frames: `runs/mouse30d/end.png` and `runs/ground30/end.png` show the carried flight camera straight behind him, as the
native one sits.

Follow-up fixes (second commit):
- The camera look's after-hook kept pending counts whenever the camera reported dragon mode. It now keeps them only
  while the controlled actor is the dragon in mouse mode, so they cannot pile up and land in Rynn's next look.
  There is a test for it.
- The Arokh rows are listed as unavailable on discs without `DirectMouseLook` / `MovementInput`
  (`drakan_settings_common.h`), like `camera_follow` / `strafe`. Apply reaches the module through the same
  `drakan_input::reloadConfig(path)` as every other input key, which re-reads the file with `Config::loadOrCreate`.

Harnesses that fly a dragon with the mouse (`coop_dragon.py`, `coop_dragon_ground.py`) now get mouse steering:
faster turns, and the nose goes up on their small upward counts. Their checks are visual. `arena_dragons.py` uses W
and the attack button only.

## Not verified

- Breath / attack on the mount: not touched (another session owns it). The bindings are unchanged (`MOUSE1 = SQUARE`,
  `MOUSE2 = CIRCLE`, Square / Circle are the dragon's primary / secondary request). No run here checked that breath
  fires.
- **Grounded Arokh: no run reached it** (no attempt was made with a landing route, such as the OotF Islands start
  with Arokh placed on the ground). Landing, take-off, walking on the ground while mounted and dismounting were not
  exercised either. The `+0x94` gates
  follow the decompile but were never seen set. If the ground state uses other bits, the mouse still turns him about
  the world up axis there and could pitch him where the game does not level him.
- Axes 0 / 1 (turn / pitch) are now idle in flight on mouse and keyboard, because the mouse no longer feeds the right
  stick. Anything native keyed off them is idle too: the bank, any head look and possibly the aim of the breath.
  The session fixing breath should know.
- The F1 > Controls > Arokh rows were not opened in the game, and neither was a live Apply. The schema, store and
  menu tests pass (`settings_schema_test` checks that every field has a row).
- The USA disc: the code uses only profile layout offsets, so it should behave the same, but it was not run.
- OotF levels with Arokh and co-op dragons were not run.
- No visual bank on mouse turns: the game banks him only for its own (key / pad) turn rate. Mouse turns are flat.
- The real feel at a desk (real mouse, real frame pacing) is not measured. The scripted counts reach the module through
  the same `FrameInput` path as Raw Input.
- The full `ctest` (204 / 204 passed) ran on a tree where another session's half-edited `runtime/hle/tests/mod_test.cpp`
  failed to build, so `mod_test` ran from an older binary. The input, settings and disc profile tests were rebuilt and
  run on their own.
- One run (`smooth30`) ended in a D3D11 device hang (`DXGI_ERROR_DEVICE_HUNG`) while other sessions' games shared the
  GPU. The rerun (`smooth30b`) was clean.
