# Wartok Canyons: no poison crystal in the owner's Slot33 (the boss never woke)

Report: "All the enemies are dead and there is now supposed to spawn a crystal near Arokh ... the game seems to be in a
bugged state." Save: `mc0_ootf/BESCES-50006Slot33` (quicksave of 27 November 23:03, pack revision 2, upgraded on load to 3).

## The PC chain (wartok canyons.lvl)

The crystal appears when the Boss Blade Dragon 2455 dies (it links Count Down to Zero 2470, journal 2026-11-27
wartok-poison-shrine.md). The boss starts hidden (flags 0x100) at (301589, 15463, 241513) and is woken by:

1. Arokh enters zone Creature Detector 2542: trolls 324 / 325 get Move To Waypoint, Timer 3203 closes the mine gate 322.
2. Rynn pulls Switch 436 in the mine (290250, 23296, 303566): On to gate 322, wheel 2626 and Timer 3202.
3. Timer 3202 enables Creature Detector 2459 (Initial State Disabled, Inside To Outside, One Way) at the gate mouth.
4. Leaving that box fires Timer 2478 (gate Off), Timer 3219 (On to the boss, 7 s) and Timer 2473 (Move To Waypoint to the
   boss, 7.1 s, waypoint 2456).

## Findings

* **The boss handles its messages.** Fresh Wartok (`DRAKAN_START_LEVEL`, private packs from the installed one with only
  the timer start changed): Timer 2473 at load shows the boss, it flies to its waypoint and fights (`boss-A`, `boss-B`,
  `fresh_timer_boss_awake.png`). Link order ([2470 2456] vs [2456 2470]) makes no difference. Arming 2459 at load and
  moving Arokh out of its box wakes the boss through 3219 (`boss-C`). A quicksave and reload before the timer fires
  still wakes it (`boss-E`). The boss record's `0x1000` bit in Slot33 comes from saving, not from a consumed wake.
* **Slot33 never reached step 2.** Timer 3202 is still named in the save (timers destroy themselves after firing), the
  detector 2459 is static, Timers 2473 / 3219 are static, and the boss has 200 / 200 HP at its spawn. 3203 is gone, so
  step 1 ran and gate 322 is closed. Trolls are still alive at the mine: 324 / 325 at their spawn outside the gate
  (refs 147 / 148) and 178 to 182 inside (ref 178 = 442).
* **The lever works from Slot33.** Dismounted, Rynn facing the lever, E: Timer 3202 fires, 2459 is saved armed and gate
  322 opens (`qs-K`, `slot33_lever_pulled.png`). Use is fiddly: Rynn walks to the lever first and some presses do nothing
  (`boss-G`, `boss-H`, `boss-J`). L1 / E is Use; a flame attack is not.

## Open

* **An armed detector may not survive a reload.** `boss-D`: 2459 armed at load (test pack), quicksave, reload, Arokh into
  the box and out: the boss stayed asleep, while the same steps without the reload woke it (`boss-C`). The reloaded save
  holds 2459 as a record. Not isolated further: if confirmed, a player who saves between the lever and the gate mouth
  soft-locks. Rynn placed by the harness stayed at the lever after using it (in combat with a troll), so the full
  lever -> detector -> boss walk from Slot33 was not completed (`qs-L`).
* No converter or save-upgrade change was made: the boss itself is not at fault.

## Owner's save: the woken boss never targets Arokh

The first repair (quick load with `modsA`, Timer 2473 at load, F5) was wrong: the owner saw no boss. The boss had woken
(state `0x29021368`, 200 HP) but it flies the way point path 2456, 2474, 2475, 2476 and then parks at the last one,
inside the shrine cave mouth (about 297200, 32700, 274300), behind the gate structure. It never takes Arokh as its
target: instance data `d+244` holds the target's registry ref, Arokh's ref in a working run (`tgt-good`) and 0 in every
run from this save (`tgt-bad`). With no target it never comes out, so it is never seen and never attacks.

What it is not (each run under `build/harness/`):

| Run | Change | Result |
| --- | --- | --- |
| `gen-s1` + `gen-s2` | fresh Wartok, F5 with the boss hidden, quick load, `modsA` wakes it | fights, drawn |
| `nameT` | same save under the real level name and path | fights, drawn |
| `fresh-reload3` | fresh fight, F5 and F8 mid fight | still fights after the reload |
| `hidT2` | owner's save with the clean hidden boss record of `gen-s1` (slot fields `+48 +4c +6c +70 +1b8` kept) | parks, no target |
| `refresh1` | owner's save, boss record renamed and 2455 added so the level file builds a new boss | parks, no target |
| `sw302` | owner's save with the fresh save's `0x302` table | parks, no target |
| `aimax` | `DRAKAN_AI_RUN_MAX=64` | parks, no target |
| `bit-O`, `bit27` | clearing `+4` bit `0x1000` / setting bit 27 in the record | no effect (the restore sets `0x1000` itself) |

So the boss, its record, the level name, the AI budget and the `0x302` table are fine: something else in this save
(older state carried from the 15 to 27 November saves; the 52 small `0x104` records and the skill table could not be
grafted onto a fresh save, the game stops at a null call `0x22b5fc`) keeps the boss from acquiring Arokh. Not found.
A save made before the boss fight on the current pack does not show the fault (`gen-s2`).

## Owner's save repaired (boss killed by a test timer)

Slot33 now holds the state right after the boss died: quick load of the original save with a private copy of the
installed pack whose Timer 2473 runs at load and sends Blow Up (5) to the boss (`modsK`, `kill1`). The boss dies
(`0x21001350`), its death message (8) counts 2470 down, the shards rise and the gas lightning runs; F5 at that point.
Loaded with the installed pack (`take1`): the released crystal floats over the pit, Arokh flies into it, the scene
plays and his breath list (`+0x6b0`) goes from 1 to 2. Evidence: `build/review/wartok-crystal/`
`slot33_boss_killed_shards_lightning.png`, `slot33_crystal_taken_installed_pack.png`, `fresh_save_boss_fights.png`,
`user_save_boss_parked_no_target.png`. The boss fight itself is skipped in this save. Earlier versions are kept in
`dist/DrakanPC/save-backups/manual/2026-11-28-before-boss-wake/` (original) and `.../2026-11-28-woken-boss-unseen/`.
