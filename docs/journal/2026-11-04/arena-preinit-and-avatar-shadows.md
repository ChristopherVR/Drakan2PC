# Deathmatch campaign initialization and the selected avatar's shadow

## Findings

The user's first-map menu launch already logged `straight into the map, no intro`. That only bypassed Level 0's
intro; Surdana still contained 18 Stomp Players, 195 detectors, 176 timers, conversations, NPCs and quest actors.
The post-load sweep waited for a controllable player / ghost readiness, so their initialization could start the
mission or cutscene before deletion. Faster cleanup after the fact is not a reliable solution.

The shadow was not submitted through the visible-mesh entry. Generic model-shadow routines `18d658` / `18cfa8` were
false leads in the tested character path. A temporary, delayed vertex-submission probe found surviving player geometry
through the character projector, with return PCs `1ab80c`, `1ac080`, `1a9b7c`, `15290c`. The full projector is `1ab890`:
manager in a0, model in a1. The temporary vertex / stack probes and false-lead hooks were removed after identification.

## Changes

* Optional FileService per-open filter; it can provide private memory-backed bytes without mutating IsoFs or existing
  handles. The game installs it only for an active versus arena's base LVL, after normal path redirection / mod lookup.
* `hle/ArenaLevel`: resolve LVL dependency classes, filter placed actor streams before init, remove campaign / NPC-bank
  placements, prune links to removed IDs, preserve environment and all other SRSC record payloads. Invalid input rejects
  the open rather than silently playing an unfiltered mission. Co-op / campaign and unrelated helper levels are unchanged.
* Environment policy keeps native player / camera, level properties (fog, light, physics), static / lit scenery,
  vegetation, sky, doors, light sources and ambient effects. The supported stock source levels retain 3518 / 290 / 547
  placed environment / player / camera actors respectively after removing 1157 / 300 / 135 campaign placements.
* The stock arenas explicitly request `0x33cbc` as starting loadout. The dagger factory creates a valid inventory-private
  item without a world registry ref; the old world-snapshot-only stow gate could never accept it. Owned factory items
  with valid model / item type / initialized data are now admitted to native Stow; malformed / dying actors are not.
* `drakan_local_character::View::shadowObject` maps the controlled visual model to the selected NPC. The projector hook
  changes a1 before native bounds / pose / silhouette preparation; a0 (manager) is not changed. Duplicate cosmetic-body
  projection is skipped. No gameplay actor visibility, skeleton or inventory data is replaced.

## Validation

* Synthetic `arena_level_test`: terrain bytes preserved; NPC (including a static NPC-bank model), detector, timer and
  Stomp placements removed; links pruned; private versus handle differs from original campaign handle; source FS bytes
  stay unchanged; malformed input rejected. Optional ISO smoke test passed all three stock arena source levels.
* `character_roster` covers shadow source substitution, duplicate suppression and unaffected other actors / Rynn mode.
  The menu / preview / avatar-policy tests pass with the new code.
* **[game]** `build/check_clean_arena_menu.py`, evidence `build/harness/arena-menu-preinit-shadow/`: actual title menu ->
  Multiplayer -> Host first map -> Start. No DRAKAN_START_LEVEL shortcut was used. Native actor dump before the old sweep
  has 3310 entities, no Stomp / detector / timer / conversation / dragon classes. One Monk avatar, direct launch, movement,
  native character-shadow substitution and clean exit; native starting dagger reached the right hand after explicit loadout.
* **[game]** Monk / Ranwulf co-op run `build/harness/local-character-stability-live/`: both projectors use their selected
  bodies, including after quick-save. Native bow equipment and firing remain functional (ammo 179 in the inspected shot).
  Inspected the stone-ground silhouettes: the Monk's pose / shape is used instead of Rynn's previous silhouette.
* The play folder was refreshed through Build-LocalGame.ps1. A lingering user process 31216 was terminated only after
  explicit permission; no other user process was terminated.
