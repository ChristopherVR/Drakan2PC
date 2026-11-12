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
The guided builder does this for you: `Build-Drakan.ps1 -HdTextures on` (installer option "Make HD textures after the build") downloads
the pinned tools once and runs the same `extract` / `modextract` / `upscale` steps into `<play folder>/textures`, including the
converted Order of the Flame pack when chosen, and prints an estimate first (`BUILDING.md`, "Optional extras"; `scripts/buildtool/extras.py`).
By hand:
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


## 10. PBR materials (UE5 conventions), 2026-11-01

Goal: every game texture as a UE5-style material set a modder or the UE5 level export can use. Tool: `analysis/scripts/pbr_pack.py`
(Python, next to `hd_pack.py`, because it consumes that tool's `build/hdpack/index.json` + `src/<hash>.png` and the upscaled
`dist/DrakanPC/textures/<hash>.png`; it needs no ISO and no game run). Output is generated per machine under `build/pbr/` (or
any `--out`) and never committed: the pictures derive from the disc.

```
python -I analysis/scripts/pbr_pack.py build --select LEVEL1ON,LEVEL1TS --sheet     # Surdana: 287 materials, contact sheets
python -I analysis/scripts/pbr_pack.py build                                          # every texture of every TXD (about 2960)
python -I analysis/scripts/pbr_pack.py sheet --out build/pbr --cols 4 --tile 128
```

Output (`--out`): `Textures/T_<Name>_BC.png`, `_N.png`, `_ORM.png`, `Materials/M_<Name>.json`, `index.json`. `<Name>` is
`<Txd>_<id>` of the preferred source (the selected TXD, then level / world TXDs, then COMMON), e.g. `Level1on_00762`; the same
picture living in several TXDs is one material whose JSON lists every source in `sources`. A different picture that would get an
existing name gets the first 4 hash digits appended. Images are upright (the TXD stores rows bottom-up; the tool flips them).

| Map | Content | UE5 import |
| --- | --- | --- |
| `_BC` | BaseColor, de-lit; alpha = opacity only for Masked / Translucent / Additive | sRGB on, TC_Default |
| `_N` | normal, **DirectX** (green points down the image); three.js needs `normalScale.y = -1` | sRGB off, TC_Normalmap, no green flip |
| `_ORM` | R = ambient occlusion, G = roughness, B = metallic | sRGB off, TC_Masks |

`Materials/M_<Name>.json` (schema 1, consumed by the editor Materials view and by the glTF / UE5 export): `name`, `textureHash`
(the renderer's content hash = HD pack file name), `sources`, `sourceSize` / `size` / `hd` / `hdFactor`, `domain` (`Surface`, or
`UserInterface` for INTERFCE / SYSTEM / DEBUG / PS2MUSIC which get a BaseColor only), `blendMode` (`Opaque` / `Masked` /
`Translucent` / `Additive`), `shadingModel` (`DefaultLit`, `Unlit` for Additive), `twoSided`, `opacityMaskClip`, `textures` (paths
relative to the pack root), `textureSettings` (sRGB / compression / normal convention per map), `uvTiling`, `emissive`
(`{source:"baseColor", intensity}` or null), `scalars` (`roughnessMean`, `metallic`), `heuristics` (every parameter that was applied
and the measured alpha fractions), `overrides` (matching override rules).

### Heuristics (what is a guess)

* **De-light**: the PS2 art has lighting baked in. The wrap-around low-pass (sigma 12 percent of the size) of the linear luminance
  is divided out with exponent 0.6 (ratio clamped 0.5..2); albedo is then limited to sRGB 30..240 (PBR range). FX glow sprites are
  left as painted. This is conservative; tile atlases with a magenta filler (LEVEL1TS) keep the filler, which the game never samples.
* **Height / normal**: the de-lit luminance blurred at 0.6 and 2.5 source texels, slope by central differences per **source** texel
  (so HD and source resolution look alike), strength 1.2, wrap-around (seamless tiles). A texture whose height std is below 1.5/255 gets
  a flat normal. This is "bump from painted detail", not recovered geometry: bricks and stones read well, flat colour areas do not.
* **AO**: cavity of the height against its own low-pass, 0.55..1. **Roughness**: 0.78 default, 0.88 when green dominates (foliage),
  0.92 FX, plus 0.25 x (0.5 - luminance), clamped 0.3..1. **Metallic**: always 0 by default; nothing in the textures says "metal".
* **Blend**: 16 / 24 bit and 5551-palette textures (the GS ignores their alpha) and textures with `amax` 0 are Opaque; only alpha 0 or
  opaque (partial < 4 percent) is Masked; partial alpha is Translucent; textures only in FX / MAGIC are Additive + Unlit + emissive;
  constant alpha below 0x7C is Translucent. **Two-sided**: Masked / Translucent / Additive and PROCTEES. The game's per-polygon
  double-sided flag is in the model (`0x0204` flags bit 1, `level-format.md`), not in the texture, so an exporter should OR it per mesh.
  On Surdana (287 textures) the classifier gives 282 Opaque, 3 Translucent, 2 two-sided Opaque: almost everything is opaque; the
  cut-out foliage is mostly in PROCTEES and the model alpha flags.

### Overrides: `configs/pbr_overrides.toml`

`[[override]]` tables with `match` (hash, material name, or fnmatch pattern over the name or a source such as
`LEVELS/LEVEL1ON/LEVEL1ON.TXD#693`) and any of `delit`, `normal_strength`, `ao`, `roughness`, `metallic`, `blend`, `two_sided`,
`emissive`, `tiling`, `opacity_mask_clip`, `skip`. Later rules win. The shipped file only documents the keys. Names are stable
because they come from TXD + texture id.

### Editor preview

Files tab, **Materials** button (`editor/packages/app/src/materialsui.ts`, pure parsing in `materialpack.ts`, tests
`test/materialpack.test.ts`): open (or drop) the pack folder; the list shows every `M_*.json`, the right side shows BaseColor /
Normal / ORM and a three.js `MeshStandardMaterial` sphere or plane (room environment, light angle slider, Normal / ORM toggles) with
the JSON's blend mode, two-sidedness, emissive and tiling applied. It works without a disc.

### Renderer (runtime/gs)

When this section was written the renderer had no use for the maps. Since 2026-11-12 the Direct3D 11 backend shades
relief from the hash-keyed maps of section 12 (`material_detail`): it derives the surface frame per pixel, so it needs
no vertex normals or tangents, and it corrects the texel instead of replacing the game's baked lighting. The UE5-named
pack of this section is still for the editor and the export.

## 11. Refit to the source (2026-11-12)

`hd_pack.py upscale` ends with a refit of every image it has a source for; `hd_pack.py refit` runs it alone (an
existing pack, no GPU). `--no-refit` leaves the network's images as they are. Tests: `scripts/tests/test_hd_pack_refit.py`.

Why: the network may add detail finer than a source texel, but the audit of the pack (journal
`2026-11-12/gfx-materials.md`) found that it also moves the picture:

* **Colour drift.** The pack image averaged back down to source size differs from the source by a median of 3 to 4
  levels in its mean colour, more than 6 levels on 573 of the 2,962 disc textures and up to 31. Two textures drawn next
  to each other (a terrain tile and the far-terrain atlas made from it) drift by different amounts.
* **Lost grain.** 231 textures came out flatter than their source (snow, sand, fine noise): the network read the grain
  as compression noise.
* **Soft alpha.** Smoke, cloud and glow textures whose picture is in the alpha plane got strokes and shards in it
  (the `realesrgan-x4plus-anime` model on alpha). The earlier fix only caught textures with constant RGB.

What it does (`refit_image`): the difference between the source and the image averaged back down, low-passed over one
source texel, is added back (the network's own finer detail stays); for a flattened texture there is no low-pass, so
the image averages to the source almost exactly; a soft alpha plane (at least 16 alpha levels and a quarter of the
visible texels in between) is replaced by the source's, enlarged bicubically. Transparent texels take no part (their
colour is bled filler), tiling axes wrap, HUD sources (INTERFCE, SYSTEM, DEBUG, PS2MUSIC) keep the network's alpha.
`refit_done.txt` in the pack lists `<hash>@<version>:<file size>`, so an image is refitted once and again only when a
later upscale made it anew. Files are replaced through `refit-tmp-<hash>.png` (not a 16 hex digit name: the game never
reads one), never written in place.

Measured on the disc pack plus the installed Order of the Flame pack (5,371 images): see the journal for the counts
and the time. It does not remove content the network invented at texel scale or above with the right mean colour
(a cracked-rock look on a noise texture stays); that needs another model or a per-texture review.

## 12. Material maps for the game and other engines (2026-11-12)

`analysis/scripts/material_maps.py build --work <hdpack work> --hd <textures>` writes, next to the HD images and keyed
like them by the content hash:

| File in `<textures>/materials/` | Content |
| --- | --- |
| `<hash>_n.png` | tangent-space normal, RGB = xyz * 0.5 + 0.5, linear. +X = image right (+u), +Y = image **down** (+v): the DirectX / UE5 convention in the row order of `<hash>.png` itself |
| `<hash>_orm.png` | R = ambient occlusion, G = roughness, B = metallic, linear (UE5 `TC_Masks`) |
| `<hash>_h.png` | height, 8 bit grey, 128 = mean surface, linear; wraps where the texture tiles |
| `materials.json` | schema 1, generator version, conventions and one record per hash: `class`, `classReason`, `sources` (`<TXD path>#<texture id>`), `sourceSize`, `size`, `fromHdImage`, `tiles`, `flatNormal`, `textures`, `scalars` (`roughnessMean`, `metallic`), `heuristics` |
| `materials.txt` | the table the game reads: `<hash> <class id> <class> <mean roughness> <metallic>` |

The maps have the source texture's size times `--scale` (default 2: half the 4x image, a quarter of its pixels) and the
row order of `<hash>.png`, so whatever draws `<hash>.png` with some UVs samples the maps with the same UVs. The key is
the renderer's content hash of the decoded guest texture: it does not depend on the disc region, the level or the pack
a texture comes from (the same picture has the same hash everywhere; a picture whose palette alpha the game edits is
reached through `alias.txt` like its HD image). `pbr_pack.py` (section 10) remains the exporter that renames, de-lights
and flips images upright for UE5; this step is the plain, hash-keyed record the game and any other engine can read.

Everything is a deterministic filter of the picture (no network, no random numbers; `--jobs` only splits the work):

* **Height**: perceptual luminance with its large-scale shading divided out, summed as three bands (0.5, 1.5 and 5
  source texels) weighted by their width, so stones and planks stand taller than grain. Bright = high. This is relief
  from painted detail, not recovered geometry: painted lettering and stains become relief too.
* **Normal**: central-difference slope per source texel times a class strength. **Occlusion**: cavities of the height,
  0.6 .. 1. **Roughness**: class base, darker and recessed parts a little rougher. **Metallic**: 1 for the metal class.
* **Classes** (`CLASSES`, ids are stable): `terrain` (the PROCTEES tile set), `character` (NPCS), then by the picture:
  `foliage` (cut-out or green dominant), `stone` (low saturation), `metal` (neutral bluish with strong contrast),
  `wood` (brown hue), else `surface`. No maps (`none`): HUD and effect sources, translucent layers, flat colours and
  the level `*TS` far-terrain atlases (blurred mosaics with filler). The class is a guess; `classReason` says which
  rule applied.

Tests with synthetic textures: `scripts/tests/test_material_maps.py` (which way the normal leans, tiling, flat input,
classes, file names and table, reproducible bytes, incremental rerun). The builder runs the step after the upscale
(`extras.py`, step `hd-materials`).

### In the game: `material_detail` (Direct3D 11)

`[video] material_detail = off | hard | all` (PC Settings > Video > "Material detail"; env `GSR_MATERIAL_DETAIL`),
`material_detail_strength` (0.25 .. 2), `material_specular` (0 .. 1, default 0). The default is `off` (opt-in until
its look in motion has been checked in real play, see the journal). `off` binds none of it: the picture is
the one without the feature. `hard` shades terrain, stone, wood, metal and generic surfaces; `all` adds characters and
foliage.

* **Loading.** The job that loads a texture's HD image also reads `<hash>_n.png` and `_orm.png` and packs them into one
  RGBA8 texture with mips (R, G = normal x, y; B = occlusion relative to the map's mean; A = roughness;
  `material::packTexels`). It counts against `texture_budget_mb` with the image it belongs to (plus one third at
  `--scale 2`). A texture without an HD or upscaled copy, or without a record, has no material.
* **Which draws.** Depth-tested perspective triangles of the open world phase whose texture colour is modulated by the
  vertex colour (`material::eligible`): not the HUD, sprites, sky layers, decals, additive glows, render-target
  textures or region-wrapped textures.
* **Shading** (`gs_material.inc`, CPU mirror `material::shade`). The GS path has no normals or world positions, so the
  pixel shader rebuilds them: the view-space position from the pixel and Q (1 / w, linear in screen space), the face
  normal from its screen derivatives, the directions of u and v on the surface from the UV derivatives. The texel is
  multiplied by `1 + strength * (n.x * T.L + n.y * B.L)`, clamped to 0.5 .. 1.6, where L is the level's sun
  (`SceneLighting`, the directional light of the layer under the eye; a fixed light from above and to the side when
  none is captured) and T, B the unit directions of u and v. The term of the flat normal is left out on purpose: the
  game's baked vertex lighting already holds it, a flat normal changes nothing, and the mean of a texture does not
  move. Faces turned from the light keep 35 % of the relief, cavities are darkened by half the occlusion, and the
  effect fades between 60 and 120 m. The result is still multiplied by the vertex colour and fogged by the GS
  pipeline, so the level's lighting, colour and fog stay as they are.
* **Specular** (`material_specular`, off by default): a Blinn highlight from the same light, scaled by
  (1 - roughness)^2. Roughness is an estimate per class, so this is opt-in.
* **Other backends.** The table, the rule, the constants and the texel are in `runtime/gs/src/material_detail.h`
  (no graphics API, test `gsr_test_material_detail`). The WebGPU backend does not load or shade materials yet; the
  list of what it needs is in the journal.
* Debug: `GSR_MATERIAL_DEBUG=1` face normal, `2` the map's normal, `3` the detail term alone, `4` the u direction;
  `GSR_MATERIAL_LOG=1` prints the table size and, at exit, maps loaded, their bytes and draws shaded.

## Converted (Order of the Flame) levels

Converted terrain textures are enlarged to 128 px (`DRAKAN_TERRAIN_TEX_MIN`, wrap-around Catmull-Rom, stored 8 bpp) because OotF ground is 64 px
(99.8 % of Ruined Village cells) where the disc's is 128 px (61 % of LEVEL1NA cells). Mod textures have their own hashes, so the HD pack must be built from the converted zips: `python -I analysis/scripts/ootf_hd_pack.py
<pack.zip>...` (or `ootf-convert.ts ... --hd`) runs `hd_pack.py modextract` (index of every SRSC member by runtime hash, zips
unchanged since the last index skipped) and one incremental `upscale` (existing hashes skipped, alias.txt merged, chunk failures retried
tiled then reported with exit 1) into `dist/DrakanPC/textures`. Full 13-level campaign plus 10 arenas: 3129 unique textures (1367 at
128 px, 1002 at 64, 423 at 32, 284 at 256), 1382 new, 18 s index + 162 s upscale (RTX 5080), +1.2 GB; a rerun takes 1 s. See
`docs/journal/2026-11-01/ootf-hd-all-levels.md` for the in-game check (hashes of the enlarged terrain textures match the runtime;
unmatched dump entries are non-texture noise and 1x1 fills).
Textures the converter takes from the disc instead of converting (Drakan 2 textures for OotF objects, `docs/reference/modding.md`)
are byte-for-byte copies, so their hash is the disc texture's: the Drakan 2 pack's replacement applies and `ootf_hd_pack.py` skips them.

**HD sources of converted textures (2026-11-12).** With `ootf-convert.ts --hd` (the builder passes it when HD textures are on) the
converter writes `<pack>.hdsrc/<sha1 of the 0x0040 record>.rgba` (`u32 w, u32 h, RGBA`) for every texture it quantised **or
resized**, and for a resized one the file holds the PC pixels before the resize (`Ps2Texture.hdSource` in `ootftex.ts`): the
64 px ground tiles it enlarges to 128, the textures it squares for skinned and display-list models (256 x 128 -> 256 x 256 and
the like) and sizes it rounds up to a power of two. The guest texture and its hash are unchanged. `hd_pack.py modextract` indexes
such a source with its own size (`sw`, `sh`, `exactv = 2`); `upscale` always writes the pack image at the guest size times
`--scale`. An opaque source at most half the guest size (the ground tiles) is enlarged to the guest size with a wrap-around
Lanczos and upscaled once (`--small-source enlarge`, the default): the network reads the soft enlargement as fine texture,
while fed the 64 px tile itself, twice (`esrgan`: x4, to the guest size, x4 again), it draws sharper rock cracks but turns
grass into a flat smear (seen in game); `lanczos` = one pass on the tile plus a resize, the softest. Any other size difference is a
wrap-around Lanczos resize of colour and alpha separately (a premultiplied RGBA resize blackens the bled hole colour), with
binary alpha sharpened again afterwards. `exact_done.txt` records `<hash>@2` for these, so images made from an older source
(the quantised picture, or the enlarged 128 px copy) are redone once. Why and what it changes: `docs/journal/2026-11-12/ootf-texture-quality.md`.
