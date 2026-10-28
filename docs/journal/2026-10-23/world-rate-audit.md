# Audit: 30 Hz assumptions at uncapped / 60 world rates

Follow-up to [running-jump.md](running-jump.md). Goal: find other gameplay that assumes the PS2's 30 Hz world tick and
misbehaves at `world_rate = uncapped` (80-250 Hz in harness runs, ~111 Hz on the dev machine).

## Static sweep (full decompile, `build/recomp_ui/alldecomp`, scripts in `build/jumptest/scan*.py`)

The engine is PC-lineage and mostly frame-time correct: 354 functions use the tick time `engine+0x24`
(`engine = *0x5565d4 = 0x4de5e0`), easing is written `x += min(1, dt*k) * (t - x)`, and velocities are
displacement / `engine+0x2c`.

| Pattern | Hits | Verdict |
| --- | --- | --- |
| literal 30 / 1/30 | 20 functions | `325d30` velocity cap: **bug, fixed earlier**. `2f1d60` samples an animation in 1/30 s steps (data time, fine). `2fa328` uses 1/30 only while paused. `38a548` is `dt * 30` (normalised, fine). `280738`/`314ab0` rumble strength from a hit count. `2163d0`, `187f88`, ... are distances. |
| divide by `engine+0x2c` | 21 | displacement / dt, rate independent (no other cap found). |
| persistent state eased by a constant without dt | 11 functions | **`141d80` (Engine_Tick wind gusts): direction += 25% per tick, bug, fixed.** The rest are one-shot (bounce / landing velocity scaling in `330f78`, `2ddef8`; spawn randomisation in `46c0f0`; degree->radian conversions). |
| integer `-1` countdowns in functions without dt | 221 | Nearly all `+0x98` handle reference releases and list counts; not usable statically, covered by the behaviour runs below. |

## Behaviour comparison (same ms-timed script, `DRAKAN_WORLD_RATE=30` vs `uncapped`)

| Area | 30 Hz | uncapped | Result |
| --- | --- | --- | --- |
| On foot (`compare.txt`, 30 vs 86 FPS): run, running jump, strafe, back, crouch, standing jump | e.g. jump apex 611, standing jump 963 | 620, 983 | same within 2-3 % (integration step) |
| Arokh flight (`flight.txt`, level 4A): thrust, climb, brake, stick turn | stick turn 2.52 rad | 2.48 rad | same |
| Arokh **mouse** turn (`[mouse] dragon_mode = stick`) | -0.22 rad | -0.08 rad expected before fix | **bug, fixed**: now -0.21 rad (`flight3.txt`, time-based mouse) |
| Patrolling NPC (`fixture-hostile675`, 20 s) | ~1015 units / 2 s | same path, same speed | same |
| Two hostiles vs idle Rynn after provoking (`fight.txt`) | Rynn 100 -> 0 in ~8 s, 10-20 per hit | ~10 s, same hits | same (combat randomness) |

## Fixes

- `drakan_frame_rate.h` `windHook` (0x141d80): before the native step, shift the direction so `0.75 x + 0.25 t`
  becomes `t + (x - t) * 0.75^(dt*30)`. Only when the tick is shorter than 1/30 s.
- Stick-mode mouse (`runtime/input` `PadSynth`): `stick_full_counts` was counts per *update*; the game integrates the
  deflection over its tick time, so the same physical mouse motion turned ~30/rate as far at high rates. `FrameInput`
  now carries `frameSeconds` (host time between `beginFrame` calls = one world tick) and the full-deflection count is
  `stick_full_counts * frameSeconds * 30` (clamped 1000 Hz .. 4 Hz). Calibration at 30 Hz is unchanged. Affects
  Arokh flight with the default `dragon_mode = stick` and `look_mode = stick`; direct look was already
  displacement based.

Not covered: Arokh breath / melee, bow and magic projectiles, cut-scene timing, particle effects.

## Arokh, full pass (Ravenshold 4A, `build/jumptest/arokh_*.txt`, `flight*.txt`)

Same ms-timed scripts at `DRAKAN_WORLD_RATE=30` and `uncapped` (61-110 FPS), guest RAM dumps decoded with
`scripts/harness/combat_ram_report.py` (`build/jumptest/arokh_report.py`, `arokh_new.py`, `arokh_speed.py`).

| Action | 30 Hz | uncapped | Result |
| --- | --- | --- | --- |
| Thrust (W 1.5 s) | 15,030 units | 14,770 | same |
| Descend (Ctrl 4 s) / hover floor | -21,350, floor 1,024 above surface | -21,320, same floor | same |
| Climb (Space 2 s) | +24,300 | +24,390 | same |
| Brake (S), keyboard turn (A/D) | 2.52 rad | 2.48 rad | same |
| Mouse turn (stick mode, time-based input) | -0.22 rad | -0.21 rad | same after the fix |
| Mouse pitch (stick mode) | 12.4 deg | 11.5 deg (even counts), 9.3 deg (33 ms pulses) | close after the fix; ~7 % residual with even input, more with bursty input (the pitch path is non-linear in per-update deflection) |
| Breath tap (LMB) | 1 fireball (factory 0x3971f8 + 3 effect objects) | same | same |
| Breath held 1.5 s | 2 fireballs | 2 | same fire rate |
| Fireball speed | 33,450-36,800 u/s | 33,190-33,950 u/s | same (spawns ~1-2k units further at the same wall time: lower input latency) |
| Breath after wheel (next breath) | fires, same factory (only one breath available here) | same | same |
| RMB (0x4007: sets +0x90 bit 5, clears +0x8e4) | no spawn | no spawn | same |
| F / R targeting | no target in range | same | not exercised |

Not testable in 4A: landing / dismount / remount. Arokh hovers 1,024 units above water or decks and never lands in the
four attempts; `0x400a` (V / R3) is the camera mode toggle, `0x4009` (E / L1) mounts only when Rynn is controlled, and
no input command switches the controlled actor away from the dragon (only the level / camera code at 0x243620,
0x362310 does). Landing appears to be level-scripted. Arokh taking damage was not tested (no hostiles reached).
