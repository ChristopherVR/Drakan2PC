# Mod support in the runtime (2026-10-31)

Format and rules: [modding.md](../../reference/modding.md). Code: `runtime/hle/include/hle/ModPack.h`, `src/ModPack.cpp`
(std-only header), glue in `drakan_overrides.cpp` (before the UI overrides), `DirectoryOverride` kept for `DRAKAN_OVERRIDES`.

- Packs: folder or zip (own inflate, stored/deflate, CRC checked, zip-slip entries dropped), `mod.toml` / `mod.json`
  (small TOML subset parser, JSON parser). `mods/mods.ini` gives enabled flags and load order; new packs are appended as off.
- Validation per pack: manifest, format, profile id and profile hash (FNV-1a of "serial|saveId"; PAL `b9f60ba118125a54`),
  duplicate ids, dependencies (fixpoint). Conflicts per entry and per text line are logged, later wins.
- Limits: not raised. `MAX_LEN` 0x40800 is a request limit of RSRCIOP; guest buffer sizes per type are unverified, so a
  replacement that crosses the limit where the original did not is rejected at apply time (original kept), growth below it is
  allowed with a log line. Evidence for raising would need per-type buffer audits.
- Tests: `runtime/hle/tests/mod_test.cpp` (75 checks, in the `runtime-tests` ctest run).
- In-game check (private folder `build/mods-play`, nothing in the repo): folder pack `modtest_a` (INTERFCE.TXD palettes of
  76 textures turned magenta, XDB bin + text.ini) plus zip `modtest_b` (conflicting XDB 118 entry) and a USA-only zip.
  Log: A and B loaded, conflict reported with B winning, USA pack skipped with the profile message. Screenshot
  `build/harness/modtex/play.png`: HUD text, map icon frame and health orb are magenta.
- Finding: XDB text changes (ids 118, 121, 130, 168) are not visible on PAL because the title, pause and options menus are
  drawn by the host UI, not from the game strings. Text packs only show where the native game draws text.
- Not done: Mods page in the PC settings menu (`ModManager::report()` / `status()` are ready for it).
