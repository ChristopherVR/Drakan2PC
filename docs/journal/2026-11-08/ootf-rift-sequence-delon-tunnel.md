# Issue 5: the Rift World "6 s sequence", Delon on foot, and the tunnel flight

Follows [the rift flight note](../2026-11-07/ootf-rift-flight-and-delon.md). Private runs under `build/ootf11/h/` (own copy of
the 16:27 executable in `build/ootf11/play`, sha256 `5c63cef4...`), god mode, other agents' games left alone. Pack: a fresh
`ootf-convert.ts --ootf build/ootf/cab_disc --ps2 build/odbx/data --template LEVEL1NA --campaign 1` from formats `9e1856f`
with **no `OOTF_TEST_*` variable set** (`build/ootf11/pack`, 23 levels, 10,751 files, 2 min 30 s).

## 1. The sequence that moved Arokh into the rift is the ending test aid

The sequence ref `0x00080052` is PS2 SSD record 82 of `LEVEL1ON.SSD`, in folder 81 "OotF OotF 12 - Rift World". Its body
starts with the name **"end sequence"**: it is the PC "end sequence" (PC SSD id 115, 50 actions on rynn, arokh, camera, Delon,
the mirror room, the faders and the GUI changer; the probe counted 51 with the converter's additions). Records 83 and 84 are
"10.1a" and "10.2a". In the PC level only Stomp Player 473 plays it, after Ribs timer 472, after the Navros DeathCounter 162,
which the four Dr. Evil heads (108 to 111) count down when they die. The scene's first arokh actions place him over the altar
hole (341767 94803 183845), which is where the flights saw him "jump" before he fell into the pit.

What fired it 6 s after the start: the pack the earlier runs used, `build/harness/ootf7/packe`, was converted with
`OOTF_TEST_ENDING` (the test aid of `ootfpuzzle.ts` `testEnding`, introduced for the ending proof of
[2026-11-01](../2026-11-01/ootf-puzzles-ending.md)): four timers at 0,0,0 send the death message to counter 162 a few seconds
after the load. The converted Rift World of `packe` has 157 objects, four of them those timers (568 to 571, class 8/128, links
162); the clean pack has 153 and nothing but the four heads links 162. The credits note of 2026-11-07 used that property on
purpose ("the pack that carries the ending timers"); the flight runs inherited it by accident.

In game, same executable, same idle direct start of Rift World with `DRAKAN_STOMP_PROBE=1`:

| Run | Pack | Arokh over 890 input samples | Sequence ticks |
| --- | --- | --- | --- |
| `watch-packe` | `ootf7/packe` (ending timers) | moved to 341767 94803 183845, then falls to y 83,491 and below | `ref=00080052`, 51 actions, from 34.7 s |
| `watch-clean` | `ootf11/pack` | stays at 342418 100119 319535 | none |

So it is not a conversion bug and not part of the PC level's opening. No converter or runtime change. Recipes that fly or
fight in Rift World must use a pack built without `OOTF_TEST_ENDING` (and without the other `OOTF_TEST_*` aids unless the run
needs them).
