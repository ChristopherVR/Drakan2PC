# Issue 5: the Alwarren hammer route and the converted cut scenes of the other levels

Follows [Islands bell](../2026-11-06/ootf-islands-bell.md) and [Stomp Player play order](../2026-11-06/ootf-stomp-play-order.md).
Runs, recipes and helpers are private under `build/harness/ootf-alw/` (`run.py` = the volroute driver with an `EXE=`
override, takes `GAME_SLOT.lock` only when free; `state.py` object state per RAM dump, `data.py` instance words (a
counter's value is word 0), `inv.py` Rynn's inventory cells, `touch.py` her touch list, `seqsum.py` scene ticks from
`DRAKAN_STOMP_PROBE`, `slots.mts` converted id to registry slot, `aid.mts` the test aid below, Ghidra exports under
`dec/`). Packs were built from the committed formats tree (a resolve hook pointed `@drakan/formats` at a HEAD copy, so
other sessions' uncommitted formats edits stayed out): `pack` = HEAD before this work, `pack4` = with the fixes below.
Executable: `dist/DrakanPC/drakan.exe`, and for the German check a private incremental build
(`Build-LocalGame.ps1 -Incremental -DistDirectory build/harness/ootf-alw/dist`). Direct starts, god mode.

## The route (PC data)

Alwarren holds the Bell Hammer of Alwarren (525). The PC chain: six Barrier Crystals (Keys, Lock Codes 691 to 696) lie on
clasps around the city; picking one up runs a timer and a "count down to zero" counter that raises one of six platform
pillars (movers), and each pillar carries its platform lock (Key Lock, same code) up with it. Using the six crystals on the
six locks counts counter 679 down to zero, which turns the barriers off. The "starving wargiant" 2016 sends I'm Dead to
counter 1765, which plays scene "Bellhammer1" (Stomp Player 1758), hides the spinning display hammer and, through timer
1762 (5 s) and Visibility Toggler 1763, shows the real hammer; timer 1764 (Run Instantly, 0.1 s) hides that hammer at
load through the same toggler. Then the exit 154 (Creature Detector 153) leads back to the Islands, where carrying the
hammer into the bell tower's column rings the bell (verified with a moved hammer in `bell-h`).

## Steps in game

| Step | Result | Evidence |
| --- | --- | --- |
| 1. Collect the six crystals (placed on each clasp, E) | pass | each crystal's handle leaves the world cell (cell -1); counter 2915 6 -> 0; inventory cells 0, 1, 2, 8, 9, 16 hold the six Keys (`alw-2`, `alw-6b`) |
| 2. Pillars rise | **fail, fixed**: counters 672 to 677 went 1 -> 2 and nothing moved (`alw-1`). With the fix the six counters reach 0 and the pillars rise 410 units, carrying their locks (`alw-3`) | `data.py` word 0, pillar positions 47398 -> 47808 |
| 3. Use each crystal at its lock (placed 220 units off the lock, facing it, E) | **fail, fixed**: the Use handler found the right key (`0x31ac68` returned its cell) but Rynn never sent it, and counter 679 stayed 6 (`alw-6`, `alw-7`). With the fix 679 counts 6 -> 0, one per lock, and the six crystals leave the inventory (`alw-6b`) | `DRAKAN_TRACE_FN`, `data.py`, `inv.py` |
| 4. Barriers off | pass: counter 679 at zero, barrier 1221 / 1301 flags change (`alw-6b`) | `state.py` |
| 5. The Bell Hammer is hidden until the giant dies | **fail in the German game, fixed** (see 3 below); in English it is hidden from load (flags bit 0 clear) and E next to it takes nothing (`alw-11`, `alw-13`) | `state.py` 525 flags `18001044`, cell stays 223 |
| 6. Kill the starving war giant | **not achieved by the harness**: with the Magic Long Sword equipped (inventory, Enter) and up to 12 rounds of placed attacks the giant stays alive (`alw-11`, `alw-13`); the scripted attacks face away from it (the placement yaw is not aimed at a moving target). Not a converter finding; open | `inv.py`, data+0xa48 = the sword |
| 7. Scene "Bellhammer1", hammer shown, pickup | covered only with a test aid: `pack4a` (`aid.mts`) unlinks timer 1764, so the hammer is not hidden and the giant is not needed; see the table below | |
| 8. Exit to the Islands (placed outside, then inside detector 153) | pass: message box (screen 2), save screen (5), `exit taken in 'OotF 07 - Alwarren' at 221184 57763 385024` (`alw-13`) | lifecycle / levelchange log |

Downstream of the giant, with the test aid `pack4a` (timer 1764 unlinked by `aid.mts`, nothing else changed; the hammer is
then visible from load, as it was in the German game before fix 3):

| Step | Result | Evidence |
| --- | --- | --- |
| 9. Take the Bell Hammer (placed on it, E) | pass: handle leaves the world, inventory cell 2 holds 525 (`alw-14`) | `state.py`, `inv.py` |
| 10. Exit to the Islands carrying it | pass twice (`alw-16`, `alw-17`), **stalled once** (`alw-15`: `[stall] guest input boundary silent`, watchdog in the allocator at `0x4acdd0` while the Islands load). The long sessions `alw-13` (no hammer, after the fight) and `alw-14` stalled the same way; a short session with a crystal, the sword, the hammer with only one of its two pickup links, or nothing loads (`carry-*`, `alw-exit-*`). Not reproduced reliably, so not fixed: open | lifecycle log |
| 11. Islands: Rynn on foot, hammer carried | pass: controlled actor `0x10009`, inventory cell 0 a Key (`alw-16`, `alw-17`) | `cam.py`, `inv.py` |
| 12. Ring the bell (placed outside, then into the column; walking with jumps stopped at the dais step as in `bell-b`) | pass: scene "Bell Hammer" (`0x00080011`) 101.5 to 110.6 s, both whirlpool movers running (`f9c` 0 -> 0x36 / 0x2), column detector flags change (`alw-17`) | `seqsum.py`, `state.py` |
| 13. Drop into the rift box | pass: message box (screen 2) and save screen (5) at 113.9 s (`alw-17`) | input log |
| 14. Save and reach Rift Crystal | not reached in this run: the scripted taps did not complete the second save on the card (the same as `bell-f`); `bell-h` covers this step with the moved hammer | |

## Fixes (formats `34ddc63`, runtime `ModPack.cpp`)

1. **PC class defaults of name-mapped logic classes.** A PC class mapped onto a PS2 class of the same name keeps the
   PS2 class's defaults; only cloned classes merge the PC defaults. The PC "count down to zero" class counts Off (0) down
   and On (1) up, the PS2 "Count down to Zero" the reverse, so every Alwarren counter fed by a timer's Off counted up.
   `ConvertOptions.keepPcDefaults` now writes, into each object of a name-mapped logic class (engine kind `logic` except
   Stomp Players, map markers and Air Currents), every int / float / bool / enum field the object leaves at its PC class
   default when that default differs from the PS2 one. The converter prints the changes per level (`PC class defaults kept
   on N object field(s)`); in the campaign they are counter messages and reset values, timer Start Mode, Time Until
   Trigger, Trigger Message and Disable/Re-Enable Message, detector Detect Only Once / Detect Method, and Fader times and
   start mode (Alwarren 229 fields, Volcanic 115, Islands 64, Rift Crystal 61, Succubus 49, Wartok 31, Grotto 21, Ruined
   Village 12, Grimstone 7, Rift World 4, Book 2). The enums agree on both games (PC `Dragon.rfl` lists "Run
   Instantly,Run When Triggered", "Toggle Visibility,Depends on Message", and the same message list as the ELF). Air
   Currents are left out: their PC class speed (Islands 1 lu/s against the PS2 16) is untested in flight. Test:
   `ootfpcdefaults.test.ts`.
2. **Keys are equippable.** On success the PS2 Use handler (`0x3168a0`) equips the matching key (`0x3179c8`, message
   0x1045) and its use action (`0x3195b0`) sends the key from Rynn's hand to the lock (handler `0x353550`, message 0x10e4);
   with nothing in hand the action waits for good (data+0xa48 / +0xa58 stayed 0, `+0xab0` 0x20). Every native key that
   opens a lock has "Equippable?" 1; the converted Keys took the template's 0 (the PC class has no such field). Converted
   Keys (engine 33) now get Equippable 1 (`CLONE_ADJUST`). Test in `ootfclass.test.ts`.
3. **Language trees.** The game in `dist/DrakanPC` runs the German tree (`language = german`). Its `LEVEL1ON.ODB` differs
   from the English one the converter reads: 578 class records against 582 (four English dialogue classes missing, a German
   one at 2356), and the Italian (585) and Spanish (583) trees add ids 2352 to 2356 too; every other ODB is identical
   (compared over all 21 ODBs of the four trees). Two consequences: the pack's new class 8:2356 (Alwarren's Visibility
   Toggler) lost to the German dialogue class of that id, so every converted toggler ran as runtime class 0x100ba (a
   dialogue line) and the hammer was never hidden (`alw-t`, against 0x10079 in English, `alw-t3`); and the pack's field-list
   override (0x0103, English lists plus one per new class) paired lists with the wrong records on the German tree. Fixes:
   the converter starts new class ids 16 above the ODB maximum (`LANGUAGE_ID_MARGIN`, test in `ootfclass.test.ts`), and the
   runtime, when a pack's 0x0103 override does not fit the container (own records + added records), keeps the container's
   own lists and appends the pack's last lists, one per added class (`rebaseFieldLists`, stored-block zlib; `mod_test`).
   In game with the private build, German: `field lists of an ODB with 578 own classes kept, the pack's 102 new lists
   appended`, togglers are 0x10079 again and the hammer is hidden at load (`alw-de`). Note: class ids of converted classes
   change with this pack, so OotF saves made with older packs may not match it.

## Converted cut scenes of the other levels

Each level started directly (`pack4`, English, god mode, `DRAKAN_PLAYTEST_HOLD=20`); Rynn placed outside the trigger, then
inside it (mounted starts dismounted first); the run waits until the scene has started, then for 300 uninterrupted
gameplay ticks, then D. Scene names from the pack's `LEVEL1ON.SSD`.

| Level | Trigger | Scene | Plays | Ends, control back |
| --- | --- | --- | --- | --- |
| Wartok Canyons | Farmer Deector 478 | MagicFarmer_1 (`0x0008004f`) | 52.6 to 137.1 s | yes: D moves Rynn 5,300 units (`scene4-wartok`) |
| Grimstone Mines | Start Stomp Scene 3.1 (1251) | Interrogation (`0x00080038`) | 26.5 to 82.8 s, Rynn moved by the scene | yes: D moves her (`scene-grim`); "positioning the hider" also runs at load (12.5 s) |
| Grotto | Pressure Plate 308 (Arokh landed on it) | Grotto Fly Through 1 (`0x0008003b`) | 18.6 to 28.6 s | yes: D moves Rynn (`scene2-grotto`) |
| Islands | bell column (earlier) | Bell Hammer | see `bell-h` | yes |
| Alwarren | detector 2018 (earlier) | Bowling for Crystals | see `alw-new` | yes |
| Rift Crystal | trigger 393 | Wisp2 (`0x00080021`) | 13.7 to 19.7 s | yes: D moves Rynn 1,000 units (`scene2-riftc`) |
| Islands Night | Detector 4922 | Rimril dies (`0x0008000d`) | 15.2 to 58.3 s, Rynn moved by the scene | yes: D moves her 1,900 units (`scene4-night`) |
| Succubus Lair | Detect and Execute 3011 | ALC (spiral puzzle) (`0x00080029`) | 11.9 to 35.0 s | yes: D moves Rynn 1,400 units (`scene2-succ`) |
| Volcanic | earlier (9.1, 9.2, 9.6a, Arrow No Flame) | | see `vol-a`, `vr11` | yes |
| Rift World | Trigger 183 (mounted) | 10.1a (`0x00080053`) | 21.9 to 54.3 s | yes: D moves Arokh (`scene2-riftw`) |

Harness notes: a `place` holds Rynn for 240 boundaries unless `DRAKAN_PLAYTEST_HOLD` is set (W during the hold does
nothing); `sync play N` returns at once when the game was already in gameplay, so it only waits for a scene's end after a
wait long enough for the scene to have started; several scenes wait seconds for their resources after the trigger.

## Still open

* The war giant kill (step 6) and thus "Bellhammer1" and the hammer reveal by the PC chain, unplayed; the harness needs
  aiming at a target (or a recipe that lets Rynn's targeting turn her).
* The load stall after long Alwarren sessions or with the hammer and its pickup chain (step 10): intermittent, the
  allocator spins during the Islands load; needs a heap trace of the transition.
* The Grotto scene started when Arokh was placed next to the plate; which object pressed the plate was not separated.
* Air Current defaults (left on the PS2 values) and the Fader / timer default changes in levels other than Alwarren were
  checked in the converted data and through the scene runs above only.
* The German in-game check used a private build of the working tree (including other sessions' uncommitted runtime
  changes); `dist/DrakanPC` still has the old runtime until the next deploy.
