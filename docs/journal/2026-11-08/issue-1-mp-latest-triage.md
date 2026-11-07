# Issue 1, Rabe's latest multiplayer items: triage against HEAD

Reporter items: (1) MP then Continue crashes and New Game loads the arena Surdana, (2) stale map preview, (3) remote
players stutter or rubberband, (4) no message when the host disconnects clients.

Method: read the attached log (`build/MP2SP.log`), history and journals. No game session was run in this pass.

| Item | Finding |
| --- | --- |
| 1 | The log contains `[arena] arena world mode on` at the match launch and never `off`, then `guest-branch:missing-target` JALR to 0 right after `level 'Level 1A - Surdana' loaded`. That is the bug fixed by 539d3e4b (2026-11-07, in v0.1.6): the session teardown did not call `drakan_arena::setSession(false)`. Verified in game then (Continue and New Game, `docs/journal/2026-11-07/mp-to-sp-reset.md`). The redirect was already cleared; the arena open filter was the cause. The log is from a build before that commit. |
| 2 | Fixed by 5f3748f3 (2026-11-07): the in-match "current view" capture is opt in (`DRAKAN_MP_CAPTURE=1`), stale captures are not read. |
| 3 | Fixed by 3b01e5fe (2026-11-07, protocol v9): remote bodies replayed on the sender's pose time through a jitter buffer with bounded speed trim, instead of a clock chasing every snapshot. This matches the reporter's guess (speed corrected backwards). |
| 4 | Partly present: the guest returns to the title automatically and the Join Game screen shows "Connection to the server lost" (`mp_host_leave.py`, 2026-11-06). The message is only visible after opening Join Game, so the reporter saw none. A title-level notice is still open (needs a UI overlay, not done here). |

Needed from the reporter: confirm items 1 to 3 on v0.1.6 or later; their log predates 539d3e4b.
