# Map vote, map pictures proof and rotating character previews (2026-11-01)

Follow-up of `map-previews.md` and `mp-results-modes-server.md`. Tags: **[test]** unit tests, **[game]** real instances
(`build/harness/...`, nothing of it is committed).

## 1. Map picture capture proven in a live two-instance match
* `netplay_menu_two_instances.py --port 28700 --name prev_run1 --prefix prev-ui --play-sec 70` (new `--play-sec`) with a fresh
  `DRAKAN_PREVIEW_DIR`: A hosts Surdana Meadow from the menu, B joins. **[game]** B's log: `map picture: capturing ...
  (preview camera)`, `preview shot: standing at 450762 171700 340186 yaw -73 pitch 8`, and `Surdana_Meadow.png` (1280x720)
  appeared in the empty cache. The shot waits for the follow camera to hold still (steady gate); the picture is a clean view
  of the meadow, no HUD (only the fps meter, which the menu crop cuts away).
* Co-op: `--coop` into the same cache: `Level_1A_-_Surdana.png` captured from the start view (Rynn and the partner stand-in in
  frame). Both kinds are drawn with the crop band uv y 0.08 .. 0.74.
* The Host / Join / lobby pages then draw the captured picture instead of the sketch (screenshots under
  `build/harness/prev_pages*/`, see the final report).

## 2. Map vote on the results screen
Design and wire format: `docs/reference/netcode.md` "Map vote". Server (`MatchServer::beginVote / onMapVote / leaveResults`),
client (`MatchClient::sendMapVote`, `mapVote()`), glue (`drakan_netplay::voteMap`, `Scoreboard::vote*`), screen
(`HudState::vote`, three cards under the standings: picture, vote count, name; `mapVoteFrame`: click a card or Q / E).
The standings show fewer rows while a vote is up (4 / 3). Edits in `drawHud` are additive next to the other session's results
buttons (the buttons are untouched).
* **[test]** `net_map_vote_test` (candidates in rotation order, filled with the current map, tally, change / withdraw, leaving
  voter, timer applies the leader, ties lowest index, no votes keep the map, host Next map uses the vote, co-op / one map: no
  vote, wire round trips), `gameui_mapvote_test` (card rects, click, Q / E, pictures requested in drawHud),
  `net_protocol_test` (type range).
* **[game]** `scripts/harness/mp_vote_check.py --port 28700` (see the final report for the run).

## 3. Rotating character models in the co-op character picker
The title menu has no model renderer and no world, so the pictures are made at run time from a loaded level and played back,
like the map pictures (nothing is stored in the repository; the cache is `<previews>/chars/<class id hex4>.png`).
* Studio (`drakan_arena.cpp` `studioTick`, `DRAKAN_CHAR_PREVIEWS=all|<hex ids>`, needs `DRAKAN_GHOST=1`): in a loaded level with
  the player standing still (the follow camera holds still), one window shot of the empty spot beside her; then every roster
  character is spawned there as a ghost stand-in (`drakan_ghost::spawn(t, character)`), turned in 12 steps with
  `pinTransform`, one window shot per step. A worker thread subtracts the empty shot from each frame (alpha = how much a pixel
  changed, closed over small holes where the model's colours match the ground), crops a 2:3 cell and writes a strip of 12
  frames of 160 x 240 RGBA. Characters with a strip are skipped (resumable). `DRAKAN_CHAR_PREVIEW_MAX=n`, `_LEFT=1` test aids.
* Harness: `scripts/harness/char_previews.py --dir <cache> [--ids ee7,1798] [--max N]`.
* Menu: `ImageKind::CharTurntable` (index = class id | frame << 16), `CharacterEntry::turnFrames` (the glue sets it for the
  selected character once the strip is on disk and decoded), the lobby's co-op character panel plays the 12 frames (6 per
  second) instead of the drawn figure; characters without a strip keep the drawn figure.
* Limits: the strips only exist after a studio run (by hand with the harness or `DRAKAN_CHAR_PREVIEWS=all` in a level; the game
  does not start it by itself, it takes the player out of play for minutes). The model plays its idle clip while turning
  (arms move), the background difference leaves a thin halo on some textures.

## Verification (what ran)
* **[test]** `ctest --preset runtime-tests`: everything passes except `disc_profile_pal` / `_usa` (exit 0xc0000409, not touched
  by this work; the full runtime-tests tree also builds `net_map_vote_test` and `gameui_mapvote_test`).
* **[game]** `mp_vote_check.py` (two instances, base port 28700): all 8 checks passed. Both players saw three cards (City of the
  Damned with the drawn sketch, Inquisitor's Lair and Surdana Meadow with captured pictures), both voted the second one (server
  log `vote: player 2 voted ...`, tally 2), A pressed Back to lobby, `vote result: Inquisitor's Lair: The Depths`, the lobby
  showed that map with its picture.
* **[game]** `mp_pages_check.py --coop`: Join list / lobby show the co-op start-view picture; the host lobby shows Rynn turning,
  the joiner's lobby the chosen villager turning (`character turntable 0000 / 0ee7 / 1609: loaded`); strips made by
  `char_previews.py` (ids ee7, 1798, 1609, and Rynn via `--max 1`). Host page and Join page of the deathmatch showed the
  captured Surdana Meadow picture (`prev_run1` / `prev_pages_coop`).
* Findings: the Versus Host page's Game Type row is greyed now (focus skips it): `host_keys` pressed it once too often (Back
  was focused), fixed to 6 downs. The LAN sweep of a busy PC missed the server in several runs (Join list stayed on
  "Searching"), re-runs found it. `DRAKAN_CHAR_PREVIEWS=0` means off, so Rynn (id 0) alone is made with `--max 1`.
* Not verified: the vote with a mouse click (keys only), a vote with three real human players, a full-roster studio run
  (only 5 characters were made), the strips on a non-Windows build.
