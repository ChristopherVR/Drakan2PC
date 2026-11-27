# Wartok Canyons: the poison crystal shrine (crystal before the boss, no lightning)

Report: "Arokh is able to enter the poison crystal trigger point and can see it before killing the dragons. It also
doesn't make the lightning sounds and effects. It seems to be broken even when killing the dragons."

## What the PC level does (wartok canyons.lvl)

There is no barrier. Before the fight the shrine is mostly hidden: the "Poison Gas Quickening Power up" 2538 (Resources
4974), the Gas Crystal Pit 2460, the Gas Pit Cover 2462 and the four Gas Shards 2463 to 2466 start hidden (flags 0).
The Boss Blade Dragon 2455 links Count Down to Zero 2470 (start value 1). When it dies the counter fires: Visibility
Toggler 2482 shows the crystal, pit, cover, shards and the Gas Pit Flare Generator 2537; Timer 2469 blows the cover up;
Timer 2472 (0 s, On) raises the shards and arms Creature Detector 2483; Quickening Effect 2467 morphs the ground; Sound
Trigger 3067 plays "Arokh In Awe of Poison Rune"; and Timer 2540 sends **Blow Up** 7 s later to the four Gas Lightning
Generators 2531 to 2534 (Run Mode 1, On Trigger 0), which then release "Gas Lightning01 / 02" (engine 58) around the
crystal for good, with their launch sounds. The crystal's own links (3079 Stomp Player timer, 2544 gate cog 323) fire
when it is taken.

## Three faults in the converted level

1. **The crystal shows itself.** The PS2 Dragon Power-up (runtime class `0x10039`) sets its visible bit (entity `+4`
   bit 0) on its first update whatever the level flags say (watch: `0x18001048` at load, `0x18021049` 0.25 s later), and
   a dragon that flies into it takes its breath (Arokh's breath list `+0x6b0` went from 1 to 2). The disc has no hidden
   power-up that something shows later: its two hidden ones (LEVEL3ES 119, LEVEL6EN 2238) are linked to a Dragon and
   taken at load.
2. **Blow Up ends a PS2 generator.** Handler `0x2ce120` takes Blow Up as "finish" (journal 2026-11-16
   ootf-arokh-scenes.md), so Timer 2540 stopped the lightning before it ever ran. Census of PC Timers sending Blow Up to
   generators: only Wartok 2540 sends it to generators alone; Grimstone Mines 2499, 2505 and 2617 send it to explosion
   generators together with a barrier or a crystal (left alone, now a converter warning).
3. **The bolts lived 3 ms.** Gas Lightning01 / 02 have 3 frames of 0.001 s on the PC. With the timer fixed (test pack,
   timer started at load) the generators ran (`e+4` bit 2, release timer counting) but no frame showed a bolt; 0.05 s per
   frame showed them. "Max Length (lu)" is an int on the PC and a float on the PS2, so the class merge skipped it and the
   bolts kept the template's 0.5 lu.

## Fix (converter, formats `ootfgenerators.ts`, `ootfclass.ts`, `ootfpipeline.ts`)

* `timerBlowUpToOn`: a PC Timer whose links are all generators sends On instead of Blow Up (Wartok 2540).
* `mergeDefaults`: a PC int field carries into a same-named PS2 float field; a lightning bolt (engine 58) "Seconds Per
  Frame" below 0.03 s (the disc's usual value) becomes 0.03. Applies to every converted bolt (Gas, Ice, Lava, Light and
  Soul lightning: the Ruined Village bonding bolts now take the PC length 4 lu instead of the template's 15).
* `addPickupGenerators`: a hidden Dragon Power-up that a Visibility Toggler shows and no dragon links (the four
  "Quickening" crystals: Wartok 2538, Grotto 777, Alwarren 2569, Volcano 2332) is released by a new Object Generator
  (closest native mechanism) where it floats: one copy, no velocity, On Trigger 0, Limit 1, "Parts Inherit Links From
  Parent?" 1 so the released crystal keeps 3079 and 2544. The released class is a copy of the crystal's class ("... On").
  The generator is started by the Timer the PC trigger starts soonest with a starting message (Wartok: 2472) or by the
  trigger when it is a Timer. The placed crystal keeps its id and class (the save layout) but moves 20000 under its
  ground, and after the class plan its PS2 record loses its model block, breath and pickup sound (`inertPowerUpRecord`).
  Zeroing the PC class instead made 7 referenced classes disappear from the layout, so the change is made on the record.
* Why not just links on the counter: a save holds the records of stateful objects with their links and position (the
  owner's `Slot33` has the counter 2470 and the crystal 2538 at its old place), while a Timer that has not run is not in
  a save and is created from the pack. The new generator reaches old saves through `save_added_objects` (revision 3).

## Evidence (exe of `dist/DrakanPC`, the owner's `drakan.ini`, copies of `mc0` / `mc0_ootf`, pack built with `--previous`
the installed pack: build `493b53de681b8791` kept)

| Run (`build/harness/`) | Pack | What | Result |
| --- | --- | --- | --- |
| `poison-look` | installed (before) | `Slot33` quick load | crystal above the shrine (`loaded.png`) |
| `poison-touch`, `poison-take2` | installed | fresh Wartok, fly into the crystal | breath list 1 -> 2: taken before the fight |
| `poison-exp-ga3` | test, timer at load + bolt frames 0.05 s | burst of shots | bolts around the crystal (`b*.png`) |
| `poison-fixA` | fix | fresh Wartok, look and fly through | no crystal (`before.png`), breath list stays 1 |
| `poison-fixkill` | fix + Timer 2473 sends Blow Up to the boss at 3 s | burst | shards rise, crystal appears, bolts; taken: list 1 -> 2 |
| `poison-qs-final` | fix | `Slot33` quick load, fly through | loads (`upgraded on load: level objects added [3233]`), no crystal (`loaded.png`), list stays 1 |
| `poison-qs-kill` | fix + the same test timer | `Slot33`, boss dies | shards, crystal and bolts (`q*.png`); taken: list 1 -> 2 |

## Not verified / differs from the PC

* The lightning sounds were not listened to: the bolts carry their converted launch sounds (Gas Lightning01 sound 11,
  02 sound 653) and the generator a random sound; no audio capture was made.
* The released crystal appears with Timer 2472 (0 s after the counter), the same moment as the PC toggler; the boss was
  killed by a test timer (Blow Up), not in a real fight.
* The crystal's links on pickup (scene timer 3079, gate cog 323 via 2544) did not fire in the earlier pickup test with the
  placed crystal either; not checked with the released one.
* A save of an earlier pack made after the boss died but before the crystal was taken shows no crystal (the placed one
  is inert now and the generator's trigger has already run).
* Grimstone Mines 2499 / 2505 / 2617 still blow up their explosion generators together with other objects (no release on
  the PS2); reported as a converter warning, not changed.
* Grotto, Alwarren and Volcano get the same crystal treatment; only Wartok was run.
