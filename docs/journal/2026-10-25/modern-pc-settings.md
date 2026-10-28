# PC Settings as modern pages inside the modern pause menu

Request: "If modern menu is on, can the PC settings rather be part of the modern menu and not an additional pop up on
its own. The current one is meant for traditional." Done in `runtime/gameui` (new component `SettingsPages`) and
`runtime/settings` (one schema for both UIs); the runtime glue in `ps2xRuntime` is not wired yet (see "To wire").

## Decisions

* **F1 stays the overlay.** F1 (and Back + LB + RB) opens the ImGui overlay in every state, including over the modern
  pause menu: it is the only settings UI that works in gameplay and in the native menus, and one key keeps one meaning.
  Inside the modern pause menu, Options > PC Settings and Controller > Key Bindings open the modern pages.
* **Edits apply at once** (like the modern Audio page), no Apply / OK: every change is a `SettingsAction` that the glue
  applies through the overlay's own apply path and saves. Risky display changes (window mode, window size, render scale:
  the overlay's `videoRisky` keys) show "Keep these display settings?" with the overlay's 12 s revert. Restart keys show
  "Restart required" (Restart Now / Later); the help panel's footer keeps the list for the visit.
* **One schema, generated from the settings code, not a second table.** The overlay's rows were hand-built ImGui (about
  900 lines of `Menu.cpp`). They are now produced by `dsettings::buildSettingsSchema` (`Schema.cpp`) as a
  `gameui::SettingsSchema` (data only, in gameui so the pages need no settings dependency), and the overlay draws them
  with a generic row drawer that uses the same widgets, strings and order. `settings_demo --screenshots` before and
  after: all 14 screenshots pixel identical (two differ only below the noise threshold: the capture box pulse and the
  popup fade, which also differ between two runs of the old build); the skinned `--styleshots` differ only where the
  flame animation differs between runs. Only intentional behaviour change: every combo now focuses its current value
  when opened with the keyboard / pad (before only the enum combos did).
* The schema keys are the `changedKeys` names (`video.vsync`, `input.mouse.sensitivity_x`); values are the ini
  spellings, set through the store's field table (`setSettingValue`). Pseudo keys: `video.window_size` ("WxH", the size
  presets / exclusive modes), `video.window_custom` (the overlay's free size field, overlay only), `ui.link_sensitivity`
  (modern only: the overlay draws it as the "Same as horizontal" check box; changing the vertical sensitivity on its
  own unlinks). Scope `OverlayOnly` also hides "Menu size" (the overlay's own size) from the modern pages.
* Main moved during the work: the new Gameplay > Combat > "Dual wielding" option (`general.dual_wield`) is in the schema
  and therefore in both UIs.

## What was built

* `gameui/SettingsSchema.h` (+ `.cpp`): `SettingRow` (kind, label, help, `unavailable` / `needs`, enabled, restart,
  confirm, `currentValue`, choices, slider range / step / format / log, scope), sections, pages, `BindingLayerView`.
* `gameui/SettingsPages.h` (+ `.cpp`): landing page, category pages with tabs and a scrolling list, help panel in place
  of the status strip, list card for choices, key binding pages with capture / conflict / clear / defaults, keep /
  restart / problem / defaults cards, pointer (hover, click, slider drag, arrows, tabs, footer hints), keyboard and pad.
* `PauseScreen` hook (small, for the merge with the title menu work): `PauseConfig::pcSettingsPages`,
  `PauseState::pcSettings / now / capturedKey`, `PauseFrameOutput::settings`, `pcSettingsOpen()`, `pcSettings()`. The
  PC Settings row opens the pages (trail "Paused / Options"), Key Bindings opens the gameplay keys directly; while open
  `frame()` returns the pages' drawing; Back from them returns to the row that opened them. No schema = old behaviour.
* `settings/Apply.h`: `applySettings` (the body of the old `SettingsMenu::apply`), `revertVideo`, `resetPageDefaults`,
  `bindSlot`, `clearSlot`, `resetLayer`; the overlay calls them.
* `settings/Schema.h`, `settings/ModernSettings.h` (library `drakan_settings_schema`): the builder and the modern glue.

## Tests and previews

* `gameui_settings_test` 113 checks (navigation, toggles / choices / sliders, list card, keep card time-out and revert,
  restart and problem cards, pointer drag, scrolling, bindings capture / conflict / clear / defaults, page defaults,
  aspects, the PauseScreen hook); `settings_schema_test` 825 checks (every store field has a row, every choice / toggle
  value round-trips through `applySettingChange` -> schema, ModernSettings: live apply, restart reasons, drag without
  save then one save, revert, bind / reset, page defaults, restart, errors). Existing suites unchanged and passing
  (gameui 262 / 370 / 470, settings 361, settings_menu 125, settings_demo self test 46 / 63).
* Previews: `settings_pages_preview <dir>` (real schema, fake RTX host) and `gameui_preview <dir>` (`pcsettings_*`,
  synthetic), rendered with `runtime/gameui/tests/render_drawlist.py`; copies in the main checkout's
  `build/gameui-preview-pcsettings/` (landing, display at 16:9 / 21:9 / 4:3, scrolled, DLSS section, list card, keep
  card, restart card and footer, audio slider drag, controls, gameplay, gameplay keys, capture, conflict, Key Bindings
  from the Controller page).

## To wire (ps2xRuntime, not done here)

`drakan_settings_host.cpp` `pauseDraw`: create `dsettings::ModernSettings` with the host and `g_overlay->store()`, set
`cfg.pcSettingsPages = true`, fill `PauseState::pcSettings / now / capturedKey` (keyboard keys while
`pause.pcSettings().capturing()`, through the input module's host-screen events), call
`applyAll(out.settings, ui.screen.pcSettings())`, and `open()` it when the pages open or the F1 overlay closes. The
`OpenPcSettings` / `OpenKeyBindings` cases of `drakan_pause_screen.inl` stay for the no-schema path. Snippet and the
table of what each action does: `runtime/gameui/README.md`, "PC Settings glue contract".

## Open questions

* The amber restart marks are the overlay's static ones (render scale, texture upscaling, HD textures, output device,
  prompts, language, world rate), while the runtime's `HostCaps::restartKeys` lists only world rate, language and
  prompts, so a render scale change shows "*" but no restart card. Make the mark follow `restartKeys`? (It would change
  the overlay's look.)
* The modern pages show no "Other bindings in the file" (unknown `CMD:` tokens) and not the controller layout view; the
  overlay keeps both.
* Live slider drags call the host every frame while held (volume, FOV ...). Fine for the setters measured so far; a
  setter that rebuilds resources would want `final`-only application.
* The help text says "Changes apply at once and are saved"; the title menu version (other agent) can reuse
  `SettingsPages` with `setTrail({"Options"})` and its own `wantsClose` handling.
