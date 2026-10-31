# Drakan disc inspector / editor workspace

TypeScript + Vite + three.js, npm workspaces. Design: `docs/proposals/level-editor-and-multiplayer.md`
("Editor architecture"). Background: `docs/journal/2026-10-31/`.

| Package            | What                                                                                                                                                                                                                                                                          |
| ------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `packages/formats` | Pure TypeScript, no DOM. `Iso9660`, `Srsc` (reader and byte-identical writer), `Txd` (to RGBA8), `LevelList` (`LEVELLST.TXT`, `.DB`, 8.3 name mangling). Works on a random-access `ByteSource`, so a browser `File` or `Blob.slice` can back a 4 GB image without loading it. |
| `packages/app`     | Vite + three.js inspector: open an ISO, list levels, browse each file's SRSC entries (type, id, sub, size, zlib), hex view, decoded textures as quads in an orbit-controlled viewport. Parsing runs in a Web Worker.                                                          |

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
(pure logic, tested). Notes and limits: `docs/journal/2026-10-31/editor-ux-overhaul.md`. Hooks for the renderer:
`LevelView.setGround({ meshes, height })`, `levelUi.assets().setThumbnailProvider(fn)`.
