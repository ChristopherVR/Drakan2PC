# Drakan disc inspector / editor workspace

TypeScript + Vite + three.js, npm workspaces. Design: `docs/proposals/level-editor-and-multiplayer.md`
("Editor architecture"). Background: `docs/journal/2026-10-31/`.

| Package            | What                                                                                                                                                                                                                                                                          |
| ------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `packages/formats` | Pure TypeScript, no DOM. `Iso9660`, `Srsc` (reader and byte-identical writer), `Txd` (to RGBA8), `LevelList` (`LEVELLST.TXT`, `.DB`, 8.3 name mangling). Works on a random-access `ByteSource`, so a browser `File` or `Blob.slice` can back a 4 GB image without loading it. |
| `packages/app`     | Vite + three.js editor and disc browser: open an ISO, edit levels, maps, story and saves; the Files tab lists each file's SRSC entries, hex view, decoded textures as quads in an orbit-controlled viewport. Parsing runs in a Web Worker.                                    |

## Run

Node 20 or newer.

```sh
cd editor
npm ci
npm test          # vitest, synthetic fixtures only
npm run build     # typecheck all packages, then vite build of the app
npm run dev       # dev server; open the printed URL and pick or drop your own ISO
npm run format    # prettier (LF, 4 spaces, 120 columns)
```

Texture viewport: right mouse rotates, left mouse pans, wheel zooms, click a quad to select its entry.

## No-assets rule

The player supplies their own disc image. Nothing from the game is committed, bundled or uploaded: the ISO is read
locally in the browser. Tests build their fixtures in code (`packages/formats/test/fixtures.ts`); never add real game
bytes, extracted files or screenshots of game content. `scripts/repository_guard.py` enforces this in CI.

## Format notes

- SRSC layout follows `analysis/scripts/iop_srsc.py`. The writer keeps the original header bytes, data area and any
  trailing bytes verbatim, so an untouched container re-serialises identically (the gate before any editor writes).
- Texture rows are stored bottom-up (`runtime/ui` `Txd.h`); `analysis/scripts/txd_decode.py` does not flip them.
  `decodeTxd` returns top-down by default.
- Pixel planes may be stored as `u32 compressedLength + zlib stream` (the level 0 tile set, and the 16/32 bpp textures in
  `DRAKAN2.RRC`), not only for 8 bpp. `txd_decode.py` rejects those as a size mismatch.

## Save mode

The Save tab edits native `.rsg` saves (hp, gold, position, skills), lists memcard slots with their screenshots, and
compares two saves. Edits stay pending until SAVE (File System Access API, keeps a `.bak`) or EXPORT (download). Core
logic: `packages/formats/src/save.ts`, `packages/editor-core/src/save-doc.ts`. See
`docs/journal/2026-10-31/editor-save-mode.md`.

## Level editor

The Level tab is a docked editor: toolbar, Outliner (left), viewport, Inspector (right), bottom dock (Asset Browser, History,
Validation, Mod) and a status bar. Open an ISO, pick a level in the toolbar, edit, then Export mod (zip or straight into
`mods/`; the Mod panel shows the in-game steps). Press F1 for every shortcut: Q/W/E/R tools, X local/world, G grid snap,
F frame, Del, Ctrl+D/C/V/Z/Y, H/L hide/lock, Shift+1..9 camera bookmarks. Drag a class from the Asset Browser into the
viewport to place it; right mouse + WASD flies; alt+left orbits. Edits autosave to IndexedDB. Code: `packages/app/src/editor/*`
(panels), `level.ts` (viewport), `levelui.ts` (wiring), `packages/editor-core/src/{snap,arrange,clipboard,catalog,schema,validate}.ts`
(pure logic, tested). Notes and limits: `docs/journal/2026-10-31/editor-ux-overhaul.md`. Opening a disc level now loads terrain and textured models (`sceneload.ts`, `scene/layer.ts`); the Display menu holds
terrain/model/helper-class toggles, lighting, fog and draw distance. Notes: `docs/journal/2026-10-31/editor-render-integration.md`.

## Projects, tasks, help and the Files tab

The start page of the Level tab offers tasks (edit a campaign level, create a multiplayer map, build a new level from an
existing one, change the story, edit a save, import an Order of the Flame level). A task creates a **mod project** (the
Project dock tab: name, id, author, version, levels in campaign order with their exits, copies of levels, the level Play
starts in, one export) and shows a checklist in the corner that ticks off as the editor sees each step done. The Help
button in the top bar opens short guides; the "?" on each panel opens the one for that panel. The guide text is in
`packages/editor-core/src/guides.ts`, and `docs/reference/editor-guide.md` is generated from it (a test keeps them equal).
The old Inspect tab is now **Files**, an advanced disc file browser with a header that says so, a file tree, an entry list
and a preview (picture, text, hex) with buttons that open the file in the Level or Story editors. Code:
`packages/app/src/editor/project/*`, `startpage.ts`, `packages/editor-core/src/{project,guides}.ts`. Notes:
`docs/journal/2026-10-31/editor-workflows.md`.

## Multiplayer Map mode

The Multiplayer Map tab of the bottom dock turns a campaign level into a PvP arena: map details, spawn points, pickups, flag /
base / zone markers, bounds, a preview of what the arena removes from the campaign, custom game modes and export as a mod
pack (`maps/<id>.ini`, `previews/<name>.png`). Format and what the game runs today: `docs/reference/netcode.md`
("Authoring in the level editor", "Custom game modes"). Code: `packages/app/src/editor/arena/` (store, viewport overlay,
panel, preview capture) and `packages/editor-core/src/{arena,gamemodes,arenapack}.ts` (tested).

## Desktop app (Tauri)

`packages/desktop` wraps the same Vite app in a Tauri v2 window; the browser build is unchanged and stays the default
(`npm run dev` / `npm run build`). The desktop shell adds what a browser cannot do safely:

- **Open ISO** uses a native dialog and remembers the disc (File > Open Recent; the last disc reopens at start-up). The
  worker reads the image with range requests through Tauri's asset protocol, scoped to that one file, so a multi-GB ISO
  is never loaded whole.
- **Saves**: Open Save / Open Memory Card Folder list the `.rsg` files; Save writes in place and keeps `NAME.bak`
  (`.bak.1`, ...) beside it. Export (mod zip, `.level.json`, edited `.rsg`) opens a native "save as" dialog.
- **Mods and Play**: Write to mods folder and Play use the remembered game folder (the play folder with `drakan.exe`,
  e.g. `dist/DrakanPC`; chosen on first use or via File > Game (Play) Folder). Play (toolbar, F5 or the Mod panel) writes
  the pack into `mods/<id>/`, enables it in `mods.ini`, then starts `drakan.exe` with `DRAKAN_START_LEVEL` set to the open
  level (New Game loads it), optional auto-skip of title/intro, and optional multiplayer test variables
  (`DRAKAN_NET_HOST` / `_MODE` / `_CONNECT`, `DRAKAN_GHOST`; see `docs/reference/netcode.md`). Only a fixed list of
  `DRAKAN_*` variables can be passed (`src-tauri/src/paths.rs`).
- Native File / Edit / View / Help menus and window size/position persistence.

The webview gets no blanket fs, shell or http access: `src-tauri/build.rs` lists the 15 app commands and
`capabilities/default.json` grants each by name. Saves are only readable or writable after the user picked them in the
session; mod packs are written only under the game's `mods/` with validated ids and relative paths.
Platform code is `packages/app/src/platform.ts` (the one place that tells browser from desktop), `desktop.ts` (menu and
start-up glue), `launch.ts` (Play environment, tested) and `editor/playopts.ts` (Play controls).

```sh
cd editor
npm ci
npm run desktop:dev     # Vite dev server + desktop window (hot reload)
npm run desktop:build   # release build and NSIS installer under packages/desktop/src-tauri/target/release/bundle
cd packages/desktop && npx tauri build --debug --no-bundle   # quick debug exe
cd packages/desktop/src-tauri && cargo test                  # Rust unit tests
```

Needs Rust (rustup user install, MSVC build tools from Visual Studio) and WebView2 (part of Windows 11). Settings live in
`%APPDATA%/dev.drakan.editor/settings.json`. Icons are generated from `app-icon.svg` at build time (`scripts/prepare.mjs`;
no binaries are tracked). Dropped files in the desktop window arrive as browser `File`s (no remembered path).
