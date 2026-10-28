# Keep the modern inventory visible behind native dialogs

The modern Tab / strip / map overlay used `visible = show && !box`, fading the entire host UI out for first-visit
help, native Yes / No questions and skill-point confirmations. This exposed the hidden native inventory's live world
and grid fill while the prompt was open.

The overlay now keeps rendering the modern menu with no host input while a native box owns input. A hook on the
native brass frame draw (`2c94a8`) publishes its actual painted bounds, including the ornaments, for GUI screen
frames (return address `2bd388`) and frame widgets (`427fc8`). Tutorial prompts use the latter. The overlay copies
only that rectangle from its existing pre-overlay frame after drawing the modern menu. It uses the presented
viewport, native frame dimensions, HUD scale and rendered page anchor. Native buttons and dismissal paths remain
in charge; passive host selection requests wait in the existing EE executor until the box closes.

The crop resets when the box closes, changes or a new host session begins. If the frame or bounds are unavailable,
the previous fade-aside path keeps the native prompt accessible. This is a rectangular native-frame crop: the
transparent outer parts of the ornamental border retain their native backdrop.

Validation:

- Incremental game build through `Build-LocalGame.ps1`, existing `build/final-game`; deployed through the same
  script to `dist/DrakanPC`.
- Fresh-card Surdana run `build/harness/modern-dialog-v3`, recipe `build/modern-dialog-recipe.txt`: first Equipment
  help prompt visibly overlays the modern menu; Enter dismisses it, the menu remains usable, and Tab closes back
  to gameplay. Inspected `help_open.png`, `help_closed.png` and `menu_closed.png`; the run exits normally.
- Ten existing gameui / inventory-view / Tab-preview tests pass in `build/runtime-tests`.
- Changed UI sources pass clang-format; `git diff --check` and the tracked-file repository audit pass.

Build housekeeping: stale CMake IPO probe caches had an empty generator platform and blocked the x64 configure.
Preserved those generated probe directories with `-before-dialog-fix` names under the existing build tree, allowing
CMake to regenerate them. No game data or captures are tracked.

## Modern prompt presentation

Added `gameui/Dialog.h` / `Dialog.cpp` to present native message boxes with the same theme and renderer as the
modern menus: opaque dark stone, gilt heading, ivory reflowed body text and choice cards with the red focus trace.
The reader follows native label / button widgets, preserving captions, focus and enable state. Empty label
placeholders are skipped; pre-wrapped native label lines are joined before host wrapping. A short prefix ending
in a colon becomes the heading. Unknown controls, malformed lists or missing content retain the native crop.

Button cards remain within the native centred-label mouse hit regions (minimum width 66 GUI units), so native
mouse, keyboard and pad activation still own the interaction. The modern renderer changes presentation only;
it neither answers a confirmation nor changes its default choice. Button font sizes fit the available width.

Validation: the dialog reader / presentation tests and five existing gameui test executables pass (six
targeted tests). Added synthetic help and Yes / No scenes to `gameui_preview`; inspected the generated 16:9 help
and 4:3 confirmation images in `build/modern-dialog-style-preview`. Native capture
`build/combined-build06-private/t/build06-final-native32/ram_cat_spells_prehelp.ram` confirms the widget layout,
including null-text label placeholders.

Rebuilt and deployed with the canonical incremental script (`build/final-game`, Jobs 2, CompilerProcesses 1) after
the other chat's shared game build completed. The first overlapping build attempt exhausted compiler memory;
stopped our competing configure and waited for the shared build to finish before retrying.

Live runs from `dist/DrakanPC`:

- `build/harness/modern-dialog-style`: inspected modern Equipment and Spells help, Enter dismissal, subsequent
  page changes and the return to gameplay. Both prompt openings / closings and normal script exit are logged.
- `build/harness/modern-dialog-style-input`: the modern OK card receives the native mouse press / release
  (`gui=256,307`, screen 1), then closes. A subsequent Spells prompt closes with gamepad Cross. Inspected the
  returned Inventory / Spells pages; menu close and script exit succeed.
- Source formatting, whitespace checks and the repository audit pass. Captures and RAM probes stay under `build/`.
