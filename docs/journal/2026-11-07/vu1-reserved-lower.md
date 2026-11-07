# "VU1 reserved lower" and 2 fps in the converted Volcanic level: terrain chunks with too many textures

Follows [closed-loop driver](ootf-closed-loop-driver.md) section 6. Runs, recipes and tools are private under
`build/harness/vu1-lower/` (recipe `r1.txt`: the third Succ Switch approach of `ootf-volroute/sw1`, place at
(127203, 53800, 218061) facing 1.5708, W, E, stand). Executable: incremental builds of main into
`build/harness/vu1-lower-dist`. Pack: `ootf-volroute/pack` (formats `4d21976`). Nothing from either game is quoted.

## Result

Root cause is converted data. The OotF terrain converter wrote terrain chunks that list 9 to 12 textures; the PAL discs
never list more than 8 (histogram over all 14 disc LVLs: the maximum is 8, in one chunk each of LEVEL2RE, LEVEL1OR, LEVEL1NA
and LEVEL8OS). In the Volcanic LVL, 1,457 chunks list 10 textures, 238 list 11 and 42 list 12.

* The engine builds a 10-texture chunk into draw groups (68-byte records, the page's group array at page `+84`) of which only
  7 are filled; the rest stay zero. The terrain mesh drawer `FUN_00158148` still walks them, and for an empty group the
  packet builder `FUN_001ed0b8` (`a3` = count) emits a REF tag with QWC 0 and a VIF `UNPACK S-8, masked` with `NUM = 0`. NUM 0
  means 256 on the VIF, so the VIF takes the next 256 bytes of the packet as unpack data and parses the rest out of step.
* In the captured packet the out-of-step parse lands on a GIF `TEX0` / `MIPTBP1` A+D pair and reads `0x85607852` as
  `STMOD` (mode 2, difference). The game sets `STMOD 0` once at boot and never again, so every later UNPACK adds the row
  register: the VU1 header, flag word and GIF tag templates in VU memory become steadily growing garbage, every batch then
  builds broken GIF packets, and the VU1 runs stop with the internal codes `0xfffffff8` (XGKICK sees a GIF tag with FLG 3) and
  `0xfffffffb` (packet runs past 64 KB without EOP). That is the "VU1 reserved lower" flood (40,000 to 550,000 lines per run)
  and, because every frame's VU1 work is garbage, 2 to 6 frames per 2 seconds for the rest of the session.
* The `pc` in those lines (0x1f0, 0xe70, 0x1018, 0x1580) is the entry of the run (the resume points after the E bits), not the
  failing instruction; the "instruction" is the stop reason. The logging itself is not the slowdown (the log is written by a
  separate thread through a pipe).
* 11 and 12 textures overflow the 0x4c-byte chunk record (10 ref slots) as `level-format.md` 10.1 already describes; that
  overwrites the weight-array descriptor, which fits the GS state built from level bytes the mip-level work found.
* Why the stair foot: the zero groups belong to three 10-texture chunks of layers `ss5` / `ss6` around the sanctum (for
  example `ss6` chunk 30, 1 x 2 cells, one drawn cell and one hole, 10 window textures). Whether the out-of-step parse hits a
  register code depends on what follows the empty group in that frame's packet, so it fires within seconds in about one run
  in three with Rynn standing there and after minutes on the walks; once it fires it stays (no later STMOD repairs it).

Why the chunks got that many: `buildPs2TerrainBlob` split crowded chunks only down to 1 x 2 cells, and a chunk also lists the
textures of the ring of cells around it (x0-1..x1, z0-1..z1), so a 1 x 2 chunk can see 12 textures; the limit was 10.

## Fix

`editor/packages/formats/src/terrainblob.ts`: `MAX_CHUNK_TEXTURES` is 8, crowded chunks are split down to single cells, and a
single cell that still sees more than 8 textures keeps its own texture first and then the neighbours' most used ones (the
dropped ones lose a little blending at that chunk's border; the neighbour chunks still draw their own cells). Test:
`test/terrainblob.test.ts` "never lists more than 8 textures in a chunk..." (every cell its own texture), which fails on the
old builder.

Verification: the Volcanic LVL of the pack with every layer that lists more than 8 textures rebuilt by the new builder (324 of
941 layers, slots, seq, level tag and texture order kept; `audit/repack.mts`), same recipe and executable:

| Pack | runs | empty-group unpacks (`[unpack0]`) | out-of-step VIF codes | "VU1 reserved" lines | flips per 2 s at the end |
| --- | --- | --- | --- | --- | --- |
| old | `r1`, `r4`, `r20`, `r30`, `r40` (fired), `r2`, `r10` to `r14`, `d3` (did not) | 522 to 1,029 in every traced run (`d1`, `d3`, `r40`) | `STMOD 85607852` in `r20` and `r30` | 246 to 64,367 where it fired | 3 to 6 where it fired, 56 in `d3` |
| rebuilt | `f50` to `f53` | 0 | 0 | 0 | 41 to 65 |

(The traced runs use `PS2X_VU1_THREAD=0 DRAKAN_VU_BATCH=0` and the trace hook, so their rates are lower than a normal game.)

The pack has to be rebuilt with the fixed converter (all levels: the histogram above has 9 to 12 texture chunks in every
converted level with terrain).

## Stock levels

The PAL discs keep every chunk at 8 textures or fewer, so they cannot produce the empty groups. Of all harness logs under
`build/`, the only stock-level run with the message is `local-character-view/b` (Level 1A with a netplay ghost, 2026-11-03,
242,355 lines from 27 s); it was not reproduced or traced here, its cause is open.

## Diagnostics added

* `PS2X_VIF_REGLOG=1|<dir>`: first use of each distinct VIF1 STCYCL / STMOD / MPG code, with the bytes around it, the commands
  parsed before it in that transfer and where the bytes sit in guest RAM (`<dir>` also saves the transfer).
* `DRAKAN_TRACE_FN=1ed0b8` (with a large `DRAKAN_TRACE_N`): prints only the zero-count calls (`[unpack0]`), with the group
  record and its page (`+11..+16` the page's box, `+84` the group array).
* The "VU1 reserved" report now names the stop reason and prints the first 32 and then every 4096th, with the total.

## Not done

* The XGKICK path treats a GIF tag with FLG 3 as an error and stops the run; the hardware handles FLG 3 like IMAGE. Only
  garbage data reaches it here, so it was left.
* The exact rule by which the page builder fills 7 of 10 groups was not derived (the record builder is in the
  `0x154d98` / `0x157f80` area); 8 is the disc's own maximum.
