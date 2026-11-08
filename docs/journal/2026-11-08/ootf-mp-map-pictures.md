# Order of the Flame multiplayer maps get menu pictures (2026-11-08)

Report: the ten OotF maps of the `ootf-levels` pack (`maps/OOTF*.ini`, `level = OotF - ...`) showed the drawn sketch on
the multiplayer cards, because `map_previews.py` only knew `configs/mp_maps.ini` (see `mp-map-pictures.md`).

## Approach

In-engine pictures through the existing harness, then shipped inside the pack:

* `scripts/harness/map_previews.py` now also reads the `[arena]` sections of the enabled packs in `<src>/mods`
  (`maps/*.ini` of each zip; `mods.ini` `id = off` skips a pack; `--no-mods` turns it off). The game loads the packs itself
  (the harness folder junctions `mods/`), so `DRAKAN_START_LEVEL=<arena level title>` and `DRAKAN_ARENA_FORCE=<arena id>`
  work as for built-ins.
* Game fix, `drakan_arena.cpp`: `DRAKAN_ARENA_FORCE` was resolved once at init, before the mod packs register their maps
  (`[arena] DRAKAN_ARENA_FORCE: unknown map 'alwarren-of-death-air'`, no preview shot, no picture). It now stays pending and
  `addMaps` retries it as each pack's maps arrive. Needs a game rebuild to take effect.
* `scripts/harness/pack_map_previews.py` adds `previews/<safe map name>.png` to each pack zip from `previews/maps/`
  (idempotent, atomic zip replace, `mod.toml` untouched so the pinned `build` id and saves stay valid; missing pictures
  skipped). The game copies a pack's `previews/` to `previews/mods/` where the menu finds them.
* `Build-LocalGame.ps1`: after the map picture step, a second non-fatal step runs `pack_map_previews.py` on the play
  folder's `mods/`.
* Editor: `OotfLevelInput.preview` (PNG) in `buildOotfPack` ships `previews/<previewFileName(arena name)>` (ignored with a
  warning when the level has no arena); `ConvertRequest.previews` (by file name) for the import flow and `--previews <dir>`
  for `editor/scripts/ootf-convert.ts`. The browser top-down `capturePreview` is not used: the in-game route gives real
  in-engine frames.

## Tests

`scripts/tests/test_pack_map_previews.py` (discovery, injection, idempotence, pinned manifest),
`editor/packages/editor-core/test/ootf-previews.test.ts` (LVL builder stubbed, synthetic data).

## Game changes needed

* `DRAKAN_ARENA_FORCE` stays pending until the packs' maps register (above).
* Air arenas: Rynn spawns on a launch pad far above the floor and the fall killed her, which sent the game back to the
  title before the shot. The harness now sets `DRAKAN_PLAYTEST_GOD=1`. A map without an authored `preview` point shoots from
  `spawn[0]`; she lands below and beside it, so the "stands on the point" check in `previewShotTick` now only applies to an
  authored point.

## Result

All 10 maps give a real in-engine frame on the first attempt (about 1 minute each), written to a scratch folder and
injected into a copy of the pack (10 `previews/*.png` entries, second run a no-op). Quality varies because the OotF maps
have no authored `preview` point: Arena of Death (air), Grotto of Death (air), Hill of Fire, Relic, Nemesis, Island and
Volcano of Death read as map pictures; Alwarren of Death (air) is a dark cave floor, Rise to Power (mod) is nearly all
sky and Sanctuary (ground) mostly sky. Fix by adding `preview = x y z yaw pitch` to those arenas (the editor's arena panel
can author it), then re-run with `--force`. The pictures are made from the player's disc and are not committed.
