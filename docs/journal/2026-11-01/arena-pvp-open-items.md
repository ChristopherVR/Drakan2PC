# Arena PvP open items: ghost collision cells, first placement, ranged run (2026-11-01)

Follow-up to `arena-ranged-pvp.md` and `arena-match-stale-position.md`. Base port 28600 (inquisitor-depths 28600, city 28610,
ranged 28620). Build `build/final-game`, harness copies of the play folder. Tags: **[game]** measured in two instances.

## 1. Real blade hits on the ghost (inquisitor-depths, city-skeleton-square) [game]
Cause: the engine's refile (`0x1496a0`, vtable `+0x8c` of `*(world+0x24b4)`) starts with
`if (actor[+0x7c] == -1) return;` after the bounds update. An actor whose cell index is -1 is never filed again. A ghost that
is created while no cell is resident at its spawn position (the second ghost of a session is made right after a level reload
and the first in the instance that has not streamed the spot yet) has cell -1 for ever, whatever position it is moved to; the
sector update then "succeeded" with cell -1 and the blade sweep found nothing. It was not the floor of the arena (the lookup
itself, vtable `+0x6c`, finds cell 11 / 12 / 37 at the same coordinates a moment later; the same ghost position had cell 11 in
the other instance).
Fix (`drakan_ghost.cpp refileHandle`): when the cell is still -1 after the normal refile, call the lookup by hand (old cell
-1, flags 5, the actor's position), write the index into `+0x7c` and run the refile again, which then links the actor into
that cell's list. Log (`DRAKAN_GHOST_LOG=1`): `actor X had no collision cell: filed in cell N (now N)`.
Proof, without `DRAKAN_PVP_TEST_CLAIM`:
* inquisitor-depths (`build/harness/pvp_inq_3`): `claim: hit on player 2, weapon 0, damage 11` from real swings, server `hit:
  player 1 hit player 2 for 88 (weapon 0), health now 12`, then 0, `death: player 2 killed by player 1 (weapon 0)`.
* city-skeleton-square (`build/harness/pvp_city_1`): claims with damage 16, three deaths, `match over (frag limit): player 1
  wins, results (kills/deaths): [1 'Alpha' 3/0] [2 'Bravo' 0/3]`.
The pickup / quick-save checks of `arena_two_instances.py` failed in these two runs: the level was loaded twice and the
recipe's key presses (22 s) came before the second load finished on the loaded machine; not related to the change (the claim
refusals read `player 1 is too far (distance 5085 ...)` for the walk-to-pickup test aid).

## 2. Ranged run [game]
* Mana: the console `heal` was never needed. The stats block keeps current / maximum mana at `+0x1c` / `+0x20` (same encoding as
  the HP words, enc(v) = rotl32(v, 8) ^ key). A new game has **maximum 0** (empty orb, `mana word 0`, no casts at all in one
  run); `spellTick` now raises the maximum to 100 and refills the current mana every 300 boundaries by a direct write. Log:
  `spell: mana refilled 71 -> 100` (casts cost 12-15 points each). The refill does not touch health (the server owns it).
* A bow at the start without walking onto it: the level's own bow is filed into the inventory with the stow routine, like a
  loadout item (`DRAKAN_ARENA_LOADOUT_ADOPT="x,y,z"`, the adopted item is protected from the world filter): `loadout adopt:
  the level's item ... (class 1004f)`, `loadout: 1 items given`, `armed: the bow (actor ...) is equipped`. Each instance has its own
  copy of the level's items, so every player can get one. Cloning through the factory's object argument (`a3` of `0x181ad8`,
  the path that builds from an existing object record) was not tried: the record layout is not known. Spawning a bow from its
  resource id stays impossible (self ref stays 0) on this level's resource set.
* Harness: `arena_ranged.py --adopt` (map `ranged-adopt`, spawn yaw 90: with yaw -90 B's fireballs burst on the tent poles and
  table ~200 units in front of the spawn, none reached the ghost; with 90 about one in twenty casts crosses the ghost), `--a-delay`
  (Alpha holds fire 40 s so the arrows do not end the match before a spell lands), frag limit 6, the "HP drop replicated"
  checks also accept the lethal hit (`killed by player N` is logged instead of `hit by`).

## 3. First placement races the bounds check [game]
The log order showed the cause: `player placed at spawn 0 ...` (during the load) and then `level up: ... player at <level
start>`, i.e. the game applied its own start position after the placement. The placement "took" (checked once, 40 boundaries
later, while the old position was still there) or was retried while terrain streamed, and the bounds check later fetched the
farthest spawn. Fix (`drakan_arena.cpp`): the assigned spawn is kept across level loads and applied again when the level is up
(`level up: placing at the assigned spawn N again`; also re-arms the loadout, which a reload resets), and a placement that
took is watched for 120 more boundaries (`pulled N units away from spawn S right after the placement: placing again`). Seen in
every run above (inquisitor-depths, city, surdana ranged): `placing at the assigned spawn` fired in each instance and no
`back to spawn N` followed.

## 4. Result of the ranged harness and the removed-pickup crash [game]
* `python scripts/harness/arena_ranged.py --port 28620` (default, bow picked up from the ground; frag limit 8, `--a-delay 10`,
  Bravo's ghost-of-Alpha at 350 units): **14 of 14 checks passed in one run** (`build/harness/ranged_pick_1`): arrows and
  spells both crossed the ghost, the server accepted both (weapon 2), kills credited to both players, match over. The
  `--adopt` variant passed 13 of 14 twice (run 3: only the `hit by player 2` regex, which missed a lethal hit, since fixed;
  run 4/8: the other side's single kill arrived after the match ended / the machine was too loaded for B to join in time).
* Coordinator report: B's guest thread died with `JALR source=0x341548 target=0` on the removed pickup actor when A took the
  bow. 0x341548 sits in `0x341510` (equip check), called with an inventory cell's / hand slot's handle. A player who stands
  next to the pickup (B spawns 300 units from the bow) can have the engine take its local copy into her inventory by touch;
  when the server then says "taken by player 1" the arena destroyed the actor and left the cell and hand slot pointing at it.
  `dropFromInventory` (drakan_arena.cpp) now zeroes every cell / hand slot that references the handle and marks the list dirty
  before the kill (log: `pickup copy X was already in our inventory: N reference(s) cleared`). **Not proven**: the crash did
  not reproduce in my runs (no `guest-branch` line, and the new log line did not appear either), so the diagnosis is from
  the disassembly, not from a reproduction.

## Not done / open
* A map-file key for an adopted loadout item (the ini only knows factory resources).
* The regression triage note (`regression-triage.md`) concludes the "B frozen at the title" runs were ~5 fps load, not a hang.
