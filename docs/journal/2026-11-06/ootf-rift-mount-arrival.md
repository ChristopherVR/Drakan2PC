# Issue 5: arriving in Rift Crystal on Arokh (fixed)

Follows [ootf-progression-2.md](ootf-progression-2.md) ("New blocker"). Runs and helpers are private under
`build/harness/ootf-riftmount/` (`run.py`, `pair.py`, `build.ps1`, recipes, `runs/<name>/`) and `build/harness/lc-mount/`
(Ghidra exports of the functions below, `cam.py` reads the controlled actor and camera from a RAM dump, `players.mts` lists the
Rynn starts, rideable dragons and exits of every PC level, `plan.mts` prints the campaign plan's exits).

## What the PC data says

* Rift Crystal holds a Rynn start (206) and no rideable dragon (class "Dragon", engine class 28). The other PC levels without
  one are the Intro, the Ruined Village Book, Grotto and Islands Night; every flown level (Islands, Grimstone, Wartok, Ruined
  Village, Succubus Lair, Volcanic, Rift World, Alwarren) has one.
* The Islands rift box 4131 ("Detector", Task Trigger Only) has Detect Which "Both" (class default 2): the PC fires it for Rynn
  on foot and for Rynn on Arokh, and links it to exit 4132, which keeps the class default Maintain Player Position 1. The exit
  object lies at height 0, 75,700 units under the box.
* Rift Crystal has no exit back to the Islands (its only exit, 308, leads to Islands Night), so the converter synthesizes the
  arrival marker at its Rynn start.

So the PC player arrives in Rift Crystal on foot at the Rynn start: there is no dragon to put her on, and no exit to keep an
offset from. The PC executable itself was not run or disassembled for this; the conclusion rests on the level data.

## What the PS2 engine does

Decompiled (Ghidra, read-only project) and checked with trace runs:

* The transition `0x362d38` notes before the load whether the controlled actor is Rynn (a word on its stack, sp+0xbfc; its
  inverse, "the dragon is controlled", at sp+0xc1c). Rynn and her dragon are found through `0x3d2518` / `0x3d2658`: from the
  controlled actor and its `+0x4d8` link (the dragon's rider, the rider's dragon).
* After the load the same two functions now find the new level's own Rynn and dragon. `0x362310` respawns the carried Rynn over
  the level's Rynn, then the carried dragon over the level's dragon when there is one, restores each from the transition's
  stream (`0x16c590`) and kills the level's copy. Control (`ctx+0x4b0`) is set only by the respawn whose word is set.
* Mounted into Rift Crystal: the level's Rynn links no dragon, so the dragon respawn is skipped, and Rynn's respawn runs with
  "Rynn controlled" 0. Nobody takes control: `ctx+0x4b0` keeps the level's Rynn, which the respawn has just killed. On the next
  tick the input command handler `0x23ce18` reads that actor's freed controller at `data+0x57c` and calls through a null method
  (`missing-target` JALR at `0x23d924`). Trace `t1`: one respawn only (`index 3`, control 0, the level Rynn with `+4d8 0`),
  `kill-list flush: 2 entries`, then the crash.
* The engine has no branch for this case. A rider also carries her rider state: `data+0x94` bit 0 is set on the actor that
  hands control over (`0x44d3b0`, the mount) and the word is cleared by the actor that takes control (`0x456de0`, reached from
  Rynn's dismount handler `0x32d760`, and `0x44d3b0` for the new controlled actor).

## Fix (runtime)

`drakan_levelchange.cpp` wraps `0x362310`. At Rynn's call (return address `0x363dec`), when "Rynn controlled" is 0 and the
transition has no dragon to respawn next (its s7 is 0), the policy `drakan_mount_arrival.h` hands control to Rynn: the 7th
argument and both stack words are rewritten, so the rest of the transition also treats her as controlled (her input setup
`0x44d6c8`, the camera placed from her offset). A wrapper on `0x44d6c8` at the transition's call (`0x3640b8`) then clears her
control word `data+0x94` the way `0x456de0` does. Without that clear she arrives but neither reads the pad nor falls (run `f1`:
`gameplay=0` for the whole run, frozen at the arrival height). Every other case (on foot, or mounted into a level with a
dragon) is left alone. Policy test: `mount_arrival_policy` (`ps2recomp/ps2xRuntime/tests/mount_arrival_policy_tests.cpp`).

## Fix (converter): no kept offset onto a synthesized marker

With control fixed and pack6 as converted (`f2`), Rynn arrived 76,000 units over the Rift Crystal start: the kept offset from the
Islands exit object. She fell for about 20 s, and after the landing the world was no longer drawn (World 0.0 fps), W and D did
not move her and `data+0x94` read `0x200`; the state survived F5 / F8. With `mods7` (pack6 with the Islands exit's Maintain
Player Position set to 0 by hand, `f3`) the arrival is clean.

`planCampaign` (formats, `ootfcampaign.ts`) now drops Maintain Player Position on a PC exit whose destination has no PC way back
(the destination only gets the synthesized marker at its Rynn start). In the campaign this changes the Islands exit to Rift
Crystal and the Succubus Lair exit to Volcanic; Rift Crystal 308 and Volcanic 1237 already had 0 in the PC data. Test:
"drops Maintain Player Position on an exit whose destination has no way back". The private pack `pack7` was converted from the
working tree (which also held another session's uncommitted converter work on sequences and puzzles).

## Runs

| Run | Build / pack | What | Result |
| --- | --- | --- | --- |
| `t1` | trace build, pack6 | Islands box, mounted | crash as before; trace above |
| `f1` | control fix, pack6 | same | no crash, `arrived on a mount in a level without a dragon: Rynn arrives on foot`; `gameplay=0`, Rynn frozen 76,000 over the start |
| `f2` | control and control-word fix, pack6 | same | `Rynn takes control on foot (control word 1 cleared)`, `gameplay=1`; long fall, then no world drawn and no movement (above) |
| `f3` | same, mods7 | same, with F5 / S / F8 / D | arrival at the start (95232, 220840, 185237), W 5,049 units, F5, S 2,765 away, F8 back at (95262, 221938, 180188) exactly, D 625 after the load, world drawn at 168 fps, clean scripted quit |
| `v1` | final build, `pack7` | Islands box on Arokh, F5 / S / F8 / D | `arrives on foot`, `control word 1 cleared`; at the start (95232, 220840, 185237), W 5,038 units, F5, S 2,770 away, F8 back at (95262, 221938, 180199) exactly, D 625 after the load, clean scripted quit |
| `r08-09v` | final build, `pack7` | Rift Crystal to Islands Night (WhirlpoolSteps, two jumps) | exit taken, Islands Night at the Exit Whirlpool (596992, 54570, 457728), W, F5, S, F8 back at (597174, 54570, 460613) exactly, D 3,237, clean quit |
| `g05-06v` | final build, `pack7` | Grotto to Islands (arrives on Arokh) | `rider respawn: rider controlled 0, dragon follows 1` (left alone), on Arokh at (331714, 21021, 819011) as in `g05-06d`, W, F5, F8 exact, D 1,488, clean quit |
| `s10-11v` | final build, `pack7` | Succubus Lair to Volcanic (the ship) | `rider controlled 1, dragon follows 1` (left alone); with Maintain now 0 the arrival is the Volcanic start (268753, 151400, 502588) instead of the kept offset (283569, 152900, 502503 in `s10-11`); W, F5, S 2,795 away, F8 exact (269343, 151498, 501686), D, clean quit |

## Other mounted exits

Destinations with a rideable dragon (Islands, Grimstone, Wartok, Ruined Village, Succubus Lair, Volcanic, Rift World, Alwarren)
keep the engine's own path: the dragon respawn follows and keeps control (`g05-06v`). Destinations without one: Rift Crystal
(fixed here), Grotto (from Grimstone's mine "Creature" box) and Islands Night (from the Rift Crystal whirlpool). The last two
were reached on foot in the runs so far; a mounted entry there now takes the same on-foot path. The Intro and the Ruined
Village Book have no rider at all.

## Remaining

* The Islands bell route (bell, cut scene, whirl doors) is still not played; the runs place Arokh in the rift box.
* Rynn keeps whatever else the rider state holds (her carried pose or animation); nothing visible in the runs, but a dismount
  animation is not played on arrival.
* The PC engine's own handling of Maintain Player Position onto a level start was inferred from the level data, not observed.
