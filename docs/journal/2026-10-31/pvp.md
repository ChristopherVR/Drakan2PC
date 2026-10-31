# PvP deathmatch: hits on ghosts, server resolution, death and respawn - 2026-10-31

Goal: players hurt and kill each other. Result in short: **verified in two game instances** (PAL disc, Release, D3D11, private
play folders `build/pvp-{a,b}`): A's dagger hits B's ghost, the server accepts the claims, B loses health with the engine's
hit reaction, dies (frozen with the death clip), respawns 3 s later at a spawn point, scores update, the match ends at the
frag limit with results. Reference doc: `docs/reference/netcode.md` "PvP". Tags: **[game]** measured in the game, **[dec]**
from the decompile, **[test]** unit tests.

## 1. Why the blade never hit a ghost, and the fix

* The ghost handler swallows damage (events 4 and 8 are the only ones it saw), but in `ghost-spike.md` the dagger never hit
  a ghost at all ("the blade stays dead"). [game]
* The sweep `0x4979a8` asks the collision-cell query (`*(*(ctx+0x348)+0x5a4)` vtable `0x519c10` +0x2c -> `0x123818`, then
  `0x124060` per cell) for actors; a cell's candidate list is the linked list at `cell+0x58` (nodes `{next, actor}`),
  cells are 0x164-byte records at `*(world+8)`, the actor's cell is `actor+0x7c`. [dec]
* A probe (`pvp-cell` lines, run `pvp8`) showed the player listed in its cell's list (flag `0x104` bit 28 set) and the ghost
  not (`listed 0 of 20 nodes`), with a bounding sphere (`skeleton+0x164`) frozen at the spawn position. [game]
* The engine's mover (`0x1494c8`, vtable `+0x84`) removes a listed actor from its cell, and `0x1496a0` (vtable `+0x8c`) files
  it again (new cell index, list node via `0x15af70`, sets bit 28). A host write of `actor+0x14` does neither.
* Fix in `drakan_ghost.cpp` (`refileHandle`, every tick a ghost moved by more than 1 unit; also exported as
  `refileActor` for the player's teleport): clear `0x104` bit 11, `+0x84` update while bit 28 is set, `+0x8c` refile. Run
  `pvp9`: `blade ... hits ghost 1`, blood spray at the contact. [game]
* The engine's hit application runs on the ghost (contact record, effects, message to the victim). Local HP stayed 84 -> 84
  over dozens of hits (the swallowing handler drops the message) [game]; the hook still checks the HP block around the call
  and restores it, and counts a hit as new only when the blade's own hit list (`blade+0x1a0`) grew (one per victim per
  swing; `0x49bbf8` returns early for a victim already hit).
* Messages: the damage message is `0x10db` (type word at +0, damage at +0x10, attacker ref at +0x24, 0x34 bytes) sent through
  the engine message object `0x5565e0` (`ctx+0x33c`, vtable `0x51b218`, send `+0x14` = `0x171150`). It is not needed: the claim
  takes the damage from the contact record (`*(contact+0)` = `(int)(item property 0x1a x blade+0x194)`, 4 for the dagger).

## 2. Server

`MatchServer`: weapon damage table (`weaponMaxDamage`, `unarmedMaxDamage`), melee / projectile range, `damageScale`,
`Hit.weapon` = hand (0 / 1) or projectile (2) without a protocol bump (the attacker's own state carries the held record
ids), per-reason rejection logs, `scoreboard()`, `outcome()` (`Scoreboard.h`: `ScoreRow`, `decideOutcome`), dead players keep
publishing (death animation on the ghosts) and the corpse's pose is forgotten at the respawn. `drakan_server` takes
`--damage-scale`, `--respawn-ms`, `--max-hit-range`; `applyDrakanPvpTuning` is shared with the listen server. Tests
(`net_match_test`, `net_loopback_e2e_test`): table and unarmed limit, range for melee vs projectile, bad `weapon`, round trip
Hit -> Death -> Score -> Respawn on both sides, dead players' states, frag-limit and time-limit outcomes, ties, team result,
damage scale and the tuned limits. [test]

Real numbers: Rynn runs ~2500 u/s; the dagger hit a ghost 90..260 units away; the dagger does 4 (inventory "Damage 4"),
claims came in as exactly 4. Accepted claims in the harness run: 24 hits, damage 32 each (x8).

## 3. Victim side: what failed first

* Calling `0x314ab0` (Rynn's damage method) **nested** (`IGuest::call`) hung the guest in two of three runs
  (`pvp11`, `pvp12`): the method posts sounds / effects that wait on the heap semaphore. Posted (`IGuest::post`, a dispatcher
  thread) it works: HP 84 -> 64 and the flinch, run `pvp13`.
* **The engine's own death is unusable for a match.** Lethal damage (`pvp13`..`pvp15`) hides the body within a second and
  raises the single-player game over overlay "Press Enter to load the last saved game. Press the Esc key to go to the main
  menu."; Enter makes the game call the checkpoint loader `0x16adb8(engine, "BESCES-50006Slot1/<level>.rsg")`. During the
  death `ctx+0x4b0` stays and the death clip `0x20577` plays on the lower channel. [game]
* Calling that loader from our side (`guest.post`) hangs the guest after the load (`[guest-branch:missing-target] JALR
  target=0x0 source=0x3abef8`, run `pvp15`), the same hang as the lifecycle exit test aid; only the game's own flow can reload.
* So: HP never reaches 0. A match death freezes the player (behaviour handler swapped for one that swallows all but the
  destroy, the ghost technique), writes the death clip into every skeleton channel each tick (it plays: a collapse, run
  `pvp17`), keeps HP 1, and the server's Respawn restores the handler, moves the player and sets HP. The game over never
  shows because the player never dies in the engine. Others see the death through the replicated clips; the respawn delay
  has no constraint from the game over any more, but 3 s keeps the corpse inside the visible part of the clip (4.7 s+).
* Spawn points: `ghost`-style level object list parsing was not needed: positions the player has walked on are known to be
  walkable, so the player's own track (a crumb per 500 units on level ground, level start first) is the spawn set, the one
  farthest from the last seen enemies wins. After a respawn `refileActor` re-files the player.

## 4. Runs (build/harness/<name>, git-ignored)

| run | what | result |
| --- | --- | --- |
| `pvp3`..`pvp6` | probe: blade hit calls, ghost distances, sphere / cell lists | blade hits actors, never the ghost; ghost not in its cell list |
| `pvp9`, `pvp10` | after the refile | blade hits the ghost, HP unchanged |
| `pvp13` | engine death via posted lethal damage | state 2, HP -936, game over overlay, reload 14 s later |
| `pvp15` | respawn by posting the loader | guest hang after the load |
| `pvp17` | frozen death + respawn, self test | collapse pose, orb empty, HP restored, respawn at a crumb |
| `pvp18`..`pvp20` | two instances, A attacks B's ghost | claims, hits, kills; first runs lost range because solid ghosts pushed A away (test aid `TEST_ANCHOR`) |
| `pvp` | `--pvp` | all checks pass: 48 claims, 24 hits, 6 deaths, scores 1..6, rejections for dead / protected victims |
| `pvp-frag` | `--pvp --frag-limit 3` | match over (frag limit): player 1 wins, results `[Alpha 3/0] [Bravo 0/3]` on both sides; the next match starts with zero scores |

Screenshots: `build/harness/pvp/{a,b}/` (B every 0.7 s: A's ghost attacking with the dagger, blood spray, the orb emptying and
refilling, the collapse). The hit effects on A's side (blood) come from the engine's own hit path.

## 5. Not done / next

* Arrows and bolts: the server accepts `kHitProjectile`; no hook sends it (find the projectile hit path, `0x10db` sources
  in the bow / crossbow code).
* Only the dagger is in the damage table; other record ids fall back to `maxDamagePerHit` (200). Log the claim damage per
  weapon to extend `drakanWeaponDamageLimits`.
* PvP while mounted, team deathmatch and the time limit in game, USA disc, kill feed and scoreboard drawing (the API is in
  `drakan_netplay.h`), lag compensation. The listen server holds a hit's validation to the two last accepted positions.
* Concurrent instances on one PC: the listen server takes the next free UDP port from the requested one
  (`drakan_netplay`), so two harness runs of different agents collide; the harness takes `--port`.
* The harness needs a card with Rynn's dagger in the usual inventory cell (`build/harness/gw-net/a/card/mc0`).
