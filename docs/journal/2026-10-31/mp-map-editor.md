# Multiplayer map authoring in the level editor (2026-10-31)

Reference: `docs/reference/netcode.md` ("Authoring in the level editor", "Authoring keys", "Custom game modes"). Tags: **[browser]**
checked in a headless browser against the real ISO, **[game]** seen in a game instance, **[test]** unit tests, **[open]**.

## What was built
* **Multiplayer Map dock tab** [browser] (`editor/packages/app/src/editor/arena/`): Map (name, id, description, modes, players,
  health, loadout with the item picker, bounds), Place (spawn / pickup / flag / base / zone / bounds tools, move + rotate gizmo,
  per item fields, Suggest spawns, Alternate teams), World (what the arena removes, keep / remove per object), Modes (custom
  `[mode]` editor), Export (pack fields, top-down preview capture, zip / mods folder / INI only).
  Start card: any campaign level of the disc, or import an `mp_maps.ini` / pack `maps/*.ini` and edit an arena.
* The panel is registered through the shell's `registerPanel`; the Validation tab got a small `addValidationProvider` hook
  (`editor/panels.ts`) so the map's issues show next to the document's.
* Edits go through the level view's command stack (`ArenaCommand` swaps the whole draft), so Ctrl+Z / History work; a gizmo drag is one row.
  Side effect: the level shows "Unsaved changes" and autosaves the (unchanged) document after a map edit.
* Pure logic in `editor-core` [test]: `arena.ts` (draft, INI writer / reader, filter plan, validation, spawn suggestions,
  default bounds), `gamemodes.ts` (`[mode]` writer / reader / validation), `arenapack.ts` (pack layout).
  `app/test/arenastore.test.ts` covers the store.
* Runtime side (see the netcode.md tables): `spawn ... team=`, `marker`, `keep` / `remove`, custom mode ids in `modes`,
  `[mode <id>]` (`net/GameModes.{h,cpp}`, test `net_modes_test`), `drakan_modes.h`, mod pack `previews/` copied to
  `previews/mods/` and looked up by the menu.

## Checks
* Export of a map built in the browser (Inquisitor's Lair, 4 suggested spawns, 2 Steel pickups, preview) -> pack `mod.toml`,
  `maps/editor-test-arena.ini`, `previews/Editor_Test_Arena.png` [browser]. Screenshots in `build/mp-shots/` (git-ignored):
  `place3.png`, `world.png`, `modes.png`, `export.png`, `picker.png`.
* In game [game] (`build/mp-shots/game_tour.py`, the arena tour on a private folder with the pack in `mods/` and the INI as
  `mp_maps.ini`; run dirs `build/harness/mp_editor_check{,2}`): the arena loads ("arena world mode on: Editor Test Arena", the
  pack's map list is read), the filter removed 52 campaign actors, the Steel Axe pickup spawned ("pickup 0 (Steel Axe) is
  back"), spawn 0 placed the player 8..400 units from the editor's point, spawns 2 and 3 were reached on the second round
  (8 and 1938 units off), spawn 1 (the upper tier, y 151028) never took within the tour, the first round's screenshots are
  black (the camera was far outside the level), `pickup 1 (Steel Battle Shield): the respawn never showed up` (the known open
  item of mp-arenas.md). The first run used the binary built before the runtime changes of this work: it parses the old keys
  only, which is why the map used none of the new ones.
* The editor's filter preview said 40 removed objects for the same level, the game removed 52 (23 creatures, 17 story, 12 loose
  items): creatures and loose items are guessed from class names, the engine goes by hit points and entity type.
* Compile check of `drakan_overrides.cpp` and `drakan_settings_host.cpp` with the new previews code (MSBuild ClCompile on the
  runtime project, no link, no deploy).

## Not done / limits [open]
* The game does not read `keep` / `remove`, markers, spawn team and facing, `score_*`, `friendly_fire`, `weapons`, mode `loadout`
  or `objective`; the menu has no row for custom modes. They are authored, written and parsed (tests) only.
* Reachability is a coarse height-step flood fill over the terrain; collision geometry, water and jumping are not known to the
  editor, so spawns can still be inside walls (spawn 3 of the check died in a lower cave).
* The terrain height function and the viewport ray can disagree at terrain edges ("over no terrain" for a spot the picture shows).
* The Tauri desktop write-to-mods path was reused (`writePackToModsFolder`), not run.
* The preview capture needs the terrain loaded; objects and models are included as they are in the viewport.
