# The batch path as a generated vertex stage (2026-11-12)

Follows [gpu-vertex-stages.md](gpu-vertex-stages.md) (the stage description, the one arithmetic source, generated
HLSL and WGSL) and [gpu-vertex-batch-path.md](gpu-vertex-batch-path.md) (the batch path of the microprogram, the
record, its CPU evaluation `gsr/vertex_batch.h`). This note records the batch evaluation written in the stage
description, its generated shaders and their proof. Nothing here is wired into a renderer; the default behaviour of
the game is unchanged. Written by the `bstage` subagent of the GPU vertex work. Paths under
`build/review/gpu-vertex2/bstage` are untracked scratch.

## 1. What exists

| File | What |
| --- | --- |
| `runtime/gs/include/gsr/vertex_stage.h` | the description gained a carried state: `Op::State` (row, lane) reads it, `Stage::state` names the node each of the twelve lanes takes (none: the lane keeps its value); `Evaluator::apply` runs such a pass stage in place, `Evaluator::stateRaw` a stage that ends a chain |
| `runtime/gs/include/gsr/vertex_batch_stage.h` | the ten pass kinds and the finishing stage as lists (`ir::batchPass(kind)`, `ir::batchFinish()`), the loop over a pass list (`ir::forEachPass`), the CPU evaluation `ir::BatchEvaluator<Arith>` |
| `runtime/gs/src/vertex_stage_emit.h` | `emitBatch`: one function per pass kind, the loop, the finishing function, in HLSL and WGSL; `planBatch`: which lanes and kinds a set of packet fields needs. The body of a stage function is now written by one routine for strips and passes |
| `runtime/gs/src/vertex_program_hlsl.h`, `vertex_program_wgsl.h` | `vertexBatchHlsl`, `vertexBatchWgsl`: structures and entry points around the generated text |
| `runtime/gs/tests/vertex_batch_cases.h` | the vertices every test shares, and the references taken from `gsr/vertex_batch.h` alone |
| `tests/test_vertex_batch_stage.cpp` (ctest `vertex_batch_stage`) | the description on the CPU against `vp::applyPass`, `vp::evaluateVertex`, `vp::batchToGif` |
| `tests/test_vertex_batch_program.cpp` (`vertex_batch_program`, `vertex_batch_program_warp`) | generated HLSL, both libraries, by stream output on Direct3D 11 |
| `tests/test_vertex_batch_webgpu.cpp` (`vertex_batch_webgpu_vulkan`, `vertex_batch_webgpu_d3d12`) | generated WGSL by a compute pass |

## 2. The description

A pass kind is an ordinary operation list built with `Builder`. Its inputs:

* constants: the thirteen rows of its `vp::Pass` exactly as the record holds them. Row 0 is the header (kind,
  param, first, count), rows 1 to 12 are `k[0..11]`. The squaring count of the point light is `Constant(0, 1)`;
* the carried row: `State(StatePos | StateCol | StateSt, lane)`, raw bits as VU memory holds them;
* the vertex: attribute row 3 = (pass mask, grid x, grid z, first row of the record's pass table), the fourth row
  of a `vp::BatchVertex` with its `slot` word replaced.

Its outputs are state lanes. Sizes: Grid 13 operations, GridFlat 12, PointLight 133, TexGenInt 34, TexGenPos 34,
Light 118, Transform 111, Project 63, Fade 13, TexMatrix 22, the finishing stage 16.

The chain of a vertex is not unrolled. The generated text holds each pass function once and one loop:

```
row = the vertex's raw row; todo = pass mask
repeat at most 24 times while todo != 0:
    base = table row + 13 * (index of the lowest set bit of todo); clear that bit
    kind = vpConstant(base).x
    row = the pass function of that kind (row, sel, base)       (a value that is no kind: unchanged)
packet = finish(row)
```

`ir::forEachPass` is the same loop on the CPU, and `BatchEvaluator` runs it with one `Evaluator` per pass of the
record (the part of a pass that depends on its constants alone runs once per record). The loop needs no pass
count: a record never sets a mask bit at or above its pass count (`vp::parseBatch` refuses one).

The squarings of the point light are eight guarded multiplications, `select(i < count, c * c, c)`, as
`vertex_batch.h` writes them. No control flow was added to the description.

The motion-vector twin: `planBatch(kPositionFields)` starts from the lanes the finishing stage reads for X, Y, Z
and adds what the passes that write them read, until nothing is added. Result: the four lanes of the position row
and the kinds Grid, GridFlat, Transform, Project. `emitBatch(language, kPositionFields, "Previous")` emits exactly
those, and the test shows that the position of a vertex does not depend on its
colour and texture rows.

### Where the description is not a word-for-word copy of `vertex_batch.h`

Each case is a test vector in `vertex_batch_stage`.

* **MAX and MINI on raw registers.** The engine compares raw register bits and passes one operand through; a NaN
  is neither greater nor smaller than anything, so the second operand comes out. `vuMax` and `vuMin` of the library
  take operands, where a NaN already is the largest finite value. They differ whenever a NaN is involved (3,727 of
  182,329 register pairs of every class, for each of the two; 0 pairs without a NaN: infinities, denormals and
  signed zeros give the same operand form either way). Examples: max(100, -NaN) is the NaN (read as the most
  negative value) in the engine and 100 in the library; min(100, +NaN) is the NaN (the largest value) in the engine
  and 100 in the library. Two places read a register raw: the depth clamp of the transform (`k[4].y`, `k[4].z`)
  and the height limits of the point light (`k[3].y`, `k[3].z`). The description writes them as a select on an
  explicit "is a NaN" test (`batchdetail::rawMax`, `rawMin`) and carries the operand form.
  **The strip stage has the same difference from the engine**: `vp::evaluate` and `buildStrips` clamp the depth
  with operands (`max(min(w, operand(fog.z)), operand(fog.y))`). With a NaN in the fog clamp constants the
  microprogram would give another fog lane than the strip record path. Not observed in any recording (the constants
  are ordinary floats); not changed here.
* **The reciprocal length** of the point light is zero for a zero length; `vuRsqrt` gives the largest finite value
  for a zero radicand. Written as `select(length == 0, length, 1 / sqrt(sum))`.
* **The square root.** `sqrtZero` returns its operand for -0 and is undefined below zero; `vuSqrt` takes the
  magnitude. The only root is of a sum of squares, which is zero or positive and never -0, where the two agree.
* **Identities.** `x * 1.0` and `x - 0` of a value that is an operand are left out, `x * -1.0` is a sign flip,
  `acc + n * 1.0` is a sum. `0 + q` (the transform's 1 / w) is kept as a sum: it turns -0 into +0.

### A fault this found in the double library of Direct3D 11

`vuAdd(literal 0, y)` returned y for y = -0 where the rule gives +0: FXC folds the double sum of a literal zero
and y to y, also under `precise` and `D3DCOMPILE_IEEE_STRICTNESS`. 3,298 of 120,240 generated vertices differed
(the transform's `0 + q` with q = -0, and S, T through it). The strip stages have no sum with a literal zero, so
they were never affected. Fix in `kHlslDoubleCore`: two zero operands are decided in integers
(`(a | b) & 0x7FFFFFFF == 0 ? a & b : ...`). The strip test was run again with the changed library: 1,215,215
vertices, 0 differ. The integer library never had the fault.

## 3. Proofs

Inputs of every path: 120,240 generated vertices (30,000 of one pass alone, 3,000 per kind, half of them with
arbitrary operands, NaN, infinities, denormals and signed zeros in the registers MAX and MINI read raw, a zero and
a nearly zero length, every squaring count from 0 to 9; 30,240 of the chains a flag word of the dispatcher
selects, with up to 24 passes; 60,000 of random chains of 1 to 24 passes in any order with any subset per vertex),
and with `GSR_VB_RECORDS` the 2,330,816 record vertices of the 57,528 records of all sixteen recordings with VIF
write lists (Surdana, street, square, fight, arena, Catacombs, Crystal Mine, Tundra, Valley, Stratos, two Isles
scenes, Ravenshold and Shiverbane in flight, Order of the Flame Ruined Village and Volcanic): 2,451,056 vertices,
2.53 passes per vertex, 10,493 of them with 24 passes.

The reference of every row is `gsr/vertex_batch.h` itself (`vp::applyPass` in the loop of `vp::evaluateVertex`,
`vp::finishRow`), not the evaluator of the description. A GPU path returns per vertex the twelve words of the
packet (X, Y, Z, the whole fog lane with bit 15, R, G, B, A, the bits of S, T, Q), the position evaluated from
"previous frame" inputs (another position row, grid values, pass mask and pass table), and the row after the
chain (eleven lanes; lane w of the texture row is read by nothing and not carried).

| Path | Vertices | Differ |
| --- | --- | --- |
| `BatchEvaluator`, reference arithmetic and integer library: all twelve lanes of the row, the packet, the fields of `vp::evaluateVertex` | 2,451,056 | 0 |
| the same per pass kind alone, each of the ten kinds | 3,000 each | 0 |
| packets of the dumped records against `vp::batchToGif`, both arithmetics | 2,604,739 packet vertices in 262,532 packets | 0 |
| the previous position with other colour and texture rows | 2,451,056 | 0 |
| Direct3D 11, doubles: RTX 5080, AMD Radeon integrated, WARP | 2,451,056 each | 0 |
| Direct3D 11, limbs: RTX 5080, AMD Radeon integrated, WARP | 2,451,056 each | 0 |
| WebGPU (wgpu-native) on Vulkan, limbs, RTX 5080 | 2,451,056 | 0 |
| WebGPU (wgpu-native) on Direct3D 12, limbs, RTX 5080 | 2,451,056 | 0 |

Mutation checks (generated vertices, then reverted): the texture matrix's last sum turned into a difference in the
description: `vertex_batch_stage` fails 29 checks (TexMatrix alone: 2,942 of 3,000 rows), Direct3D 11 and WebGPU
report 46,907 of 120,240 vertices. The emitter writing a multiply-add for a multiply-subtract (the description
untouched): `vertex_batch_stage` passes, Direct3D 11 and WebGPU report 79,859 of 120,240.

## 4. What a draw needs (for the backends)

**Vertex buffer**, one vertex per record vertex, 64 bytes, the `vp::BatchVertex` of the record copied as it is
with one word patched:

| Offset | Semantic (HLSL) / member | Format | Content |
| --- | --- | --- | --- |
| 0 | `VBPOS` / `pos` | 4 x uint32 | position row, raw |
| 16 | `VBCOL` / `col` | 4 x uint32 | colour row (integers, or an integer normal before lighting), raw |
| 32 | `VBST` / `st` | 4 x uint32 | texture row, raw |
| 48 | `VBSEL` / `sel` | 4 x uint32 | x = `passMask`, y = `gridX`, z = `gridZ` (float bits), w = **row of the record's first pass in the constants table** (replaces `slot`) |

Everything is an integer attribute (`DXGI_FORMAT_R32G32B32A32_UINT`, `uint32x4`); nothing may be converted.

**Constants**: the record's `Pass` array copied as it is into the table `vpConstant(row)` reads (Direct3D 11:
`Buffer<uint4> VpConstants`, t8, the table of the strip records; WebGPU: a storage buffer, or `textureLoad` of an
`rgba32uint` texture where a vertex stage has no storage buffers). A pass is thirteen rows; pass i of a record
starts at row `sel.w + 13 * i`. The table row is free: strip constants (sixteen rows per run) and pass tables can
share one table. Records with the same GS state batch into one draw, each vertex naming its own table row.

**Indices**: the record's strips are runs of 16-bit indices into its vertices; a strip of n indices is the
triangles (i, i + 1, i + 2), i = 0 to n - 3, plus the record's first vertex in the buffer, as a triangle list like
the strip records (`VertexProgramBatch`). Per-corner texture coordinates and colours need nothing further: the
record builder keys a vertex by (position slot, texture slot, colour slot) and assembles its row from the three,
so a record of world geometry simply has one vertex per index, and terrain and water share vertices between
strips. A record with `kBatchEmptyTag` sends an empty tag after its strips (`batchToGif` shows the packets); what
`gsr_core` has to leave in its vertex queue after such a record was not looked at here.

**Before taking a record**: every Transform or Project pass must satisfy `vp::passNeverSkips` (true for all
57,528 recorded records), else the "do not draw" bit is possible and the record is evaluated on the CPU. A record
without any such pass (the "processed rows" form, also with no pass at all) is drawn by the same shader: the loop
runs zero times and the finishing stage multiplies S and T by Q.

**Entry points**: `VSBatch` (HLSL, compiled behind `kHlslMain`: the packet goes through the ordinary `VSMain`, as
`VSProgram` does for strips; the XYOFFSET comes from `CBProgram`), or `vbEvaluate(VbRow(pos, col, st), sel)` in
WGSL. `VSBatchRaw` is the integer form the test reads.

**The previous-frame twin for motion vectors** (compile with `VP_PREVIOUS`; WGSL `vbEvaluatePrevious`): two more
attributes, which may sit in a second vertex stream:

| Offset | Semantic / member | Content |
| --- | --- | --- |
| 64 | `VBPPOS` / `ppos` | the position row the same vertex had in the previous frame's record, raw |
| 80 | `VBPSEL` / `psel` | x = its pass mask then, y, z = its grid values then, w = the row of the previous frame's pass table |

`vbPreviousPlace` returns X, Y, Z from them with the position passes alone (Grid, GridFlat, Transform, Project);
bits of the mask that name other passes are skipped, so the previous record's mask is passed as it is. The
previous frame's passes have to be in the table of this frame (thirteen rows per pass; only passes of those four
kinds are read). Which record of the previous frame is "the same object" is the backend's matching, as for
strips; a vertex without a previous record takes its own inputs.

## 5. Cost

FXC (`D3DCOMPILE_OPTIMIZATION_LEVEL3`, the renderer's flags), time over five compilations on the loaded machine:

| Entry point | Doubles | Limbs |
| --- | --- | --- |
| `VSBatchRaw`, the packet | 5.3 to 6.1 s, 137,168 bytes | 13.7 to 24.1 s, 223,780 bytes |
| `VSBatchRaw` with the previous position | 8.6 to 10.2 s, 183,100 bytes | 22.9 to 29.5 s, 296,100 bytes |
| `VSBatch` (the draw: batch path, then `VSMain`) | 5.3 to 6.8 s, 138,504 bytes | 14.3 to 20.6 s, 225,116 bytes |
| the test's entry point (previous position and row, IEEE strictness) | 8.1 to 11.7 s (38 s once) | 22.8 to 38.0 s (75 s under load) |

That is two to three times the strip entry point, once per machine with the shader cache: it has to be compiled in
the background, with records evaluated on the CPU (or left to the microprogram) until it is there. The time
follows the size, not the loop: one pass kind alone compiles in 0.7 s to 48 KB; the same text without the point
light takes 8.4 s, without point light and light 3.4 s. `[branch]` on the dispatch or a `[call] switch` change
neither the code nor the time. Levers not taken: the transform as "matrix, then the shared project function"
(one projection less), the squarings as a real loop.

wgpu-native (module with the full and the position set, 34,179 characters; the module itself parses in 3 to 5 ms):
the compute pipeline of the test 1.9 s and of the packet alone 1.3 s on Vulkan the first time (2 to 5 ms once the
driver has cached it); 2.8 to 5.5 s and 2.8 to 4.4 s on Direct3D 12 with DXC, every time.

Shader invocations per millisecond on the RTX 5080, chains as a record typically has them (1: transform; 3: grid,
texture generation, transform; 7: those, three texture matrix runs, fade; 13: those with the point light and eight
texture matrix runs). The GPU was shared (`nvidia-smi` at the start of the runs: 25, 98 and 99 % busy for
Direct3D 11, 72 to 83 % for WebGPU), so these are orders of magnitude; median of three runs, each the median of
five timings:

| Path | 1 pass | 3 | 7 | 13 |
| --- | --- | --- | --- | --- |
| Direct3D 11 doubles, packet | 390,000 | 227,000 | 135,000 | 82,000 |
| Direct3D 11 doubles, packet and previous position | 140,000 | 164,000 | 94,000 | 65,000 |
| Direct3D 11 limbs, packet | 1,620,000 | 1,810,000 | 1,980,000 | 1,240,000 |
| Direct3D 11 limbs, packet and previous position | 1,530,000 | 1,070,000 | 710,000 | 370,000 |
| WebGPU Vulkan limbs, packet (compute) | 2,560,000 | 2,120,000 | 1,610,000 | 610,000 |
| WebGPU Direct3D 12 limbs, packet (compute) | 1,200,000 | 2,140,000 | 1,100,000 | 890,000 |

Direct3D 11: stream output, eight draws of 262,144 vertices appended to one buffer per round, the last vertex read
back and checked against the CPU evaluation after every round, 16 to 1,024 draws per timing; the adapter's
pipeline statistics count every invocation. Short chains with limbs are not limited by the shader (the rate does
not fall from 1 to 7 passes). WebGPU: eight dispatches of 262,144 per timing (1 to 4 ms), which is short; the
spread within a run was up to a factor of four. Whole proof runs, with upload and full read back: 2,451,056
vertices in 66 to 141 ms on the RTX 5080 on every path, 0.5 to 1.1 s (doubles) and 1.0 to 2.8 s (limbs) on WARP,
0.6 to 3.0 s and 0.3 to 2.1 s on the integrated AMD adapter.

CPU, one thread: the oracle (`vp::applyPass` chain, packet, previous position) 860 ns per vertex on the recorded
mix (2.5 passes per vertex).

## 6. Not established

* A draw through any renderer: the tests return integers by stream output and by a compute pass. `VSBatch` (the
  entry point of a draw) was compiled but not compared with `VSMain` on the CPU evaluation as `VSProgram` is.
* The generated WGSL in a vertex stage, a browser (Dawn / Tint), Linux, an Intel adapter, AMD or Intel through
  WebGPU.
* The NaN rule of MAX and MINI in the strip stage (section 2) is stated from reading the code, not from a run.
* Timings on an idle GPU.

## 7. Reproduce

```sh
cmake --preset runtime-tests -B <tree>
cmake --build <tree> --config Release --target gsr_test_vertex_batch_stage gsr_test_vertex_batch_program gsr_test_vertex_batch_webgpu
# records of a recording, from a play folder: DRAKAN_VU_BENCH=<file.vur>,1 DRAKAN_VU_BENCH_BATCH=1 DRAKAN_VU_BENCH_BATCH_DUMP=<file> drakan.exe
export GSR_VB_RECORDS="<dump>;<dump>;..."
<tree>/bin/Release/gsr_test_vertex_batch_stage.exe
<tree>/bin/Release/gsr_test_vertex_batch_program.exe                               # GSR_VP_WARP=1, GSR_VP_ADAPTER=<n>, GSR_VB_COST=1, GSR_VB_BENCH=5
DRAKAN_GPU_BACKEND=vulkan <tree>/bin/Release/gsr_test_vertex_batch_webgpu.exe      # or dx12; GSR_VB_BENCH=5
```
