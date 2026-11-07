# Editor round 4: first workflow checks in the browser (started, not finished)

Browser build (Vite dev server, Playwright Chromium, software rendering), the player's PAL disc, Level 1A - Surdana,
1280x720 with a cleared profile. Every step below was done by driving the real UI, timings are from the test browser.

| Step | Result |
| --- | --- |
| Open the disc, then the level list shows Surdana | 0.8 s; the level opens in 5.3 s (4675 objects) |
| Outliner search "Rynn", click the row | 1.2 s, 1 selected, Details shows the object |
| Move tool (W), duplicate (Ctrl+D), delete (Del), undo twice, redo | object count 4675, 4676, 4675, 4675, 4676 as expected; the Undo button enabled and disabled correctly |
| Duplicate Rynn | **Bug:** the copy was made, leaving two player starts and no message. Now refused with the toast "Rynn exists once in a level. Move her instead of copying her." (`isRynnStart`, test added) |
| Place > Creatures, single click on Ballista | selects the row only; double-click placed "Ballista #18254" at the middle of the view (the row tooltip says so) |
| Details of the new object | position, rotation, "This object cannot be scaled", fields, links and a Remove button, all labelled; editing X to 12345 and Ctrl+Z restored the old value |
| Keyboard Tab order from the toolbar | Objects, Landscape, Add, level list, Save, Select, Arrange, Export, Play, Play options, theme, help: sensible, nothing skipped or trapped |
| Logic graph (Ctrl+Shift+K) | opens in 1.6 s with its toolbar |
| Export (Ctrl+E) with no edits | the dialog says "Nothing to export yet: no level in the project has changes and there are no story edits" instead of writing an empty pack |
| Console errors during the run | none |

## Continuation: the rest of round 4

Everything below was done by running it (browser build, Chromium with software rendering, the player's PAL disc and the
23-level OotF conversion; the desktop app for the game start). Timings are from that machine.

### 1. First-user path

| Step | Result |
| --- | --- |
| Place by double-click: creature (Ballista), item (Ale), door/mechanism (ALC), triggers | each adds one object and selects it; the Place rows list class names alphabetically, so Doors & mechanisms starts with "ALC", "Altar_R_01" (no search-first hint) |
| Single click on a Place "action" row (Lever opens door, Timed trigger ...) | opens the ready-made form in the Logic dock, placing nothing until Create (as its tooltip says) |
| Ready-made puzzle form with no door in the level | **Friction fixed:** an empty "Door" drop-down and a toast "Pick the door the lever controls." after Create. The form now says "No door in this level yet. Place one first, then open this again." |
| Story: New cut scene | 2 objects (a player and a trigger box) plus sequence 1411 in 4.4 s, with a toast saying so |
| Export (Ctrl+E) of those edits | 3 files: the changed LEVEL1NA.LVL entry and the story sequence; downloaded as a zip |
| That zip in a copy of the play folder (`build/editorplay`, `mods.ini` switched and put back) | the game logged `mods: loaded 'my-campaign-mod' 1.0.0 ... 1 new entries` next to the character motion pack |
| Add > Cut scene / Dialogue line / Quest / Vendor | open the Story composers (no object until their forms are filled), as designed |

Not done in this item: wiring the placed lever, door and trigger together in the Logic graph by dragging pins, the shop
and quest forms filled to the end, a level exit chained to a second level, landscape painting, and Play of the
exported edits in the desktop app (the Play paths themselves were verified earlier: disc level, converted level, restore).

### 2. Sweep

`ui-sweep.mjs` now injects five error states (a file that is not a disc image, a corrupt saved level, a folder that is not
an Order of the Flame install, Play in a browser, a level that does not exist) and, with `--iso`, selects an object and
opens its context menu. With a disc, one size takes about 25 minutes in software rendering (the 3D view is drawn at 30
percent resolution); without a disc, three sizes take about 5 minutes. Findings fixed: raw parser messages in the status
line and toasts ("primary volume descriptor not found", "Unexpected token ... is not valid JSON") now read as sentences
(`shared/friendly.ts`, tests); two drop-downs in the dialogue composer overlapped. Level-loaded last run: only overlaps
between the 3D view's own badges remained, which the sweep now excludes. Not scripted: a failed Play on the desktop, a
missing game folder, a corrupt project folder (they need the desktop shell).

### 3. Interaction states, keyboard, ARIA

* The Outliner had roles but no keyboard operation at all. Arrow keys, Home and End, Right and Left (open and close a group),
  Enter and Space (select, Ctrl adds) now work; the row under the keys has a ring. Verified by pressing the keys.
* No visible focus ring on the menu bar, custom drop-downs, splitters, tool buttons, search fields, side tabs, help
  buttons, link handles and category rows: added (found by focusing every control and comparing its style).
* Menus, the palette list and the tree had no label: added. 0 controls without an accessible name.
* No pressed state on buttons, no hover or disabled state on the switch: added.
* Not audited: drag-over and loading skeleton states per component, screen reader output.

### 4. Viewport

Islands (the largest converted level, 2382 objects, 976k triangles): picks take 0.3 ms median and 1.3 ms worst over 60
points, 60 fps, the object list is ready 0.3 s after the open call. F frames the selected object exactly; wheel zoom,
middle-drag pan and right-mouse WASD fly work (orbit is Alt + left drag, as the shortcuts list says). **Fixed:** fly speed
was a fixed 60 units per second, about 38 units in 0.6 s with the camera 1000 units out; it now scales with the distance to
the focus point (minimum 60). **Open finding:** with the Surdana template, 253 of 297 classes used by Islands are not in the
editor's class list, so objects show as "class 16948" markers and the status line says "33 of 297 classes decoded"
(the earlier Intro-template conversion decoded 316 of 342). The game runs those levels; the editor view of them is poor.
Root cause not found.

### 5. Autosave and prompts

An edit is written 1.5 s later to IndexedDB, the status bar says "autosaved hh:mm", and opening the level again shows the
banner "Autosaved edits from ... found for this level" with Restore (4675 to 4676 objects) and Discard. **Added:** the page
asks before closing only while an edit is not yet written (checked: no prompt on a clean level, a prompt 0.3 s after an
edit, none after the autosave); the first version of this prompt fired on every close because opening a level also
schedules a write, which a check in the browser caught and a test now covers (`editsAtRisk`). Switching to another level
with edits asks nothing and loses nothing: the banner offers the edit when you come back. Project open, close and switch
in the desktop app were not exercised in this pass.
