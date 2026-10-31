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

* Done in section 6: arrows (projectile scan), the weapon table, kill feed / scoreboard / results drawing, the time limit
  in game.
* PvP while mounted, team deathmatch in game, USA disc, lag compensation. The listen server holds a hit's validation to the two last accepted positions.
* Concurrent instances on one PC: the listen server takes the next free UDP port from the requested one
  (`drakan_netplay`), so two harness runs of different agents collide; the harness takes `--port`.
* The harness needs a card with Rynn's dagger in the usual inventory cell (`build/harness/gw-net/a/card/mc0`).

## 6. Menu to results flow, HUD, chat, ranged hits, weapon table (later the same day)

Goal: one polished menu -> deathmatch -> results -> next match flow. Result: **verified in two game instances**
(`python scripts/harness/netplay_menu_two_instances.py --pvp`, private play folders `build/mp-full-{a,b}`, output
`build/harness/mp_full`; all checks pass): A hosts through the Multiplayer menu (3 kill limit through the test-aid env
`DRAKAN_MP_FRAG_LIMIT`, the menu itself offers 5 and up), B finds A in the LAN list and joins, A starts the match from the
lobby, both load Surdana through the New Game launch path, B types a chat line and A answers (real keyboard input), both
equip the dagger from the inventory, A kills B three times (kill feed, death overlay, scoreboard), the server ends the
match ("match over (frag limit): player 1 wins ... [HostRynn 3/0] [JoinRynn 0/3]"), the banner and standings show on both,
15 s later the server returns to the lobby, both leave with Esc > Quit > Quit to Title, the title opens on the lobby page
(`title: resumed the multiplayer lobby`), A starts the next match with zero scores. Tags as above: **[game]**, **[test]**.

### What was built

* **HUD from `scoreboard()`** (`drakan_settings_host.cpp` `mpHudDraw`, `gameui::mp::drawHud`): rows / kills / deaths / ping /
  time, limits chip with the time left, kill feed with the weapon ("HostRynn killed JoinRynn with Rynn's Dagger"), death
  overlay with the respawn countdown and "Killed by", results banner ("Match over - HostRynn wins (frag limit)", "Draw
  (time limit)") with the final standings, "Match finished - Esc > Quit to Title ..." in the level after the results.
  `ScoreRow` gained `pingMs` / `timeSec`, `KillFeedEntry::weaponName`, `Scoreboard::winnerName`. [game] screenshots
  `a_m1_17.png` (kill feed, chip), `a_m1_33.png` (results), `cap_*.png` (desktop captures: death overlay, F4 scoreboard,
  results, second match). [test] `gameui_multiplayer_test`: feed lines fade, death overlay, results keep the standings.
* **Time limit in game** [game]: `DRAKAN_MP_TIME_LIMIT_SEC=50` with no kill limit (`--time-limit-sec 50 --frag-limit 0`,
  run `build/harness/mp_full_time`): `server: time limit reached` exactly 50.0 s after `phase -> playing`, "match over
  (time limit): tie", banner "Match over - Draw (time limit)" on both, back to the lobby, second match. The chip counts down
  (8:40 left at the start of a 10 minute match).
* **Resume on the lobby**: `TitleScreen::resumeMultiplayer` / `MultiplayerScreen::openLobby`; opened by the glue when the title
  comes back while the session is up.
* **Chat typing** [game]: see below, it did not work before.
* **Weapon table** `net/WeaponTable.h` from `analysis/scripts/weapon_table.py` (2875 RAM dumps): 40 melee records (damage
  from item property `0x1a`, stance `0x1e`, 12 names resident in the dumps' string manager), 20 bow records, arrow damage 5.
  The server limit for a melee claim is 4 x the item damage per weapon (it was the dagger only), projectile claims are
  capped at 40 (`ServerOptions::maxProjectileDamage`). [test] `net_match_test`: table lookups, the dagger / hammer limits, bow
  entries, arrow cap (41 rejected, 5 accepted), display names.
* **Ranged hits** [game]: `drakan_pvp.cpp` `scanProjectiles` (design in `docs/reference/netcode.md` "Weapons and damage").
  Run `python scripts/harness/netplay_two_instances.py --pvp --weapon bow --card build/harness/gw-net/a/card/mc0 --port 47920`
  (`build/harness/bow-ranged`): A equips the Short Wood Bow (`local weapons: right 0 left 31bc9`; while an arrow is drawn
  the right hand holds the arrow item `0x3379d`, which is also the arrow actor's resource), draws and releases every 3 s at
  B's ghost (`DRAKAN_PVP_TEST_PLACE=700` rotates it around A): `[pvp] arrow 1740610 hit ghost 1 (player 2) damage 5 -> claim`,
  `claim: hit on player 2, weapon 2, damage 5`, `hit: player 1 hit player 2 for 40 (weapon 2), health now 60`, then 20; B's
  hit reaction `hit by player 1: 40 damage`. 2 of 16 shots crossed the ghost (the test placement only has it in the arrow's
  path in part of its 16 s rotation).

### What failed first (and the fixes)

* **Chat typing never worked in game.** The input script has no characters, so the earlier note "verified by unit tests only"
  was the whole truth. Typed characters reach ImGui only while the screen layer wants the input (`SettingsOverlay`
  `screenInput`; the HUD is a screen layer that never wanted it), so F6 opened the line and nothing arrived, and the open line
  froze the game input for good. Fix: `g_mpChatOpen` makes `wantsInput` true while the line is open. Verified with real
  keyboard input (`scripts/harness/winkeys.py`, `SendInput`: F6, `KEYEVENTF_UNICODE` text, Enter): `chat: <JoinRynn> gg from
  JoinRynn` in A's log, `chat: <HostRynn> hello back HostRynn` in B's. The input is one character per frame at most (ImGui's
  trickle), so the harness types 10 characters per second; at two instances' frame rates a faster gap lost characters. One
  run showed foreign characters in the line ("tigahteello back"): the keyboard is shared with the person using the PC, so
  the harness checks that a line arrived and sends it again otherwise.
* **F4 / F6 read the OS key state of every window** (`GetAsyncKeyState`): with two instances on one PC both opened the chat on
  one F6. They now count only while `drakan_win::focused()`.
* **Launch taps, again.** The intro skip from the multiplayer menu (START + Cross every 500 ms from the moment of the press)
  failed in several runs: the taps opened the pause menu in the cut scene level (screens 23 / -1 flapping for 120 s until the
  server's load timeout). The intro movie is screen `0x1d` (ends only with a tap), the cut scene level behind it needs a START
  tap once it plays, and never during the loading screens. Taps now: during screen `0x1d`, then from 6 s after it ended while
  no screen is up. Match load took 10 s instead of 120 s in the following runs.
* **The second match started before its level loaded.** After Quit to Title the level hash of the first match was still set
  (the title reports no load), so `LoadDone` went out at once and the server entered Playing 10 ms after Loading with the
  players still at the title (the time limit and the chat running already). The hash is cleared at the Loading phase.
* **Inventory, first visit**: the modern inventory raises a native help box on its first opening; the first ENTER closes it, the
  second equips (the harness presses ENTER twice; the dagger is the first cell of a new game, third row in the old fixture card).
* Desktop captures from the harness thread need `SetProcessDpiAwareness` or the window rectangle is scaled wrong.
* Other players' ping is unknown (no field on the wire): the column shows "-", the own ping is the measured RTT.

### Limits

* Arrow damage is the starting arrows' 5 for every arrow (the damage lives on the arrow item, which the claim does not
  inspect); spells claim a flat 10 and casting was not verified in game. The server caps both at 40.
* Names in the kill feed: 12 melee weapons and 20 bows by id; unnamed records read "sword" / "two-handed sword" / "hammer" /
  "Bow". The names resident in the 2875 dumps are all there is; the rest of the string table is only resident on demand.
* "Back to the lobby" needs Esc > Quit > Quit to Title from the player (the engine cannot be sent back to the title from
  outside its own flow safely, see section 3); the banner says so.
