# Issue #7: binding labels follow the controller scheme and the Arokh table

Report: the F1 binding catalogue, the controller page and the default ini comments describe scheme A on foot only,
while the game keeps a scheme (A / B / C, `opt+0x2e8`, `JoyConfig_Scheme`) and a separate pad -> command table while
Rynn rides Arokh.

## Evidence checked

* `analysis/input/tables.txt` holds the six tables exactly as the issue quotes them (0x511ed8 ground, 0x511ee8 the
  other set).
* `analysis/input/003fa258_Options_ApplyButtonScheme.c`: the 0x511ee8 set is chosen when `FUN_003d2798(0x4fbac8)` is
  non-zero, and `0x3d2798` is the game's dragon-mode predicate (journal 2026-10-17 combat-encounters). So 0x511ee8 is
  the Arokh table, as the issue says.
* **R3 is not the mount toggle.** The issue quotes the old [L] guess of `input.md` section 4 ("0x400a mount/fly
  toggle"). The handler of `0x400a` in `Game_OnInputCommand` only touches the camera object (`ctx+0x4b4`, its
  `+0x48` flags and mode `+0x14c`), and two journals already corrected it: 2026-10-17 combat-encounters ("Command
  0x400a and gate 0x32c3f0 concern R3/V camera/view mode") and 2026-10-23 world-rate-audit ("`0x400a` (V / R3) is the
  camera mode toggle, `0x4009` (E / L1) mounts"). R3 is labelled **Camera view toggle**; mounting stays on Call
  Arokh / Use (0x4009).
* Labels follow the command id, not the issue's English names: `0x4009` is "Call Arokh / Use" in every scheme (the
  issue's "Jump/Use" for B / C is that same command).

## What changed

* `runtime/settings` (`Bindings.cpp/.h`): the six tables, `padCommand`, `commandLabel`, `actionLabel` (foot, current
  scheme), `actionLabelOnArokh`, `actionHint`, `setControllerScheme` / `controllerScheme`. The spell-free "Parry"
  follows command 0x4007 wherever the scheme puts it. The eight scheme buttons share one "Actions" group (L2 / R2 were
  under "Targeting", wrong for B / C). Wheel: "Previous / Next item (context-dependent)".
* F1 binding table: "(on Arokh: X)" beside a key whose flight meaning differs; the tooltip names the scheme. The
  controller view has On foot / On Arokh / Pad columns and names the scheme. The modern settings pages get the same
  label through `buildBindingViews`.
* `drakan_menus.cpp`: `nativeOptionsTick` publishes `opt+0x2e8` each tick; `installNativeOptions` seeds it from
  `drakan_native_options.ini` before the title.
* Default `drakan_input.ini` comments (`runtime/input/src/Config.cpp`, comments only) say scheme A on foot and point to
  F1 for B / C / Arokh; V, 6 and the wheel lines corrected.

## Not changed, and why

* **Button prompts** (`runtime/ui` Prompts.cpp, TextRules.cpp): they replace a pad button (glyph, diagram cap, "L1" in
  text) with the key bound to that button. That mapping does not depend on the scheme, so they stay correct. The
  disc's own sentences name buttons, as on the PS2. No code change; prompts are built once at start.
* **Rebinding onto a used key** already asks in both menus (ImGui `conflictAsk`, gameui `SettingsCard::Conflict`):
  Replace / Use for both / Cancel, Replace first. That was already in the reported commit `bacb3ea`. Only a
  hand-edited ini stacks actions silently (that is the file format: `KEY = A | B`). No Swap choice was added. Tests
  pin the Replace and Use for both commits (`testRebindConflicts`).
* The strafe / dragon remap in `runtime/input` (issue #11) is untouched.

## Side finding

`[gamepad] debug_menu_combo = L3+R3` (`input/Config.h` says both clicks "have no game function"): R3 is the camera
view toggle, so the chord also toggles the camera view. Not changed here.

## Verification

`settings_test` (`testControllerSchemes`, `testRebindConflicts`) and the runtime-tests ctest run. Not verified in real
play: a scheme B / C card, the Arokh labels other than climb / dive (measured earlier), and the F1 pages on screen.
