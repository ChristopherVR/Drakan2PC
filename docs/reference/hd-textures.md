# HD texture pipeline: review on real data, gaps, fixes, workflow

Tested 2026-10-17 with a private copy of the shipped exe (`build/research/run_hd*/drakan_hd1.exe`, env `GSR_TEXDUMP`, `GSR_TEXDIR`, `GSR_TEXREPLACE`, `PS2X_GS_SHOT`); data in
`build/research/run_hd/tex1/dump`, `run_hd3/tex_a`, `tex_b`, `build/research/tex_up`. Tools: `analysis/scripts/hd_dump_stats.py`, `hd_txd_census.py`, `hd_upscale_placeholder.py`,
`hd_batch_realesrgan.ps1`. Pipeline code: `runtime/gs/src/d3d11_backend.cpp` (`setupUpscale`, `scanTexDir`), `gs_texup.cpp` (`hashContent`, `writePngRGBA`, `readImageFile`).

## 1. How it works (verified)
* A texture is hashed **after decoding** (CLUT + TEXA applied, RGBA8, alpha 0..0x80 in GS units) with a 64 bit hash of the level-0 pixels + width/height. Dump = `<texdir>/dump/<hash>.png`
  (alpha 0x80 -> 255); replacement = `<texdir>/<hash>.png|.dds` (any 2/4/8x multiple, else resampled to the nearest power-of-two factor); mips are regenerated from the replacement; render targets
  (framebuffer-as-texture) are never hashed; `exclude.txt` lists hashes to skip.
* Works end to end: dump (234 textures from boot -> title menu -> intro), x4 placeholder upscale, restart with `GSR_TEXDIR=<dir>`: the title menu frame, background tiles and text render from the
  replacements (`build/screenshots/hd_replace_menu_zoom_base_vs_replaced.png`: left original, right replaced; the box frame is visibly smoother).

## 2. Gaps found (with evidence) and fixes

| # | Finding | Evidence | Proposed fix |
|---|---|---|---|
| 1 | **CLUT-animated textures explode the dump and never match a replacement.** The highlighted title-menu item (a 128x128 8-bit texture whose palette is animated every frame) is a **new hash every frame**. | 25 s on the title menu: **164 of 174 dumped textures are variants of this one texture** (identical alpha silhouette). Two identical menu runs share only **10 of ~175 hashes**. About half of the first 234-texture dump was this one texture. The same mechanism will hit water / fire / glow palette effects in levels (not counted yet). | In `setupUpscale` also hash the *index plane* for indexed formats (T8/T4): `idxHash`. More than 4 distinct `contentHash` for one `idxHash` => volatile family: dump at most 2 variants, list it in `volatile.txt`, never look up a replacement (live xBRZ/smooth still applies, keyed by content hash). Long term: upscale the index plane and sample the palette at draw time (needs a paletted upscaled texture path). Sketch: `patches/proposed/hd_volatile_hash.md`. The visible garbled highlight itself is being fixed by another agent. Palette-only steps: HD index maps, section 9. |
| 2 | **Constant-alpha-0 textures**: CT24/CT16 textures are decoded with TEXA alpha (0 when TEXA unset and TCC = 1), so the dump PNG holds real RGB with **alpha 0** (title background tiles, 256x256). An upscaler that premultiplies or drops RGB under alpha 0 produces **black**. | My first placeholder upscale premultiplied: the menu background rendered completely black (`run_hd3/shots/menu_replaced.png`); carrying a constant alpha over and scaling RGB alone fixed it (`menu_replaced2.png`). 9 of 234 dumps are alpha-all-0, 161 opaque, 64 partial alpha. | Documented; `hd_upscale_placeholder.py` and `hd_batch_realesrgan.ps1` handle it. Optionally the dumper could also write a `<hash>.a0` marker so tools know. |
| 3 | Dump PNGs are **uncompressed** (stored deflate blocks): 234 files = 18 MB; the whole game (~3000 textures, 56 Mpx) will be roughly 230-450 MB. | `writePngRGBA` | Use real deflate (miniz/stb) or run `oxipng` afterwards. |
| 4 | Alpha above 0x80 (palettes with up to 0xBE exist) is clamped to 255 in the dump. | `a >= 0x80 -> 255` | Rare (fx); accept. |
| 5 | CLUT / alpha variants of one picture get different hashes ("same RGB, different alpha": 1 group of 4 files in the first dump). | `hd_dump_stats.py` | acceptable. |
| 6 | Mips: replacements get generated mips, the dump holds level 0 only (correct). | code | fine |
| 7 | Hash is **not stable for textures our own override layer repaints**: the keyboard-prompt generator (`runtime/ui`, `INTERFCE.TXD` 97/124/94) rewrites them at load time, so their hashes depend on `drakan_input.ini`. | docs/reference/ui-prompts.md | Exclude those three in `exclude.txt` after the first dump. |

Not observed: hash collisions, partial-upload garbage (gsr decodes at first draw and invalidates by block stamps), mip-level confusion.

## 3. Placeholder test numbers
`hd_upscale_placeholder.py` (Lanczos x4 + unsharp) of 234 textures: 26 MB of PNG, all loaded (no `could not be loaded` message), N = 4 detected from the size.
Volatile (palette animated) textures stay at the original/xBRZ resolution: visible as the soft, garbled highlighted menu word next to crisp neighbours.

## 4. How many textures does the game have?
`hd_txd_census.py` over the 35 English TXD files: **3282 TXD textures, 2947 unique decoded images** (+ 19 with a compressed layout the offline decoder rejects, the level-0 tile set; the game
uploads them decoded, so gsr dumps them anyway). Unique images by size: 64x64 1033, 128x128 929, 256x256 553, 32x32 414, 16x16 14, a few 32x64. 2902 are 8-bit paletted, 245 32-bit, 74 16-bit, 42 24-bit.
Total **56.1 Mpx** of source = 0.9 Gpx / 3.6 GB raw RGBA at x4 (PNG on disk maybe 1.5-2.5 GB; gsr's 8192 px cap and `texture_budget_mb` LRU keep VRAM bounded).
Expect ~3000-3500 dump files after a full playthrough (more if the animated-CLUT variants are not capped, fix 1).
Biggest files: RESOURES 429, WORLDCON 363, NPCS 341, LEVEL4TS 194, LEVEL4ON 181, FX 169, LEVEL2RE 164, LEVEL1TS 160, PROCTEES 158, LEVEL1ON 134, INTERFCE 97.
The five language trees differ only in `LEVEL*TS.TXD` and strings; use the English tree for the pack.

A complete alternative to playing through every level with dump on: build the pack **from the TXDs** (decode with `txd_decode.py`, apply the same alpha mapping, hash with `hashContent`). The texture
audit showed gsr's decode equals the TXD image bit-exactly (25170 exact + 9114 with edited alpha of 34284), so the hashes can be predicted offline: deterministic, all levels at once, no play-through.

## 5. Practical workflow for the user (RTX 5080)
1. Dump: `texture_dump = 1`, play the levels you care about (or build from TXDs, section 4). Dumps go to `textures/dump`.
2. Optional: delete variants listed in `volatile.txt`; `oxipng -o2 --strip all textures\dump\*.png`.
3. Upscale with Real-ESRGAN ncnn Vulkan (not installed here; `realesrgan-x4plus` for terrain/walls, `realesrgan-x4plus-anime` or `realesr-animevideov3` for hand-painted UI/fonts):
   `powershell -File analysis\scripts\hd_batch_realesrgan.ps1 -Dump textures\dump -Out textures -Exe C:\tools\realesrgan\realesrgan-ncnn-vulkan.exe`
   (resumable, keeps file names, handles constant alpha). Rough estimate for 56 Mpx input at x4: 10-30 minutes on a 5080. Keep the 16-hex names. For tiny 32x32 icons x2 is plenty.
4. Seams: tileable textures (terrain, walls) wrap in gsr (REPEAT in logical texels); ESRGAN does not know about wrapping, so wrap-pad the input by 16 px on each side before upscaling and crop after if seams show.
5. Run with `hd_textures = 1`, exclude bad ones via `textures/exclude.txt`; rescan is live from the F1 overlay.

## 6. Observations on the user's screenshots
(`Screenshot 2026-10-17 064807.png` title menu, `065020.png` Level 0, `065114.png` inventory help)
* Title menu: the background art is low resolution (3 tiles of 256x256 CT24 = 768x512 source) and the menu words are 8-bit font sprites magnified ~2.5x and bilinear filtered, so everything is soft; the highlighted
  word is the CLUT-animated glow (gap 1). These are source-resolution limits, not port artefacts; HD textures are the fix.
* Level 0 (065020): the ground/grass is very blurry and streaky (64x64 / 128x128 tiles magnified 8-10x, anisotropic filtering off). There are also **dark rectangular shadow decals with a vertical stripe pattern**
  at the NPC's feet and at lower right: a soft blob would be expected, this looks like a real rendering bug (suspects: alpha test/blend of the shadow sprite, a shadow texture with TEXA/AEM, or mip selection on a tiny texture).
  A `GSR_DRAWLOG=<vsync>-<vsync>` capture of such a frame is the next step; it should be passed to the gsr owner. Tree edges are aliased (no MSAA).
* Inventory help (065114): panel and prompts are fine; the small garbled "OK" glyph under the text is the CLUT-animated highlight (known, other agent).
* The title and Level 0 screenshots also show that the captured window is only partly repainted by the D3D child (capture artefact, unrelated to the game).

## 7. Implemented fixes (2026-10-17)
* **Volatile textures** (gap 1): gsr tracks every texture-memory slot (tbp, tbw, psm, size). A slot re-decoded with *changing content* 3 times within 90 frames (content hash compared only for slots that were re-decoded,
  and only until 6 identical re-decodes show a plain re-upload) is "volatile": not dumped, never looked up for replacement, no upscale job. Measured: the 128x128 T8 fire highlight, the CT24 256x256 tiles
  re-uploaded per frame in the menus (not the FMV: movies are stamped into the frame buffer, `fmv.md` section 9) and palette faded menu backgrounds are flagged; the title background tiles, which the game merely re-uploads unchanged every frame, are not. Flagged slots are listed in `<dump>/volatile.txt`
  and recover after 600 frames without a re-decode. Index-plane hashing (the earlier sketch) was not used: the EE regenerates the indices every frame, not only the palette.
* **PNG dumps** are real deflate now (dynamic Huffman + LZ77 + per-row filters, `gs_texup.cpp`, no zlib dependency): 3-6x smaller (a 256x256 tile 98 KB -> typically 20-60 KB). Output opens in PIL.
* **Alpha**: PNG alpha = GS alpha 0..0x80 mapped to 0..255 (0x80 -> 255, above 0x80 clamps to 255); replacement files are read with the inverse mapping (255 -> 0x80). RGB under alpha 0 is written as decoded
  (CT24 / CT16 textures with TEXA alpha 0 keep their picture; unit test). Dumps whose alpha is constant 0 get an empty `<hash>.a0` marker so external tools never premultiply them.
* **Runtime generated textures**: the keyboard prompt textures (INTERFCE.TXD 97 / 124 / 94) are excluded from dump and replacement. `drakan_ui::PromptOverride` reports the decoded result, the runtime
  calls `gsr::excludeTextureHash(gsr::textureContentHash(...))`. Verified: with `DRAKAN_NO_PROMPT_EXCLUDE=1` the two hashes it printed (`95592a458ffe0558`, `4586dd7302104c23`) appear in the dump, without it they do not.
* End to end (once): title -> Options -> Controller with `GSR_TEXDUMP=1` (29 PNGs, all valid) -> `hd_upscale_placeholder.py x2` -> restart with the pack: `[texup] ... replace` for the replaced hashes.

## 8. AI HD pack (Real-ESRGAN), defaults, regeneration (2026-10-17)
The pack is generated **from the disc**, offline, with `analysis/scripts/hd_pack.py` (run with `python -I`, needs numpy + Pillow) and the prebuilt `realesrgan-ncnn-vulkan` (v0.2.5.0 Windows release, models
`realesrgan-x4plus` and `realesrgan-x4plus-anime`) that lives in `tools/hdpack/realesrgan/` (downloaded from github.com/xinntao/Real-ESRGAN releases; not part of the pack). The generated pack stays on this machine.

### Regenerate
```
python -I analysis\scripts\hd_pack.py extract --iso <disc.iso>  # ISO (or set DRAKAN_ISO) -> build\hdpack\src\<hash>.png + index.json (2962 unique textures, ~15 s)
python -I analysis\scripts\hd_pack.py upscale            # Real-ESRGAN on the GPU -> dist\DrakanPC\textures\<hash>.png + alias.txt (resumable; 2962 textures in 616 s on an RTX 5080)
python -I analysis\scripts\hd_pack.py compare --dump <texdir>\dump   # hash match rate against a texture_dump run of the game
python -I analysis\scripts\hd_pack.py hybrid  --dump <texdir>\dump   # then `upscale` again: adds dump textures that have no TXD source
```
Options: `--iso`, `--work` (default build\hdpack), `--out`, `--exe`, `--scale` (4 = ESRGAN native; 2 or 8 resamples), `--chunk`, `--gpu`.

### What it does
* **Decode = gsr decode.** `decode_runtime()` reproduces the renderer's pixels: T8 + CT32 palette (alpha as stored, 0..0x80), CT16/CT24 with TEXA = 0 (alpha 0, `r = v << 3`), palette kind 2, and the **LEVEL0TS.TXD tile set
  (+ OUT_SPTS), which stores the index plane as `u32 compressed length + zlib stream`** (the old census called these 19 "undecodable"). The hash is a Python port of `gsr::texup::hashContent`.
* **Hash match** (73 textures dumped from boot -> title -> intro -> New Game -> Level 0, `GSR_TEXDUMP=1`): 46 exact + 6 identical RGB with a different (game edited) alpha; the other 21 are palette/pixel-animated
  variants (the fire highlight family, water; not replaceable by definition, gsr flags them `volatile` and upscales them live with xBRZ). **100 % of the stable textures match.** Findings: the game edits palette alpha at load
  (0x7F -> 0x80; black entries of some tile palettes -> alpha 0; constant-alpha-0 textures -> 0x80), the edit is per texture so it is not modelled offline.
* **`alias.txt`** (`<rgbhash> <hash>`): the runtime first looks up the exact hash; on a miss it hashes the texture with alpha forced to 0 and uses the alias (or a pack file named by the RGB hash). The pack's RGB is used with the
  **game's own alpha** upscaled (bilinear; binary alpha is pushed to a ~2 px ramp so alpha-tested foliage keeps crisp edges): palette alpha edits can never break a match or fatten a cut-out.
* **Upscale**: `realesrgan-x4plus` for terrain / walls / models / skins; `realesrgan-x4plus-anime` for INTERFCE, SYSTEM, DEBUG, FX, MAGIC, PS2MUSIC (HUD, fonts, flat art, glows). Per axis the script tests whether the texture tiles
  (opposite edge as similar as neighbouring rows): tiling axes are **wrap padded by 16 px**, others mirror padded, then the pad is cropped after the 4x so there are no seams (an axis counts as tiling only if RGB *and* a non-constant alpha tile; alpha-only sky layers are white RGB and do not, `docs/journal/2026-10-23/sky-hd-seams.md`). Constant-RGB textures with soft alpha (sky panoramas, clouds, light rays) get a Lanczos 4x alpha instead of the anime model, which invents strokes and hard seams in soft alpha. Partially transparent textures: transparent
  texels are colour-bled from their neighbours before upscaling (no dark fringes), the alpha plane is upscaled separately (anime model, grayscale; binary alpha sharpened k = 6), constant alpha (opaque or the CT24 alpha-0 case) is kept
  as is with the RGB intact. Output is 8-bit RGBA PNG (zlib level 6), 4x: 64 -> 256, 128 -> 512, 256 -> 1024.
* Numbers: 3282 TXD textures -> 2962 unique -> **2962 files, 1.2 GB** (PNG), 616 s upscale. Before/after: `build/screenshots/hd_*_before_after.png` (title menu, HUD orb, Level 0 ground / trees / rocks, Rynn; 4K window, internal scale 5).

### Runtime (gsr)
* **Defaults**: `texture_upscale = auto` (internal scale 1: off, 2-3: 4x, >= 4: 8x; HUD / font / UI sprites always the largest factor via xBRZ; every factor capped so one texture with mips uses <= 1/24 of the budget and 4096 px),
  `anisotropic = 16`, `mip_bias = auto`, `texture_budget_mb = 0` in `gsr::Config` = auto (40 % of dedicated VRAM, 1.5 - 8 GB; the shipped drakan.ini sets 6144 explicitly), `hd_textures = 1`.
* Pack textures are decoded on the worker pool (PNG inflate + mip chain from the HD base, alpha weighted box filter), the native texture is drawn until the job lands, results are cached by content hash and evicted by LRU / age: no hitches.
  A 4x pack texture decodes in ~26 ms on a worker (256 -> 1024 px), 55 pack textures + 25 live-upscaled ones were requested in the first 40 s of Level 0.
* **GUI pages first** (2026-10-24): every image of the GUI page on screen (`sceneCamera.guiScreen >= 0` and no world phase open: inventory,
  menus, help boxes, not the world behind them), pack image or live upscale, is built on the render thread before the draw, so the page is HD
  from its first frame; the first open of a page stalls once (~10 textures x 10-26 ms), later opens hit the cache. 1:1 UI sprites outside a page
  use the pool's urgent lane, which starts before queued background jobs. Before, the first inventory open
  queued its pack images behind every world upscale and showed native art for a while (`guiPagePackFirstFrameTest` in `gsr_test_synthetic`).
  Runtime atlases and overlays (`registerRuntimeTexture` / `registerRuntimeOverlay`) were already built inline.
* **Measured** (Level 0 start, 3840x2160 window = scale 5, RTX 5080): texture cache peak **131 MB** (88 MB resident), whole-process VRAM delta about 440 MB including the 4K render targets; 1440p (scale 3): peak 115 MB. The hash lookups add
  nothing measurable (a miss costs one extra 64 bit hash). `GSR_TEXUP_LOG=1` prints per-job times and an exit summary (`[texup] exit: replaced=.. upscaled=.. cache .. peak ..`).
* BC7/DDS: the runtime reads BC1/2/3 and uncompressed DDS, not BC7; with a peak of 131 MB a compressed pack was not necessary, PNG stays the pack format.
* Not covered: textures that only exist in other levels' render paths are replaced as soon as they are decoded (all 2962 TXD images are in the pack); textures generated at run time (keyboard prompt textures, FMV frames, animated
  pixels) are excluded / volatile and are drawn at native resolution; palette-only animations and palette-swapped variants get HD index maps (section 9).

## 8a. Generated overlays, streamed UI atlases, uninitialised textures (2026-10-24)
* `gsr::registerRuntimeOverlay(hash, original, w, h, factor, rgba, coverage)`: an HD image for a generated texture
  whose other pictures are disc art. Where coverage is 0, the job takes the pack image of `original` (exact hash,
  then RGB alias) when replacement is on, else the xBRZ upscale; elsewhere it blends `rgba`. Used for the inventory
  icon atlas `INTERFCE.TXD` 94 (`ui-prompts.md` 1.3).
* Aliased textures: the game streams some UI atlases every frame through memory a scratch render target occupies
  (tbp 0x3e7f), so gsr assembles them from the target and skipped `setupUpscale`. When the decoded memory has an
  exact authored hash (a runtime texture or an exact pack file), it now runs, and the HD image is used.
* `texup::looksUninitialized`: textures whose rows are mostly random memory stay native in the live upscale path.
  The intro's 64x64 CT24 sky ramp is point sampled in two written bands, and the 8x smooth filter blended the
  garbage rows into them as rainbow stripes (`docs/journal/2026-10-24/inventory-sky-textures.md`).

## 9. Palette-animated and palette-swapped textures: HD index maps (2026-10-22)
Gap 1 left every volatile texture at native resolution (`setupUpscale` returns early for volatile slots unless the exact
step is in the pack), because each palette step has a new content hash. Indexed volatile slots now take a second look
(`d3d11_textures.cpp`, `setupPaletteHd`; pure CPU core `gs_palette_hd.h`, test `gsr_test_palette_hd`):

* **Detection.** For an indexed (T8 / T4) volatile slot the index plane is decoded through an identity CLUT and hashed.
  Same index hash as the previous decode of the slot = only the palette changed (`SlotInfo::lastIndexHash`).
  Indices that change too (the title fire highlight) stay on the old path.
* **HD index map, once per index plane** (worker pool): a guide image of one palette step at 4x (the HD pack file of
  that step, exact or via `alias.txt`, else the live upscaler), then every HD pixel takes the palette index of one of the
  3x3 source texels around it, the one whose colour is closest to the guide pixel (plus a small distance term, so
  equal colours keep the nearest texel). Edges follow the HD image and values stay real indices.
* **Each palette step**: the palette of the current decode is applied to the map (table lookup, render thread, capped
  at 4x = 512 px for 128 px sources), mips by 2x box filter, cached in the upscale cache under the step's content hash
  (kind 3), so a repeating cycle costs one lookup pass per step. A step with its own exact pack file still uses that
  file directly (better than a recolour). Native entries decoded while the map was building are decoded again once
  it is ready (`TexEntry::paletteWait`), so a cycle never mixes native and HD steps for long.
* **Switches**: `GSR_PALETTE_HD=0` turns it off; `GSR_PALETTE_HD_DUMP=<dir>` writes the first 32 steps as
  `<hash>_native.png` / `_hd.png`; `GSR_TEXUP_LOG=1` logs every map and adds `volatile slots / palette index maps /
  palette steps` to the exit line.
* **What it hits in practice** (New Game runs with `scripts/harness/play_run.py`, about 30 s each): Surdana 19 volatile
  slots, 12 index maps; Kragmor 23 / 13; Shadowmire 13 / 8. Almost all are **palette-swapped variants** sharing one
  index plane: Rynn's and NPC skins, wings, terrain splat tiles. No continuously cycling palette (water / lava) was on
  screen at those level starts, so a real cycling animation is still unobserved. Dumped steps
  (`GSR_PALETTE_HD_DUMP`, Kragmor) match the native colours exactly at HD-pack sharpness, with no wrong-index specks.

