# Forward walking/running jump regression

Report: W + Space could jump vertically as if standing still. The October 23 velocity-rate correction was still
installed. Reproduced with the deployed PAL executable, scripted keyboard input, a fresh isolated card and the
normal Surdana start. No player's saves or INIs were changed.

## Two causes

1. **Native Walk is not just a gait selector.** `InputModule::updateWalkCommand` now sends command `0x4010`
   for Shift / toggle-run, setting bit 1 of `0x4fbb34`. Jump-start (`0x2f90c8`) checks that flag before testing
   forward speed and dispatches vertical dodge state 19 through `0x2fe198` (return PC `0x2f94b0`). Thus merely
   repairing measured speed did not fix Shift-walking jumps. The old analog-only walk did not set that flag.
2. **Ground snap masks movement before the old correction.** On a recorded 6 ms walk tick, position history
   contained `(0, -35, 0)` while model motion was `(-7.03125, -0.015625, -0.0625)`. The native velocity chooser
   selected the vertical vector, returning `(0, -1050, 0)` and forward speed zero despite actual forward travel.
   Scaling the chosen vector cannot restore its missing horizontal component. Restoring just X/Z is also insufficient:
   the downstream speed limiter sees the huge downward component and reduces forward speed to ~429 instead of ~1168.

## Fix

- For the forward-speed query's call to `0x325d30` (return `0x325f54`), restore the **whole collision-resolved motion**
  when both position history and the selected vector are vertical-only; then apply the existing rate correction.
  Other callers (including in-air physics), the native prefer-slower mode, pause and ticks at/below 30 Hz stay unchanged.
  A stopped/wall-blocked motion vector supplies no forward speed; held W alone never manufactures momentum.
- Hook `0x325da4` too, recovering arguments from saved registers and the guest frame on resumption. The original
  entry-only post-call wrapper could miss its correction when the pause-query call yielded.
- At the precise walk-jump dodge dispatch, require forward without back/strafe, native Walk enabled, state 19
  and measured forward speed >1000. Discard that pending dodge and tail-call native jump-start `0x2f9520(type=2)`.
  This preserves the native jump/animation handling and scheduler continuation; no temporary global flag edits.
  This walk correction applies at all world cadences, not just the high-rate modes.

## Verification

`scripts/harness/recipes/forward_jump.txt` uses wall-clock timing: run/jump, Shift-walk/jump, standing jump, with
landing time between trials. `scripts/harness/jump_check.py` checks native types and actual movement/ascent.

| World rate | Run | Shift-walk | Standing |
| --- | --- | --- | --- |
| 30 | type 2, ~2458 horizontal units | type 2, ~2414 | type 1, 0 horizontal units |
| 60 | type 2, ~2474 | type 2, ~2453 | type 1, ~8 settling units |
| uncapped | type 2, ~2438 | type 2, ~2422 | type 1, ~8 settling units |

Travel is the maximum displacement during the first 1.5 seconds after the request, not takeoff velocity.
Evidence: `build/harness/jump-regression-{30,60,fixed3}/stdout_1.log`. The same checker rejects the pre-fix
`jump-regression-audit3` walk trial. Policy tests cover the recorded snap, stationary/blocked motion, untouched
native paths and the walk-redirect gates; Python tests cover missing evidence and incorrect outcomes.
Wall/back/side restrictions are unit-tested, not claimed as live scenario coverage. USA is not covered by these PAL hooks.

Final incremental rebuild/deployment and uncapped smoke check also pass (`build/harness/jump-regression-final`):
run/walk takeoff travel ~321/~327 units in 120 ms; standing takeoff travel 0. All 225 Python tests and the input /
frame-cadence CTests pass. Changed C++/Python formatting and lint checks and the repository source audit pass.

The incremental build also exposed an existing uncommitted null-controller guard calling `returnS32` before its
definition. A forward declaration fixes compilation without changing that guard's behavior.
