# Handover: editor, mods, multiplayer (recovery reviewed 2026-11-03)

**Recovery update:** [2026-11-03 session review](../journal/2026-11-03/session-recovery.md) separates the ready editor /
multiplayer changes from the unconnected campaign wizard and local OotF investigations. It also records three-player
mode harness results that supersede some unproven items below. Older bullets are historical unless rechecked there.

**Follow-through:** [campaign wizard and OotF tools](../journal/2026-11-03/recovery-follow-through.md) records the wired
wizard, real-disc route/pack-build check, tested extractor and formats diagnostics. The null-controller mitigation
remains an opt-in diagnostic: regression found a third unchecked controller pass, so that lifecycle bug is still open.

**2026-11-06 editor pass:** converted Order of the Flame levels now open in the viewport (overlay over the opened disc,
`editor/packages/app/src/virtualdisc.ts`), chain on the project's campaign map, and render at 57 to 60 fps (terrain draw
calls cut from about 9000 to 3000). Details, results of the 23-level walkthrough and the open items (import runs on the page
thread, converted levels are not persisted, Story shows disc text only, Play not re-run) are in
[editor OotF viewport](../journal/2026-11-06/editor-ootf-viewport.md).

**Round 3 (2026-11-07):** Play from the desktop app now works for disc and converted OotF levels (the converted pack is installed as `mods/ootf-levels.zip`); see [editor round 3](../journal/2026-11-07/editor-round3.md) (the UI sweep `npm run ui-sweep -w @drakan/editor-app` re-checks every workspace at 820, 1280 and 1920 px) for what was verified and what is left (see the follow-up section for the temporary Play pack, strings scope and terrain draws).

Living tracker for the editor / mod / multiplayer / engine-portability track. Last updated at commit c03a3b0
(editor) and c270bb9 (game and runtime). Details live in the dated notes under `docs/journal/2026-11-01/` and
`docs/journal/2026-10-31/`; this page says what works, what is unproven, and what to do next.

## Goal

A full level / game / mod editor (UE5-like but simple), an updated game with mod support, and multiplayer: PvP on
dedicated arenas like Drakan: Order of the Flame, and co-op on the campaign (host plays Rynn, joiners pick another
character, everyone has a dragon). Content should become engine-neutral so the game can later be ported to Unreal
Engine 5; textures move to UE5-style PBR sets.

## Where things are

| Area | Code | Reference docs |
| --- | --- | --- |
| Level / mod editor | `editor/packages/app/src` (shell in `editor/ia.ts`, `workspace.ts`, `menubar.ts`) | `docs/reference/editor-guide.md` (generated from `editor-core/src/guides.ts`) |
| Save game editor (own app) | `editor/packages/app/save.html`, `src/saveapp.ts`, `src/save/*` | `docs/reference/save-slots.md` |
| Editor model / writers | `editor/packages/editor-core/src` | `docs/reference/level-format.md`, `modding.md` |
| Desktop app (Tauri v2) | `editor/packages/desktop/src-tauri` | `editor-guide.md` |
| Netcode, server, game modes | `runtime/net` | `docs/reference/netcode.md` |
| Multiplayer menus, HUD, UI skin | `runtime/gameui` | `netcode.md`, `modding.md` ("Menus and HUD") |
| Game hooks | `ps2recomp/ps2xRuntime/src/lib/drakan_{coop,netplay,ghost,arena,pvp,levelchange,playtest}.cpp` | `world-model.md`, `env-flags.md` |
| Engine-neutral content | `editor-core/src/content/*`, `schema/drakan-content.schema.json` | `docs/reference/content-format.md`, `docs/proposals/engine-portability.md` |
| Unreal export | `editor/packages/app/src/unreal/*`, `public/ue5/drakan_import.py` | `docs/reference/unreal-export.md` |
| PBR materials | `analysis/scripts/pbr_pack.py`, `configs/pbr_overrides.toml` | `docs/reference/hd-textures.md` section 10 |

## Proven in game or editor this round

- Story: conversations start from real input (quick E tap, facing, through the NPC); edited subtitles, swapped
  sound lines and new lines play (`scripts/harness/story_dialogue.py`, 14/14). Existing and new cut scenes play,
  camera edits show (`story_cinematic.py`). Message ordinal -> engine id table decoded.
- Vendors: stock and price edits show in the shop and buying charges the edited price (`vendor_shop.py`).
- Save editor: Replace with... and Add item work in game and survive the game's own save and reload.
- Level change: exits fire from their trigger detector boxes; a real walk-in into Kragmor works and a co-op partner
  follows. Level copies load and can be linked; per-exit destinations work.
- PvP: real blade hits on all three arenas; bow and spell kills in one run (`arena_ranged.py` 14/14); full matches
  to the frag limit with results, back to lobby, next map, rematch and map vote.
- Co-op: partners' weapons in hand, dragons for three players, parked dragon idle, player and dragon health in the
  party HUD, map pictures and character turntables in the lobby.
- Menus: a mod can rename, reorder and recolour main and pause menu entries (`ui_skin_check.py`).
- Campaign packs (2026-11-02, `docs/journal/2026-11-02/campaign-packs.md`): Project Settings > Campaign exports a project as
  its own campaign (`campaign = "<id>"`, cover, own save folder, per-campaign menu skin); the title's game card lists every
  installed campaign, New Game opens its start level by its real name, an exit chains into its second level at the right
  marker and saves go to `mc0_<saves>` (proven with an editor-built two-level pack).
- Editor: UE5-style structure (project chip, editor tabs, World Settings, three dock tabs, menu bar, + Add),
  Logic graph with plain-language links, Story workspace with conversation cards and a cut scene timeline.
- Exports: Drakan Content Format 1.0 validates on Surdana; Level for Unreal (glTF) validates with the Khronos
  validator and loads back in three.js.

## Open work

### Editor

- Play test (2026-11-02, `docs/journal/2026-11-02/editor-play-direct.md`): the toolbar Play / F5 now starts the open
  level from where the camera looks, on a throw-away memory card, straight into a disc level (no title choice, intro,
  or save), and the engine loads it under its real name so exits work. Proven in game (Surdana, Stratos with a pack,
  Ravenshold, a walk-in exit to Kragmor) and by a real click in the desktop app. Not run: a level copy (keeps the
  intro route).
- Click through the new Vendors panel, Story workspace (new line in Conversations, quest area start/complete
  buttons, making a cut scene from scratch) and Logic graph keyboard paths with the real disc.
- Launch the desktop app: the separate save window, native menus (`cmd:<id>` items) and Tools > Save Game Editor
  are only built (cargo), not clicked.
- 2026-11-02 later: Play reaches the level in ~10 s (`DRAKAN_FAST_START`: boot logo fast-forwarded, New Game at once);
  level copies and OotF levels also open straight away; exits out of a copy arrive at the source's marker; Project panel
  has a Campaign map (List | Map). Clicked in the desktop app: row Play, Change destination, revert, save size check
  (`editor-desktop-clickthrough.md`). Map view not yet clicked with the disc. Small leftovers listed in that note.
- Done 2026-11-02, not yet clicked with the disc: per-object "Revert to original" (right-click, Details bar, Mod panel
  rows; `revertObjects` in editor-core), the Details header shows once, the save editor's item grid and save list rows
  use `itemGrid` / `gridTile` / `listRow` from `src/shared/components.ts`, and the Project panel has Play on every level
  row plus "Change destination" on exit rows.
- Save editor checklist steps cannot tick from the level editor window since the save editor became its own app.
- The Menus dock panel is cramped (export bar overlaps); remaining game pages (inventory, journal, map, save/load,
  multiplayer, settings) are not skinnable; the skin is read once at startup.
- Order of the Flame port state: see [ootf-port.md](ootf-port.md). The full `npm run build` passes again (2026-11-02).

- UI overhaul 2026-11-02 (journals `editor-shell-layout`, `start-page-redesign`, `project-panel-redesign`,
  `object-categories`, `story-in-viewport`): resizable docks + left Place panel; template-picker start page with a step
  checklist; Project Settings as levels tree table + message-log Checks; 19 display categories (no placed object in
  "Other"); cut scenes / shops / quests / conversations drawn in the viewport, "Add cut scene here". Follow-ups: Place
  panel should list `storyPlaceables()` (story/storyactions.ts); Story tab hides the viewport unless docked; "Show in folder" needs a Tauri command; Project Id stops following Name
  after one keystroke (reported by the docs agent, recheck); site media (`docs/site/media.json`) pending upload.
- "Add shop here" works (2026-11-02, journal `add-shop-here`): shopkeeper + shop + On box (`buildNewShop`,
  editor-core `vendor.ts`); proven in Surdana: talking (Use) to the new keeper opens its shop, a purchase takes the gold.

### Story and logic

- Quests: prove a quest starts and completes in game. The writer now uses ordinal 30 (Quest Initiate) instead of
  31 (Item PickedUp); unverified. Item classes send no messages, so "complete on pickup" is an area around the item.
- Puzzles: no on-screen proof of a door opening yet (Stratos plates are tall blocks; pick a level with a walkable
  plate). Counter with two plates, lever, timer -> door and key lock are not run in game. The NecroBB door death
  at +7000 units is undiagnosed. See `puzzle-messages.md`.
- The ordinal table's address and stride differ between two readings (`0x4fbc40` 8-byte slots vs `0x4fbd48` u16);
  values agree. Recheck before relying on the location.
- Cut scenes: single-step retiming, animation swap on its own, sound and message steps, and walking into a new
  cut scene trigger (only teleport tested).
- Host-fired exits during an entry cutscene leave the next level frozen (lock not found; next step: what the
  Valley cutscene player class 0x10033 sets on the player). See `level-exit-walk-in.md`.

### Co-op

- Real dragon mount with the call/use input (Surdana's Arokh may be story-gated; look at the dragon controller at
  `0x271ac0`). Natural dragon damage is only inferred (HP field at dragon data + 0x6b4).
- Three-player fight/death/latejoin and chest/chesthost runs failed under machine load; rerun on a quiet machine.
  Partner swings sometimes hit another player's ghost instead of an orc (known claim flake).
- Save editor items proven in game 2026-11-02 (`docs/journal/2026-11-02/save-editor-items-in-game.md`): 1-, 2- and
  3-cell items and the bow added, used/equipped, fired, kept over the game's own save and reload. Later the same day: Steel
  Club, Wood Long Bow and Bitter Touch equipped and used; Replace with... survives the game's own save and reload; mana
  power-ups are listed as "Power-ups (used at once)". (The long bow's "390" badge was a misread of 300 in a downscaled screenshot.) `play_run.py --save` no longer auto-presses (the taps opened the title's Load screen instead of letting the recipe's
  F8 load run; `harness-title-stall.md`).
- Rynn's class word `0x1f081069` (bit 27 set, game-written save) is accepted by the editor (`isRynnClass`);
  `scripts/savetool/savetool.py` still matches `0x17081069` only.

### PvP and multiplayer menus

- Map vote by mouse (keys only so far); a chained run of back to lobby, next map and rematch. Custom game modes were
  played through in game 2026-11-02 (`docs/journal/2026-11-02/custom-game-modes.md`: a pack's "Last Rynn Standing", lives,
  elimination, last one standing, pickups that never come back); not yet seen in game: the "You are out" overlay (needs
  three players: with two the elimination ends the match), team names / colours, allowed weapons, the mode loadout.
- Character turntables exist for 5 characters; the capture studio (`DRAKAN_CHAR_PREVIEWS`) is not run automatically.
- Bows cannot be spawned from their resource id; the adopt route (`DRAKAN_ARENA_LOADOUT_ADOPT`) has no map-file key.
- Pickup and quick-save checks failed on inquisitor-depths and city runs under load; recheck.
- LAN discovery sometimes misses the server under load.
- USA disc: multiplayer stays off until the address table is filled with `analysis/scripts/mp_region_find.py`
  (needs a USA disc; object layouts also differ by 4 bytes).

### Engine portability and textures

- Phase 2 of `docs/proposals/engine-portability.md`: an importer for the content format; only Surdana exported so far.
- Unreal: run `drakan_import.py` and check the axis mapping in a real UE5 install; re-export a full level after the
  skeleton fix; per-polygon double-sided flags are not exposed by the formats package yet.
- PBR: run `pbr_pack.py` over the whole disc (about 2,960 textures); masked/additive materials untested; metallic is
  0 unless overridden; the game renderer does not use normal maps (needs normals, tangents and lighting in
  `runtime/gs`).

### Tests and housekeeping

- Rust sources in the desktop app are not rustfmt-clean (never were); decide whether to enforce it.

## Releases

`.github/workflows/editor-release.yml` builds the Drakan Editor and the standalone Drakan Save Editor (Tauri, overlay
`editor/packages/desktop/src-tauri/tauri.save.conf.json`) and attaches them (NSIS installer and portable zip, fixed asset
names) to the newest `v*` Drakan PC release: `release.yml` calls it for a new tag, and a push to main touching
`editor/**` replaces the assets on the newest release. There are no more `editor-b<N>` releases (b1 to b4 are legacy).
Procedure: `BUILDING.md`, "Releases". Local save editor build: `npm run build:save -w @drakan/desktop`.

## Working practice (multi-agent sessions)

- Other sessions commit in the same tree: commit through a private index (`GIT_INDEX_FILE`), stage only your own
  files, then reset the shared index to the new HEAD.
- Build with `scripts/buildtool/Build-LocalGame.ps1 -Incremental` under `build/final-game.build.lock` (create it,
  build, delete it). `dist/DrakanPC` cannot refresh while a game runs from it; harnesses copy `build/final-game/bin`.
- Agents must not git add/commit, must not kill processes by image name, must write LF line endings and never put a
  literal newline inside a string literal (that broke the shared build several times).
- Use a private Playwright browser context and a static `vite build` for editor screenshots; the shared browser and
  the dev server get disturbed by parallel work. The level picker is the custom dropdown `#ed-level + .ui-dd`.
- Generated textures, exports, screenshots and probe output stay under `build/` or the user's folders; never commit
  game assets.
