# Runtime libraries cleanup (follow-up of the code health review)

Scope: `runtime/**` except `gs` and `gpu`. Follows "Cross-cutting duplication" in `code-health-review.md`.

## drakan_input.ini schema

- One description of the file: `runtime/input/include/input/IniSchema.h` (target `drakan::input_ini`, no dependencies,
  platform free). It lists every scalar key with section, type (Bool, Float, Int with range, Choice with aliases, Key,
  PadCombo, Ignored), default and accepted spellings, plus the key / mouse / DualShock name tables and the default file.
- Owner is `runtime/input` (the game's reader). `settings` and `ui` link the small `drakan::input_ini` target, not the
  input module, so there is no new dependency between the big libraries and no cycle.
- `Config::parse` maps schema keys onto `Config` members through one table. `SettingsStore` sends every
  `drakan_input.ini` value through the schema before its setter.
- Accepted values are the stricter of the two old readers: floats reject ERANGE / nan / inf (settings rule), `layout`,
  `toggle_key` and `debug_menu_combo` are validated (input rule; settings used to store any text), guest addresses must
  be 0..0xFFFFFFFF, `directional_attack` no longer takes `none` / `0` (the game writes `off`). Nothing the game writes is
  rejected (`input_ini_schema_test` parses the default file without warnings).
- The settings library's and the ui library's copies of the default file are gone: both use `ini::defaultIniText()`.
  The game host already passed that text to the settings store, so in game nothing changes; the standalone settings
  tools and the prompt defaults now see the game's real defaults (Enter / keypad Enter = Cross, I = VIEW_STRIP, M =
  VIEW_MAP, F5 / F8 / F3, 1..5 = QUICK1..5).
- Open: the settings binding catalogue (`settings/src/Bindings.cpp gameActions()`) still lists `ACT:0x17` "Item
  shortcut" and has no VIEW_MAP / VIEW_STRIP entries, so the Controls page cannot rebind the map and strip keys.

## Shared helpers

- `runtime/common` (header only, `drakan::common`): `common/Text.h` (ASCII lower / upper / trim / iequals / startsWith /
  endsWith) and `common/Path.h` (UTF-8 <-> `std::filesystem::path`). Fourteen per-file copies replaced.
- `exeDirectory()` already returned UTF-8, but `Config::loadOrCreate` opened that path as a narrow string (ANSI code
  page on Windows): an install folder outside the code page lost its controls file. Fixed with `pathFromUtf8`.
- Still ANSI, outside this scope: `ps2recomp/ps2xRuntime/src/lib/drakan_config_win.cpp` `exeDir()` uses
  `GetModuleFileNameA`, and `drakan_input_glue.cpp` opens `drakan.ini` with `fopen` on the UTF-8 `exeDirectory()`.

## Long functions split

- `hle::mods::ModManager::load` (450 lines) into find / check / ids / dependencies / order hints / merge steps.
- `net::MatchClient::handleMessage`: handshake, Status and Snapshot handlers.
- `net::MatchServer::onHit`: `hitRejection()` returns the first failed rule; arena potion slots named.
