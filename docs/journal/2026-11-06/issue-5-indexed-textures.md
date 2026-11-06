# Issue 5: smaller converted textures (8 bpp where it does not show)

Follow-up of [issue-5-village-scene-timing.md](issue-5-village-scene-timing.md): Ruined Village's scene 1.2a waited on a
full native texture cache, and commit ba921e92 raised the campaign budget to 8 MiB. This pass shrinks the converted
working set instead (the raise stays as a margin).

## What was big

Census of the full pack before the change (`build/harness/fixes3/packA`, probe `txdcensus.mts`, private): the
converter writes object textures `truecolor` with no pixel limit, so every texture with more than 256 colours became
32 bpp RGBA. RESOURES.TXD alone held 133 RGBA 256 x 256 (33.3 MiB) and 262 RGBA 128 x 128 (16.4 MiB) textures, against
119 and 313 of them in 8 bpp. The RGBA textures in other containers (PROCTEES, FX) are the disc's own.

## Change (formats `ootftex.ts`)

* `quantizeRefined`: median cut, then four k-means rounds that move each palette entry to the mean of its nearest
  colours and map every colour to its nearest entry (alpha weighted 4 in the distance). Fully transparent pixels keep
  an entry of their own, so cut-out coverage never changes; that entry takes the mean colour along the holes' edges
  (`holeEdgeColor`), so filtering at a cut-out edge does not darken toward black. Over the 460 RGBA textures this gains
  about 3 dB median PSNR on plain median cut (44.3 -> 47.4 dB in the offline prototype, `qeval.mts`).
* `encodePs2Texture`, `truecolor` mode: a texture of at least 128 x 128 (`indexedFromPixels`) with more than 256
  colours is written 8 bpp when the refined palette reaches 38 dB colour PSNR (coverage weighted, `indexedMinPsnr`)
  and changes no pixel's alpha by more than 8 of 255 (`indexedMaxAlphaError`); otherwise it stays 32 bpp. Smaller
  textures stay exact. The other lossy 8 bpp paths (`indexed` mode, skinned and native fast-draw textures) use the
  refined palette too; their square / indexed rules (`upgradeIndexedSquareTexture`, `addTexture(..., indexed)`) are
  unchanged. The lossless pixels still go to the HD sink, so HD packs upscale the exact image.

Tests (`ootftex.test.ts`): gentle shading at 128 and 256 goes 8 bpp within 4 levels per channel and 66 KiB; 64 x 64,
`indexedFromPixels: Infinity` and noise stay 32 bpp; a cut-out keeps every hole and every opaque pixel and carries the
edge colour in its holes; mixed graded alpha and colour noise stays 32 bpp.

## Effect on the pack

| | Before (packA) | After (packB) |
| --- | --- | --- |
| RGBA 256 x 256 written by the converter | 156 | 17 |
| RGBA 128 x 128 written by the converter | 281 | 28 |
| RESOURES.TXD | 75.3 MB | 40.4 MB |
| ootf-levels.zip | 141.9 MB | 138.4 MB |

392 textures changed 32 -> 8 bpp, 50.6 MiB -> 13.1 MiB; their PSNR against the 32 bpp version: minimum 38.3 dB,
10th percentile 41.4, median 49.0 (`cmp.mts`). `build/harness/fixes3/texture_compare.png`: five of them (the worst,
the 10th percentile, the median, a cut-out with soft edges and a 256 x 256 creature skin), 32 bpp | 8 bpp | colour
difference x8 (dark blue = fully transparent in the source). Nothing visible at 2x zoom; the worst case is a thin
cobweb whose difference sits in the strands' antialiasing.

## In game: Ruined Village opening

Same exe for all four runs (`8543e4fd`, this pass), same recipe as the timing journal (New Game into the Order of the
Flame, no input, audio on, `DRAKAN_STOMP_PROBE=1`, RAM dumps at +100 / 130 / 140 / 200 / 255 s), runs under
`build/harness/fixes3/village/<run>`. "Wait" is how long 1.2a stayed at action 0 after it was listed (`ticks.py`);
the cache columns are the texture manager `0x4db280` at +140 s (`texsum.py`).

| Run | Pack | Budget | 1.2a | Cache used / budget | Resident textures | RGBA buffers resident |
| --- | --- | --- | --- | --- | --- | --- |
| `A-def` | before | 8 MiB (default) | on its trigger | 8,388,144 / 8,388,608 | 316 | 2 x 256 KiB, 16 x 64 KiB (1.5 MiB) |
| `A-off` | before | native (`DRAKAN_TEX_CACHE_KB=0`) | waited 0.3 s | 5,659,744 / 5,662,310 | 243 | 12 x 64 KiB (0.75 MiB) |
| `B-def` | after | 8 MiB (default) | on its trigger | 8,177,640 / 8,388,608 | 380 | 4 x 64 KiB (0.25 MiB) |
| `B-off` | after | native (`DRAKAN_TEX_CACHE_KB=0`) | on its trigger | 5,661,344 / 5,662,310 | 280 | 3 x 64 KiB (0.19 MiB) |

The cache keeps everything it has loaded until it must recycle, so it is full in every run; the working set shows in
what it holds: with the 8 bpp textures 15 to 20 % more textures fit in the same budget, and the RGBA share drops from
1.5 MiB to 0.25 MiB. 1.3 started at 183.4 to 184.9 s in all four runs (the 1.5 s spread is boot time). The two shots at
+200 s (`village_s200_AB.png`, before above, after below) show the same frame of 1.2a with no visible difference.

## Remaining gaps

* The native-budget wait varied from 0 to more than 110 s between identical runs before this pass; one run per pack
  with `DRAKAN_TEX_CACHE_KB=0` shows the scene can start on time with the native budget, not that it always will. The
  8 MiB raise stays.
* 45 converter textures of 128 px and more stay 32 bpp (soft alpha mixed with many colours, or below 38 dB).
* Only Ruined Village was measured in game.
