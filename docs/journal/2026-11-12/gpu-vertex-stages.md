# Vertex stages described once: exact arithmetic for every backend (2026-11-12)

Follows `gpu-vertex-feasibility.md` (sections 3.2, 4.4, 9). That study had the strip stage as one hand-written HLSL
shader on double arithmetic and no shader for WebGPU. This note records what replaced it: one description of a
stage, one arithmetic library source, generated HLSL and WGSL, one test suite. Nothing here is wired into a
renderer; the default behaviour of the game is unchanged.

## 1. What exists

| File | What |
| --- | --- |
| `runtime/gs/include/gsr/vu_exact.inl` | the VU's float arithmetic on the bits of a float, in 32-bit unsigned integers only, written once in the subset C++, HLSL and WGSL share |
| `runtime/gs/include/gsr/vu_limbs.h` | that source compiled as C++ (`gsr::vp::limbs`) and kept as text (`kLibrary`) for the generators |
| `runtime/gs/include/gsr/vertex_stage.h` | the description of a stage (`vp::ir`: operation list, `Builder`), the CPU evaluator, the two strip programs as descriptions, the arithmetic choice (`vp::chooseArithmetic`) |
| `runtime/gs/src/vertex_stage_emit.h` | text generators: library and stage bodies in HLSL (double or integer library) and WGSL (integer library) |
| `runtime/gs/src/vertex_program_hlsl.h`, `vertex_program_wgsl.h` | the bindings and entry points around the generated text |
| `runtime/gs/tests/vertex_program_cases.h` | the vertices every test shares, and three probe stages that use every operation the strips do not |
| `tests/test_vertex_stage.cpp` (ctest `vertex_stage`) | the library and the evaluators on the CPU |
| `tests/test_vertex_program.cpp` (`vertex_program`, `vertex_program_warp`) | generated HLSL, both libraries, by stream output on Direct3D 11 |
| `tests/test_vertex_stage_webgpu.cpp` (`vertex_stage_webgpu`, `_vulkan`, `_d3d12`) | generated WGSL by a compute pass |

Shader text is generated at start-up; nothing generated is in the tree, and the Direct3D 11 shader cache keys on
the text.

## 2. The rules of the arithmetic

Reference: the engine in `ps2recomp/ps2xRuntime/src/lib/vu` (`normalizeOperand`, `normalizeResult`, the DIV, SQRT
and RSQRT cases of the lower unit, MAX / MINI and the fixed-point conversions of the upper unit).

* Operands: a denormal is a signed zero, an infinity or NaN the largest finite value of its sign.
* Every result is the exact result rounded toward zero; above the finite range the largest finite value, below the
  normal range a zero with the sign of the exact result. An exact zero sum is +0 unless both operands are -0.
* A multiply-add or multiply-subtract rounds twice (product, then sum).
* DIV: a zero divisor gives the largest finite value with the sign of the quotient, for 0 / 0 as for x / 0.
* SQRT takes the magnitude. RSQRT is a division by that rounded root; a zero radicand gives the largest finite
  value with the sign of the numerator.
* MAX and MINI return their second operand unless the first is strictly greater (smaller); -0 equals +0.
* Float to fixed point (0, 4, 12, 15 fraction bits) truncates and saturates; integer to float truncates.

How the integer library does it: add aligns the smaller operand under the larger with seven guard bits, and a
difference takes one more unit when the shift lost bits (the floor of x - y is x - ceil(y)); multiply forms the
24 x 24 bit product from 12-bit halves; divide is a long division of the significands, eight quotient bits per
32-bit division; the square root is the digit-by-digit method over the 48-bit radicand with a remainder that stays
below 2^26. Rounding toward zero is truncation, so no sticky bit is needed anywhere else.

## 3. One description

A stage is a list of operations (`vp::ir::Op`): `Imm`, `Constant` (row, lane), `Attribute` (row, lane); `Operand`,
`Neg`, `Abs`, `Add`, `Sub`, `Mul`, `Madd`, `Msub`, `Div`, `Sqrt`, `Rsqrt`, `Max`, `Min`, `Itof` and `Ftoi` with 0 to
15 fraction bits; `Less`, `LessEqual`, `Equal`, `Select`; the integer operations `And`, `Or`, `Xor`, `IAdd`, `ISub`,
`Shl`, `Shr`, `IEqual`, `ILess`. Outputs are the packet fields S, T, Q (float bits), R, G, B, A (low 8 bits), X, Y
(low 16 bits), Z and the fog lane (the whole fixed-point word: bits 4 to 11 fog, bit 15 "do not draw"). Loops are
C++ loops around `Builder` calls, so a list is straight-line; identical operations are stored once. `StripsFlat` is
108 operations, `StripsLit` 218.

From a list come the CPU evaluator (`vp::ir::Evaluator`, with the double arithmetic of `vertex_program.h` as the
reference or the integer library; the part that depends on the constants alone runs once per run), and the shader
functions `vpEvaluate(VpIn)` and `vpEvaluateAt(VpIn, constantsIndex)`. `Evaluator<DoubleArithmetic>` is the
documented oracle; the hand-written `vp::evaluate` stays as its twin and `vertex_stage` pins the two.

Motion vectors: a list names no particular set of constants, so the same stage is evaluated a second time with the
previous frame's constants (`vpEvaluateAt` with `sel.z`), and a field mask keeps only the operations the position
needs. The tests run this twin on every vertex.

## 4. Proofs

Inputs of every GPU path: 400,000 generated strip vertices (every fourth with arbitrary operands), 200,000 probe
vertices, and with `GSR_VP_RECORDS=build/review/gpu-vertex/records/all.rec` the 615,215 recorded strip vertices of
six scenes: 1,215,215 vertices, each compared in the twelve words of its packet and in the position evaluated again
with other constants.

| Path | Vertices | Differ |
| --- | --- | --- |
| integer library against double arithmetic and against the processor rounding toward zero, 18 operations | 400,000,000 operand triples | 0 |
| `Evaluator<LimbArithmetic>` against `Evaluator<DoubleArithmetic>` | 1,215,215 | 0 |
| `vp::evaluate` against the evaluator (strip programs) | 1,015,215 | 0 |
| Direct3D 11, doubles: RTX 5080, AMD Radeon integrated, WARP | 1,215,215 each | 0 |
| Direct3D 11, limbs: RTX 5080, AMD Radeon integrated, WARP | 1,215,215 each | 0 |
| WebGPU (wgpu-native) on Vulkan, limbs, RTX 5080 | 1,215,215 | 0 |
| WebGPU (wgpu-native) on Direct3D 12 (DXC), limbs, RTX 5080 | 1,215,215 | 0 |
| the draw's entry point against `VSMain` on the CPU evaluation, without and with a temporal jitter, both libraries, three adapters | 1,215,215 each | 0 |

## 5. Cost

Lit strip program, 8 x 262,144 vertices per run, the GPU shared with other sessions (18 to 86 % busy by
`nvidia-smi` at the start of a run), so these are orders of magnitude:

| Path | Shader invocations per millisecond, medians of the runs |
| --- | --- |
| Direct3D 11 doubles, RTX 5080 | 159,000 to 221,000 (five runs) |
| Direct3D 11 limbs, RTX 5080 | 336,000 to 3,336,000, median 2,136,000 (five runs) |
| WebGPU Vulkan limbs, RTX 5080 (compute) | 1,320,000 to 3,306,000 (three runs) |
| WebGPU Direct3D 12 limbs, RTX 5080 (compute) | 1,043,000 to 2,713,000 (three runs) |
| Direct3D 11 doubles / limbs, AMD Radeon integrated | 10,200 / 25,200 |
| Direct3D 11 doubles / limbs, WARP | 12,200 / 7,400 |

The integer library is about ten times faster than doubles on the RTX 5080 and 2.5 times on the integrated AMD
adapter; only the software adapter prefers doubles. `vp::chooseArithmetic` therefore takes the integer library
everywhere unless a diagnosis switch asks for doubles.

FXC, the game's two stages (`D3DCOMPILE_OPTIMIZATION_LEVEL3`): doubles 2.2 to 2.6 s and 77 KB of bytecode, limbs 5.9
to 6.5 s and 143 KB. That is once per machine with the shader cache, but too long to wait for on the render thread:
compile in the background and evaluate on the CPU until the shader is there. FXC's time grows much faster than the
instruction count: with the select and the count of leading zeros spelled out in portable integer code the same
text took 40 s (and 253 s with the probe stages), which is why those two are intrinsics of each shader language.
wgpu-native, module and pipeline with the probe stages: 0.8 to 1.8 s on Vulkan (5 ms once the driver has cached
it), 1.7 to 4.2 s on Direct3D 12 with DXC.

CPU evaluation, one thread, per vertex: 440 to 730 ns with either arithmetic (the machine was loaded). A CPU path
that is to be played on wants the processor's own rounding mode instead (alternative B of the study).

## 6. The jitter bit

Cause of the 61 of 1,015,215 positions that differed with a temporal jitter: `VSMain` computed
`1.0 - y / h * 2.0`, then added the jitter, without `precise`. FXC reorders such a three-term sum, and it chose
differently in two compilations of the same function: `(-(y / h) * 2 + jitter) + 1` in the stand-alone `VSMain`,
`(-(y / h) * 2 + 1) + jitter` where `VSMain` was inlined into the old program shader (visible in the disassembly of
both). Two roundings in another order differ in the last bit in rare cases (the test's jitter values are short
binary fractions, so most sums are exact). `VSMotion` inlines `VSMain` in the same way.

Fix: `precise` on the position sums of `VSMain`, written in the order FXC had given the stand-alone shader, so the
ordinary shader's results do not change (shown: the new entry point equals the previous `VSMain` text on all
1,215,215 jittered vertices) and every shader that inlines it gets the same instructions. The entry points now also
agree when only one of them is compiled with `D3DCOMPILE_IEEE_STRICTNESS`.

## 7. Not established

* Any browser (Dawn / Tint), Linux, an Intel adapter, AMD or Intel through WebGPU.
* The generated WGSL in a vertex stage (the test is a compute pass), and a draw through any renderer.
* The WGSL twin of the `VSMain` change (`gs_shaders_wgsl.h` is being restructured): WGSL has no `precise`; a
  proposal is in the hand-over, untested.
* That `VSMotion` and `VSMain` actually disagreed before the fix (inferred from the same inlining).
* Frame hashes of the ordinary Direct3D 11 renderer before and after the `VSMain` change.
* Timings on an idle GPU.

## 8. Reproduce

```sh
cmake -S . -B <tree> -A x64 -DDRAKAN_BUILD_RECOMPILER=OFF -DGSR_DLSS=OFF
cmake --build <tree> --config Release --target gsr_test_vertex_stage gsr_test_vertex_program gsr_test_vertex_stage_webgpu
export GSR_VP_RECORDS=build/review/gpu-vertex/records/all.rec
GSR_VU_PAIRS=50000000 <tree>/bin/Release/gsr_test_vertex_stage.exe            # 400,000,000 triples
<tree>/bin/Release/gsr_test_vertex_program.exe                               # GSR_VP_WARP=1, GSR_VP_ADAPTER=<n>, GSR_VP_BENCH=15
DRAKAN_GPU_BACKEND=vulkan <tree>/bin/Release/gsr_test_vertex_stage_webgpu.exe  # or dx12
```
