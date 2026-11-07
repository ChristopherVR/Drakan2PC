# Multiplayer to singleplayer: reset the match state on Quit to Title (issue 1)

Tester report (issue 1, RabeOhneNamen): host a deathmatch on City of the Damned: Skeleton Square, pause menu > Quit to
Title, then Continue the newest campaign save: the game exits. New Game instead loads the multiplayer (arena) version of
Surdana.

## Root cause

The tester log shows `[arena] arena world mode on` at the match launch and never `arena world mode off`. After Quit to
Title the glue calls `drakan_netplay::leave()` (the server logs "last player left, back to lobby"), the network thread
leaves its session loop and runs the `runSession` teardown. The arena world mode is driven only from inside that loop
(`drakan_arena::setSession(dm, def)` every iteration), and the teardown cleared co-op, ghosts and the level redirect
but never called `setSession(false)`. So the mode stayed on for the rest of the process:

- the file layer's arena open filter kept rewriting the base `.LVL` of every built-in map (Surdana is one), so New Game
  loaded the stripped arena Surdana;
- Continue restored a campaign save into that stripped level and crashed (`guest-branch:missing-target` JALR to 0 right
  after `level 'Level 1A - Surdana' loaded`, then `[gsr] exit`);
- the save veto (`arena active`) and the arena EE ticks (campaign removal, loadout, pickups) also kept running.

## Fix

`drakan_netplay.cpp` `runSession` teardown now also turns the arena world mode off, sets the arena and PvP local player
ids to 0, drops queued hit claims and outgoing level changes, and clears `g_launchDirect` with the other launch state.
The in-loop condition became `policy::arenaWorldWanted(sessionRunning, connected, coop, matchRunning)` in
`drakan_netplay_policy.h`, covered by `netplay_policy_test` (a torn down session never wants the arena mode).
Results-driven automatic returns are unchanged: they keep the session (and the arena mode while Results lasts) for the
next match, and the Lobby phase already turns it off.

Already reset before this change and left as is: co-op link and queues, `g_coopMode`, remotes, the New Game level
redirect (`setRedirect("")`), the local stand-in character (follows `g_inSession`), the co-op hooks (follow the link's
session serial).

## Verification

- `netplay_policy_test` passes (built alone with MSVC in `build/issue1-mp2sp`).
- In game (deployed build, one instance, modern title / pause menus, scratch recipe: title > Multiplayer > Host Game
  on the first arena, Surdana Meadow, whose base level is Surdana > Start Match > pause > Quit to Title):
  - then Continue on a card with a Surdana save in slot 1 (`build/harness/issue1-mp2sp-continue3`): `arena world mode
    off` is logged right after the server returns to the lobby, the save loads into the campaign Surdana (tents and
    villagers back) and plays on, no `missing-target`, clean script quit;
  - then New Game on an empty card (`build/harness/issue1-mp2sp-newgame2`): the intro (`Level 0 - Intro`) plays, then
    the campaign Surdana with its first objective and Save Game enabled (the arena mode disables saving).
