# Left Shift walking and configurable run toggle

The existing `WALK` binding reduced the synthesized left stick to `walk_scale`
(0.5 by default), but never sent the game's native walk command. The input unit
test covered the analog byte only. `Game_OnInputCommand` (`0x23ce18`) handles
command `0x4010` by setting bit 1 of `0x4fbb34` on press and clearing it on release.
The ground controller reads that flag to select the walking speed profile.

The input module now tracks the WALK state once per input frame and sends the
native command through the existing callback on each transition. Analog scaling
remains configurable. Physical left-stick steering and Arokh do not receive the
on-foot walk command. Menus, lost focus, host UI ownership and config reload reset
the state and release the native command on the next pad-manager hook.

`[keyboard] toggle_run = 1` makes each WALK press switch walk/run; default 0 keeps
hold-to-walk. The settings store persists it and Controls > Keyboard movement
exposes **Toggle run**. Toggle mode starts running and resets to running on the
ownership transitions above. Any source bound to WALK can operate it.

## Verification

Private scripted runs used `dist/DrakanPC/drakan.exe`, fresh harness cards and the
same forward route in Surdana. Position-log measurements over settled two-second
windows (world units/second, approximate; terrain and animation affect speed):

| Build / mode | Run | Walk | Run after release / next tap |
| --- | ---: | ---: | ---: |
| Before, hold Shift | 2532 | 1735 | 2569 |
| Fixed, hold Shift | 2540 | 1081 | 2555 |
| Fixed, toggle mode | 2539 | 1081 | 2536 |

The toggle test released Shift 150 ms after each press: the slow interval
persisted until the next press. Animation-log resource IDs remained `20606` in
these intervals; the measured result establishes the native speed-profile change,
not a distinct animation resource. Controller combat, crouch and mounted flight
were not tested live in these runs.

Input tests cover native press/release events, held keys, repeated pad reads,
within-frame taps, toggle persistence, menu/focus/overlay release, remapping,
custom walk scale and diagonal magnitude, controller-stick override and the
dragon command gate. Settings tests cover serialization and reload. All seven
input/settings runtime test executables passed. Changed C++ files passed
clang-format; `git diff --check` passed.

Private recipes, logs and comparison script are under `build/walk-review` and
`build/harness/walk-{shift-before,shift-after,toggle-after}`. The playable folder
was rebuilt using `Build-LocalGame.ps1 -WorkDir build/final-game -Incremental`.
