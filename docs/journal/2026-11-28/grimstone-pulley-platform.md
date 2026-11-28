# Grimstone Mines: the lever lifted the rope but not the pulley platform

Report (owner, Order of the Flame campaign, Slot 2 in OotF 04 - Grimstone Mines, saved 17:22): "We've pulled the lever
to lift the platform but only the rope was lifted." Two runtime faults in the engine's rope class (engine class
0x10049), both fixed in `drakan_frame_rate.h`. No converter or pack change.

Runs, RAM dumps, decompiled functions and scratch scripts: `build/review/grimstone-pulley/` (`runs/`, `dec/`,
`scratch/`, `recipes/`); not committed.

## 1. The objects

PC data (`Mountain World/Grimstone Mines/mines.lvl`), unchanged in the pack (`OOTF04ES.LVL`, same ids, links and
parameters):

| Id | Class (PC) | Engine class | Position | Role |
| --- | --- | --- | --- | --- |
| 1097 | Switch (WORLDCON 2405 in the pack) | 13 | 225766, 49222, 273578 | the lever; links 1098 (its wheel), 516, timer 2337, 1788 |
| 516 | `pulley platform` (MOUNTAON 4088) | 73 rope | 232973, 64105, 263219, turned 180 about x | Starting Length 9 lu, Final Length 6.3 lu (object overrides), Use with 1 (continuous), Rate 0.33 lu/s, Maximum Swing 0.01 (object override; the class default is 0), period 6 s; links 860, 859 |
| 859 | `platform` (RESOURES 17351) | 17 | 231424, 44530, 264704 | the grate Rynn rides |
| 860 | `Pulley Wheel` (RESOURES 17404) | 17 | 232973, 45277, 263232 | the wheel at the rope's end |

1 lu = 2048 units. The lever sends "on"; the rope sets its "changing" flag and shortens 2.7 lu (5,530 units) in about
8 s, which should raise the platform from the pit floor to the upper ledge.

## 2. Reproduction

Owner's Slot 2 copied as quicksave slot 33 into a private card (`card33`; play_run's title Continue loads the newest
save, and the autopress taps otherwise bounce between the title and the Load screen), `DRAKAN_GAME=d1`:
Rynn at the lever, rope end hanging in mid-air, platform on the pit floor (`runs/slot2-load/a1.png`). RAM: rope
length 16,588.8 (8.1 lu), platform y 44,530.

Fresh direct start (`DRAKAN_START_LEVEL=OotF 04 - Grimstone Mines`, Rynn dismounted and placed at the lever, `E`):
`runs/fresh2-native` (`DRAKAN_ROPE_CARRY=0`). RAM over 20 s: rope length 18,432 -> 16,255 -> 13,341 -> 12,902.4 (final,
flag "done"); platform y 44,549 -> 44,544 -> 44,547 -> 44,508, only swinging sideways about 500 units. Screenshot
`runs/fresh2-native/v19.png`: no platform at the ledge.

## 3. Cause 1: the swing put the load back every frame

Decompiled PAL functions (`dec/`): rope update `3e3610`, length change `3e4310`, carried-object group update
`3ab1f8` / bind `3aafd8` / offset `3aab70`, message handler `3e3d40`, restore `3e4a00` / mesh rebuild `3e30d0`.

* `3e4310(change, rope, 1)` moves every object the rope links to by `change` along the rope's axis, then changes the
  rope's length and mesh. The update calls it each frame while the "changing" flag (+8 bit 1) is set.
* The same update then turns the rope by its swing and calls `3ab1f8`, which, whenever the rope's transform differs
  from the one it last saw (exact compare `13da20`), puts each carried object back at `rope + R * offset`, the offset
  bound once (`3aab70`, in the rope's own frame). Nothing updates those offsets on a length change.
* So a rope that swings at all (Maximum Swing above 0) drops every length move on the next frame. The pulley rope swings
  0.01 (the level's own override), so only the rope shortened. A rope with no swing never changes transform and would
  carry its load.

None of Drakan 2's ropes change length while carrying anything (disc levels: three `Cable`s with equal start and final
length carrying grab platforms, a cage and hanging bags with no links), so the native game never hit this.

**Fix:** `ropeLengthHook` (replaces `3e4310`): after the native call with a1 set, it shifts each group entry's offset
(group +48 entries of 0x40, offset at +10) by the same move expressed in the rope's frame (`ropeCarriedOffsetShift`,
solve `R d = move`). Switches are left out (`ropeWindsCarried`, runtime class 0x1000d): Grimstone's four crank cables
link to their own `Pulley Crank` and `Release Lever` (to message them), so the native code moves those too and the
group puts them back; read from RAM, Cable N's group holds the counterweight and wheel (0x10011) and the crank and lever
(0x1000d).

## 4. Cause 2: a loaded rope forgot its length

The save restore of a rope (`380f70` case 0x10049) clears its flags and calls `3e4a00`, whose mesh rebuild `3e30d0` sets
the length (+7c) to the length of the rope's model. The rope's own start (`3e3378`) then winds the mesh to the Starting
Length; the restore does not. Every Order of the Flame rope uses the 8.1 lu `CableA01` mesh, so after any load every
rope in Grimstone came back 8.1 lu long (log of a reload: saved 10,240 / 6,144 / 8,704 / 3,174.4 / 18,432 / 12,902.4 /
12,083.2 / 14,336, model 16,588.8 for all). For the pulley this meant: a save made before the lever came back 8.1 lu,
so the lever would lift the platform 1.8 lu instead of 2.7 (it would stop 1,843 units below the ledge); a save made
after it showed the rope end in mid-air.

The saved length is still in +7c when `3e4a00` is called. **Fix:** `ropeRestoreHook` (replaces `3e4a00`) reads it
first and, after the native rebuild, winds the mesh back to it with `3e4310(saved - model, rope, 0)` (a1 = 0 moves no
objects), as the rope's start does (`restoreRopeLength`: positive, at most 64 lu, more than 1 unit off). The call is a
tail call: if it yields, the scheduler resumes it and it returns to the caller itself. (A first version set `pc = ra`
after the call; a quick load then jumped to a heap address after the fourth rope, `runs/final-fresh`, so a yield there
is real.)

Both hooks are PAL only and switch off with `DRAKAN_ROPE_CARRY=0` (`docs/reference/env-flags.md`). Tests:
`scripts/tests/frame_cadence_test.cpp` (offset shift for a hanging and a swung rope, singular matrix, switch filter,
length restore limits).

## 5. Evidence after the fix

Rebuilt `build/final-game` (incremental) and deployed to `dist/DrakanPC`. The installed pack is unchanged.

| Run | What | Result (RAM) |
| --- | --- | --- |
| `runs/fresh2-native` | lever, native rope (`DRAKAN_ROPE_CARRY=0`) | rope 9 -> 6.3 lu, platform y 44,549 -> 44,508 (stays down) |
| `runs/fresh2-fix` | lever, first version of the fix | platform y 44,551 -> 46,969 (5 s) -> 49,982 -> 50,034 (done) |
| `runs/final-fresh1`, `final-fresh2` | lever, then F5 / F8 (final executable) | platform 44,551 -> 50,040 and 50,016; after the reload rope 6.3 lu (natively 8.1) and platform 50,099 / 50,017 |
| `runs/final-reload-first` | F5 / F8 before the lever, then the lever | rope 9 lu after the reload, platform 44,509 -> 50,022 |
| `runs/fresh-stand` | lever, then Rynn placed above the platform | she stands on the grate at y about 50,100 at the ledge, wheel and rope beside her (`t_on2.png`) |
| `runs/final-slot33` | owner's quicksave Slot 33 (17:21:10, before the lever), lever | rope 9 lu at load, platform up to 50,073, rope done |
| `runs/final-slot2` | owner's Slot 2 | rope 6.3 lu at load (natively 8.1), platform still on the pit floor (44,551) |

Pictures: before `runs/fresh2-native/v19.png`, after `runs/fresh2-fix/v19.png` (the grate at the ledge),
`runs/slot33-lever/q00.png` and `q13.png` (Slot 33 before and after the lever), `runs/fresh-stand/t_on2.png`.
No run of the final executable logged a missing function or a stall.

## 6. The owner's Slot 2 save

It cannot be repaired by playing: the save holds the platform on the pit floor (its position) and the rope wound to
6.3 lu, and the lever toggles: the next pull sends "off" (the rope stops swinging, state 0) and the one after "on",
which in state 0 only restarts the swing, so the rope never starts changing again (`runs/slot2-fix`, three pulls).
Load the quicksave Slot 33 (17:21:10, about a minute earlier, in the corridor before the lever; the rope is at 9 lu)
and pull the lever again on the new build: the platform rises (`runs/slot33-lever`). Editing Slot 2 (moving 859 and
860 up 5,530) was not tried.

## 7. Not verified, or open

* The Grimstone crank cables (Cable N / S / E / W, counterweights, `Grim Power Count`): the fix now lets a winding
  cable lift its counterweight and wheel, but cranking was not reproduced (holding `E` at Pulley Crank 404 left Cable N
  at 3 lu natively and fixed, `runs/crank-*`), so the counterweight puzzle was not played through either way. Natively
  the crank and release lever swing with their cable (they are in its group); unchanged.
* A save made while the platform is moving: the restore clears the "changing" flag, so the platform stops where it
  was and, by the lever's toggling, does not restart (native behaviour, not changed).
* If `3e4310` yields in the middle of a winding step, that step's offset shift is skipped (the load drops back by one
  step); not seen.
* Drakan 2's own ropes with the two hooks were not run. The restore now gives every loaded rope its saved length,
  which is the length it had when saved; disc ropes whose model length equals their Starting Length see no change.
* The PC game was not run: that the PC platform rides up with its swinging rope is inferred from the level design.
* Frame rates during the runs were 26 to 120 fps with other harness instances running.
