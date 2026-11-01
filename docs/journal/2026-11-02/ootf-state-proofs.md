# Proving puzzle chains, bosses and creatures by state (2026-11-02, track C3)

Method: `DRAKAN_WORLD=2 DRAKAN_WORLD_DUMP_FRAME=n,m,...` (now a list; counts samples per level; `DRAKAN_WORLD_DUMP_LEVEL=<substring>`)
writes world JSON dumps, `scripts/harness/world_diff.py` lists what moved. Campaign runs (`DRAKAN_GAME=d1`, pack from
`ootf-convert.ts --campaign 1`). **Harness lesson:** earlier "tc_*" runs used `DRAKAN_START_LEVEL` and `sync play`, which fires in the
D2 intro level, so their screenshots / dumps were not of the converted level. Dump frames also count loading samples; the recipe clock is
in input frames, not game seconds. `DRAKAN_PLAYTEST_POS` teleporting a level that starts mounted (Wartok) crashed the guest at gameplay
start (missing-target JALR at 0x10822c); no teleport there, test aids instead.

| Item | Result |
| --- | --- |
| Switch -> Portcullis (Wartok, ids 2986 -> 2989, `DRAKAN_DBG_PULLIDS` ring 500 units) | **proven by state**: Portcullis y 13900 -> 15538 (+1638) after the E tap, player unmoved, all other copies still; run `c3_sw` |
| Pressure plate -> movers (Wartok) | **proven by state** with the detector put at the player (`OOTF_TEST_PLATE_AT`): the Secret Entrance movers (res 0x33a29 / 0x342f8 / 0x34311 near 233000 33600 129000) moved 219-307 units and a second portcullis (res 0xb093f) rose 1493 after the detector fired; a no-detector run moved nothing; run `c3_pl`. The detector over the real plate was not walked (teleport crash above) |
| Real PC triggers (Islands Detector 4131, Volcanic 2318, Succubus ship2) | **replaced by design**: `exitBoxes` places walkable boxes; those are the verified path (`ootf-link-regression.md`). Not walked in their original shape |
| Dr. Evil (Rift World, 4 giant stand-ins; `OOTF_TEST_WAKE=4 OOTF_TEST_HEADHP=3`, player teleported to the altar, `DRAKAN_PLAYTEST_GOD=1`, attack taps) | heads wake and walk to the player (distances 15000 -> 3500); **3 of 4 died** (hp -12) in `c3_rw4`; the 4th (22735 units away, did not come) stayed alive, so the counter (needs all 4) did not fire and the ending was not reached. Without god mode the player died in ~10 s to Delon / fire (health orb black), the level dropped to the main menu. The counter -> credits chain itself was proven earlier with timers (`ootf-puzzles-ending.md`) |
| Ebon Dragon, Drone (Islands, ring at the real player position, `DRAKAN_DBG_PULLAT`) | **alive and loaded**: 2 Ebon Dragons (hp 225, model 0x31abd) and 2 Drones (hp 10, 0x31acc) at exactly 1500 units in the dump; dark winged silhouettes beside Arokh in the shots; not a clean close-up (the player is mounted, camera behind) |
| Queen, Navy Hueges, Tick, bats | not re-run this pass (Queen / Hueges alive per `ootf-unproven-creatures-chains.md`) |

## Dragon PvP
* **Dying dragon fall: implemented** (`drakan_pvp.cpp` `fallDragon`): gravity 2600, roll 2.2 rad/s, down to `drakan_arena::floorHint` (the
  lowest spawn point height, the host has no terrain query) or 7000 units. Two-instance run `c3dragons` (`arena_dragons.py`, 9/9 PASS):
  `[pvp] dragon shot down at height 20650: falls to 20400`. The air arena's launch pads are the low points, so the visible drop is
  small there; a real terrain height lookup is still missing.
* **Breath visual: not done.** The flame seen in Rift World is the enemies' fire, not Arokh's breath (checked: the player's orb died
  with it). An engine effect still has to be spawned (factory `0x181ad8` with a flame emitter record, e.g. the FX "BaseFire" class kept
  by `ootf-fx-flames.md`) and pinned to the dragon's mouth; needs a resource index of an emitter in the arena pack.

## Changes
* `drakan_world.cpp`: dump frame list, per-level counting, `DRAKAN_WORLD_DUMP_LEVEL`; `scripts/harness/world_diff.py`.
* `drakan_playtest.cpp`: `DRAKAN_PLAYTEST_GOD=1` keeps the player's HP at maximum.
* formats: `OOTF_TEST_PLATE_AT`, `OOTF_TEST_HEADHP`, `DRAKAN_DBG_PULLAT` (formats 3d6a1dc).
