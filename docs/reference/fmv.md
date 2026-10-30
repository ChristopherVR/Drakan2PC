# Drakan FMV / cutscene pipeline

Status: format fully decoded and verified (all 4 English movies decode end to end with stock FFmpeg).
Tools (run with `tools/pyenv/Scripts/python.exe -I`): `analysis/scripts/vdb_common.py`, `vdb_list.py`,
`vdb_extract.py`, `vdb_ipuparse.py`.

## 1. Summary

* Movies live in `.VDB` files (SRSC containers). Video only; **no audio in the VDB** (section 5).
* Each movie is a sequence of **PS2 IPU "IDEC" intra frames**: not MPEG start-code streams, no P/B frames, no
  sequence/picture/slice headers. This is exactly the `.ipu` format that FFmpeg's `ipu` demuxer/decoder
  (`libavformat/ipudec.c`, `ipu_decode_frame` in `libavcodec/mpeg12dec.c`) understands. Converting = prepend a
  16 byte `ipum` header to the raw frame blob.
* The EE movie player drives the IPU hardware directly (libipu + hand written loop) with only the commands
  BCLR, FDEC, IDEC, SETIQ, SETVQ, SETTH; output is RGB32 via fromIPU DMA, then uploaded to the GS.

## 2. VDB container

SRSC container (see `docs/reference/iop-and-io.md` 5.4). Resource types in a VDB:

| type | id | size | meaning |
|---|---|---|---|
| 0x340 | 0 (sub 1) | 8 | `00 00 01 00 04 00 07 00` (u16 x4, version-like; same in all files) |
| 0x402 | 0 | 4 | per-file header `00 01 00 00` |
| 0x341 | 1 | 8 | u16 len + category name ("Logos" / "Movies") |
| 0x342 | N (sub 1) | var | movie header (below) |
| 0x343 | N (sub 1) | var | frame blob for movie N |

Movie header (0x342): `u16 namelen; char name[namelen]` (NUL padded, e.g. "Sony & Surreal Logo Intro_Pal",
"D2 Intro Movie01"), `u32 width, u32 height, u32 frames`, then **frames+2** `u32` absolute byte offsets into the
0x343 blob: `offs[0]=0`, frame i = `[offs[i], offs[i+1])`, and the final chunk `[offs[frames], offs[frames+1])`
is a 20 byte end-of-stream chunk (`00 00 01 b1` + zeros). `offs[frames+1] == size of the 0x343 resource`
(asserted by `vdb_common.movies()` on every movie). Max frame size 73,670 bytes, under the IOP request limit
0x40800, so the game reads one frame with one Resource Request (type 0x343, id N, sub 1, offset `offs[i]`,
length `offs[i+1]-offs[i]`, see IOP doc 3.2). No resource-level compression.

(Correction to docs/reference/iop-and-io.md 5.5: "512x264" in the logo header is really 512x512 with **264 = frame count**.)

## 3. Frame format (IDEC macroblock stream)

Every frame ends with `00 00 01 b0` (the EE skips it with FDEC 32 before the next frame). Bit layout
(MSB first), verified against FFmpeg and `vdb_ipuparse.py`:

```
byte0 = flags   b0-1 intra_dc_precision (0=8bit,1=9,2=10)   b2 dct_type bit present in each MB (IDEC.DTD)
                b4 alternate_scan   b5 intra_vlc_format (table B.15)   b6 q_scale_type   b7 MPEG-1 style coefficients
then for every macroblock in raster order (W/16 x ceil(H/16), 4:2:0, all intra; DC predictors = 128<<prec at frame start;
initial quantiser_scale_code = 1):
   if not first MB: '1'            (macroblock_address_increment = 1)
   '1' = intra  |  '01' = intra+quant (a 5-bit quantiser_scale_code follows)
   [dct_type bit if flags&4]
   [5-bit qscale code if intra+quant]
   6 blocks (Y0 Y1 Y2 Y3 Cb Cr): DC size VLC (luma/chroma tables B.12/B.13) + diff, AC run/level VLCs
   (B.14, or B.15 if flags&0x20, or MPEG-1 table if flags&0x80), escape = 6-bit run + 12-bit level, EOB
.. 00 00 01 b0
```
Intra quant matrix = MPEG default (in the EE at 0x517000, zigzag order `08 10 10 13 10 13 16 16 ...`; the
non-intra matrix at 0x517040 is flat 16 and unused). Flags seen: INTERFCE logo 0x20, intro/outro 0x22
(10-bit DC), "ez" 0x00 (MPEG-1 VLC tables). Frame sizes: 512x512 (logo, intro, outro; 32x32 MBs, the frame consumes
exactly all bits, last 32 bits = end marker) and 320x240 ("ez").

Frame rate is NOT in the VDB. It is the "Frames Per Second" property of the Movie Player object in the level ODB
(`Full,25,20,10`; EE tables 0x51b568 = interval ms {1,40,50,100} and 0x51b578 = fps {30,25,20,10}). The logo, intro
and outro players use index 0, "Full" (player object +0xa8 = 0, logged at run time): one movie frame per engine tick,
which the PAL console held to 25 frames/s. The material is 25 fps: intro 3059 frames = 122.4 s and outro 2098 = 83.9 s
match their soundtracks (123.3 s / 84.0 s). The ms clock (0x4af7f8) is COP0 Count / 295 / 1000, wall clock in the port.
The port therefore holds the engine tick to 40 ms per movie frame while a "Full" movie plays (`drakan_overrides.cpp`,
`hookMovieDecodeStep`): measured logo 10.5 s, intro 125.4 s. Changing table entry 0 to 40 ms instead gives black
flicker: the movie screen clears the frame buffer every tick and expects a frame stamped each time. `f_0x1767a0` is
resumable (entries 0x1767f4 ...), so a hook sees only the fresh calls; the frame counter (+0x2c) is compared across
calls.

The shared pacing/completion hooks now use the selected [disc profile](regions.md). PAL retains 25 fps;
USA uses 30000/1001 fps and its own decode entry, completion thunk and screen-manager address. The screen table
has 33 slots on PAL and 32 on USA. Layout and frame-rate values are explicit metadata rather than inferred from
an NTSC flag; adding a disc does not require another movie-hook implementation.

## 4. Movies on the disc

| file | id | name | size | frames | duration @25 fps |
|---|---|---|---|---|---|
| COMMON/INTERFCE/INTERFCE.VDB (EN 5.6 MB; FR 3.9, DE 5.2, IT/ES 5.1) | 4 | Sony & Surreal Logo Intro_Pal | 512x512 | 264 | 10.6 s |
| LEVELS/LEVEL0RO/LEVEL0RO.VDB (214 MB, identical in all 5 languages) | 1 | ez | 320x240 | 195 | 7.8 s (test clip) |
| | 6 | D2 Intro Movie01 | 512x512 | 3059 | 122.4 s (126 MB) |
| | 7 | D2 Outro Movie01 | 512x512 | 2098 | 83.9 s (85 MB) |
| LEVELS/LEVEL8OS/LEVEL8OS.VDB | - | empty stub (60 bytes) | | | |

No other level has a VDB. LEVEL0RO is the "Level 0 - Intro" level; its ODB contains `Movie`, `Movie Player`,
`Timer`, `Fader` classes. Movie Player properties (strings 0x544108-0x544210): `Movie to Play`, `Sound Track`,
`Render Method` {Direct DMA, Object Surface}, `Frames Per Second`, `Full Screen ?`, `List Play Order`.
Resource type 7 = "Movie" (string 0x519f58). Pictures were visually checked (logo, red splat, intro book/dragon).

## 5. Audio

Not in the VDB (only types 0x340-0x343 exist; 0x343 is pure video, `offs[]` accounts for every byte). The Movie
Player object has a separate `Sound Track` property, i.e. the soundtrack goes through the normal sound system
(SDB ADPCM via RSRCIOP SID 0x1203 / SPU2; candidates `LEVEL0RO.SDB` 3.5 MB and `PS2MUSIC.SDB` 48 MB) and is synchronised
with the video only by the game's ms timer (0x4af7f8) and the fps tables, not by an interleaved stream. The PC runtime
therefore gets movie audio from its normal SDB/SPU2 path and only needs to pace video by wall clock. (Open: identify the
`Sound Track` SDB entry used by the intro/outro.)

## 6. EE movie player (R5900 ELF `SCES_500.06`, virtual addresses)

libipu (Sony "PsIIlibipu 2300", string 0x516ff0):

| addr | role |
|---|---|
| 0x4bbaf0 | `sceIpuInit`: IPU_CTRL=0x40000000 (reset), BCLR, pushes matrix data from 0x517000 to IPU_IN_FIFO (0x10007010), SETIQ (0x50000000 intra, 0x58000000 non-intra), SETVQ 0x60000000 (CLUT 0x517050), SETTH 0x90000000, BCLR |
| 0x4bba20 | `sceIpuSync(mode)`: mode 0 busy-wait IPU_CTRL bit 31, mode 1 poll |
| 0x4bba88 / 0x4bb780 | start toIPU DMA (ch4 0x1000b400); 0x4bb718 = start fromIPU DMA (ch3 0x1000b000) (toggles D_ENABLER) |
| 0x4bb7e8 | `sceIpuSuspend`: saves D3/D4 + CTRL regs into a 0x24 byte struct |
| 0x4bb8d0 | `sceIpuResume`: restores, issues BCLR(bp & 0x7f), restarts DMAs |

Game movie player (IPU register writes at 0x176a48-0x176c48, DMA at 0x176ca4-0x176cfc and 0x177718-0x177734):

| addr | role |
|---|---|
| 0x177800 | start/init: first call prints "Initializing IPU library..." (0x51b598) and calls `sceIpuInit`; creates the decode job (0x12b640); `obj+0x230` job, `obj+0x234` ipu-initialised flag |
| 0x177898 / 0x177908 | per tick update / completion query (vtable calls on the job) |
| 0x1767a0 | **main decode step** (1748 B). Paces frames against the ms clock using the fps tables, then per frame: `FDEC 32` (skip previous end marker) -> `FDEC 0` -> `FDEC 8` (reads the flags byte) -> `IPU_CTRL = (flags & 0xfb) << 16` -> **IDEC 0x10010000** (QSC=1, RGB32 out, no dither; **0x11010000** if flags bit2/DTD) -> fromIPU DMA loop (ch3, up to 0xfc00 qwc per chunk) into the destination buffer list (`obj+0x20`) -> blit |
| 0x176e78 | issue the read of the next frame from the VDB (Resource Request type 0x343; ring of 6 slots at `obj+0x34`, 16 bytes each; `obj+0x28` -> loaded 0x342 header body {w,h,frames,offs[]}; `obj+0xa0` next frame; `obj+0x9c/0x98/0x94` read/DMA/decode ring indices) |
| 0x1776e0 | start toIPU DMA (ch4: MADR `slot+0x34`, QWC `slot+0x38 >> 4`, CHCR 0x100) for the next ready slot |
| 0x1776b8 | advance decode slot index (mod 6) |
| 0x177650 | **blit**: `(a0=obj, a1=w, a2=h)` centres the w x h RGB32 frame on the screen (screen size from the global at `*(0x5565c8)+0xf8/+0xfc`) and calls the GS image-transfer helper 0x1da260 (descriptor 0x4f8ef0); callers 0x176980, 0x176de8, 0x177968 |

IPU is touched only from 0x176xxx and 0x4bbxxx. Commands issued: BCLR, FDEC, IDEC, SETIQ, SETVQ, SETTH; no BDEC/VDEC/
CSC/PACK. Output is RGB32 (IDEC OFM=0) DMA'd to EE memory, then GS image upload ("Direct DMA") or used as an
object-surface texture ("Render Method").

## 7. Recommended PC runtime approach

The data is intra-only IDEC macroblock data (strategy "b" of the brief, but tiny), identical to FFmpeg's `ipu`
codec. libmpeg2 / pl_mpeg cannot decode it directly (no sequence/picture/slice headers; pl_mpeg is MPEG-1 only).
Options in order of preference:

1. **Native frame decoder (~300 lines C, no dependencies).** `ipu_decode_frame(buf, size, w, h, rgba_out)` implementing
   the section 3 grammar: bit reader, DC VLCs, AC VLC tables (copy from FFmpeg `mpeg12data.c`, which
   `vdb_ipuparse.py` loads), dequant `(level*qscale*matrix[j])>>4` with the default intra matrix, mismatch control,
   DC scale `1<<(3-precision)`, 8x8 IDCT, then YCbCr->RGB32 (BT.601 limited range, alpha 0x80 as the IPU CSC).
   `q_scale_type=0` -> qscale = code*2, else the MPEG-2 non-linear table. Use FFmpeg output as reference.
2. **IPU HLE at the MMIO layer** (the recompiled player runs unchanged): model IPU_CMD/CTRL/TOP/BP and D3/D4. toIPU
   DMA appends to the input bit buffer; BCLR/FDEC adjust the bit position (FDEC 0 returns peeked bits via IPU_CMD/TOP);
   IDEC runs option 1 until the `00 00 01 b0` marker and queues RGB32 output (qwc = w*h*4/16) for fromIPU DMA; clear
   IPU_CTRL BUSY when done; SETIQ/SETVQ/SETTH/BCLR(0) are no-ops apart from consuming FIFO data. PCSX2
   `IPU/IPU.cpp` + `IPU/mpeg2lib` (GPL) are the register-semantics reference.
3. **Hook above the hardware (quickest bring-up):** override the movie player at 0x1767a0 (decode step) and/or 0x177650
   (blit): decode frame `obj+0xa0` from the already loaded blob with option 1 into the guest RGB32 buffer
   (`obj+0x20` list) and fake the ring bookkeeping, or present straight to a host texture. Pace by wall clock using
   the object's fps. "Frame presented" hook point = 0x177650 (a1 = w, a2 = h).

Offline alternative for tests: `vdb_extract.py file.VDB outdir [--mp4 --ffmpeg ffmpeg --fps 25]` writes FFmpeg-readable
`.ipu` (and optionally H.264). A native decoder avoids shipping ~215 MB of transcoded data.

## 8. Verification log

* `ffmpeg -i X.ipu -f null -` decodes 264 / 195 / 3059 / 2098 frames without errors (system FFmpeg 2025-10-01 has the
  `ipu` codec, so no static build was downloaded; `decomp/tools/ffmpeg` not created).
* `vdb_ipuparse.py` independently parses frames with the section 3 grammar and consumes exactly all bits for 512x512.
* `.ipu` header written by `vdb_extract.py`: `'ipum', u32 1, u16 w, u16 h, u32 frames` (FFmpeg skips 8 bytes, reads w, h,
  frame count); playback defaults to 25 fps unless `-framerate` is given.
* FFmpeg source copies used for tables live in `build/fmv/` (mpeg12data.c, mpeg12dec.c, ipudec.c).

## 9. HD movie pack (AI-upscaled replacement frames)

How the movies reach the screen (measured 2026-10-23 with `GSR_XFERLOG`, `GSR_TEXLOG`, `PS2X_TRACE_IPU`): logo and intro
use the "Direct DMA" render method. Every decoded frame is stamped macroblock by macroblock, as 1024 host->local image
transfers of 16x16 CT32 (the IPU's RGB32 output unchanged, alpha 0x80), into **both** frame buffers (`dbp` 0x0 and 0x1000,
`dbw` 8, the 512x512 frame at (0,0)). No texture is involved: no texture decode happens while a movie plays. (The CT24
256x256 "volatile" textures of `hd-textures.md` section 8 appear after the logo, in the menus, not in the movies.) The
IDEC of every frame starts byte aligned, one byte after the frame start (`FDEC 8` consumed the flags byte).

Pieces:

| Part | File | Role |
| --- | --- | --- |
| Pack builder | `analysis/scripts/fmv_hd_pack.py` | reads the movies from the ISO, decodes them with FFmpeg's `ipu` decoder, upscales every frame 4x with Real-ESRGAN (`realesrgan-x4plus`, `tools/hdpack/realesrgan`), encodes `<out>/<key>.mkv` (HEVC CRF 18, yuv420p, keyframe each second) and writes `<key>.txt` (frame index). Default output `dist/DrakanPC/movies` (kept by `Build-LocalGame.ps1`). Default movies: intro (`level0ro_6`), outro (`level0ro_7`), logo (`interfce_4`), English disc folders. |
| IPU hook | `ps2_ipu.cpp` (`PS2Ipu::setFrameObserver`) | after each IDEC: the compressed bytes IDEC consumed and the 1024 RGB32 macroblocks |
| Host glue | `ps2xRuntime/src/lib/drakan_fmv_hd.cpp` | loads the pack (`[video] hd_movies`, env `DRAKAN_HD_MOVIES`), identifies the frame, registers its signature with the renderer, decodes the HD video ahead on a thread (FFmpeg, already linked for the MPEG stubs) |
| Renderer | `gsr/movie_frames.h`, `gs_core.cpp` (movie tracking), `d3d11_movie.cpp` | recognises a complete frame in a frame buffer and draws the HD frame over it |

Frame identity (pack <-> runtime): CRC-32 + length of the frame's bytes after the flags byte up to the `00 00 01 b0` end
marker, which is exactly the byte range IDEC consumes at run time (start bit / 8 up to the start code the IPU skips to). Runs
of identical compressed frames (white lead-in, fades) map to several indices; the host takes the next one after the last
frame of the same movie.

Synchronisation (EE thread vs GS worker): the host does not tell the GS "frame N is next", because the IPU decodes up to six
frames ahead and the GS consumes the uploads later. Instead the host registers a **signature** per decoded frame: the 64-bit
sum over its macroblocks of `blockTerm(blockHash(macroblock bytes))` (`gsr/movie_frames.h`; a sum, so identical macroblocks
do not cancel and upload order does not matter). For each frame buffer receiving 16x16 CT32 uploads the GS keeps the term
of every 16x16 slot and the running sum over the movie rectangle (centred on the 512 line screen like the blit at 0x177650).
When the sum equals a registered signature, that buffer holds exactly that frame; the HD frame is drawn into the render
target before the next draw, local->host / local->local transfer or display (`applyMovieFrames`), so the game's fades and
the subtitle overlay still go on top. Each new native macroblock in the rectangle cancels the match until the next frame
completes. Newest registration wins (identical frames keep the decoder window moving forward). Tracking switches off 600
vblanks after the last registration.

Drawing: `syncRT` first (native rows memory still leads must not overwrite the HD frame later), then a trilinear sample of
the HD texture (mip chain, so internal scales below 4x minify cleanly) into the rectangle at internal resolution, alpha
0x80 like the IPU. 2048x2048 HD frames are 1:1 at internal scale 4 (3840x2160 output of the 4:3 picture is 2880x2160, so
horizontally the 4:3 present stretches 2048 -> 2880).

Decoder: window from the frame the GS asked for last (two frames kept behind it for the second frame buffer) to three frames
past the newest IPU frame, at most 16 frames (16 MB each). A request outside the window seeks (keyframe before, decode
forward). The GS waits up to 120 ms for a frame and otherwise keeps the native one. The log prints
`[fmv-hd] <movie>: N HD frames shown, M frames fell back to the original` per movie.

Pack build notes: Real-ESRGAN's automatic tile size (200 px input) leaves faint seams along output rows/columns 799 and 1599
in flat areas (measured: mean step 0.3 levels vs 0.02 median), so the builder uses one 512 px tile per frame. With whole-frame
tiles the default parallel jobs (`-j 4:6:4`) silently write **black** frames on a 16 GB RTX 5080; the builder runs `-j 1:1:2`
and checks that sampled upscaled frames keep the source brightness. Speed on that GPU: ~2 frames/s, ~40 min for all three
movies; sizes and results in section 10.

Noise: without filtering, Real-ESRGAN turns the source's MPEG noise and 8x8 blocking into invented fine texture ("fur" on
smooth surfaces) that differs from frame to frame and shimmers in motion (reported in play). Measured on intro frames
380..400 as the mean frame-to-frame change of the 1-4 px detail band at 2048: source (bicubic) 0.70, unfiltered pack
1.30. The builder now filters before and after the upscale (`--prefilter`, `--postfilter`): `deblock=filter=strong:
block=8,hqdn3d=3:2:6:6` on the 512x512 frames (8 lead-in frames per chunk keep the recursive temporal state continuous
across chunks), `hqdn3d=0:0:6:6` (temporal only) on the HD frames while encoding. Result 0.83 (4-16 px band: 1.72 -> 0.95,
below the source's 1.66), about 77 % of the fine detail kept; the intro pack shrank from 326 to 207 MB. Rejected:
`nlmeans` (plastic), post-only or pre-only filtering (half the gain).

## 10. HD movie pack results (2026-10-23, RTX 5080, internal scale 4, 3840x2160 window)

| Movie | Key | Frames | Pack | Build time | In game |
| --- | --- | --- | --- | --- | --- |
| Sony & Surreal logo | `interfce_4` | 264 | 2 MB | 1.8 min | 264 / 264 HD frames shown, 0 fallbacks, 10.5 s |
| Intro book movie | `level0ro_6` | 3059 | 207 MB | 23.5 min | 3059 / 3059 HD, 0 fallbacks, 124.6 s, no black frame in a 20-shot burst, 4:3 pillarbox |
| Outro book movie | `level0ro_7` | 2098 | 139 MB | 14.5 min | not reached in a test run (needs the end of the game); same identification path |

* Pacing: before the 25 fps fix (section 6) the intro ran 60.9 s with the pack vs 63.7 s without (~48-57 frames/s),
  i.e. the pack does not slow the game. An earlier build converted YUV->RGBA with `SWS_ACCURATE_RND |
  SWS_FULL_CHR_H_INT` (~36 ms per 2048x2048 frame on one thread): the decoder fell behind, the GS waited for frames and
  the intro took 84 s; plain `SWS_BILINEAR` costs ~2 ms. With the fix: logo 10.5 s, intro 125.4 s, all frames HD.
* Look: clean edges and no macroblocking; Real-ESRGAN paints fine grain smooth and turns the blurred runic text of the
  book into hard glyphs. Near-white logo frames come out brighter (frame 100: mean 237 -> 251).
* Aspect: the engine keeps updating the 3D camera during the movies (logo = GUI screen 30, intro / outro = 29), so the
  camera-recency rule of `widescreen.md` section 9 widened them to 16:9. `WorldPolicy` now stays 4:3 while the IPU
  decoded a frame in the last 15 vblanks (`PS2Ipu::decodedFrameCount`); verified pillarboxed in a 16:9 window.
