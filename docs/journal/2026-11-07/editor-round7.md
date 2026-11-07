# Editor round 7: lever and door in the game, Save As, focus and spoken names

## Lever and door in the game (not proven)

Setup: a pack with one placed Lever 2000 and one placed door in front of Rynn on Surdana, linked with Opens, played from
a copy of the play folder with `DRAKAN_WORLD=2` world dumps and `place` / `key tap E` recipe lines.

* The lever signal is solid and repeatable: after `key tap E` the lever's flags go 0x0 -> 0xa or 0xb -> 0x4 or 0x7 ->
  0x0 within about two seconds, in every run, for placed levers and for the shipped lever 17095.
* The door signal was not found. A placed Desert Gate Door and a placed Town Door keep flags, position and yaw
  through the press, and the screenshots before and after (fixed camera) look the same. The shipped Surdana lever 17095
  sends message 3 (Unlock) to its Town Door 17017, and that door does not change flags either, so flags are not a
  door-open signal for these classes (the RibDoor, flags 0x19001045 -> 0x19009045, is a different class).
* A walk-through control was inconclusive: the placed lever is solid and blocked the control walk, while after the
  press Rynn passed the lever and the door position, but an offset lever with the walk beside it ended 3,000 units to
  the side and never tested the door.
* Next step: place a copy of a shipped Town Door and lever pair (not a default body), send Unlock then walk into the door
  from a measured spot, and compare the walk with and without the press.

Not done in this round: trigger to enemy spawn, the shop form to the end, a chained exit (they need the door proof's
harness first).

## Save As (desktop)

File > Save Project As (Ctrl+Shift+S, palette "Save the project as..."): asks for a name (suggested `<name> copy`, next
free `copy 2`), saves the open project, asks where for the new folder, writes the project there and continues in the
copy; the recent list gets the new folder. `project_new` refuses a folder that holds a project or any files
(`paths::check_new_project_dir`, Rust test `new_project_dirs`); the editor test covers `suggestSaveAsName`. The native
folder dialog cannot be automated, so the whole flow was not clicked through.

## Accessibility

* Two `main` elements: `inspect-main` is `hidden` while the level editor shows, so only one is exposed. Left as is.
* Focus trap: the palette, the shortcuts overlay (now with a Close button), the tour picker and the campaign wizard
  keep Tab and Shift+Tab inside (`focustrap.ts`). A Playwright probe confirmed palette, shortcuts and tour picker; the
  wizard has the same call but could not be opened in the probe. Native `<dialog>` modals trap by themselves.
  `ui-sweep` now presses Tab and Shift+Tab 14 times in every open modal and reports an escape.
* Accessibility tree read (Playwright snapshot): menu bar, main toolbar, viewport toolbars, complementary panels and
  regions have names. The outliner rows read as "Expand Player 2 Hide group Lock group" (the inner buttons spoke
  through the row): rows now carry `aria-label` ("Doors & mechanisms, 72 objects", "Lever 2000, number 16759").
  Logic graph nodes were bare SVG groups with a tooltip only: they now have `role=group` and a name with role, object
  number and selection. A real screen reader (NVDA) was not run.
