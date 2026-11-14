# AI run limit raised to 8, shadow distance default 3 (round 6)

Owner decisions on brief C and the shadow distance of the [engine limits survey](engine-limits-survey.md): raise the
level's "AI Run Max" to 8 by default and make `[video] shadow_distance` 3 by default. Both are in
`drakan_engine_limits.cpp`, rules and tests in `runtime/world/include/world/EngineLimits.h` (`world_engine_limits_test`).

## How the AI limiter works (PAL)

The limiter is a small structure at `0x5130b0`: `+0` slot list (16-bit object indices), `+4` capacity, `+8` slots in
use, `+12` run max.

* `0x2dc718` runs at level load: it reads the level's "AI Run Max" (4 on the disc levels and in the converted OotF
  levels), stores it at `+12` and empties the list.
* `0x48a4a0` runs for every creature whose class has "Controlled by AI Limiter?" set. While fewer than `+12` slots are
  in use, the creature takes one (the list grows in steps of 8 through the guest allocator, so a larger max needs no
  fixed array). When the list is full, the creature farthest from the player loses its slot to a nearer one; a
  creature without a slot does not think.
* Only those two functions read or write `+12` (the other users, `0x2d9178`, `0x46dac8`, `0x48b308`, `0x48d0d0`, read the
  count, remove an entry or clear the structure). A negative run max means "no limit" to `0x48a4a0`; 0 means none.

What the RAM shows: a creature keeps its slot after the player has left it. In the Surdana dumps the Purple Orc
Flanker at the Wartok cave mouth (object 295) and a villager in the town (296) still held two of the four slots while
Rynn fought at the orc mine, far out of their sight. With 4 the nearby creatures share what is left; with 8 those stale
slots no longer block anyone.

## What changed

`[general] ai_run_max` (env `DRAKAN_AI_RUN_MAX`), default 8, 0 = the level's own value, at most 64. The wrappers of
`0x2dc718` (after it returns) and of `0x48a4a0` (on entry, when the stored value is not the one last left there) write
`max(level value, ai_run_max)`; a level value of 0 or below is left alone. `ai_run_max = 0` installs no wrapper at all,
so the original path is one setting away. Logs: `[engine-limits] AI run max: level 4 -> 8` at each level load and
`[engine-limits] AI run slots in use: N (level max 4, run max 8)` each time the peak rises above the level's value
(`DRAKAN_ENGINE_LIMITS_LOG=1` logs every rise).

`[video] shadow_distance` default 3 (was 1; 1 = the original distances). Changed in the `drakan.ini` defaults, the
code's fallback (`world::shadow_distance::kDefault`), `env-flags.md` and the test. It is not in the settings menu. A
`drakan.ini` that already has a `shadow_distance` line keeps its value (the owner's `dist/DrakanPC/drakan.ini` has none,
so it gets 3).

## Evidence

All runs: the owner's `drakan.ini` (copy of `build/review/baseline-play`), HD textures, DLSS, the current
`dist/DrakanPC/mods/ootf-levels.zip`, exe built from `af23dd91`'s code. Fights are placements (`place` in the input
script, god mode) with Rynn swinging every 0.7 s, not played routes; the OotF opening is the campaign start with no
input. Slots read from RAM dumps (`dump ram_*`) at 10 / 20 / 30 s after the placement. Scratch:
`build/review/limits-ai/` (`runs/`, `evidence/`).

| Scene | ai_run_max 4 (original) | ai_run_max 8 | Picture |
| --- | --- | --- | --- |
| Drakan 2 Surdana, three Wartoks at the cave mouth (`441400 174950 384300`) | (survey: 2 to 3 of 4) | 3 of 8; fight and quit clean | `evidence/surdana_wartoks_ai8_s25.jpg` |
| Drakan 2 Surdana, orc mine entrance (`678000 160300 506000`) | 3, 3, 4 of 4 (full at 30 s; the town villager 296 holds one) | 4, 5, 4 (peak 6) | `evidence/orcs_sheet.jpg` (top 4, bottom 8): two orcs on Rynn in both |
| Drakan 2 Surdana, spider cave with Wartoks (`608000 152800 507000`) | 4, 3, 3 of 4 (peak 4) | 5, 6, 6 (peak 6) | `evidence/wcamp_sheet.jpg`: the 4 run was lifted out of the cave by the placement, not comparable |
| OotF Ruined Village, Scavengers (`385408 26500 219892`, direct start) | 4, 4, 4 of 4 (full the whole run) | 7, 7, 5 (peak 7) | `evidence/rv_sheet.jpg`: two or three Scavengers on Rynn in both |
| OotF Wartok Canyons start (mounted, the placement does not move Arokh) | 4, 4, 4 of 4 | 5, 5, 5 (peak 6) | `evidence/canyon_sheet.jpg`: the orc at the gate attacks in both |
| OotF campaign opening (Intro cut scene, Ruined Village raid scene, no input, 55 to 130 s) | the scene plays | the same scene, same shots in the same order (subtitles shown) | `evidence/opening_sheet.jpg` (top 4, bottom 8; the clock is input samples, so the two runs are a few seconds apart) |

No `bad resume`, exception or stall line in any run; every run reached its scripted quit (the opening needed a recipe
without `sync play`, which never fires while the cut scenes run). The limiter list is empty during the opening's cut
scenes (capacity 0), so the setting cannot affect them.

What 8 changes in these fights: more slots in use (5 to 7 above the old 4), mostly creatures that keep running out of
sight; the number of enemies on Rynn at once was the same in the pictures (two or three), because only that many were
close. No pile-up, pathing collapse or broken script was seen. Fights with more than four enemies close by (a big camp
triggered by play) were not reached by placement: the large Surdana orc camp (`529536 675008`) is hidden until a trigger.

Shadow distance: the before/after pictures of the setting are the survey's (`build/review/limits-survey/evidence/
town_villager_sd1_vs_sd3.png`); with the new default and no override the log reads `installed (shadow distance x3.00 ...)`
and `level 15 / 30, squared 566231 / 1132462 (x3.00)` in every run above.

## Frame time

`scene_ab_bench.py` under its turn lock, owner's ini, the `surdana-fight` attack script at the orc mine pose
(`--place "678000 160300 506000 -1.5708"`), `DRAKAN_AI_RUN_MAX` 4 against 8, three interleaved pairs
(`build/review/limits-ai/bench/orcs-ai`). One other game instance ran during all six runs, and a compiler during the last
8 run. The 8 runs logged 5 slots in use, the 4 runs at most 4.

| Pair | EE ms per world frame, 4 / 8 | Median frame ms, 4 / 8 |
| --- | --- | --- |
| 1 | 4.17 / 9.16 | 6.07 / 8.93 |
| 2 | 8.49 / 8.89 | 8.47 / 8.80 |
| 3 | 19.43 / 10.18 | 9.40 / 8.67 |

Medians: EE 8.49 (4.17 to 19.43) against 9.16 (8.89 to 10.18) ms, frame 8.47 against 8.80 ms (+3.9 %, 8 faster in 1 of
3 pairs). The spread of the 4 runs is larger than the difference, so no cost is measurable; the 4 run of pair 1 is an
outlier (159 fps; its frames were not compared). At most one more creature thinking in this scene, so a cost of
the order of a few tenths of a millisecond of EE cannot be ruled out.

## Tests and builds

* `world_engine_limits_test` (Release, `cmake -S runtime/world`): all checks passed (new: `ai_run_max::raised`,
  `shadow_distance::kDefault`).
* Game build under `build/.game-build.lock`, `-Incremental -Jobs 8 -NoWarnings`, `build/final-game`, exit 0.
* `clang-format --dry-run --Werror` on the changed C++ files, `repository_guard.py` clean.

## Not verified

* A fight with more than four enemies close to Rynn at once (camps that wake on a trigger) in real play.
* Played routes: every fight above is a placement; no campaign was played through with 8.
* Drakan 2 scripted encounters past the Surdana opening (Ravenshold was not run).
* Long sessions: the slot list grows through the guest allocator; only up to capacity 8 was seen.
