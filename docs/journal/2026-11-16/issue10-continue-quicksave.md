# Issue 10: Continue and the quicksave

Report: on a source build of `bacb3ea`, the title's Continue loaded the newest manual save even when the quicksave
(F5, `mc0/BESCES-50006Slot33`) was newer, and Load Game had no quicksave entry (only F8 loaded it).

## Why the reporter saw it

The Quicksave page and the quicksave-aware Continue landed in `bd0cf37d` ("List the quicksave in Load Game and let
Continue resume from it", 2026-11-15 06:48), about five hours after `bacb3ea` (01:35). `bd0cf37d` is not an ancestor
of `bacb3ea` (`git merge-base --is-ancestor` fails), so that build simply predates the feature. Nothing about
`save_pages = 4` with Slot1..4 used hides the page on current main: with a `.rsg` in Slot33 the Load screen counts
`save_pages + 1` pages and the last one is the quicksave.

## Checked on main

- Continue for slot 33: `continueStep` derives folder page 8, and `showPage` accepts it while the Load screen lists
  the quicksave. Row 0 of that page is Slot33. `drakan_save_guard::checkSlot(33)` reads the folder like any other.
- The title rescans the card on every new title screen (`titleBegin` clears `g_title`) and whenever the title comes
  back from a child screen, so a quicksave made in game is seen after Quit to Title.

## Changed

- One rule for "newest save" (`policy::newerSave` in `drakan_save_pages_policy.h`), used by the title's Continue scan
  and by "Load Game opens on the quicksave page": latest `.rsg` write time among the listed slots wins; on equal
  times the manual save wins. Before, the two places resolved this separately (both happened to favour the manual
  save on a tie).
- The Continue row says which save it loads: "Quicksave: <level>" on the row and "Quicksave: <level>, <date> <time>."
  in its description instead of "Your newest save: ...", from the existing translated "Quicksave" string (no new
  menu text). A longer "Your newest save (Quicksave): ..." was tried first and was cut off with "..." in the test viewport.
- Tests: `save_pages_policy_tests.cpp` (newer quicksave wins, newer manual save wins, tie, quicksave only, switch off,
  slot outside `save_pages`); gameui `testTitleRows` (the row and description name the quicksave, Confirm emits
  Continue 33).
- Docs: `docs/guides/saves.md`, `docs/reference/save-slots.md`.

`[general] quicksave_in_load = 0` keeps the old behaviour: Continue ignores the quicksave, only F8 loads it.
