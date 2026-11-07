# Issue 5: the end card after a game chosen on the title (item e)

Closes open item 5 of `outstanding-work.md` section 1. Private runs under `build/ootf10/h/` (own copy of the 16:27 executable
in `build/ootf10/play`, the `build/harness/ootf7/packe` pack that carries the ending timers, god mode, one game at a time).

## Why the overlay covers a title-chosen game (code)

* `CampaignCreditsOverride::patchRead` (`runtime/hle/src/ResourceOverride.cpp`) asks `current()` on every read, and
  `current()` asks the callback `drakan_game::currentModId()`. Nothing is cached per game: only the entry ranges of the
  file are cached, and those do not depend on the game.
* The title's choice calls `drakan_game::select(index)`, which sets the index that `currentModId()` reads. So a read after
  the choice sees the campaign, however late the choice came. (A `DRAKAN_START_LEVEL` run with `[general] game = d2` never
  selects the campaign, so it keeps the stock credits; that is the run family of the earlier proofs.)

## One real run

Recipe: `[general] game = ask` and no `DRAKAN_GAME`, the modern title, the card `mc0_ootf` holding a Rift World save made with
the same pack (a slot-1 copy of an F5 quick save; a save from another pack build is listed as "not available" and Continue
is missing). Title: Down and Enter on the game card (Order of the Flame), the title then offers "Fortsetzen" with the
save's level name (the log prints `[game] switched to ootf`), Up twice and Enter. The save loads Rift World; the pack's ending
timers send the four head deaths, the ending scene plays and the credits scroll over the Wartok farm with the campaign end
card ("Original game: Drakan: Order of the Flame, Surreal Software, 1999", "Engine: Drakan: The Ancients' Gates, Surreal
Software, 2002"), not the stock list. About 90 s after process start.

Harness notes: input script times are absolute from the clock start (not delays between lines): lines meant to be 1 s apart
must be written 1 s apart. Keys: `key tap ENTER` and `key tap DOWN` work on the modern title and game card; `pad tap CROSS`
and `pad tap DPAD_DOWN` do not reach the game card.
