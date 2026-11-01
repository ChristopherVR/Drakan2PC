# Inquisitor Depths full match, stale-position refusals, fall respawn, save veto (2026-11-01)

Two instances, `scripts/harness/arena_two_instances.py --map inquisitor-depths --port 27300`, build `build/final-game`.

## The "4 fps with two copies" report
Not reproduced. Measured from `[frame-uncapped]` (completedFlips / seconds) in the harness logs, with 11-15 other game
instances on the machine and the CPU at 88-100 percent: in play 25-60 world fps per instance with two instances in
a fight (the F-key fps overlay shows the same, e.g. "World 58.9 fps"), 100-180 on the lobby / results screens, 20-40
while the level streams in. The unfocused window is not throttled (`pause_when_unfocused` defaults to 0). The campaign
filter removes 49-50 actors (23 creatures / NPCs, 17 story objects, 10 loose items) on this map in both instances.
The earlier 4 fps came from the machine load of that round (and from the run being compared to a quiet machine); nothing in
the arena code was slowing the frame.

## What did go wrong, and the fixes
1. **Placement gave up too early on a fast machine** (`drakan_arena.cpp`). The spawn teleport is retried every 40
   boundaries, six times; the terrain around the spawn point streams in by wall time, so at 110 fps the six tries were
   spent in two seconds, the player stayed at the level start (outside the arena, over a void), and the fall check then
   re-teleported every tick and reset the retry counter (86 "fell out of the map" lines). Now 40 tries, and the bounds
   check stands aside while a placement is retrying.
2. **Stale position on the server** (`runtime/net` MatchServer). After a (re)spawn the client keeps publishing the old
   life's / the level start's position until the game has moved the player (an arena level takes 15-20 s to load, longer than
   the 8 s trust window). The speed check then refused every state, so the other instance never showed the ghost
   ("posed" / "ghosts shown 0" for a whole match) and pickup claims failed with "is too far (distance 76577 ...)".
   - a state within 3000 units of the player's spawn point is accepted as the arrival (`spawnPending` holds until she has
     arrived once, however long the level takes);
   - 12 speed-rejected states in a row that agree with each other are taken as a teleport the server was not told about
     (`player N resynced to x z after 12 rejected states (stale last position)`);
   - a pickup claim is in reach if the last position, the spawn point (while she has not arrived), or the newest refused
     position is.
   Unit tests in `runtime/net/tests/arena_test.cpp` (claim after a stale state, arrival, resync, claim from a refused newest
   position, and refusal once the position is real and far).
3. **Hit test aids**. `DRAKAN_PVP_TEST_PLACE` pins did lose to the net layer's `setPose` (race): new
   `drakan_ghost::pinTransform`. On this map the engine's blade sweep never finds the ghost (the ghost's refile gives
   collision cell -1 at the arena floor, while the meadow gives a cell), so no blade hit ever fires. New aid
   `DRAKAN_PVP_TEST_CLAIM=<ticks>` sends the claim a landed blade would have sent. [open] real blade hits on a ghost in
   the Depths: the refile returns no cell there.
4. **Lobby page over the level** (`drakan_settings_host.cpp`). With the shortcut launch the title comes up under a live
   session and `resumeMultiplayer` put the lobby page ("Loading...") over the running level, holding the world at 0 fps.
   The resume now only happens when the match phase is Lobby or Results.

## Result
Full match, frag limit 3, player 1 3-0, `match over (frag limit): player 1 wins`, the Match Over screen with the
scoreboard and the 15 s "Next match in" countdown (`build/harness/arena_inq_r8/a/a_fight_49.png`), then back to the lobby and
a new match loading.

## Fall out of the world / save veto
New aids `DRAKAN_ARENA_TEST_FALL=<boundaries>` (drops the player 30000 units) and `DRAKAN_ARENA_TEST_SAVE=<boundaries>`
(calls the engine's save writer once). Results are in the final report of this round and the harness logs
(`[arena] fell out of the map ... back to spawn N`, `placement of spawn N took after 1 try`,
`[lifecycle] save write vetoed (multiplayer arena)`).
