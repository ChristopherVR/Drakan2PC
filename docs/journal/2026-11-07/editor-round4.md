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

## Not done in this round

The coordinator's round 4 list is far larger than this pass. These were **not** done and nothing is claimed for them:

* The full first-time-user path (new mod from a template, enemy plus pickup plus door plus lever plus trigger wired in the
  Logic graph, conversation, shop, quest, cut scene, level exit chained to a second level, landscape painting, export
  and Play of the result in the desktop app).
* The ui-sweep level-loaded states in software rendering and the injected error states (bad pack, failed Play, missing
  game folder, corrupt project, unsupported disc).
* The interaction-state audit of `shared/components.ts` (hover, focus ring, disabled, pressed, drag-over, skeletons),
  keyboard-only use of menus and docks, and an ARIA audit.
* Viewport quality checks (camera navigation feel, selection outline, object icons, picking speed on the biggest level).
* Autosave and crash recovery, unsaved-changes prompts, project open, close and switch flows.
