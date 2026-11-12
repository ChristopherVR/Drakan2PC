# The batch path of the VU1 microprogram, read to the end (2026-11-12)

Read from the local listing (`analysis/vu`, not tracked) and checked run by run against the engine with the VU1
replay (`DRAKAN_VU_BENCH_BATCH`, section 7). Addresses are byte offsets in micro memory; "qw N" is quadword N of VU
data memory; lanes are x y z w. Everything here is behaviour in the reader's own words, no instruction text. Written by the `batch` subagent of the GPU vertex work; the orchestrator re-ran the proof of section 7 (see [gpu-vertex-implementation.md](gpu-vertex-implementation.md)). Paths under `build/review/gpu-vertex2/batch` are untracked scratch.

Code: `runtime/gs/include/gsr/vertex_batch.h` (record, CPU evaluation), `ps2recomp/ps2xRuntime/src/lib/vu/
ps2_vu1_gpu_batch.h` (classification of a run, index-list walk, the `Tracker`), `ps2_vu1_check.cpp` (the proof),
`ps2recomp/ps2xRuntime/tests/vu_gpu_batch_tests.cpp` (ctest `vu_gpu_batch.exact`).

## 1. Data layout

| qw | Content | Written by |
| --- | --- | --- |
| 0..3 | object-to-clip matrix (columns) | EE |
| 4 | fog: y, z = depth clamp, w = slope | EE |
| 5, 6 | viewport offset, viewport scale | EE |
| 7.x | flag word (16 bits) | EE, rewritten between the MSCNTs of a batch |
| 8 | constant colour and alpha (integers); .w is replaced by the set-up with flag 0x800 | EE, set-up |
| 9 | run header: x = base address, y = count, z, w (meaning depends on the run, see 2.2) | EE before every MSCNT |
| 10.w | slope of the depth fade | EE |
| 11 | GIF tag of a strip (packed, PRIM in the tag, four registers ST, RGBAQ, FOG, XYZ2, NLOOP 0, EOP) | EE |
| 13..16 | light direction matrix; 13 and 14 also are the point light's position and axis | EE |
| 17..20 | light colours (integers); 17, 18, 19 and 20.x also are the point light's colour, limits, slope and exponent | EE |
| 21, 22 | constants of the set-up's object fade | EE |
| 23..26 | texture matrix rows (26 = translation) | EE |
| 27..43 | per strip entry: OR (x) and AND (y) of the clip codes; the seventeenth entry lands in 43.x, 43.y | index-list run with the clipper |
| 43 | grid: x = columns, y = rows, z = address of the heights | EE (x, y overwritten by a clip-code index-list run with seventeen entries) |
| 44, 45 | grid origin, grid step | EE |
| 47 | a zero quadword for the flat grid | grid stage with flag 0x2000 |
| 65 | scratch of the clipper (65.x = OR of all clip codes of the run) | index-list run with the clipper |
| 66..290 | **positions**, slot 0..224 | EE (raw), grid, transform (in place) |
| 291..515 | **colours** (integers), or integer normals before lighting, slot 0..224 | EE, set-up fill, lighting, point light, fade |
| 516..740 | **texture rows**: x, y = S, T; z = 1 / w after the transform; w = clip code with flag 1 | EE (x, y), generators, texture matrix, transform |
| 741..772 | two index lists of 16 quadwords (four 16-bit indices each) | EE |
| 773..1023 | packet buffer of the index-list runs | index-list runs |

A vertex index of an index list is 1-based: index i uses position qw 65 + i, colour qw 290 + i, texture row
qw 515 + i, i.e. slot i - 1. The strip stage of the object path uses the same memory with another layout (slots from
qw 54, integer normals 240 higher, texture coordinates 480 higher), so the normals of a long strip table land in the
colour and texture arrays of the batch layout.

What the EE writes between the runs (from the VIF write lists of the new recordings, section 7): positions and
normals or colours as whole quadwords; the colours of constant-alpha batches as x, y, z only; **before every depth
fade only the alpha lane** of the slots it covers; per-corner texture coordinates as x, y only (they overlay the
texture rows of slots 0..127, whose z lane keeps the 1 / w of those slots); nothing in the texture rows between two
texture matrix runs.

## 2. The stages

### 2.1 Set-up (MSCAL at 0x0000, stops with the pc at 0x01F0)

Loads qw 0..6 into VF01..VF07; they stay there for the whole batch (no batch stage writes them). The flag word:

* 0x800: object fade. The clip-space w of the point qw 22 (matrix times it) is clamped to [qw 21.y, qw 21.z], the
  alpha of qw 8 is scaled by (qw 21.x - qw 21.w * w) / 128, truncated and written back to **qw 8.w**.
* 0x8: the colour rows of all 225 slots are filled with qw 8 (after the fade).

Always executed by the microprogram (0.4 to 2.1 % of the engine time); the tracker only notes that a new batch
starts and that flag 8 makes the colour rows real.

### 2.2 The dispatcher (every MSCNT of the batch)

Every MSCNT resumes at 0x01F0 (after the set-up), 0x0E70 (after a vertex run), 0x1018 or 0x1580 (after an
index-list run); all four lead to the same test: **qw 9.z above zero is an index-list run, else a vertex run**.

A vertex run (base = qw 9.x, count = qw 9.y) executes, in this order, selected by the flag word:

```
if flags & 0x13C0 == 0:              [0x10: light]  transform
else if flags & 0x80:                fade                                   (nothing else)
else if flags & 0x200:               texture matrix                         (nothing else)
else:
    if flags & 0x1000:               grid (0x2000: flat)
        if int16(flags & 0xC000) > 0:    point light                        (0x4000 set and 0x8000 clear)
    if (no grid, or the point light ran) and flags & 0x100:   texture coordinates from integers
    else if flags & 0x40:            texture coordinates from positions
    [0x10: light]  transform
```

The test of the point light is on a signed 16-bit value, so bit 0x8000 switches it off. With flag 1 the fade and the
transform are other code (2.3). Every loop is a do-while: a count below one would run 65536 times; the recogniser
declines such a header.

All vertex stages are **vertex local**: stage S on slot s reads and writes only lanes of slot s (plus constants).
What each one does (rounding: every sum and product toward zero, a multiply-add rounds twice):

| Stage (loop at) | Reads | Writes | Function |
| --- | --- | --- | --- |
| grid (0x05D8) | position y as an integer (the height); with 0x2000 a zero quadword instead | position x y z w | x = origin.x plus one step.x per column, z = origin.z plus one step.z per row, both **accumulated** along the grid (each sum rounded), y = origin.y + 2 * height - 65536, w = origin.w. Rows * columns cells from qw 43.z, which has to be the run's base |
| point light (0x0680) | position (floats), colour rgb | colour rgba | d = light - position; cosine term = max(0, dot((-d.x, d.y, -d.z), axis)) / length(d), squared qw 20.x times (zero when that count is not above zero); rgb = min(255, rgb + 2 * light colour * term); **alpha is replaced**: min(128, 128 - k19.w * k18.y + k19.w * clamp(d.y, k18.y, k18.z) - k18.w * abs(d.y) / length(d)). 1 / length comes from the E unit: square root toward zero of the rounded sum of squares, then 1 / that; zero for a zero length |
| texture coordinates from integers (0x0950) | colour row as four integers | texture row x y z w | (S, T) = rows qw 23..26 times the vector; z = 1.0, w = 0 |
| texture coordinates from positions (0x0A70) | position (floats) | texture row x y z w | the same with the position |
| light (0x0BD0) | colour row as an integer normal | colour rgba | four terms max(0, direction row . normal); rgb = min(200, sum of light colour * term + ambient); alpha = min(200, qw 8.w) |
| transform (0x0D10) | position | position (12.4 fixed point x, y; integer z; fog and skip bit in w), texture row z = 1 / w | clip = matrix * position; q = 1 / clip.w; fixed = trunc(16 * (offset + clip * scale * q)), the w lane with offset.w - slope * clamp(clip.w); the same formulas as the strip stage |
| fade (0x0318) | texture row z (the 1 / w the transform left), colour alpha | colour alpha | alpha = trunc(max(0, 1 - qw 10.w * (1 / z)) * alpha): the depth is recovered as the reciprocal of the stored reciprocal |
| texture matrix (0x04D8) | texture row x, y | texture row x, y | (S, T) = row 23 * S + row 24 * T + row 26, x and y lanes; runs on base 516 over all 225 slots whatever the vertex count is |

Consequences read off this table:

* The fade does not have the depth, it has 1 / w from the transform of an **earlier MSCNT**, so a fade run needs the
  transform of the same slots to have run in this batch.
* The texture matrix is **cumulative**: layer k's coordinates are layer k - 1's times the matrix of layer k (the EE
  does not rewrite them in between). The fade is not: the EE rewrites the alpha lane before each fade.
* The texture matrix run covers the slots the batch never filled. Those hold what earlier batches left and are
  multiplied again by every layer of every batch; in the converted Order of the Flame terrain this is where the
  values next to the largest float come from (`perf-cpu.md` 3.2): nothing draws them.
* The alpha clamp of the point light is 128 and its colour clamp 255 because the constant register is loaded after
  the instruction in the same pair has used the old value.

### 2.3 Vertex runs with clip codes (flag 1)

The transform (0x0DB0) stores the **clip-space** position (no division) in place, 1.0 in the texture row's z and a
clip code in its w: bits 0..3 from the CLIP flags (x above w, x below -w, y above w, y below -w, against the
magnitude of w), bit 4 when z is above w. The fade (0x03F0) takes the depth from the position's w. These runs, and
every other vertex run of a batch with flag 1, are left to the microprogram.

### 2.4 Index-list run without clip codes (stops at 0x1008, pc 0x1018)

Header: x = address of the list, y = quadwords of the list (four 16-bit indices each), z = offset of the per-corner
texture coordinates (also the "this is an index-list run" mark), w = first slot of the per-quadword colours. For the
j-th index of the list (0-based):

* index <= 0: end of a strip. With vertices pending, their count and the EOP bit go into the tag, the packet is
  kicked and the next packet starts right behind it in the buffer. **With no vertex pending the run ends.**
* index > 0: one vertex is appended to the packet, copied from the processed arrays:
  position quadword of slot index - 1 to the XYZ2 register and its w lane also to the FOG register;
  colour: flag 4 = the colour row of the slot, else the colour row of slot (w + j / 4), one per index quadword;
  texture: flag 0x20 = S, T of the slot's texture row, else S, T of the texture row of slot (z + j - 1), a stream
  parallel to the index list; both times 1 / w of the slot (its texture row's z), which also is Q.
* When the list ends without an end index, the pending vertices have been written to the buffer but are **not
  sent**.

There is **no reject rule and no per-triangle decision** in this variant. The only "do not draw" bit a vertex can
carry is bit 15 of the fixed point fog lane, exactly as in the strip stage, a function of that vertex alone
(`passNeverSkips` proves from the constants that it cannot be set: true for every one of the 57,528 records of the
new recordings, and the bit was set on 0 recorded vertices). Strips are
separate packets with PRIM in the tag, so a restart is a new tag. The buffer holds 251 quadwords, i.e. at most 62
vertices per run; a list that would run past the end of memory is declined (not observed).

Lanes the run does not write (the w lane of the ST quadword, three lanes of the FOG quadword) keep what earlier
packets left in the buffer; a GS reads none of them.

### 2.5 Index-list run with clip codes (stops at 0x1570, pc 0x1580)

Three walks over the same list:

1. Per strip entry the OR and the AND of the clip codes of its vertices go to qw 27 + entry; the OR over the
   stored entries goes to qw 65.x. Two details decide exactness. The first vertex slot after an end index is
   preceded by the "code" of the end index itself: lane w of qw 515 + index, for index 0 the alpha of colour slot
   224, which is ORed and ANDed into the next entry. And the exit test after sixteen entries reads the entry
   counter one instruction after it was incremented (the only such branch hazard in the batch path), so seventeen
   entries are stored and the last one overwrites the grid header qw 43.x, 43.y. If the AND over the entries (the
   seventeenth left out) is above zero as a signed 16-bit value, **the run ends without sending anything**.
2. Strips are built like in 2.4, with the perspective division done here (same arithmetic as the transform's second
   half, texture row z taken as 1.0). A strip whose AND is not zero is dropped. A vertex with a clip code sets the
   skip bit on itself and on the next two vertices of its strip (the three triangles it takes part in). After the
   last strip an **empty tag** (the template itself, zero vertices) is kicked.
3. Only when the OR is not zero: every triangle with a coded vertex is clipped against the planes and sent as a
   fan with interpolated vertices.

So in a batch with flag 1 every index-list run is this variant (the unclipped variant cannot run on clip-space
positions; the EE never mixes them), and the split is **per run and exact**: a run whose vertices all have clip
code zero sends exactly the strips of 2.4 computed from the clip-space positions, plus the empty tag (or nothing at
all when it has no strip entry with a vertex). The recogniser takes those from the codes in VU memory
(`FamClipInside`, record pass `PassProject`); a run with any coded vertex goes through the clipper on the CPU.

## 3. Hand-over between runs, and what persists

* Registers: VF01..VF07 from the set-up are the only register state a later run of the batch reads. Every run
  reloads all integer registers it uses and overwrites every float register lane it stores from. A run that is not
  executed therefore only has to leave the pc where it would have stopped (0x0E70, 0x1018, 0x1580).
* VU memory: the three arrays are the hand-over. In place per stage as in the table of 2.2; the index-list runs read
  the arrays and write only the packet buffer, qw 27..43 and qw 65; the flat grid writes qw 47. The tracker writes
  qw 27..43, 47 and 65 itself for the runs it takes, so below the packet buffer its memory is the microprogram's.
* Between batches: nothing is handed over on purpose, but the arrays keep their content. A new batch overwrites
  what it uses: the EE uploads positions and colours or normals of its slots, the set-up refills the colour rows
  (flag 8), the generators and the transform rewrite the texture rows of its slots. Slots above the new batch's
  vertex count keep old content; the only reader of those is the texture matrix run (2.2).
* The EE between the MSCNTs of one batch: section 1, last paragraph. Batches of world geometry (flags 0x0C, 0x14,
  0x80C, 0x814) come in several chunks: vertex runs over [66, n1), [66 + n1, n2), ..., index-list runs, then the
  positions are uploaded again and the next group of vertex runs follows in the same batch.

## 4. Does it compose into one function per vertex? Yes

Every vertex stage is vertex local (2.2), the index-list run copies. So the chain of stages a slot went through is
one function from the slot's raw row (three quadwords) and the constants of each stage to the packet fields, and the
CPU evaluation writes it as exactly that: `evaluateVertex` = for each pass of the record that applies to the vertex,
`applyPass` on the row, then `finishRow`. Points that needed care:

* **The grid's accumulated coordinates.** x of column c is the result of c rounded additions, z likewise per row.
  That is a function of the column and row, not a per-vertex computation worth repeating: the record builder runs the
  two accumulations once per batch (`gridAxis`, columns + rows additions) and gives each vertex its two values
  (`BatchVertex::gridX`, `gridZ`). The per-vertex part of the grid stage is y and w. The window is not 15 x 15:
  grids from 2 x 2 to 12 x 17 occur, the bound is rows * columns <= 225.
* **Data-dependent loops.** The point light squares its cosine term qw 20.x times; the pass carries the count,
  `kBatchMaxExponent` = 8 bounds it (a larger count is declined; 4 and 5 occur). The number of passes of a vertex
  grows with the terrain layer (grid, texture generator, transform, one texture matrix per layer so far, the last
  fade): the record holds at most 24 passes (up to nine texture matrix runs per batch and 13 passes in a record
  occur in the new recordings, 19 in an older one). Vertex chunks of one batch repeat the same stage with the same
  constants; such passes are stored once per record.
* **Per-corner inputs.** Without flag 0x20 the S, T of a corner come from another slot's texture row, without flag 4
  the colour from another slot's colour row. A record vertex is therefore keyed by (position slot, texture slot,
  colour slot): its row is assembled from the three, and the passes that apply are those the position needs plus,
  for the colour, lighting only (lighting reads nothing but its own row). Declined (`corner source`, not observed):
  a per-corner texture coordinate that a pending pass would produce, a foreign colour that depends on a position
  (fade or point light pending on it).
* **Reuse of intermediate results.** The fade reuses the transform's 1 / w, the texture matrix the previous layer's
  coordinates. Both stay inside the slot, so they are just more passes of the same vertex.
* **The skip bit** is per vertex (2.4). A triangle's draw decision never depends on a neighbour, except in the
  clipper variant, which stays on the CPU when any code is set (2.5).
* **Index lists** are strips with restarts: each strip a run of indices into the record's vertices. With flags
  0x20 and 4 (terrain, water) vertices are shared between strips, so an index buffer pays; without them every
  corner has its own texture coordinate and the record has one vertex per index.

## 5. The record

`gsr::vp::Batch` (program id 3), wrapped in the same GIF tag as a strip record (FLG = 3, magic in the upper half):

```
BatchHeader   program, prim (of the strip tag), stripCount, indexCount, vertexCount, passCount, flags
Pass[passCount]      kind, param, first, count, twelve constant rows   (208 bytes)
uint32 strips[stripCount]   length of each strip in indices
uint16 indices[indexCount]  into the vertices
BatchVertex[vertexCount]    raw row (position, colour, texture: 48 bytes), passMask, gridX, gridZ, slot  (64 bytes)
```

Pass kinds: Grid, GridFlat, PointLight, TexGenInt, TexGenPos, Light, Transform, Fade, TexMatrix, Project. The
constant rows of each kind are listed at `struct Pass`. `flags` bit 0: the run ends with an empty tag.

* One record per index-list run, self-contained (it copies the rows and the constants), so `gsr_core` can batch
  consecutive records with the same GS state like strip records, and a capture carries them unchanged.
* The record does not depend on any setting: which runs become records does, what a record means does not.
* `batchToGif` turns a record into the packets the microprogram sends (CPU evaluation): the oracle, and the fallback
  of a backend without the path (feed the packets to the ordinary GIF handling).
* A shader gets: the passes as constants, per vertex the row, passMask and the two grid values, an index buffer
  with strip restarts. Pass bodies are straight-line `vu::*` calls with selects; the loop over passes is bounded by
  24, the squaring loop by 8.

## 6. The tracker (state machine across runs)

`vu1gpu::batch::Tracker::step()` is called before every MSCAL / MSCNT with the mask of enabled stage families of
that call and returns Run or Skip (with a record to send for an index-list run). It keeps, per slot, two lane
masks and a short list of pending passes:

* **virtual lanes**: their content is what the slot's pending passes produce; VU memory still holds the older
  content. A vertex run the tracker can express is not executed: its stages become pending passes on the slots they
  cover.
* An index-list run is built from memory plus the pending passes its vertices need (found by walking each slot's
  list backwards from the lanes the run reads). Not executed.
* **Bail-out** is exact and local: any run the tracker lets through (clip codes, clipper, a disabled family, a shape
  it does not follow, the engine not at a run boundary) is preceded by `materialiseAll`, which applies the pending
  passes of every slot to VU memory with `applyPass`. The proof shows this writes the microprogram's bits (section
  7, "stages").
* **EE writes** (`noteWrite(qw, lanes)`, to be called from the VIF unpack): written lanes are real again. A pending
  pass that read a written lane from memory is dropped; what it had produced and is not overwritten by the same
  write becomes stale. This is how the fade of the previous layer disappears when the EE rewrites the alpha lane,
  and why the per-corner texture uploads over the texture rows are harmless (they write x, y; the pending
  transform's result is z).
* **Stale lanes**: at the next set-up the pending passes are forgotten; lanes that were still virtual are marked
  stale, meaning VU memory does not hold what the microprogram would have left. A write clears the mark. A stage
  with a stale input gives stale outputs. A record is never built from a stale lane (`stale input`), and an
  executed index-list run that draws one is counted (`staleDraws`): that would be a wrong vertex. It needs a batch
  that draws a slot it has not filled; **0 in all sixteen recordings with real VIF write lists**. The end indices of
  a clip-code list read an alpha lane as a code (2.5); the tracker refuses the run when that lane is not real.

Design decision behind the stale mark: keeping pending passes alive across batches would make the texture matrix
runs pile up forever on the slots no batch fills, and evaluating them on the CPU costs more than the microprogram.
Dropping them is free and is only wrong for a draw from a slot the batch never wrote, which is detected.

Families (`Family` bits, read per call): Strips, Fade, TexMatrix, Grid, PointLight, TexGen, LightTransform,
Primitive, ClipInside. A vertex run is deferred only when all its stages' families are on; with a family off the run
is executed after the bail-out, and later index-list runs of the batch are still taken, from real rows (then the
record has no passes: the "processed vertices" form, which needs no vertex arithmetic on the GPU beyond S, T times
Q). Decline reasons, each with a counter: family disabled, engine state, clip codes, clipper, header shape, grid
shape, light exponent, tag template, index list, corner source, packet buffer, chain length, stale input.

What the integration has to provide: the call before each run (with `Vu1Func::canEnter` as `engineOk`), sending the
record, and `noteWrite` from the VIF unpack with the lanes written (the slow unpack path already computes them as
`writtenLanes`; the fast path knows them from the format and mask). The strip hook must ask
`Tracker::unknownLanes(54, 719)` before it builds a strip record from memory.

## 7. Proof

`DRAKAN_VU_BENCH_BATCH=1` in the VU1 replay (`ps2_vu1_check.cpp`). The replay restores the recorded VU memory before
every run, which would hide what a run that is not executed leaves undone, so the check keeps a memory of its own:
it receives only what the EE wrote between two runs and what the tracker and the runs it lets through write, and a
third interpreter executes those runs on it. Per recording, from the first microprogram call on (the registers of
the runs before it are not the recorded ones):

1. **Stages**: every vertex run without clip codes, its stages evaluated on the snapshot with `applyPass`, compared
   with the memory the microprogram leaves, every lane below the packet buffer.
2. **Records**: every run the tracker takes, `batchToGif` of its record against the packets the microprogram sends
   for the run: tag (vertex count, EOP, PRE, PRIM), packet count and order, and per vertex S, T, Q bits, the four
   colour bytes, the fog byte, X, Y, Z and the skip bit.
3. **Everything else**: every run the tracker lets through is executed on the check's own memory and must send the
   packets it sends from the recorded snapshot (same fields), and before it runs the check's memory must equal the
   snapshot in every lane below the packet buffer that is not marked stale.

The EE's writes are needed lane by lane: a lane rewritten with the value it would have had anyway (uniform water
colours after a point light without a light term; an alpha lane a fade left unchanged) is invisible in the
snapshots. The recorder therefore now writes them (`<file>.writes`, from the VIF unpack), and all scenes were
recorded again with it (8000 runs each, `record_all.sh`). The eight older recordings have no such list; there the
writes are inferred from the snapshots (whole quadwords for positions and colours, pairs for texture coordinates),
which is wrong in exactly the cases above and shows as false stale lanes.

Sixteen recordings with VIF write lists (`summary-new.md`; final code built on `28485c31`; four of them again on `14519428`, the base of the patch, with the same counts):

| | |
| --- | --- |
| vertex runs evaluated against the microprogram's memory | 34,725, 0 lanes differ |
| records | 57,528 |
| vertices compared | 2,604,739, **0 differ** |
| runs executed on the check's memory | 35,473, **0** with other packets, **0** lanes other than recorded |
| executed runs drawing a stale lane | 0 |
| slots brought up to date by the CPU evaluation (bail-out) | 0 |
| lanes that changed without a reported VIF write | 0 |
| strip stage (with the window fix of section 8) | 708,250 vertices, 0 differ |

Scenes: Surdana start, fight, street, town square; Inquisitor's Lair arena, Catacombs, Crystal Mine, Valley of the
Fallen (interior cut scene), Stratos (on foot), Northern Tundra, Andrellian Isles (two cut scenes: Arokh in flight
in rain and lightning; a dialogue close-up in rain), Ravenshold and Shiverbane (both mounted on Arokh), Order of
the Flame Ruined Village and Volcanic.

Declined runs, all sixteen recordings together: only "clip codes" (vertex runs of batches with flag 1) and
"clipper" (index-list runs with a vertex outside). No other reason occurred with all families on.

Stage families switched off (`families.sh`: four recordings times ten masks, among them "nothing", "index lists
only", "everything but index lists", each vertex family alone off): 40 of 40 runs with 0 vertices differing, 0
other packets, 0 lanes other than recorded, 0 stale draws. With index lists off every batch bails out: 128,173 to
474,705 slots per recording were brought up to date by the CPU evaluation and the microprogram then sent the
recorded packets from them.

The eight older recordings (`summary-old.md`, writes inferred): 32,967 vertex runs evaluated, 0 lanes differ;
53,464 records, 2,442,550 vertices compared, 0 differ; but 1,404 index-list runs refused for a stale input, and 250
of the 41,244 executed runs sent other packets (160 of them in Ravenshold, the water). Those are the invisible
rewrites: the same scenes recorded with write lists have none.

Unit tests (`vu_gpu_batch_tests.cpp`; ctest `vu_gpu_batch_policy` in the `runtime-tests` preset and
`vu_gpu_batch.exact` in the VU test directory): 5,600,369 checks. Each helper against the engine's arithmetic (host
floats, rounding toward zero, denormals flushed) on 400,000 operand sets with every operand class: add, sub, mul,
multiply-add, multiply-subtract, square root, reciprocal length, reciprocal, max, min, abs, integer to float, float
to fixed point with 0 and 4 fraction bits. Policy: the stages a flag word selects, each family switch, every
decline reason that can be provoked, pending passes against passes applied to memory, EE writes, stale lanes, index
lists, clip-code lists, the seventeenth strip entry.

## 8. The strip stage's declined runs, and the replay saving

* "Strip table past the vertex window" (3 to 15 % of the strip runs outside Surdana): tables that fill the window
  exactly. The last strip ends at slot 292 and the end mark is read from header slot 293, whose length word (qw 533)
  lies just below the texture coordinates, where the EE puts it. The recogniser refused a next header beyond slot
  291; only vertices have to stay inside. With that test removed every such run is taken: 708,250 strip vertices in
  the sixteen new recordings and 985,073 in the eight older ones, 0 differ.
* "Strip longer than a packet buffer" (one run in three recordings): the first resumed run of a recording, which
  the replay starts with the pc the file holds from its end. Not a run the game makes.
* The single-thread saving smaller than the stage share (study 5.7): not reproduced. Fight recording, best of 7,
  one process after the other: all runs executed 29.10 ms, strip runs built as records 13.82 ms, minus 52.5 %; the
  per-run times of the same pair give 53.1 % for the strip runs (8,044 ns executed against 169 ns to build the
  record). The study's table predates the window fix (72 strip runs still executed) and its two rounds differed by
  30 %; nothing else was found.

## 9. What it buys on the CPU, measured

Share of the VU1 engine time (per-run best of 3, single thread) of the runs that become records
(`summary-new.md`, second table):

| | strips | batch vertex passes | batch index lists | all taken | clip codes and clipper (stay) | set-up, other (stay) |
| --- | --- | --- | --- | --- | --- | --- |
| Drakan 2 on foot (12 recordings) | 7 to 49 | 14 to 36 | 18 to 49 | 53 to 87 | 10 to 42 | 2 to 12 |
| flight: Ravenshold | 0 | 41 | 49 | 90 | 9 | 1 |
| flight: Shiverbane (ice cave) | 19 | 11 | 26 | 57 | 39 | 5 |
| Order of the Flame: Ruined Village, Volcanic | 2, 3 | 61, 46 | 21, 31 | 84, 80 | 12, 17 | 4, 3 |

81 to 97 % of the vertices sent are in taken runs.

That share is not the saving. A record has to be built, and the microprogram's index-list run is already a plain
copy (about 15 ns per vertex in the compiled engine). Single-thread replay, best of 9, the tracker deciding on its
own memory (`timing.txt`; EE writes applied outside the timing; other games were running):

| recording | all executed, ms | strips as records | strips and batch as records | change |
| --- | --- | --- | --- | --- |
| surdana | 9.80 | 3.89 | 3.36 | minus 66 % |
| fight | 11.12 | 5.96 | 4.85 | minus 56 % |
| square | 8.14 | 7.49 | 6.03 | minus 26 % |
| tundra-c | 10.43 | 7.14 | 5.56 | minus 47 % |
| catacombs | 7.24 | 6.28 | 5.24 | minus 28 % |
| ravenshold | 7.54 | 8.16 | 5.95 | minus 21 % |
| shiverbane | 12.94 | 12.88 | 9.17 | minus 29 % |
| ootf-ruined | 3.59 | 3.76 | 2.39 | minus 33 % |
| ootf-volcanic | 5.12 | 6.45 | 3.12 | minus 39 % |

Per run (Ravenshold, square): a deferred vertex run costs 70 to 900 ns instead of 390 to 7,300 ns executed; an
index-list run costs about 870 ns as a record against 750 ns executed. So **the vertex passes are the saving of the
batch path on the VU1 thread; the index-list runs break even at best** with a record that copies 64 bytes per
vertex, the same amount the packet holds. The first version of the builder cost twice the microprogram and made
Ravenshold slower than executing everything (7.41 to 8.79 ms).

Bail-out is expensive: with index lists off (every batch brought up to date by the double arithmetic of the CPU
evaluation) the same replay takes 13 to 36 ms, three to five times the microprogram. It has to stay the exception;
with all families on it did not happen once in the sixteen recordings.

## 10. Estimate

* **Exactness**: every batch stage without clip codes is a per-vertex function and was reproduced bit for bit;
  nothing in the batch path fails that test. The parts that stay on the CPU are the ones the study named: batches
  with clip codes (their vertex runs and their index-list runs with a vertex outside), 9 to 42 % of the engine time
  per recording (highest indoors and close to walls: Valley 42, Stratos 39, Shiverbane 39), plus the set-ups.
  Index-list runs of clip-code batches whose vertices are all inside can be taken (`FamClipInside`), but they are
  few: 0 to 196 of 122 to 1,177 such runs per recording.
* **More expensive than the study assumed**:
  1. The index-list run, 18 to 49 % of the engine time, gives no CPU saving by itself (section 9). Getting one
     needs a leaner hand-over than a self-contained record per run: the rows of a batch sent once (48 bytes per
     slot, and 16 again when the EE rewrites alphas for a layer), and per index-list run only indices, per-corner
     texture coordinates and the pass list. Not built.
  2. The tracker needs the EE's writes into VU memory lane by lane, from the VIF unpack (also from its fast path).
     Without them it cannot tell a rewritten lane from an untouched one, which the old recordings show as stale
     inputs.
  3. The vertex function is a chain whose length depends on the draw: 1.1 to 6.7 passes per record on average, up
     to 13 (19 in an older recording), because the texture matrix is cumulative over the terrain layers. A shader
     is therefore a loop over a pass list with a switch per kind, or a variant per chain shape; with the exact
     arithmetic that is several times the strip shader's work per vertex for the upper terrain layers.
  4. World geometry (flags 0x0C, 0x14 and their fades) has a texture coordinate per corner: 40 to 97 % of the
     records outside the Order of the Flame levels have one vertex per index, so an index buffer only pays for
     terrain and water.
  5. What VU memory holds after a batch that was not executed is not what the microprogram would have left (stale
     lanes). A draw from such a lane is detected and counted, not repaired. It did not occur.
* **Cheaper than feared**: no reject rule or neighbour dependency in the unclipped index-list run; the skip bit is
  ruled out by the constants for every record seen; no flat shaded draw, no primitive other than triangle strips;
  the grid generator needs no window of its own, only two values per vertex; the hand-over between runs is the
  three arrays and nothing else.
* **Not covered**: a shader for any of it; the run-time hook and the VIF write notifications (the recorder uses the
  slow unpack path while recording, the product would need the fast path to report lanes); `gsr_core` handling of
  the Batch record; live frames; the USA disc; mounted flight was recorded in two levels only, spell effects not at
  all (the weather of the Isles cut scene is the nearest thing).
