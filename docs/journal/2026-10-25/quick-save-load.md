# Quick save / quick load — 2026-10-25

User request: adopt Drakan: Order of the Flame's quicksave/quickload functionality in the PC port.
The original game's Q/L keys are documented in its walkthroughs, e.g.
[Dan Simpson's guide](https://gamefaqs.gamespot.com/pc/61696-drakan-order-of-the-flame/faqs/2541).
Default here: F5/F8, preserving Q = Quick Inventory; rebindable QUICKSAVE/QUICKLOAD actions allow Q/L.

Native saves, not emulator snapshots. The dedicated Slot33 lies beyond all configurable manual slots.
The EE input-boundary state machine uses the native pause/slot flow and the existing modern-menu save/load
confirmation mechanisms. It restores the manual page only after the native operation leaves its slot screen.
Cancellation disarms pending timer/confirmed-load flags before changing folder names. Missing quicksave is a no-op.
Reference and limitations: `docs/reference/save-slots.md`, “Quick save / quick load”.

## Validation

Built and deployed with `Build-LocalGame.ps1 -Incremental -WorkDir build/final-game`; launched only from
`dist/DrakanPC`. Private input/general/native-options inis and private cards under `build/harness/quick-save-*`.
The source card was the existing private `build/harness/polish-save/mc0`, not the player's card. HD replacements
and DLSS were disabled for these functional checks. Input was scripted; real hardware was not exercised.

* `quick-save-roundtrip`: missing F8 ignored; held F5 made exactly one save; second F5 overwrote Slot33;
  F8 loaded that save after moving away. Both saves returned directly to gameplay. Save request -> gameplay:
  21.692 -> 22.312 s and 46.565 -> 47.208 s; load: 87.697 -> 88.610 s. One `.rsg` in Slot33;
  all three files in manual Slot1 remained byte-identical (SHA-256 comparison). `overwritten.png` and `loaded.png`
  show the same saved position. The final Esc pause and resume worked.
* `quick-save-restart-fixed`: fresh process, card containing only Slot33, `DRAKAN_SAVE_PAGES=1`, autopress and
  autostart off. Title F8 -> load -> gameplay (21.952 -> 24.310 s). `quick_progress.png` shows the progress card;
  `restart_loaded.png` shows the saved position. Run exited normally through the script's quit.
* `quick-save-final`: repeated the full round trip against the final deployed build after the title-input fix and
  immediate slot-host cleanup. The tracked `scripts/harness/recipes/quick_save_load.txt` uses the supported
  standalone `clock milliseconds` header (the first probe ran as frame timing). Automated log/card checks passed:
  exactly two save requests/completions, one load request/completion, missing load ignored, one quicksave `.rsg`,
  unchanged manual Slot1 hashes and normal script quit. Four renderer screenshots; 50 s including boot.
* The first restart check found the host screen neutralizing the title's shortcut before the integration could
  read it. `InputModule::titleQuickLoadPressed` now captures the bound edge before neutralization, only at screen 8
  and outside the settings overlay; the native pad sample remains neutral. Regression checks cover that boundary.
* Focused CTest: `input_test`, `inventory_views_test`, `settings_test`, `gameui_saveload_test` all pass. Tests cover
  default/old/custom/NONE bindings, title ownership, progress without browser actions, Options migration and
  persistent unbinding (including a custom quickload key). The input reload-thread test used a private TEMP
  directory; its shared default temporary file produced intermittent failures during concurrent compilation.
* Repository source audit passes. No game-derived files were added to the tracked tree.

The initial build exhausted Windows commit memory during concurrent VU compilation; the focused test build was
retried with two jobs. Shared-tree linker/deployment file locks were resolved by rerunning the approved builder.
Existing repository edits are retained. Native disk/card error dialogs, mounted flight and death-screen quickload
were not exercised; death-screen quickload is not wired. Continue still selects the newest manual save.
