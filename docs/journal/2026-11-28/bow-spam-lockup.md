# Bow: Rynn locks up after spamming left click (the quiver's last arrow)

Report (owner, Order of the Flame campaign): "Long bows (potentially crossbows and normal bows / heavy bows have the same
issue): if you spam left click it breaks Rynn and the bow, we can't walk further or anything."

Cause: not the clicking. Rynn locks up on the shot that empties the quiver. The owner's quiver held 6 arrows while the HUD
showed 61, so spamming reached the last arrow quickly. Native PS2 engine behaviour. Fixed in the runtime
(`hookNoArrows`); no pack change.

Runs, RAM dumps, decompiled functions, recipes and scratch scripts: `build/review/bow-spam/` (`runs/`, `dec/`, `recipes/`,
`scratch/`); not committed.

## 1. Reproduction

Owner's Slot 1 (`OotF 04 - Grimstone Mines`, saved 20:15) copied as quicksave Slot 33 into the private card
`build/review/bow-spam/card`; `play_run.py --save-ootf ... --env DRAKAN_GAME=d1`, the player's `drakan.ini` and
`drakan_input.ini` (`bow_aim = ootf`). Rynn placed at the Grimstone lever corridor (225544, 49718, 273747).

| Run | What | Result |
| --- | --- | --- |
| `spam3`, `spam3b`, `spam3w1`, `spam3w2`, `spam3t`, `spam3t2` | 40 taps standing, then W held with 40 clicks (70 ms apart), then S, W | locked on the 6th shot in all six runs: `+0x94` 0x80, `+0xab0` 0x300002, all channels on the Stand `2047b`, position frozen while W / S are held (`spam3/c0.png`..`c2.png`) |
| `spam3w` | same recipe, 5 shots | no lock-up |
| `slow9` | 9 slow shots standing (300 ms press, 1.6 s apart), then S | locked on the 6th shot |
| `slow9-native30` | as `slow9` with `bow_aim = native` (private input ini), `DRAKAN_WORLD_RATE=30`, `DRAKAN_FRAME_LIMIT=30`, `DRAKAN_VSYNC=0` | locked on the 6th shot |

So neither the click rate, the walking shot, the OotF / modern bow adapters, the mouse-to-pad path nor the uncapped
world cadence matters: the 6th arrow does it.

## 2. Cause

RAM (`scratch/pstate.py`, `bow.py`, `inv.py`): the bow (`player+0xe90` = 5) has `bow+0x14c` = -1, so it takes its
arrows from a separate quiver in the inventory (resource 0x7cd, slot 18). The quiver's count (its instance `+0x4c`) was 6
at the load and 3 after three shots; after the 6th shot the quiver item is gone from the inventory. The HUD's bottom-right
number read 61, 58, 55 over the same shots.

Decompiled PAL functions (`dec/`), traced live with `PS2X_TRACE_PCS`:

* The shot launches at once from the bow's weapon check `303d18` (launch `20e6c8`), which then checks the ammunition
  again (`49cc78` / `49cac8`: find the quiver slot). With the last arrow gone it sends the player message 0x104c
  ("no arrows").
* The player's handler `328c40` hands 0x104c to `2113b8`: `+aec` = 0 and the cached Stand (`+0xabc`, `2047b`) on every
  channel (`32d010`). Nothing else: the bow shot stays running (`+0x94` bit 0x80, `+0xab0` bit 0x200000).
* The bow update `20ff00` ends a shot (`305460`, which clears 0x80) only when the archer clip on the torso channel
  (property 0x52) is finished (record flags & 0x201 == 1). The archer clip was replaced by the looping Stand, and with
  no nocked arrow it calls `20f9c8` every frame, which finds no quiver and sends 0x104c again. The trace shows
  `20f9c8 -> 49cc78 -> 49cac8 -> 2113b8` every frame for good. While 0x80 is set Rynn cannot walk, turn, jump or change
  weapon.

Every bow (holder mode 5) goes through `20ff00` / `303d18`, so the Long, Heavy and Speed bows and the plain bow behave
the same. Drakan 2's own bows go through the same code. Not run there (no disc bow save at hand).

## 3. Fix

`hookNoArrows` (`drakan_input_glue.cpp`, replaces `2113b8`, PAL only): on entry, when
`bowAttackLeftWithoutArrows(player)` (`runtime/input/src/BowAim.cpp`: bow out, `+0x94` bit 0x80, `+0xab0` bit 0x200000)
is true, it first ends the shot with the bow's own end of shot `305460(player)`, as `20ff00` does when the archer clip
finishes, then runs the native handler (Stand, `+aec` = 0). The check is made on entry because the handler's motion
call can yield; a resumed handler is left alone. `DRAKAN_BOW_EMPTY_QUIVER=0` keeps the native lock-up
(`docs/reference/env-flags.md`). It runs in every bow mode. Unit test: `runtime/input/tests/bow_aim_tests.cpp`
`testBowAttackLeftWithoutArrows`.

## 4. Evidence after the fix

Rebuilt `build/final-game` (incremental) and deployed to `dist/DrakanPC`.

| Run | Result |
| --- | --- |
| `fix2-slow9` | 6 shots fired as before; `[bow] no arrows left: the running shot is ended`; `+0x94` 0 after the 6th; S then moves Rynn about 970 units (`b1.png`); later clicks with the empty quiver start a draw, are ended the same way and leave her free |
| `fix2-spam3` | 6 shots (3 standing, 3 walking); after the last one she keeps walking and runs on to (224223, 281968), well past the old lock-up point (`c2.png`) |
| `fix2-slow9-native30` | `bow_aim = native` at 30 fps: same, `+0x94` 0, she walks away |

The first five shots in every run fire, launch and nock natively as before (the hook only acts in the state above).

## 5. Not verified, or open

* **The HUD number.** With the owner's converted bow the HUD showed 61 while the quiver held 6 arrows; it drops by one
  per shot (61, 58, 55) and still showed 55 once the quiver was gone. Its source was not found (no 61 / 58 / 55 in the
  RAM dumps as an int, short or float). The owner will now see Rynn stop shooting after the quiver is empty while the
  HUD still shows arrows. Worth a separate look (the HUD counter and the converted bows' `+0x14` / `+0x18` pair,
  `87b67b17`).
* Crossbows (holder mode 4) end their shots elsewhere and are not covered by the fix; no crossbow in the Order of the
  Flame saves, not run.
* Other bow types than the owner's (Heavy, Speed, Drakan 2's disc bows): same native code path, not run.
* Picking arrows up again after the fix: not run (the bow should nock from the new quiver as natively).
* Shooting the last arrow with the native sight (RMB) open: same handler, not run.
