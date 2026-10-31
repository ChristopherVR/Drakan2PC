# editor/ browser test (2026-10-31)

First run of `editor/` in a browser and against a full disc image. Spec: `docs/proposals/level-editor-and-multiplayer.md`.
Scaffold notes: `docs/journal/2026-10-31/editor-scaffold.md`.

## Blocker: the real ISO could not be uploaded in the browser

The Playwright file chooser only accepts paths under `decomp/` (`.playwright-mcp` and the repo root). The ISO at
`C:\Games\Drakan - The Ancients Gates\Drakan - The Ancients' Gates.iso` was refused. I did not copy the ISO into the
repo or route around the restriction. Consequences:

- The browser UI was exercised with a synthetic ISO built by `packages/formats/test/fixtures.ts` (about 16.9 MB: 14
  level names in `LEVELLST.TXT`, one level directory with a small texture container, and a 16 MB `DRAKAN2.RRC`-style
  SRSC with 2048 entries). It was served from the dev server for the test and removed afterwards.
- The real disc was exercised through the same `formats` code the worker uses, in Node, with `fs.openAsBlob` as the
  backing `Blob` (`blobSource`). Browser timings for the real disc are therefore not measured.

To finish the browser check on the real image, load it from a path under `decomp/` (or relax the allowed roots) and
repeat the steps below.

## Real disc (Node, formats package, Blob-backed)

| Check | Result |
| --- | --- |
| ISO open (PVD, volume id `DRAKAN2`) | 2-6 ms |
| Languages | ENGLISH, FRENCH, GERMAN, ITALIAN, SPANISH |
| `ENGLISH\LEVELLST.TXT` | 14 levels, every level directory resolves |
| Full walk | 926 files, 3,449,229,591 bytes, 30-41 ms |
| SRSC containers | 685 files open; 0 texture decode errors; 0 size mismatches |
| `DRAKAN2.RRC` (ENGLISH) | 297 entries, 4 textures, all decode (16/32 bpp zlib planes included) |
| Memory | RSS about 78 MB at open, peak 174 MB across the full pass; heap 12 MB at end. The 3.45 GB image is never read whole |

Before the fix below, indexing `NPCS.ADB` (4,389 entries) took 4.5 s in Node, almost all of it in per-entry zlib
probes.

## Browser (synthetic ISO, Playwright browser, 1400x900)

- Open via the file input: about 21 ms to parse and list. 14 levels shown. Language select disabled (one language).
- Select a level, then a file: file list and entry table correct (type, id, sub, offset, size).
- Hex view: 4 KB paging on a 5,200-byte entry (Next/Prev, Next disabled at the end). Also paging on an 8 KB entry.
- Texture viewport: 4 textures decode and display (8 bpp palette, 16 and 32 bpp zlib planes, 24 bpp), screenshot checked.
- Orbit: right-drag rotates, wheel zooms, left-drag pans; the camera moves. Drag no longer selects a quad (see fix 2).
- `DRAKAN2.RRC` (16 MB, 2048 entries): listed and opened. Open time 12,785 ms before fix 3, 52 ms after.
- Console: no errors or warnings since the last navigation after the favicon fix (before it: one 404 for `favicon.ico`).

## Bugs fixed

1. **Drop overlay covered the whole page.** `#drop` set `display: grid`, which overrides the `hidden` attribute, so the
   overlay was always up (computed `display: grid` with `hidden` set). Added `#drop[hidden] { display: none; }` in
   `packages/app/src/style.css`.
2. **Pan/orbit picked a quad.** A left-button pan ends with a `click` on the canvas, which selected the quad under the
   pointer. The viewport now records `pointerdown` and ignores a click that moved more than 4 px
   (`packages/app/src/viewport.ts`).
3. **Slow SRSC listing.** `SrscReader.isCompressed` was called once per entry (one Blob read each). Added
   `SrscReader.zlibFlags()` (`packages/formats/src/srsc.ts`), which scans entries in offset order through 1 MB windows and
   returns the same answers. The worker's `openFile` uses it. NPCS.ADB index with flags: 4.5 s to 51 ms (Node); DRAKAN2.RRC
   open in the browser: 12.8 s to 52 ms.
   Test added: `packages/formats/test/formats.test.ts`, "batches zlib detection for all entries with the same answers as
   per-entry checks" (checks equality with `isCompressed`, read count with the default window, and equality with 16-byte
   windows).
4. **Favicon 404.** Added `<link rel="icon" href="data:," />` to `packages/app/index.html`.

## Checks

- `npm test`: 22 passed (21 before, plus the new one).
- `npm run build` (typecheck of all packages, then vite build): passes. Output contains no test fixtures.
- `npm run format:check`: passes.

## Remaining issues (not fixed)

- Clicking a texture quad or a texture row narrows the viewport to that one texture (`selectEntry` calls
  `showTexturesFor([index])`). Probably intended, but it hides the rest of the set until "Show textures" is pressed.
- Opening a level directory that has no files (or no `LEVELLST` entry for that dir) reports "not a directory" in the
  status line. The message is accurate but the level list could mark missing directories.
- Browser timing for the real 3.45 GB image is still unmeasured (see blocker above).
- Only the default Playwright browser was used; no other engines were tried.
- `npm audit` reports 5 advisories (2 critical, 1 high) from the install. Not reviewed in this pass.

## Follow-up (same day)

- Selecting a texture row or quad now keeps the file's whole texture set on screen, outlines the selected texture and
  frames it (`TextureViewport.focus`); the set is laid out first when it is not already shown.
- Levels whose `LEVELLST.TXT` directory is not on the disc are shown dimmed/italic with a tooltip, and clicking one
  explains that instead of reporting "not a directory" (`LevelInfo.present`).
- `npm audit`: vite 8.3.1 → 8.3.5 and fflate 0.8.2 → 0.8.3 fix the high/moderate advisories. tinypool (critical) and
  `@vitest/mocker` (moderate) are dev-only test-runner dependencies of vitest 3.x; their fix needs vitest ≥ 4.1.12,
  which npm 11.3.0 and npm 10.9.2 both fail to install ("Cannot read properties of null (reading 'edgesOut')"), also
  from a clean `node_modules`. vitest stays on 3.2.7 until a newer npm resolves it. Nothing from vitest ships in the
  built app.
