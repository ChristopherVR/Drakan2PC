# Data-driven menus and HUD (UI skins)

What was hard-coded: `Theme` defaults (colours, sizes), row labels and order in `TitleScreen.cpp` / `PauseScreen.cpp`
(`titleRowLabel`, `pauseRowLabel`, `rowsOf`), the title/pause column geometry (constexprs), the Quit card text and title row
descriptions, and the HUD accent (`th.gilt` in `drawHud`). `drakan_settings_host.cpp` / `drakan_pause_screen.inl` only
construct the configs; they needed no change (screens build `Theme` through its constructor, which applies the skin).

Done: `gameui/UiSkin.h` + `UiSkin.cpp` (INI parse/write, global skin, `Theme()` applies it, `applyRowOrder`, key tables),
hooks in Title/Pause screens and `drawHud` (additive; `Theme::hudAccent` defaults to gilt so the look is unchanged),
`ModManager::uiFiles()` (`ui/*.ini`), loading in `drakan_overrides.cpp`, `configs/ui/default.ini`, tests
`gameui_skin_test` and `editor-core/test/uiskin.test.ts`, the editor Menus tab (`editor/menus/`). Reference:
`docs/reference/modding.md` "Menus and HUD".

Not done: pages other than title/pause/options pages (inventory, journal, map, save/load, multiplayer, settings) keep their
strings in code; the pause "Resume" back hint and page titles are fixed; no per-element rect layout beyond the nine
numbers; the skin is read once at startup.
