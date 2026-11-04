# Order of the Flame co-op campaign harness

`scripts/harness/ootf_coop_campaign.py` runs the converted Drakan: Order of the Flame campaign as a host + joiner pair and checks
every level automatically. It builds on `netplay_two_instances.py` (private play folders `build/ootfcoop-a` / `-b`, own memory
cards) and the single-level `ootf_level_run.py` (extraction of the PS2 template data into `build/odbx/data`).

```
python scripts/harness/ootf_coop_campaign.py                       # all 13 levels, one co-op session each (about 3 min per level)
python scripts/harness/ootf_coop_campaign.py --levels 1,3 --timeout 150
python scripts/harness/ootf_coop_campaign.py --chain 12            # Intro, then exit 0 of every level in turn (probe DRAKAN_LEVELCHANGE)
python scripts/harness/ootf_coop_campaign.py --solo --levels 7     # same checks, host only
python scripts/harness/ootf_coop_campaign.py --name all1 --reanalyse   # recompute the report from saved logs
```

* The pack (all 13 levels, `--campaign 1`) is converted once into `build/ootf-run/coopcamp/mods` (`--reconvert` after a formats
  change; `DRAKAN_MODS` points there, `dist/DrakanPC` is untouched).
* Instances are started with `DRAKAN_GAME=d1`; A is `DRAKAN_NET_HOST` + `DRAKAN_NET_MODE=coop`, B `DRAKAN_NET_CONNECT`. The built-in
  recipe waits for gameplay (`sync play 300`), walks W (A) / A (B) twice and takes `l_start`, `l_walk1`, `l_walk2` (chain mode: a
  screenshot every 8 s). `DRAKAN_START_LEVEL=<title>` selects the level; in the campaign the New Game redirect plays the Intro first,
  so control is only counted after the `afterLevelLoad` of the expected level.
* The game lock `build/ootf-run/game.lock` (mkdir) is taken for each session only and released afterwards; a lock older than 40 min is
  treated as stale. The harness only kills its own PIDs.

## Checks (report fields)

| Field | Meaning |
| --- | --- |
| `loaded`, `loaded_levels` | `afterLevelLoad ... ok=1` for the expected title (chain: the list of levels reached) |
| `control` | GUI `gameplay=1` or periodic `[in-log] ... play player=` after that load, before another level loads; this proves the input profile, not successful movement or quest progression |
| `crash` | `missing-target`, `[crash]`, access violation, non-zero exit code |
| `stall` | any `[stall]` or `[watchdog t=` line (input boundary silent for 8 s) |
| `hang` | the instance had to be killed by the harness (timeout + 45 s) |
| `flips_min`, `flips_median` | `completedFlips` of the 2 s `[frame-uncapped]` lines after control, first interval skipped (about 100 = 50 fps; the 13 levels run 70 to 350) |
| `ghost` | `ghost of player N '...' spawned` (co-op only) |
| `exits_taken`, `follow` | `exit taken in '<level>'`, `level follow: arrived` lines (chain mode) |
| `unexpected_not_found` | `[hle] ... not found` resources that are not the usual stand-in noise (missing pack content) |
| `puzzles`, `dropped_props_total` | converter statistics for the level from the pack's `convert.log` (plates, detectors, quest markers) |

PASS = loaded + control + recorded successful process exit + no crash / stall / hang + both ghosts (co-op), plus all requested
probe exits in chain mode. This is a load/transition smoke test; forced probe exits do not establish normal campaign completion.
Reanalysis preserves exit codes, hangs and chain requirements from `report.json`; a quit request in the log alone cannot establish
successful shutdown. Missing process evidence fails validation. Set `DRAKAN_INPUT_LOG=120` to sample the input profile when a
cinematic releases control without changing the GUI screen.

Output goes to `build/harness/ootf-coop/<name>/`
(`report.json`, `report.md`, `<level>/{a,b}/stdout_1.log` and screenshots); nothing there is committed. Exit status 0 when every
session passed. Results of runs: `docs/journal/2026-11-02/ootf-coop-campaign-run.md`.
