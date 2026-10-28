# Attack + jump froze Rynn in the jump pose at high world rates

Report: starting a melee attack and jumping could leave the animation "locked". Reproduced with the dagger equipped
(`iso/mc0` save), `world_rate = uncapped` and 60: in every trial where Space came during a swing, Rynn flew the whole
jump in one frozen pose and stayed in it after landing for ~1.8 s. At `world_rate = 30` the same input played the
in-air and landing animations normally. Jumping first and then attacking was never affected.

## What the game does

Pressing jump during a swing (`303ff8`, `ab0 & 0x800`, ab0 bit 2 clear) does not end the attack: `304f00` moves it
to layer `player+0xaec` (prop 0x52, here 2; ab0 bit 2 set) and plays the jump animation `0x2077c` there at the
swing's time, while `2f90c8` starts the takeoff `0x208c8` on layer 0. Records: model = `*(*(player+0x80)+0x50)`,
record of layer n = `*(model+0xe8) + s16 *(model+0xdc)[n] * 0x80` (`+0` flags, bit 0 finished; `+0x50` id, `+0x54`
speed, `+0x58` time).

The in-air update `2fa328` (decomp around `2fb6fc`) switches layer 0 to the in-air loop `0x208a0` once the takeoff
is finished or `time >= length - 0.03`:

1. `450cf0(0, player, 0)` (call at `2fb8b0`): layer 0 speed 0. The model applies it to every bone record, including
   the upper-body records the attack layer drives.
2. with ab0 bit 2: `3253a0(...)` saves the attack layer's anim / time / speed, plays `0x208a0` on layer 0 and
   re-plays the saved attack layer.

The attack layer then runs `0x2077c` on to its end, and the attack ends into `0x208a0` (`305460`).

## Why the rate matters

`0x208c8` is 0.393 s long. A 30 Hz tick is 0.033 s, wider than the 0.03 s window, so `2fa328` sees the takeoff
already finished (speed 0, flags bit 0) and step 1 writes nothing. At 60 Hz it sees t = 0.367 (inside the window,
still playing), step 1 writes speed 0 into the attack layer's records too, and step 2 saves and re-applies that 0.
The attack layer sits paused at t = 0.470, never finishes, and the attack only ends when other input (the next
move) resets it.

Measured with probes on `450cf0`, `450518` (play) and `10a4d0` (record parameters) plus a guest stack walk (removed
again):

| | base layer at the call | `450cf0` writes | `3253a0` re-plays `0x2077c` |
| --- | --- | --- | --- |
| 30 Hz | `0x208c8` t 0.393, speed 0, finished | none | t 0.453, speed 1 |
| 60 Hz | `0x208c8` t 0.367, speed 1 | speed 0 to every bone record | t 0.470, **speed 0** |

## Fix

`drakan_frame_rate.h` `layerSpeedHook` (installed for world rate 60 / uncapped): `450cf0` called from `2fb8b8` with
speed 0 on layer 0 while ab0 bit 2 is set returns without writing. Layer 0 is replaced by the in-air loop in the same
code path, so the only effect of the call at 60+ Hz was the bug.

Verification (`build/attackjump/attack_jump.txt`: 15 trials of attack-then-jump at 0-600 ms, jump-then-attack,
running variants, mashing; `freeze_summary.py`, `anim_trial.py`):

| run | freezes > 0.1 s |
| --- | --- |
| uncapped, before | 6 (1.67-1.84 s) |
| 30, before | 0 |
| uncapped / 60 / 30, after | 0 / 0 / 0 |

After the fix the animation sequence (attack layer + base layer) of the uncapped and 60 runs matches the 30 Hz run
except two steps in the mash trial, where input timing lands differently. The `[in-anim]` harness log line (records
of the attack and base layers, player flags) stays for this kind of check (`docs/reference/input.md`).
