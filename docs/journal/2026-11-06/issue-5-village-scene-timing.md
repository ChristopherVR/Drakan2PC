# Issue 5: why the Ruined Village opening ended at a different time in every run

Follow-up of [issue-5-village-scene-and-title-notes.md](issue-5-village-scene-and-title-notes.md), which found that the
Ruined Village opening ends on its own but between +140 s and later than +174 s after gameplay start. Full 23-level pack
(`build/issue-4-full/ootf-levels.zip`, `packs/full`), audio on, no input after New Game, same driver as before
(`build/harness/scene-timing/drive.py`, a copy of `verify-2-4/drive.py` writing to `build/harness/scene-timing/<run>`).
Scene state was logged without stalling the game by the existing `DRAKAN_STOMP_PROBE=1` tick line (once per second:
sequence, elapsed, action cursor, native `dt`), plus `DRAKAN_FRAME_AUDIT=1` for the game clock. Helpers (scratch, not
committed): `ticks.py` (per-sequence listed / started / wait from the probe log), `seqdump.py` (the STOMP manager
`0x4dba68` in RAM dumps), `texcache.py` / `descs.py` (native resource caches), `heap.py` (guest malloc chunk walk).

## What the opening is

Ruined Village plays three STOMP sequences back to back, all converted with their PC times unchanged
(`ootfseq.ts` copies the f32 action times):

| Ref | Name | Length | Actors | Started by |
| --- | --- | --- | --- | --- |
| `8003c` | 1.1 (orc ambush) | 61.19 s | 30 | the level |
| `8003d` | 1.2a (captives, dying villager) | 52.85 s | 55 | "Trigger 1.2" at 61.19 s of 1.1 |
| `8003b` | 1.3 | 90.76 s | 10 | "Trigger 1.3" at 52.85 s of 1.2a |

The earlier "scene end" (Rynn jumping from the fixed opening point) is the switch from 1.2a to 1.3.

## Not the cause

* **Frame rate / wall time.** Scene elapsed time follows the wall clock: 3.0 s of `elapsed` per 3.0 s of log time at
  100 to 450 fps (`world_rate = uncapped`), and `dtSum` per 2 s window = 2.00 s. The native tick adds the measured
  `0x4de604` (1 ms game clock); it only loses time in a hitch longer than the engine's 0.1 s `dt` clamp. A RAM dump is
  such a hitch, but a whole run of 10 s dumps lost about 2 s (`idle600`: 1.2a's start estimate drifts 62.3 to 64.2), not
  30. Running at the original cadence (`DRAKAN_WORLD_RATE=30`, `DRAKAN_FRAME_LIMIT=30`, run `cap30`) did not help.
* **Converted timer values.** 1.1 ran 60.6 to 61.0 s in every run, 1.2a 51.9 to 52.8 s once started; both match the
  authored lengths. Nothing random in the scripts.
* **Audio.** Not varied (audio was on in every run); the wait below is the preloader waiting for cache entries, and
  the pending entries it named were models and textures, not sounds.

## The cause: 1.2a's start waits for its textures

A STOMP sequence at action 0 first calls the preloader (`1a49f0`) for everything its first 4 s use; while that returns
"pending" the sequence stays at action 0 with `elapsed` -0.003 (`1a4c50`). The 4 s of 1.2a cover 55 actors (villagers,
knights, cart, slave, weapons). In the slow runs the probe showed it pending on model and texture entries (texture
`0x31cda`, model `0x31a1e` of Huege P1/P3, model `0x31d63` of Axe2), and the RAM dumps show the native texture cache
(manager `0x4db280`, budget at +4, bytes in use at +8) full in every dump: 0x565748 of 0x566666 used, with the needed
textures absent and the cache's load counter frozen for the whole wait (`full4i`, 34 s of dumps).

`1bf3e0` (texture manager vtable +0x54) is what the cache does when a texture would overflow the budget: it may only
recycle an evictable buffer of exactly the requested size among the 64 (96 above 32 KiB) least recently used
unreferenced entries. When none fits, the texture is not loaded and the scene keeps waiting until some buffer happens to
free up. How long that takes depends on which textures the previous scene and the visible world still reference, so it
varies from run to run:

| Run | Texture budget | 1.2a waited |
| --- | --- | --- |
| `idle300`, `idle600` (verify-2-4) | native | about 0 s |
| `probe2` | native | 0.3 s |
| `off1` (`DRAKAN_TEX_CACHE_KB=0`) | native | 2.2 s |
| `full4w` (verify-2-4) | native | about 16 s |
| `probe1` | native | 20.4 s |
| `full4i` (verify-2-4) | native | more than 76 s (still waiting at the end) |
| `cap30` (30 fps cadence) | native | more than 110 s (still waiting at the end) |
| `tex8m` (`DRAKAN_TEX_CACHE_KB=8192`) | 8 MiB | none (started on its trigger) |
| `v1`, `v2`, `v3` (fixed build, default) | 8 MiB | none (started on its trigger) |

That is the whole spread: 1.1 and 1.3 never waited, and once 1.2a starts the rest is fixed. In the original D1 PC game
everything is in memory and 1.2a starts the moment "Trigger 1.2" fires, so the wait is a port defect, not original
behaviour. The disc's own levels are built for the 5.4 MiB cache; the converted levels keep more and bigger textures
resident (in the full cache: 105 8-bit 128x128, 23 RGBA 128x128 and two RGBA 256x256 textures).

## Fix (`fix(game)`)

`drakan_texture_budget` (PAL) wraps `1bf3e0`: while a campaign pack is the current game it raises the texture
manager's budget to 8 MiB whenever the cache would overflow (the engine sets the budget itself, 7 MiB at boot and
0x566666 in a level, so the raise is applied again after each reset; it never lowers a budget). The disc game keeps the
native budget. `DRAKAN_TEX_CACHE_KB=<KiB>` (1024 to 16384) sets the budget for every game, `0` turns the raise off.
Policy in `drakan_texture_budget_policy.h`, test `texture_budget_policy` (runtime-tests preset).

Verification on the fixed build (`6d44336b...`, three default runs and one `off1` control, same recipe):

* 1.2a starts on its trigger in every run; 1.3 (the old "scene end") starts at probe time 186.8, 186.6 and 184.0 s,
  2.6 s apart, and that remaining spread is how long the game took to boot and reach New Game (1.1's start differs by
  the same amount). At 140 s after gameplay start 1.2a's elapsed time was 44.36 and 44.78 s in the two runs with dumps.
* The log names the raise: `[texbudget] texture cache budget 5662310 -> 8388608 bytes (in use 262520, request 66688)`.
* Guest heap (`heap.py`): 18.8 to 19.3 MiB in use with the raise against 15.3 to 16.6 MiB before, heap top at
  0x1914000 (about 25 MB, the main stack is at 0x1fbfdf0). The cache fills the new budget too (0x7ffe30 of 0x800000).
* After 1.3 no sequence is active (`seqdump.py` at +255 s): the player has control, as before.

## Remaining gaps

* Only Ruined Village's opening was measured. The cache fills any budget, so a later converted level with an even
  larger scene working set could wait again; the probe tick line shows it (`wait` at action 0).
* The heap margin is smaller by about 2.6 MiB; no campaign run past Ruined Village has been made on the fixed build.
* The converter could shrink the working set instead (the RGBA 128x128 and 256x256 textures as 8-bit indexed); not
  done here, the formats submodule had another session's uncommitted work.
