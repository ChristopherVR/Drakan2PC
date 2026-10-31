# Multiplayer arenas: PvP in a dedicated map instead of the campaign (2026-10-31)

Reference: `docs/reference/netcode.md` "Multiplayer maps". Tags: **[game]** measured in two game instances / one instance,
**[test]** unit tests, **[open]** not working or not verified.

## What was built
* Direct launch [game]: redirecting `Level 0 - Intro` (what New Game opens) to the map's base level loads the arena straight into
  gameplay: no movie, no cut scene, no taps (`build/harness/exp1`). `drakan_lifecycle::setLevelNameMapper` makes every
  listener see the real level name (ghosts treated the level as the cut scene "Level 0" otherwise).
* World filter [game]: Surdana 4601 -> 4189 entities, creatures with HP 137 -> 0, loose items 107 -> 0 (`arena_surdana-village`
  dumps `arena_before/after.json`, `exp_cam2`); Inquisitor 689 -> 731 (own pickups added back), creatures 23 -> 0
  (`arena_inq3`, `arena_pvp_inquisitor-depths`), City 693 -> 525 (`exp_city`). Class list in netcode.md.
* Saving vetoed (`drakan_lifecycle::setSaveWriteVeto`), journal/map markers emptied, quick save/load ignored. The veto line
  was not triggered in the runs (nothing tried to save); only the hook is verified to compile and install. [open]
* Server spawn choice [test][game]: farthest from the others, never the previous point; two instances got spawns 0 and 2
  (`arena_inq3`). A stale published position (level start) is not trusted for 8 s after a spawn (unit test).
* Teleport placement retry [game]: the engine puts a player back when the floor at a far target is not resident; retried
  every 40 boundaries, took 1..5 tries across 46k units (`exp_inq2`).
* Ghosts of players 10k..46k away appear and move [game] (`arena_range-*`, `arena_pvp_inquisitor-depths`).
* Pickup protocol (PickupClaim 23 / PickupState 24, separate block after the co-op messages, same wire version) and server
  rules [test]: claim, refusals (dead, far, taken, bad index), respawn after the delay, reset at a new Playing phase, late join.
* Map format, compiled-in defaults (`configs/mp_maps.ini`), player file, mod pack `maps/*.ini`, `drakan_arena::maps()` [test].
  `drakan_server --level <map> [--maps file]`.

## Follow-up (same day): what was found and fixed
* **Item spawn fixed [game]**: `IGuest::post` passes four arguments, the factory takes six (the `resIndex` was garbage). The post
  now goes to a host trampoline like the ghost's (`itemStage1/2`, slots 0x100400+). Melee weapons and, on most runs, potions
  appear at the requested spot and are bound as pickups.
* **Pickup end to end [game]** (`arena_meadow`, instance A): the player moves into an item and the engine takes it (auto pickup
  by touch: the class record has "Max Horz AutoPickup Distance" / "Max Relative AutoPickup Height"); the arena sees it gone
  -> `claim: pickup 5` -> `server: pickup 5 (Invisibility Potion) taken by player 1, back in 15 s` -> `pickup 5 ... is back`
  (screenshots `arena_<n>_pickup5_taken_here.png`, `..._back.png`). A teleport onto an item or a stepwise write of the
  position does NOT trigger the pickup (the engine overwrites the written position; `DRAKAN_ARENA_TEST_GOTO` and the Use taps
  never took an item). Use (L1) is not the pickup button. [open] B's factory spawns of potions / bow sometimes never show up
  (`the respawn never showed up`; retries raised from 3 to 8), so "B removes its copy" was not seen; weapons were fine on both.
* **Starting loadout [open]**: the spawn part works, picking it up needs the player to walk into it, equipping is manual. A New
  Game on a level other than Surdana has no weapon at all (Inquisitor, City). The equip message `0x1045` -> `0x31e460` was
  located but its payload not decoded. Surdana's arena works because the dagger is in the new game.
* **Fight [game]**: `arena_meadow` two instances on `surdana-meadow`: spawns 0 / 2, 7+ hit claims accepted, the match ended with
  results (PASS on spawn, claim, world dump, fight, "pickup back on both"; FAIL only on the server-accepted / B-removed regex
  because B's potions never spawned). Not run on a non-Surdana map for the missing weapon; not run through the new menu (the
  menu agent's rows were not re-read; `arena_two_instances.py` uses the DRAKAN_NET_* shortcuts).
* **Surdana Village dropped**: the keep floats over a void (players fell, `lower 208a0` frozen). Replaced by `surdana-meadow`
  (the start meadow: spawn at Rynn's start and flowers, valid ground, no void).

## Harness runs (build/harness, git-ignored)
`exp1` direct launch, `arena_surdana-village`, `exp_cam2`, `exp_city2`, `exp_inq2` (tours with `tour_sheet.png`), `arena_range-{10k,21k,36k}`,
`arena_pvp_inquisitor-depths`, `arena_inq2`, `arena_inq3` (two instances).

## Process notes
The build lock `build/final-game.build.lock` was left behind by this work for an hour; released. Other agents' broken string
literals (a `\n` turned into a real newline by a shell heredoc) blocked the game build twice; two one-line fixes in
`drakan_settings_host.cpp` and `drakan_ghost.cpp` were made to unblock. `configs/mp_maps.ini` is read at CMake configure time:
rebuild after editing it.
