# Custom game modes: built in the editor, played in the game

Goal: a modder makes a multiplayer game mode in the editor, exports it in a pack, and it runs in the game with its rules
enforced. Reference: `docs/reference/netcode.md` "Custom game modes"; pack layout: `docs/reference/modding.md` (`maps/*.ini`).

## What a mode could say before, and what was added

Before: name, description, base (dm / tdm), 2..4 teams, points per kill / death / objective, frag (points) limit, time limit,
respawn delay, friendly fire, start health, capture / hold objectives. `loadout` and `weapons` were parsed but not run.

Added (every key is enforced; nothing is authored-only):

| Key | Who enforces it |
| --- | --- |
| `lives = N` (0..99) | `MatchServer`: a death takes a life, the last one puts the player out (no Respawn, cannot hurt or be hurt), the match ends `LastStanding` when one player / team is left. Late joiners watch once two sides have played; a leaver can end it. `Score` carries `lives` / `eliminated` (trailing). |
| `score_team_kill` | `MatchServer::onHit` (friendly fire on) |
| `spawn_protection` | `MatchServer::spawnPlayer` (replaces `respawnProtectionMs`) |
| `pickup_respawn = map / never / N` | `MatchServer::onPickupClaim` |
| `weapons` (now run) | `MatchServer::onHit`: a hit with a held record outside the list is refused, bare hands count |
| `loadout` (now run) | `drakan_netplay` appends it to the map's loadout before `drakan_arena::setSession` |
| `team_names`, `team_colors` | the HUD (scoreboard, results, lobby, banner), from the local catalogue by `modeId` |

Left out because the runtime cannot enforce them: a suicide / fall score (an environment death never reaches the server; the
PvP glue keeps the engine's HP above 0 and only server Hits kill) and "dragons allowed" (the air arenas need Arokh, a level
object the arena filter keeps).

Plain words: `net::modeGoal` / `net::modeSummary` (and the editor's `modeGoal` / `modeSummary`, same text) give the HUD's top
chip and scoreboard header ("Last Rynn Standing - Last one standing, 3 lives each") and the Host page's rules line. The
results banner says "Alpha is the last one standing" (`HudState::winnerText`), the standings put players still in first and
show a LIVES column (OUT when eliminated), an eliminated player sees "You are out" instead of the countdown. System chat
lines (player 0) no longer show "?:".

## Editor

Multiplayer Map > Modes: list with each mode's goal, New mode from a template (Last one standing, Frag race, Team battle,
Capture the flag, King of the hill) or blank, a grouped form (About; Players and teams with names and colour pickers; How a
match is won; Scoring; Respawn; Weapons and pickups), and an "In the game" card with the same text the game shows, an
"Offer it on <map>" box with the reason a map cannot play it (`modeMapProblem` = the runtime's `modeFitsArena`), the problems
that block the export and the mode file. Older saved modes get the new keys' defaults. Checked in a browser against the disc
(Inquisitor's Lair as the terrain): the panel fits the narrow right dock (the form's grids reflow to two columns).

## In game

Pack built with the editor's own code (`parseArenaIni` on `configs/mp_maps.ini`, the "Last one standing" template,
`buildArenaPackFiles`): `mp-last-rynn` = `mod.toml` + `maps/inquisitor-lrs.ini` (the Inquisitor arena listing
`modes = dm, last-rynn-standing`, and the `[mode last-rynn-standing]`: 3 lives, frag limit 0, 5 minutes, `pickup_respawn =
never`). Script and output: `build/scratch/gamemodes/make_pack.ts`, `build/scratch/gamemodes/mods/`.

`python scripts/harness/arena_game_mode.py --mods build/scratch/gamemodes/mods --map inquisitor-lrs --mode last-rynn-standing`
(two instances; A hosts through `DRAKAN_NET_HOST` + the new `DRAKAN_NET_GAME_MODE`, B joins). All 15 checks passed:

```
server: pickup 0 (Crown of Thorns) taken by player 1, does not come back (game mode)
server: player 2 has 2 lives left
server: player 2 has 1 life left
server: player 2 eliminated (out of lives)
server: last one standing
server: match over: player 1 wins
B: score: player 2 kills 0 deaths 3 points 0 lives 0 (eliminated)
B: match over (last one standing): player 1 wins, results (kills/deaths): [1 'Alpha' 3/0] [2 'Bravo' 0/3]
```

No Respawn of B between the elimination and the results; pickup 0 never came back on either instance. Screenshots
(`build/harness/game_mode_lrs/`): `a/a_fight_19.png` (top chip "4:05 left   Last one standing   Lives 3"),
`b/b_s02.png` (results: "Alpha is the last one standing", LIVES 3 / OUT, the system line "Bravo is out of lives"). The Host
page (`--host-page 3`, `build/harness/game_mode_host/a/host_mode_2.png`): Game Mode "Last Rynn Standing", Score / Time / Teams
fixed by the mode, the rules line "Three lives each. The last one standing wins. Free for all. 3 lives each; the last player
standing wins. 5 minutes. Pickups do not come back.", MODES "Deathmatch, Quick Frags, Last Rynn Standing".

Found on the way:

* The `DRAKAN_NET_HOST` shortcut hosted before the mod packs' maps and modes were registered (the pack map was unknown to the
  server: no arena, no mode). With `DRAKAN_NET_GAME_MODE` it now waits for the map (`drakan_arena::findMap`) first. The menu
  hosts long after start-up and was never affected.
* With two players an elimination ends the match at once, so the "You are out" overlay needs three players (drawn in
  `gameui_multiplayer_test` only).
* When the last opponent leaves, the server ends the match as last one standing, but a client computes its own reason from
  the roster (the leaver is gone from it) and logs "time limit". Cosmetic; the winner is right.

## Tests

`net_custom_rules_test` (new), `net_protocol_test` (Score's second optional tail), `gameui_multiplayer_test` (LIVES / OUT,
"You are out", winner line, team names and colours), editor-core `arena.test.ts` (new keys round trip, validation, goal /
summary text, `modeMapProblem`, templates valid).
