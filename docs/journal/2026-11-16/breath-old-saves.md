# Arokh's breath from old saves, and from any armed mount (2026-11-16)

Follows [save-state-fix.md](save-state-fix.md) section 4. The owner's `Slot33` (Ruined Village, before the awakening)
reached Wartok Canyons with a breathless Arokh, and so does his Wartok Canyons save `Slot14`. Private work, never
tracked: `build/review/breath-1116/` (play folder `play/`, a copy of `dist/DrakanPC` without textures, with its own
`elf/` copy on the `elf =` line; copies of the owner's files in `before/`; private exe build `gb/` linking a copy of
`build/final-game`'s `ps2_generated.lib`; runs in `runs/`, pictures in `ev/`, prototype saves in `proto/`, Ghidra output
in `dec/`). Nothing from the game is quoted here beyond addresses, offsets, ids and field names.

## Two independent faults

### 1. The rider's stale attack state (every armed mount, new games included)

* The dragon's breath is his attack command `0x4006`: `Game_OnInputCommand` (0x23ce18) calls the dragon controller's
  slot `+0x164` (0x270870), which sets bit 1 of his `+0x90` while the key is held (`[dragon]` trace `f90=40002`).
* Before that call it looks at the rider (the player data `FUN_003d3768`, which for a controlled dragon is the actor
  named by the dragon's u16 registry reference `+0x4d8`, 0x3d2518): when the rider's `+0x594` is 0 (or `+0xab0` has
  0x8000) and her `+0x94` has 0x400000 or 0x200000, it returns without calling the dragon.
* 0x400000 is Rynn's "attack in progress": set by 0x317c78 when an attack starts, cleared by 0x318348 from her controller
  update when it ends. Her controller does not run while she rides.
* Live trace of Rynn's instance words through the awakening (`tools/livesave.py` now logs `+0x90 +0x94 +0xab0 +0x594`):
  `runs/t_aw33` (Slot33) and `runs/ng_aw` (a new game with Atimar equipped): `+0x94` goes from 0 to `0x400001` as she
  climbs on during scene `4c` (bit 0 = mounted) and stays so; the unarmed new game (`acc_awnew`) arrives with `0x1`.
  The Wartok Canyons saves keep it: `Slot14`, `acc2_aw33` and `ng_aw` Rynn records hold `0x400001`.
* Proof: the `acc2_aw33` Wartok save (dragon with the relinked breath references) breathes 0 times as written
  (`runs/ld_acc2`), 5 times with only Rynn's `+0x94` patched to 1 (`proto/acc2_rynn`, `runs/p_acc2_rynn`). The armed new
  game on the old exe: 0 breath projectiles (`runs/ng_aw`, dragon record has the breath references).

So the owner's "no breath" was not only an old-save fault: **any player who reaches the awakening with a weapon equipped
got a dragon whose breath key did nothing**. The "new game breathes" checks so far were all unarmed direct starts.

### 2. A dragon whose breath list was never made (old pack saves only)

* A dragon's breath is a list in his instance data: entries of 16 bytes `(weapon resource, alternate weapon resource,
  0, 0)` at `+0x6a8`, capacity `+0x6ac`, count `+0x6b0`, chosen `+0x6a4` (-1 = none). His message handler 0x27d0e0 adds
  an entry for message 0x1028 (and 0x102a, which also chooses it); a power-up the dragon takes sends it (the dragon walks
  his link list in 0x27dd08 / 0x451d00, takes each `0x10039` power-up through 0x3d4be8 and drops the link, the "taken"
  mark). The breath code 0x26d130 returns at once while the count is 0.
* RAM on Wartok arrival: new game and `acc2` `(chosen 0, buffer, capacity 5, count 1)`, entry `0x302 / 0x228` (FireBall 3
  / FlameBreath); `Slot14` (`runs/ld_s14`) `(-1, 0, 0, 0)`. The saved records show the same as the words before the
  `0000c842` marker (`ffffffff 0 0 0` in `Slot14` and in `Slot33`'s Ruined Village dragon).
* Under the 15 November pack the Fireball Power Up had empty Weapon fields, so the dragon "took" it without getting an
  entry; the power-up is gone from those saves, and `Slot14`'s level has none to relink.
* The new game's Wartok dragon also links a carried uid-less power-up record; with the list present that link is not
  needed to breathe (`ld_acc2` patched breathes without one).

## Fix (`ps2recomp/ps2xRuntime/src/lib/drakan_dragon_breath.cpp`, PAL)

Every PadMgr update while the controlled actor is the dragon (class `0x1001c`):

1. Rider: the dragon's `+0x4d8` reference -> registry -> the rider (class `0x10009`); if her `+0x94` has both bit 0
   (mounted) and 0x400000, 0x400000 is cleared. A rider cannot be mid-swing on a dragon, and nothing sets the bit again
   while she rides. Both games (the fault is the engine's).
2. Stand-in: in Order of the Flame only, outside a multiplayer arena (the arena power-ups are kept empty on purpose,
   `ootfbreath.ts`), when the list was never made (count 0, no buffer, chosen -1), the dragon's own handler is called
   once per dragon and level with message 0x1028 and the resources of "!! FireBall 3 !!" (`0x340a`, engine `0x10008`)
   and "!! FlameBreath !!" (`0x29e4`, engine `0x10015`), the converter's fire stand-ins. Posted guest call on a host
   trampoline (the crystals' way: the message and payload go on the dispatcher stack, so the handler's own allocation
   and semaphores run as natively). A dragon that took a power-up normally always has an entry and is left alone; so is
   a dragon before the awakening (not controlled).

`DRAKAN_DRAGON_BREATH=0` turns both off, `_LOG=0` silences `[dragon-breath]` (`env-flags.md`). The converter's held-back
relink lines stay unwritten and are no longer needed: the game repairs both `Slot33` (after the awakening) and `Slot14`
without touching the save; the next save writes the repaired dragon and rider. No pack change, no reconversion.

## Acceptance (private exe `bin/drakan.exe` md5 `a634d4a3...` = this code before a whitespace-only clang-format; pack = `dist` `ootf-levels.zip` md5 `17e01067...`)

| Check | Result | Evidence |
| --- | --- | --- |
| Slot33 through the awakening, breath in Wartok Canyons | **Pass**: rider cleared, stand-in given (`entry 302 / 228`), 3 breath projectiles | `runs/fin_aw33` (`stdout.log` `[dragon-breath]`, `result.json`) |
| Slot14 (owner's Wartok save) | **Pass**: same two lines, 3 projectiles, amber orb (was grey) | `runs/fin_s14`, `ev/fin_breath_sheet.png` |
| New game, unarmed (direct start, as before) | **Pass**: 5 projectiles, no `[dragon-breath]` line (nothing to repair) | `runs/fin_ngnew` |
| New game with Atimar equipped (title New Game, Tab, Square, F5; `proto/ng_atimar`) | **Pass** 3 of 4: 6, 7, 2 projectiles (`fin_ngarmed2/3/4`); `fin_ngarmed` 0, unexplained (rider cleared, screen and gameplay normal over the taps; its Wartok save breathes 3 on load, `runs/ld_ngarmed`). Old exe: 0 (`runs/ng_aw`) | `runs/fin_ngarmed*` |
| Drakan 2 Catacombs `mc0/Slot1`: load, F5, F8 | Pass | `runs/fin_d2cat`, `ev/fin_d2cat_sheet.png` |
| Felled push tree on a new game (Task 2) | Pass: one Atimar strike fells it, no flame by the standing tree, the stump or the trunk | `runs/ng_tree2` (`t_stand`, `tl_*`, `t_mid`, `ta_*`), `ev/ng_tree2_*.png` |
| Same-session reload (Slot14, give, F8 without F5, breathe) | Pass: a second give line after the reload; 4 fireballs before, 3 after (control new-game save: 2 + 2) | `runs/rl_s14`, `runs/rl_new` |
| Alternate breath (stream, `0x10015`) on the stand-in dragon | Pass: 2 streams before and after the reload (control 2) | `runs/alt_s14`, `runs/alt_new` |
| The next save keeps the repair | Pass: `fin_s14`'s F5 holds the breath references in the dragon record and the list in RAM after a load (count 1); loaded with `DRAKAN_DRAGON_BREATH=0`, the breath bit is set for 488 ticks as with the module on and the orb is amber (few projectiles there: the F5 was made hovering against the cave wall, 0 off / 1 on) | `runs/off_s14f5t`, `runs/on_s14f5`, `runs/off_s14f5d` |
| A normal dragon controlled from load (Rift World direct start) | Pass: no `[dragon-breath]` line, 9 fireballs | `runs/rw_ctl` |
| Tests | ctest 205 / 205 (5 data-dependent skips reported), formats vitest 768 / 768 (one run under build load timed out `ootftex`, passed alone and in the rerun), `repository_guard` clean | `logs/ctest.log` |

The tree: first try from 1,600 units (`runs/ng_tree`) only swung at air; from 600 units (`ng_tree2`) the first strikes
removed it and the trunk lay over the gap with Rynn on it.

## Not verified / limits

* USA disc: the module is PAL only (`identityAddresses`), so a USA build keeps both faults.
* Why `fin_ngarmed` produced no breath (1 of 4 armed runs) was not found.
* The stand-in is fire for every breathless OotF dragon; a save that should have had another breath first (none known:
  Ruined Village's power-up is the fire one) would get fire.
* The rider fix trusts bit 0 of `+0x94` as "mounted" (seen 1 while riding, 0 on foot in every trace).
* Drakan 2 while mounted: the rider rule applies there but Drakan 2 was only loaded on foot (Catacombs).
* Rynn's sword after a dismount from a dragon whose rider state was cleared (her `+0xab0` attack bits stay as they were).
