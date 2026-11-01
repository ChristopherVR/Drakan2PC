# Release housekeeping and smoke test (2026-11-02)

* `Build-LocalGame.ps1 -Incremental` on `build/final-game` refilled `dist/DrakanPC` (the stale copy was missing `mods/`, the new exe).
* Fresh packs (private, `build/ootf-run/rel/mods/`): `ootf-levels.zip` = `ootf-convert.ts --campaign 1` with the 13 campaign levels in play
  order (9119 files); `ootf-arenas.zip` = the 10 `Multiplayer/*` levels with `--id ootf-arenas` (158 files, 10 maps). HD:
  `ootf_hd_pack.py` indexed both and upscaled the changed hashes; `dist/DrakanPC/textures` now holds 5074 textures.
* Installed in `dist/DrakanPC/mods` with `mods.ini` order arenas, then levels (the later pack wins the shared `RESOURES.*` files).
* Smoke (harness `play_run.py`, 960x540, shots in `build/ootf-run/rel/smoke/{ootf3,d2b}`):
  * Title -> "Choose your game" -> Order of the Flame -> menu -> Multiplayer (Host Game, Join Game, mplayer.com; no Co-op) -> Host Game lists
    the arena maps with `Drakan 1 / CUSTOM` tags -> New Game -> Solo: `OotF 00 - Intro` loads, host-fired exit 0 loads
    `OotF 01 - Ruined Village`, Rynn stands in the village (the first screenshot after arrival can be black: arrival fade).
  * Drakan with the packs installed: card -> Drakan -> New Game -> Solo: `Level 0 - Intro` then `Level 1A - Surdana`, Rynn under control; no
    OotF level in the lifecycle log.
* Observed: the Drakan 2 run replaces `RESOURES.MOD/.TXD` and `LEVEL1ON.*` from the campaign pack as well (pack-level file replacement,
  by design); it played Surdana normally.
* Tests: editor build and tests, Python tests, repository guard, ruff pass. clang-format found trailing-comment alignment in
  `drakan_pvp.cpp` and `drakan_input_glue.cpp` (fixed). A CRLF working copy of `Kernel/Stubs/MemoryCard.cpp` (index is LF) fails the
  local check only; CI checks out LF.
