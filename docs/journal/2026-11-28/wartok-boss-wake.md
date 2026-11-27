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

## Owner's save repaired

Slot33 was rewritten from a play of the save itself: quick load with a private copy of the installed pack whose Timer
2473 starts at load (`modsA`), the boss woke, left its spawn and circled the shrine, then F5 (`build/harness/wartok-wake`).
The new save carries the installed pack's stamp (build `493b53de681b8791`, revision 3). Loaded with the installed pack
(`wartok-verify`): the boss (class `0x1001e`, 200 HP) keeps flying around the shrine. The original is kept in
`dist/DrakanPC/save-backups/manual/2026-11-28-before-boss-wake/`. The kill, crystal and pickup were not played.
