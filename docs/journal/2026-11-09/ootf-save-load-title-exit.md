# OotF: loading a save from the title opened the level-change prompt at once

User report: loading a save for Order of the Flame seems broken.

What it looked like: from the title's Load screen, confirming slot 2 (a Ruined Village Book save, written while the
player stands on the level's exit) went loading screen (gui 14) -> "In order to progress..." prompt (gui 2) within 0.1 s,
then the level-change save screen, instead of the level. Slot 1 of the user's card is refused on purpose: it records an
older build of the ootf-levels pack (1a8d4afc503be6a1; installed pack is 8dcc5fd38986db9e), the Load screen greys it with
the pack message. Slot 2 and slot 33 records match the installed pack, so the pack record check is not at fault, and the
HUD / switch campaigns / intro movie / autopress changes are not involved (no [saves] refusal, restore completes).

Root cause: the exit arrival guard (drakan_exit_guard.h) is only (re)armed by the first live engine tick of a level
(`g_framesInLevel == 1`). A load restores the player onto the exit and the engine touches it before that tick, while the guard
still holds the previous state (not armed), so the touch went through. A pause-menu load happened to keep the old armed state.

Fix: drakan_levelchange.cpp `onLevelEvent` AfterLevelLoad also calls `g_guard.levelStart(...)`, so exit touches are held from the
moment the level is live; the first live tick still re-takes the real arrival spot. Log after: `exit ... touched before the
player walked off her arrival spot: ignored` twice, the level plays, and the Book's own timer (f63cbc34) ends it about 30 s
later. Evidence: build/review/saveload/ (before_fix.log, after_fix.log, screenshots).
