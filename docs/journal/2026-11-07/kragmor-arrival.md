# Kragmor arrival: Arokh was left outside the world (fixed)

Question: does a real player get stuck in Level 1B (Kragmor) after taking the normal exit from Surdana (1A)? Short answer:
yes, in the port as it stood. Runs and recipes are private under `build/krag/` and `build/harness/krag_r*`.

## What the data says

* 1A exit detector 12249 has no `Detect Which?` (class default), so Rynn on foot takes it. 1B has one exit only, the way back
  (12277, fired by detector 12276 with `Detect Which? = 1`, a mounted Rynn). Kragmor is a dead end that needs Arokh.
* Every level places one Dragon object. Surdana's (id 2, `Initial Mode 1`) sits on the end-of-level stage at y 200000
  (Zola Dane, Jassad, the Empress and the fountains are there), about 570,000 units from the exit to Kragmor. It is not a
  summoned or story-placed dragon; nothing in Surdana before the exit moves it (a 100 s New Game run: still at 737879
  202773 198858, airborne bit set, not landed, never ticking).
* The transition (`0x362d38`) carries Rynn's linked dragon (Rynn data `+0x4d8`, set at level start in 1A too) into the next
  level and keeps his offset from the exit ("Maintain Player Position", class default 1; `0x362310` re-roots the pose,
  `0x3d38d0` and the rest do nothing about distance). The port hooks around it (`drakan_levelchange.cpp`) only change
  control for a mounted arrival into a level without a dragon; they do not touch the dragon's pose.

## What happened (run `krag_r4`, normal New Game, god mode, `place` only next to the exit)

Arrival in 1B on foot at the Rynn pad (342160 172630 235736), as before. The carried Arokh arrived at 344657 244202
-337720, the same 573,000 units behind Rynn that the stage was behind the exit, outside the world, still airborne
(`+0x94` 9, `+0x9c` 0) and frozen for the rest of the run (a second dump 15 s later is identical). No Arokh to mount, so
detector 12276 can never fire. 1B's own Dragon (`Initial Mode 0`, 354326 177002 275981) is only used by a level start
(`--level 1b`), which is why that start flies and why earlier chain runs passed.

The same would happen in the PS2 game with the same guest code and data, so this is not a port-only difference; I could
not find any story step in Surdana that relocates the dragon before the exit (the cave entrance teleporters 15858 / 17794 /
17796 with "Dragon Takes Off Upon Teleport" are Rynn-triggered cave hops; walking into the one at 160793 192923 428047
did nothing in a 40 s run, and the original use was not established). A player who takes the on-foot exit therefore has
no way back from Kragmor in the port.

## Fix

`drakan_dragon_arrival.h` (pure policy, test `dragon_arrival_policy`) and `bringDragon()` in `drakan_levelchange.cpp`, run
at the entry of the transition: when Rynn is the controlled actor (on foot) and her linked dragon is more than 100,000
units from her, the dragon's position is set 4,000 units beside her and 3,000 above (the pose a Mode 1 start gives him:
airborne, taking off). He keeps every other state value and lands by himself in the next level. A mounted exit, or a
dragon near Rynn (Tundra: 2,500), is untouched. `DRAKAN_LEVELCHANGE_KEEP_DRAGON=1` turns it off.

## Verification (normal New Game, god mode, F5/F8)

* `krag_r6`: 1A -> 1B by the real exit; log `dragon was parked at 737879 202773 198858, put beside Rynn at 739377 157206
  772312`; in 1B Arokh at 345123 173010 235072, `+0x94` 1, `+0x9c` 1 (landed), 3,000 units from Rynn, 20+ s stable.
* `krag_r9` / `krag_r11`: Rynn walks into him and is mounted (controlled actor class `0x1001c`, `player+0x94` 1).
* `krag_r11` (exit 1B -> 1A fired by the host probe `DRAKAN_LEVELCHANGE`, mounted): arrival in 1A mounted on Arokh at the
  1A gate (729109 153519 772025); F5 then W then F8 kept the mounted state and place. The mounted detector 12276 itself was
  not walked into: Arokh cannot be steered out of the arrival cave with scripted keys (he pitches into the ceiling), so the
  detector check stays unverified beyond its data (`Detect Which? = 1` fires for Rynn on a mount).

## Not verified

* 4A -> 4B -> 4A and 3A -> 3B were not re-run; 4B's Arokh (Mode 1) is 2,500 units from Rynn so the new rule does not apply
  there, and Shiverbane / Valley start mounted.
* The original PS2 behaviour was not observed (no disc play outside the port); the claim "the same carry happens" is
  from the shared guest code and level data.
* Flying Arokh by hand from the Kragmor arrival cave to the 12276 box: not done.
