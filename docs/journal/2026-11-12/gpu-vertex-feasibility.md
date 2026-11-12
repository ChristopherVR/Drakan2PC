# GPU vertex processing: feasibility, one working program, cost of the whole job (2026-11-12)

Question from the owner: "Can we update the game to be more GPU heavy than CPU?" The idea evaluated here: stop running
the PS2 vector unit's microprogram on the CPU for every vertex, recognise what a VU1 run is about to do, and hand its
inputs to the renderer as a draw whose vertex shader repeats the program's arithmetic.

This is a study with one prototype, not a rewrite. Scratch data, recordings, captures, runs, the private build tree
and the integration patch are under `build/review/gpu-vertex/` (not tracked).

## 0. Short answer

* The game uploads **one** VU1 microprogram (1523 instruction pairs, two entry points). What varies is the stage a run
  executes. Twelve stages cover everything; two groups carry the cost: the **strip stage** (objects wholly inside
  the view: characters, props, trees) and the **batch path** (terrain, world geometry, and every object that touches
  the edge of the view), see section 2.
* The strip stage was prototyped end to end on Direct3D 11. Its vertex shader is **bit identical** to the
  microprogram for every field a GS reads: 895,640 recorded vertices of seven scenes against the engine's own
  packets, 1,015,215 vertices on the GPU against the CPU evaluation, every frame of three captures (4,379 frames,
  0 different), and matched paused frames in three levels (sections 5.3 to 5.6).
* Exactness is not free: the VU rounds every operation toward zero. With ordinary float arithmetic in the shader
  0.29 % of the recorded vertices move by a sixteenth of a pixel and 41 % get another depth or fog value. The shader
  therefore rebuilds the VU rounding from double arithmetic, which Direct3D 11 offers as an optional feature and WGSL
  does not offer at all (section 3).
* Measured saving of the one stage (5.7): a third of the VU1 work per frame in the Surdana still view and 40 % in a
  fight (exact counts), 30 to 35 % of the VU1 worker's CPU time and 14 to 17 % of the GS worker's there; 11 to 23 %
  in two other views; nothing in flight and nothing in the converted Order of the Flame level, because those draw
  through the batch path. **A frame rate gain was not established:** on the shared machine, whose GPU other games
  kept busy, the frame rate did not follow the worker times outside the spread between repeats.
* The whole job means converting the batch path, which is four to six times the prototype's work and carries the
  real risks (section 6). Recommendation: **go in stages**, starting with one measurement on a quiet machine and
  the cheap independent step (section 8).

## 1. Conditions

* Machine: Ryzen 7 9800X3D, RTX 5080, shared all day with other sessions. What ran beside each measurement is given
  with it. Exact results (vertex comparisons, frame hashes, pair counts) do not depend on the load; times do.
* Private tree: a git worktree of `93327bc0` with the prototype, built by `Build-LocalGame.ps1 -Incremental` into
  `build/review/gpu-vertex/work` and `.../play` (a copy of `build/review/baseline-play`, ELF read from a private
  copy). Nothing was run from or written to `dist/DrakanPC` or `iso/`.
* Settings: the owner's ini with two changes: a 1280x960 window (internal scale 3) and **DLSS off**
  (`ini/windowed.ini`), and the 3840x2160 borderless original with DLSS off (`ini/owner4k.ini`). HD textures on,
  ray effects, terrain detail and material detail off, foliage wind on unless stated. The prototype only takes draws
  while none of those features is on (5.2).
* Scenes: the still views of `scripts/perf/cpu_scene_bench.py` and `scripts/perf/scene_ab_bench.py` (New Game on an
  empty card): `surdana` (Level 1A start), `surdana-town`, `surdana-fight` (three Wartoks at the cave mouth),
  `arena-1e` (interior, a Wartok in front of the camera), `tundra-c`, `catacombs` (interior), `ravenshold` (mounted on
  Arokh) and `ootf-ruined` (Order of the Flame, Ruined Village).

## 2. Inventory: one program, twelve stages

Read from the local listing (`analysis/vu`, not tracked) and measured with the VU1 recorder and replay of the CPU
session (`DRAKAN_VU_RECORD` / `DRAKAN_VU_RING`, `DRAKAN_VU_BENCH`), extended here by one line of facts per run
(`DRAKAN_VU_BENCH_RUNS=<csv>`: entry and exit pc, flag word, batch header, packets, vertices, pairs, best time) and
`scripts/perf/vu_run_classes.py`, which names the stage of a run.

### 2.1 What the program is

One image of 1523 pairs (0x2F98 bytes), uploaded once, entries at 0x0000 and 0x26E0. Every later MSCNT resumes after
the E bit where the previous run stopped, so a "program" in the sense of this study is a stage between two stops,
selected by the pc and by the flag word in VU memory quadword 7. The same image is on the USA disc
(`docs/reference/regions.md`).

| Stage (entry pc to stop) | What it does | Per run inputs | Per vertex inputs |
| --- | --- | --- | --- |
| Object set-up (0x26E0) | view-projection times model matrix; optional distance fade of the object (flag 0x800: alpha from the projected centre, object skipped at zero); optional box test (flag 0x400: eight corners against the view volume) | view-projection rows, model matrix, fade and box constants | none |
| Ladder rungs (0x2A40, 0x2A50) | nothing; a culled or box-crossing object consumes its MSCNT here | | |
| **Strips, lit** (0x2A60 to 0x2F98) | per strip: four directional light terms from the integer normal, ambient added, clamp at 200; position times matrix, 1 / w, viewport scale and offset, fog from clamped w, 12.4 fixed point; one packet per strip | matrix (registers), viewport scale, offset, fog limits and slope, ambient and alpha, light direction and colour matrices, strip tag | position (4 floats), texture coordinate (3 used), normal (4 integers); strip lengths in header slots |
| **Strips, constant colour** (0x2A60 to 0x2C48, flag 8) | the same without lighting | the same without lights | position, texture coordinate |
| Batch set-up (0x0000) | loads the matrix and constants; optional fade value | | |
| Batch vertex pass, depth fade (flag 0x80) | per vertex alpha from depth (the terrain layers' distance fade, `docs/reference/graphics.md`) | fade constants | positions, colours, in place |
| Batch vertex pass, texture matrix (flag 0x200; the loop at 0x04D8) | 2 x 3 matrix on every ST pair, **in place** in VU memory | matrix | ST |
| Batch vertex pass, grid or point lights (flags 0x1000, 0x2000, 0xC000), then light and transform | read from the listing only: a generator over a 15 x 15 vertex window (terrain chunks carry flags 0x106C, 0x126C, 0x10EC) and a point light loop with a distance term | header quadword 43 and more | |
| Batch vertex pass, light and transform (flag 0x10 lights) | three directional lights and ambient, transform, fog, fixed point into the processed arrays | lights, viewport | positions, normals, colours, ST |
| Batch vertex pass with clip codes (flag 1) | the same, plus a 6 bit outcode per vertex from the VU CLIP flags | | |
| Batch primitive pass (stops at 0x1018) | walks index lists (four indices per quadword), copies processed vertices into strip packets, kicks | batch header, index lists | none (reads the processed arrays) |
| Batch primitive pass with the clipper (stops at 0x1580, flag 1) | per triangle: reject by outcodes, draw, or clip against five planes with interpolated vertices, emitted as fans | the same | |

The EE changes the flag word between the MSCNTs of one batch, so a terrain chunk is a sequence: depth fade, texture
matrix, grid and transform, then one primitive pass per index list and layer.

### 2.2 Shares

Share of the VU1 engine's own time (single thread replay, best of 5 per run, no VIF unpack and no hand-off to the
renderer), and in brackets the share of the vertices sent to the GS. One recording of 16000 consecutive runs per
scene, about four frames. `surdana` and `ootf-ruined` are the CPU session's recordings (read only); the others were
made with `DRAKAN_VU_RING` three seconds into the still view.

| Stage | surdana | surdana-fight | surdana street | arena-1e | tundra-c | catacombs | ravenshold | ootf-ruined |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **strips, lit** | 44.9 (45) | 53.8 (51) | 25.0 (22) | 40.8 (29) | 35.6 (33) | 16.7 (10) | 0 | 2.3 (3) |
| strips, constant colour | 0.6 (1) | 0.2 (0) | 0 | 0 | 0 | 0 | 0 | 0 |
| object set-up and rungs | 8.8 | 3.9 | 0.5 | 0.8 | 0.7 | 0.5 | 0 | 0.1 |
| batch: primitive pass | 16.4 (47) | 19.2 (46) | 26.0 (68) | 25.3 (63) | 24.3 (59) | 48.8 (86) | 50.0 (97) | 19.2 (85) |
| batch: primitive pass with the clipper | 14.1 (7) | 7.8 (3) | 20.9 (9) | 16.6 (8) | 18.0 (8) | 14.4 (4) | 7.7 (3) | 14.0 (12) |
| batch: light and transform | 2.9 | 6.1 | 4.8 | 8.2 | 6.1 | 13.3 | 14.8 | 3.8 |
| batch: grid or point lights, then transform | 6.0 | 5.1 | 11.5 | 1.9 | 6.1 | 0.5 | 19.6 | 8.2 |
| batch: texture matrix | 1.8 | 1.1 | 2.4 | 2.6 | 2.5 | 0.4 | 2.2 | 40.5 |
| batch: depth fade | 4.0 | 2.0 | 4.1 | 2.7 | 4.2 | 0.3 | 4.3 | 9.2 |
| batch: light, transform, clip codes | 0.3 | 0.4 | 4.0 | 0.8 | 1.8 | 3.9 | 0.6 | 0.8 |
| batch set-up | 0.5 | 0.4 | 0.7 | 0.4 | 0.7 | 1.2 | 0.7 | 2.1 |
| engine time of the recording, ms | 14.3 | 26.4 | 22.9 | 26.3 | 26.3 | 14.3 | 16.2 | 10.3 |
| vertices sent | 323,643 | 579,788 | 561,699 | 555,911 | 608,481 | 528,599 | 478,814 | 89,958 |
| vertices from the clipper (fans), % | 1.1 | 0.6 | 1.2 | 1.3 | 1.9 | 0.7 | 0.3 | 2.6 |

Reading it:

* **Good news:** there are no dozens of special cases. The strip stage is two variants of one loop, and it is the
  largest single item wherever characters and props fill the view (a fight: 54 %).
* **Bad news:** it is zero in flight and almost zero in the converted Order of the Flame levels, the two cases the
  owner's numbers show as slowest per vertex. Everything there goes through the batch path, whose cost is spread over
  six vertex passes and two primitive passes. The largest single batch item, the primitive pass (16 to 50 %), is not
  vertex arithmetic at all: it is a branchy copy of processed vertices into packets driven by index lists.
* The batch path with the clipper is 8 to 21 % of the time for 3 to 12 % of the vertices; only 0.3 to 2.6 % of all
  vertices actually come out of the clipper.
* A strip run has 95 to 160 vertices (29 in the Order of the Flame level) in strips of 5 to 10; a recording holds
  87 to 1,693 strip runs.
* In the Order of the Flame level the texture matrix pass alone is 40 % (the CPU session's finding about the
  converted terrain's coordinates, `perf-cpu.md` 3.2).

Not recorded: the Surdana town square with many villagers and any view with spell effects. `surdana street` is a
placement on the road (`454500 161900 494600`), not the square.

## 3. What a GPU path needs, and how exact it can be

### 3.1 The arithmetic

What matters for an identical picture is what ends up in the GS packet: X and Y as 12.4 fixed point (low 16 bits),
Z as a 32 bit integer, the fog byte and the skip bit (bits 4 to 11 and bit 15 of one fixed point lane), four colour
bytes, and the float bits of S, T and Q. Everything before that is VU float arithmetic with these rules, all of which
the recorded data exercises:

* every add, multiply and divide rounds **toward zero**, and a multiply-add rounds twice (the product, then the sum);
* operands: a denormal is a signed zero, an infinity or NaN is the largest finite value; results never leave the
  finite range (overflow gives the largest finite value, underflow a signed zero);
* float to fixed point truncates and saturates; MIN and MAX are plain comparisons;
* the converted Order of the Flame terrain feeds values next to the largest float into the texture matrix pass
  (`perf-cpu.md` 4.1); a shader for that pass has to treat them like the engine does.

Measured on the 615,215 recorded strip vertices of six scenes (the test of 5.4 prints it): the same formulas in
ordinary round-to-nearest float arithmetic give another X or Y on **0.29 %** of the vertices (never by more than one
sixteenth of a pixel), another depth on **40.8 %**, another fog byte on 40.6 % (the fog term sits on its clamp ends,
where one unit in the last place crosses an integer), one colour byte in 615,215, and other S or T bits on 78 %.
Visible consequence of the approximate version: no visible shift, fog off by one step of 255 on distant geometry,
and depth values that differ in their last unit, which can flip the order of coplanar surfaces (decals, shadows on
terrain) in single pixels. It would not pass a frame hash.

### 3.2 How the shader gets it exactly

A double holds the product of two floats exactly, and the sum of two floats up to a residual whose sign two more
double sums give. The float below the exact result then follows from one comparison. The repository already uses
this for WebAssembly, where the rounding mode cannot be switched (`Vu1Simd::Software`, `vu/ps2_vu1_simd.h`).
`runtime/gs/include/gsr/vertex_program.h` writes the strip stage this way (`vp::evaluate`), and
`runtime/gs/src/vertex_program_hlsl.h` is the same text in HLSL. 1 / w is found as the largest float whose product
with w does not exceed one, so the precision of the GPU's float division does not matter. Integer normals convert
exactly through two float halves.

Limits of the method:

* It needs **double support in shaders**: an optional Direct3D 11 feature (present on the RTX 5080 and on the
  software adapter WARP). **WGSL has no 64 bit float**, so the WebGPU backend (the default renderer, and the only one
  on Linux and in the browser) cannot use this shader text. The same result is reachable there with integer
  arithmetic on the float's bits (a 24 x 24 bit product in 32 bit limbs, a 48 bit sum); that is three to five times
  the operations per vertex and was **not written or measured**.
* `precise` is needed on the double intermediates, and the program must be compiled with the same flags as the
  ordinary vertex shader: with `D3DCOMPILE_IEEE_STRICTNESS` on the program alone, the renderer's own `VSMain`
  (inlined after the program) was compiled differently from the stand-alone one, and 536 of 1,015,215 clip positions
  differed in their last bits. Without the flag: 0.
* With a temporal jitter added to the position (DLSS on), 61 of 1,015,215 positions differ in the last bit of one
  clip coordinate between the two shaders. The prototype does not draw jittered pictures; a version for DLSS has to
  make the two entry points share that addition exactly.

### 3.3 Clipping, guard band, strip restarts

* The strip stage **never clips**: it only runs for an object whose box test passed (or which has none). Whatever
  its vertices are, including w <= 0 and coordinates that wrap at 16 bits, the shader produces the same integers as
  the program, and the same wrapped coordinates reach the rasteriser. Nothing is approximated.
* Strip restarts: each strip is its own packet with PRIM in the tag, so a restart is a new tag, not an ADC bit. The
  "do not draw" bit of XYZ2 is bit 15 of the fixed point fog lane. The prototype proves from the constants of a run
  that no vertex can set it (`vp::neverSkipsVertices`) and evaluates the record on the CPU otherwise; it was never
  set in 895,640 recorded vertices. A shader could carry it per triangle with one more attribute.
* Flat shading (PRIM without IIP) takes the colour of a triangle's last vertex; the prototype keeps such a lit draw
  on the CPU. Not observed.
* The batch path with the clipper builds new vertices on the planes of the view volume, with interpolated colours
  and fog rounded by the VU, and emits fans. A GPU clips the same triangle to the same region, but interpolates
  differently: edges on the screen border and the colours along them would differ by fractions of a pixel and of a
  colour step. **Not reproducible exactly on a GPU**; it should stay on the CPU.

### 3.4 Verdict per stage

| Stage | On a GPU | Why |
| --- | --- | --- |
| Strips, lit and constant colour | **exact, shown** | this prototype |
| Object set-up | stays on the CPU | per object, 0.1 to 9 % of the time, and its result decides which path an object takes |
| Batch vertex passes without clip codes | exact in principle, **not shown** | the same arithmetic rules; needs the grid and point light passes read properly, the in-place texture matrix, and the hand-over between MSCNTs |
| Batch primitive pass | per draw work on the CPU (index lists to an index buffer), **not shown** | no arithmetic; the reject rule of the unclipped variant was not read to the end |
| Batch passes with clip codes and the clipper | **stay on the CPU** | interpolated vertices cannot be matched |

## 4. Design

Backend neutral by the standing rule ("changes must be made so that work is not done once per renderer",
`vulkan-backend.md`). What the prototype implements is marked **[built]**, the rest is proposal.

### 4.1 Where the interception happens

At the MSCNT, before the microprogram runs, on the VU1 worker: `vu1gpu::intercept`
(`ps2xRuntime/src/lib/vu/ps2_vu1_gpu_program.h`) **[built]**. The recogniser accepts a run only when the pc is the
stage's resume point, the micro memory is the compiled image, no exact-engine pipeline is pending, the strip tag
template has the layout the stage's packets assume, and the strip table fits the vertex window. Everything else runs
on the CPU as before. The set-up stage of the object always runs on the CPU; its registers are the record's matrix.

### 4.2 The neutral description of a vertex-program draw

`gsr::vp` in `runtime/gs/include/gsr/vertex_program.h` **[built]**: a record of a header (program id, the PRIM value
of the tag, strip count, vertex count), sixteen rows of constants, the strip lengths and the raw vertex rows, all as
the 32 bit words VU memory held. `gsr_core` turns consecutive records with the same GS state into a
`VertexProgramBatch` (a triangle list of raw vertices, each naming its constants) and hands it to
`BackendApi::drawVertexProgram`. A backend that returns false from `vertexPrograms()` never sees one: the core then
evaluates the record with `vp::evaluate` and feeds the result through the ordinary `kick`, which is also the
reference every shader is tested against.

### 4.3 Coexistence with the GS stream

The record travels **inside the PATH1 stream**, wrapped in one GIF tag with FLG = 3 (a GS ignores that tag's data) and
a magic number in the tag's upper half **[built]**. Consequences: the order with every other packet (state changes,
texture uploads, the batch path's packets) is the stream order; the arbiter, the capture and the bridge pass it
unchanged; a renderer that does not know the tag draws nothing for it but stays in sync. In `gsr_core` a record first
writes PRIM like the strip tag would, a pending ordinary batch is drawn before a program batch starts and the other
way round, and after a batch the current vertex registers are what the last strip vertex left **[built]**. Depth,
blending, alpha test, scissor and texture are the GS state of the batch; on Direct3D 11 the draw goes through the
ordinary `draw()` with three stand-in vertices spanning the scissor, so the target, texture and blend decisions are
the same code **[built]**.

### 4.4 Shaders once for both backends

Today HLSL and WGSL are hand-kept twins (`gs_shaders.h`, `gs_shaders_wgsl.h`; no generator, no test that compares
the texts; the shared check is the synthetic test suite and the capture replays on both backends). Proposal for
vertex programs, in this order of preference:

1. **One description, generated shader text.** A stage is a short list of VU operations over named inputs (the strip
   stage is about 50 operations, the lit variant about 110). Keep that list as data in a neutral header, evaluate it on the CPU
   for the reference, and generate the HLSL and the WGSL bodies from it, each over its backend's exact-arithmetic
   library (doubles in HLSL, integer limbs in WGSL). The two libraries are written once and tested by the
   stream-output test of 5.4 and its WebGPU twin (a compute pass writing to a buffer). New stages then cost a list,
   not two shaders.
2. Until the WGSL library exists, WebGPU takes the CPU evaluation of the records. Records are independent of each
   other, so that evaluation can run on several cores (section 7, alternative B), which the microprogram itself
   cannot.

### 4.5 What else is affected

* **Render targets and readbacks.** A vertex-program batch reports the scissor as its bounds, so the dirty rows of a
  target are the scissor's rows. For the frame buffer that changes nothing; small scratch targets (shadow and
  portrait targets, 128 pixels wide) keep the CPU evaluation in the prototype (records are only taken for targets at
  least 512 wide) **[built]**. A complete version should take the bounds from the object's box, which the set-up stage
  has already projected.
* **The game's own effects that read vertices back.** None do: VU1 memory cannot be read by the EE, and no later
  run was found to depend on what a skipped strip run would have left (replay of 16000 runs per scene without the
  taken runs: 0 other runs sent different packets, in eight recordings). The packet buffers a skipped run would have
  written are read by later runs only in lanes a GS ignores (`vu1-multicore.md`); the live matched frames against the
  microprogram itself (5.6) cover that case in practice.
* **Scene model capture, ray scene, BVH.** They read guest RAM models, not the GS stream, and are unaffected. The
  surface reconstruction (`surface_builder.h`, `ResidentScene::match`) un-projects GS triangles and needs their
  vertices: with a ray effect on, either the record is evaluated on the CPU for that consumer only (the cost comes
  back), or reconstruction moves to the records, which carry object-space positions and the matrix, a better input
  than un-projected screen triangles. The prototype switches itself off while a ray effect is on.
* **DLSS motion vectors.** Today `prepareObjectMotion` matches each draw's vertices with the previous frame's on the
  CPU (0.6 to 0.85 ms per world in Surdana, `dlss-speed.md`). With records, the previous frame's constants of the
  same object and strip (the motion scope already identifies the object) can be bound next to the current ones and
  the vertex shader emits both positions: no per-vertex CPU work, and skinned models are covered because the EE has
  already skinned the positions it uploads (the previous positions have to be kept per object, about 16 bytes per
  vertex). Not built; the prototype switches itself off while the upscaler is on.
* **Model LOD, draw distance.** Guest side settings; they change which runs arrive, not how.
* **Terrain detail, material detail.** Both reconstruct a surface frame per pixel today because the stream has no
  normal and no world position. Terrain is batch path, so it gains nothing before stage 2 of section 6. For strips
  the record has the normal and the object-space position; with the model matrix (the set-up stage's input, not in
  the record yet) the shader can output world position and normal for per-pixel lighting. The prototype switches
  itself off while either setting is on.
* **Mod packs, the Order of the Flame campaign, the USA disc.** The recogniser keys on the microprogram image, which
  is identical on the USA disc, and on the data layout of a run, not on level content, so packs and converted levels
  take the path whenever their objects do. Not run on the USA disc.
* **Captures and the replay oracle.** A `PS2X_GS_CAPTURE` taken with the path on contains the records (they are GIF
  packets), and `gsr_replay` replays them through the same `gsr_core`: with `GSR_VERTEX_PROGRAM` unset it evaluates
  them on the CPU, which is the oracle picture; with `GSR_VERTEX_PROGRAM=1` it draws them on the GPU. That is how
  5.5 compares every frame. A capture taken with the path off has no records and replays as before. What a capture
  with records cannot check is the record against the microprogram; that is the VU1 replay's job (5.3).

## 5. The prototype

### 5.1 Which program and why

The strip stage: the largest measured share that is tractable (17 to 54 % of the engine time in the on-foot
Drakan 2 scenes), no clipping, no state carried between runs, one packet layout.

### 5.2 What exists

Committed (self-contained, nothing calls the run-time hook in the main tree):

| File | What |
| --- | --- |
| `runtime/gs/include/gsr/vertex_program.h` | the record, `vp::evaluate` (CPU evaluation with VU arithmetic), `vp::neverSkipsVertices` |
| `runtime/gs/src/vertex_program_hlsl.h` | the HLSL: `vpEvaluate`, the draw's entry point `VSProgram`, the test's `VSProgramRaw` |
| `runtime/gs/tests/test_vertex_program.cpp` (ctest `vertex_program`) | shader against CPU evaluation by stream output |
| `ps2recomp/ps2xRuntime/src/lib/vu/ps2_vu1_gpu_program.h` | recogniser `buildStrips`, hook `intercept` |
| `ps2_vu1_check.{h,cpp}` | replay options `DRAKAN_VU_BENCH_RUNS`, `DRAKAN_VU_BENCH_GPUPROG`, `DRAKAN_VU_BENCH_GPUPROG_DUMP`; `vu1check::gpuProgramIntercept` |
| `scripts/perf/vu_run_classes.py` | stage shares of a recording |

Not committed, `build/review/gpu-vertex/patches/gpu-vertex-integration.patch` (checked against `2efd1599` with
`git apply --check`; 538 lines): the three line hook in `ps2_runtime.cpp`, the record handling and batching in `gs_core.cpp`,
the two entry points in `gs_internal.h` / `backend_select.inl`, four counters in `gsr.h`, and the Direct3D 11 side:
the new `d3d11_vertex_program.cpp`, members in `d3d11_backend_impl.h` and a three line hook in `d3d11_draw.cpp`
(bind the program's buffers and shader after the ordinary bindings, draw its vertex count). It is kept out because
`d3d11_draw.cpp` and `d3d11_backend_impl.h` are being restructured by another session and `ps2_runtime.cpp` had other
uncommitted work in the tree.

Switches of the patched build, all off by default: `DRAKAN_GPU_VERTEX=1` (send strip runs as records; `alt` = one
second on, one off), `GSR_VERTEX_PROGRAM=1` (draw records on the GPU; unset = evaluate on the CPU; `alt` = 64 frames
each), `GSR_VERTEX_PROGRAM_LOG=1`, `GSR_VERTEX_PROGRAM_ANYWHERE=1` (replay tool: no world phase needed),
`GSR_VERTEX_PROGRAM_DEBUG`.

The Direct3D 11 path takes a record only in the world phase, into a target at least 512 wide, with the upscaler,
neural rendering, every ray effect, terrain detail, material detail and the LOD statistic off, with IIP set for lit
draws, and when the constants rule out the skip bit. Per batch the CPU does: one copy of the constants, one copy of
three raw vertices per triangle (64 bytes each), one draw call. Records of consecutive objects with the same GS state
share a draw call (12 records per batch on average in the Surdana capture).

### 5.3 Proof 1: the CPU evaluation equals the microprogram

`DRAKAN_VU_BENCH_GPUPROG=1` in the VU1 replay: for every run the recogniser accepts, the record is evaluated and
compared with the packets the engine sends for that run (which the same replay checks against the reference
interpreter).

| Recording | strip runs taken | left on the CPU | vertices compared | vertices that differ | other runs with different packets when the taken runs are skipped |
| --- | --- | --- | --- | --- | --- |
| surdana | 949 | 0 | 148,847 | 0 | 0 of 15,051 |
| surdana-fight | 1,630 | 72 | 280,425 | 0 | 0 of 14,370 |
| surdana street | 876 | 36 | 118,368 | 0 | 0 of 15,124 |
| arena-1e | 1,344 | 96 | 144,576 | 0 | 0 of 14,656 |
| tundra-c | 1,122 | 198 | 156,492 | 0 | 0 of 14,878 |
| catacombs | 504 | 30 | 44,436 | 0 | 0 of 15,496 |
| ravenshold | 0 | 0 | 0 | | 0 of 16,000 |
| ootf-ruined | 87 | 0 | 2,496 | 0 | 0 of 15,913 |

895,640 vertices, every one equal in the bits of S, T and Q, the four colour bytes, the fog byte, X, Y, Z and the
skip bit. The runs left on the CPU (3 to 15 % of the strip runs outside Surdana) have a strip table that runs past
the vertex window the recogniser accepts, and one has a strip longer than a packet buffer; what those runs are was
**not examined**.

### 5.4 Proof 2: the shader equals the CPU evaluation

`gsr_test_vertex_program` runs the shader with stream output and compares integers.

* `VSProgramRaw` (the program's result as integers) against `vp::evaluate`: 400,000 generated vertices (game-like
  matrices, and every fourth case with arbitrary operands: denormals, the largest floats, infinities, NaN, zero and
  negative w, integers of any size) plus the 615,215 recorded vertices of six scenes
  (`GSR_VP_RECORDS`, dumped by the replay): **1,015,215 vertices, 0 differ** on the RTX 5080; the 400,000 generated
  ones also 0 on WARP.
* `VSProgram` (what a draw binds: the program, then the renderer's ordinary `VSMain`) against `VSMain` run on the
  CPU evaluation, all twelve output floats bit for bit: 1,015,215 vertices, **0 differ** (with a temporal jitter 61
  differ in one last bit, see 3.2; printed for information).

History worth knowing: the first shader version drew stretched garbage in the foliage of the live game. The version
restructured for the test has drawn every picture correctly since; the two differ in structure, not in arithmetic,
and **the cause of the first failure was not found** (removing the one `precise` that was added later does not bring
it back in the test). The test exists so that such a version cannot be drawn again without being noticed.

### 5.5 Proof 3: every frame of three captures

Captures taken from boot with `DRAKAN_GPU_VERTEX=1` (so the stream holds the records), replayed twice by
`gsr_replay`: records evaluated on the CPU, and records drawn by the vertex shader. A hash of every displayed frame:

| Capture | frames | internal scale | vertices drawn by the shader | frames whose hash differs |
| --- | --- | --- | --- | --- |
| Surdana, look round and walk | 1,423 | 3 | more than 14.2 million | 0 |
| the same | 1,423 | 8 | the same | 0 |
| Northern Tundra spot C, look round | 2,009 | 3 | more than 6.3 million | 0 |
| Catacombs start, look round and walk | 947 | 3 | more than 2.6 million | 0 |

Capture MD5s before and after: unchanged. These replays ran the build that still had the strictness flag of 3.2 on
the program shader, i.e. with 0.05 % of the clip positions differing in their last bits: the frames were identical
all the same. The final build was checked again on the Surdana capture (5.8).

### 5.6 Proof 4: matched frames in the live game

A GS capture cannot be the oracle for the live path, so the live game was paused (the engine redraws the frozen world
under its pause menu every frame; foliage wind off) and the path was switched inside the run, ten screenshots 0.5 to
0.7 s apart per run. Compared outside the host's frame-rate text:

| Run | switch | level | shots identical |
| --- | --- | --- | --- |
| `GSR_VERTEX_PROGRAM=alt` (records on the CPU against records on the GPU) | every 64 frames | Surdana | 10 of 10, 5 phase changes between the shots |
| | | Northern Tundra | 10 of 10 |
| | | Inquisitor's Lair | 10 of 10 |
| `DRAKAN_GPU_VERTEX=alt`, `GSR_VERTEX_PROGRAM=1` (the microprogram itself against the GPU path) | every second | Surdana | 9 of 10; the first shot still shows the game's quest notice, which then leaves |
| | | Northern Tundra | 10 of 10 |
| | | Inquisitor's Lair | 10 of 10 |

1280x960 window, internal scale 3. During the pause both paths kept drawing (the counters of the first Surdana run:
9.9 million vertices on the GPU and 9.9 million on the CPU between two log lines).

### 5.7 What it saves

Interleaved A/B, one exe, switches only: `cpu` = defaults, `gpu` = `DRAKAN_GPU_VERTEX=1 GSR_VERTEX_PROGRAM=1`.
Still views, 1280x960 window, internal scale 3, DLSS off, 15 s windows (`cpu_scene_bench.py`; the Surdana rows
`scene_ab_bench.py`, 8 s windows). **The machine was never quiet:** one to four other `drakan.exe` ran during every
run, and the tool reported the GPU at 99 % from those games before each Surdana run. Thread times are CPU time per
world frame; median, and every run in brackets.

| Scene (other games) | variant | VU1 Mpairs per frame | VU1 worker ms | GS worker ms | EE ms | frame ms | world fps |
| --- | --- | --- | --- | --- | --- | --- | --- |
| surdana (2 to 4) | cpu | 3.86 | 5.51 (5.00, 5.51, 6.56) | 4.16 (3.87, 4.16, 4.82) | 5.65 | 5.77 (5.27, 5.77, 7.41) | 159.7 (178.2, 159.7, 131.5) |
| | gpu | 2.56 | 3.83 (3.83, 4.03, 3.74) | 3.56 (3.43, 3.67, 3.56) | 5.71 | 5.43 (5.59, 5.43, 4.52) | 160.7 (145.7, 160.7, 202.5) |
| fight pose (1 to 3) | cpu | 6.60 | 9.34 (9.34, 10.06, 9.00) | 7.83 (7.83, 8.57, 7.58) | 5.71 | 9.30 (9.30, 10.55, 9.04) | 102.3 (102.3, 95.4, 102.7) |
| | gpu | 3.93 | 6.11 (6.55, 6.11, 5.54) | 6.46 (6.92, 6.46, 5.89) | 6.59 | 8.59 (12.97, 8.59, 6.51) | 105.5 (78.3, 105.5, 145.9) |
| tundra-c (2) | cpu | 4.42 | 7.55 (7.89, 7.20) | 6.63 (6.80, 6.45) | 9.10 | 9.19 (9.69, 8.70) | 103.4 (96.9, 109.9) |
| | gpu | 3.58 | 5.82 (5.68, 5.96) | 5.96 (5.68, 6.25) | 8.65 | 8.77 (8.43, 9.11) | 112.7 (116.6, 108.7) |
| arena-1e (1 to 2) | cpu | 4.23 | 7.29 (7.21, 7.38) | 6.93 (6.86, 7.01) | 8.08 | 8.50 (8.48, 8.52) | 115.7 (114.7, 116.7) |
| | gpu | 3.51 | 6.51 (6.73, 6.28) | 6.86 (6.90, 6.81) | 8.24 | 8.40 (8.52, 8.28) | 115.3 (114.1, 116.5) |
| ravenshold (1 to 2), one pair | cpu | 2.39 | 4.04 | 3.78 | 4.49 | 4.63 | 213.6 |
| | gpu | 2.39 | 3.69 | 3.49 | 4.27 | 4.48 | 221.7 |
| ootf-ruined (2), one pair | cpu | 4.18 | 7.67 | 8.65 | 10.38 | 10.29 | 92.9 |
| | gpu | 4.15 | 7.81 | 10.18 | 10.75 | 10.95 | 91.4 |

The fight pose is the placement of `scene_ab_bench.py`'s `surdana-fight` (three Wartoks attacking, god mode) without
Rynn's scripted swings. The Surdana rows were taken with the build before the last change of the program shader's
compile flags (3.2), the others with the final build.

What is established:

* **VU1 work:** the pairs executed per frame fall by 33.7 % in Surdana, 40 % in the fight, 19 % in the tundra and
  17 % in the arena view (exact counters), and by nothing in flight and in the Order of the Flame level.
* **VU1 worker CPU per frame:** minus 30 % in Surdana (5.51 to 3.83 ms), minus 35 % in the fight (9.34 to 6.11 ms),
  minus 23 % in the tundra, minus 11 % in the arena view; in all four the ranges of the two variants do not overlap.
* **GS worker CPU per frame:** minus 14 % in Surdana, minus 17 % in the fight, minus 10 % in the tundra (ranges
  disjoint in all three), nothing in the arena view. The worker no longer parses a packet and builds three
  vertices per triangle for these draws; it copies three raw vertices per triangle instead. An index buffer would
  cut that copy and the shader's work to a third; not built.
* An earlier Surdana series under heavier load (2 to 4 other games, world fps 42 to 71) gave the same thread
  pattern: VU1 worker 6.02 (5.87 to 6.46) to 4.28 (4.21 to 4.52) ms, GS worker 4.94 to 4.10 ms.

What is **not** established:

* **Frame time and frame rate.** Surdana: three pairs, 178 to 146, 160 to 161 and 131 to 202 world fps. Fight: 102
  to 78, 95 to 106, 103 to 146. Tundra: 97 to 117, 110 to 109. Arena: no change. The medians differ by minus 0.3 to plus
  9 % in favour of the GPU path, and every difference is inside the spread between repeats. The very first trial (one pair, 3 to 4 other
  games, an earlier build whose foliage was drawn wrong) had shown 100 against 162 world fps in Surdana; the series
  above do not confirm a gain of that size.
* Why the frame rate does not follow the worker times here. Observed in the Surdana series: the time inside
  `Present` per call was 0.14, 0.66 and 0.59 ms on the CPU path and 2.95, 1.48 and 0.71 ms on the GPU path, while the
  timed GPU world pass was 0.65 against 0.70 ms. With the GPU fully used by other games, a renderer thread that
  finishes its frame earlier waits in `Present` instead. That reading is an **inference**; it predicts a gain on a
  machine whose GPU is idle (the owner's: 0.3 to 1.5 ms of GPU work per frame with DLSS off), and that has to be
  measured there. The best pair of the fight (9.04 to 6.51 ms, one other game) is what the worker times would allow,
  not a result.
* **The EE's wait.** The EE thread's CPU per frame did not fall (it polls while it waits, so its CPU time is about
  the frame time in both variants). How much of its wall time is waiting for the workers was not sampled again.
* The GPU cost of the double arithmetic: 0.05 ms more on the timed world pass in the Surdana median, inside the
  spread; roughly 90,000 shader invocations per frame there. Not measured on a quiet GPU, on other vendors, or at the
  owner's scale 8.
* 3840x2160, the Surdana town square, and anything with DLSS on (the path is off there).

Single thread, engine only (the VU1 replay of a recording, best of 9, strip runs executed against strip runs turned
into records by the hook):

| Recording | all runs executed, ms | strip runs built as records, ms | change |
| --- | --- | --- | --- |
| surdana | 13.40 (17.91) | 8.32 (8.94) | minus 38 % |
| surdana-fight | 21.98 (26.31) | 15.99 (17.00) | minus 27 % |
| arena-1e | 23.57 (24.29) | 18.95 (19.13) | minus 20 % |
| tundra-c | 25.00 (30.47) | 20.58 (22.11) | minus 18 % |
| catacombs | 15.94 (20.67) | 16.92 (17.38) | none |

Two rounds, the lower one first and the other in brackets, with one or two other games and compilers running. The
two rounds of one variant differ by up to 30 %, so these are weak numbers. They are also smaller than the stage
shares of 2.2 predict (the fight: 54 % of the per-run times, 27 % here); that difference was **not explained**.

### 5.8 Tests and builds as they were

Builds:

* Private tree (`build.sh`, shared lock each time): the first full build compiled the generated game code and
  failed on a syntax error of mine after 690 s; the second stopped after 349 s at the pack step (the worktree had no
  `editor/packages/formats` sources; copied from the main tree); seven incremental builds after that passed (21 to
  62 s each, one waited 260 s for the lock).
* **A download happened that was not listed first.** The private work directory was seeded with the generated sources
  of `build/final-game`, but the build script found the recompile stamp stale and rebuilt the recompiler, whose build
  fetched its five pinned dependencies from GitHub (ELFIO, toml11, fmt, libdwarf-code, rabbitizer, at the commits of
  `scripts/buildtool/toolchain.lock.json`). raylib, the FFmpeg archive and wgpu-native were taken from copies of the
  shared build tree; nothing else was downloaded.
* Main tree after the code commit `42d47274`: `Build-LocalGame.ps1 -Incremental -NoDist` into `build/final-game`
  with the lock, exit 0 in 56 s (tree at `09dc9b0a` plus the other sessions' uncommitted work).
* Stand-alone `runtime/gs` trees of the worktree and of the main tree: `gsr_replay`, `gsr_test_synthetic`,
  `gsr_test_vertex_program` built.

Tests run:

* `gsr_test_vertex_program`: pass on the RTX 5080 (1,015,215 vertices with the recorded ones, 400,000 without) and
  on WARP (400,000), built from the worktree and from the main tree.
* `gsr_test_synthetic` with the integration patch: 395 checks, 0 failed (Direct3D 11; it feeds no records).
* VU1 replay against the reference interpreter with the replay options on, eight recordings of 16000 runs: 0
  mismatches in `ootf-ruined` and `arena-1e`, 1 in each of the others, always run 0 (clip flags from before the
  recording, the artefact `perf-cpu.md` describes).
* Final build (program shader compiled with the renderer's flags): Surdana capture 1,423 frames and Catacombs capture
  947 frames at scale 3, 0 different from the CPU evaluation.
* `python scripts/run_tests.py`: 557 tests, OK (2 skipped). `scripts/repository_guard.py`: 0 prohibited files.
  clang-format 19.1.5 (the one shipped with Visual Studio) and ruff clean on the committed files.
* About 50 scripted game runs with the path on (recordings, captures, paused comparisons, benchmarks) reached their
  scripted quit with exit code 0, in Surdana, the tundra, the arena, the catacombs, Ravenshold and the Order of the
  Flame level.

Not run: the `runtime-tests` ctest preset, `gsr_test_live`, any WebGPU or Vulkan run of the patched build (those
backends compile with the default "no vertex programs" and evaluate records on the CPU), the USA disc, a
play-through.

Measurement etiquette: `scene_ab_bench.py` takes turns with the other sessions' benchmarks through a lock; my runs
waited up to 19 minutes for a turn, so the fight, tundra, arena, Ravenshold and Order of the Flame series were run
with `cpu_scene_bench.py`, which does not take the lock. They may have disturbed another session's measurement, and
were disturbed by them.

## 6. The whole job, honestly

Shares are of today's VU1 engine time (section 2.2). Effort is for one experienced person who knows this code, and
includes the proofs of section 5 for each step.

| Step | Removes from the VU1 worker | Effort | Risk |
| --- | --- | --- | --- |
| 0. Land the strip stage on Direct3D 11 as it is (after the backend restructuring): the patch, a settings entry, bounds from the object box instead of the scissor, the runs now left on the CPU | 17 to 54 % in on-foot Drakan 2 scenes, 0 in flight, 2 % in Order of the Flame | 1 week | low: proven; the open point is the unexplained first shader failure |
| 1. Strips with DLSS, ray effects, terrain and material detail on: motion vectors from the vertex shader, the jitter bit, reconstruction from records | nothing more on the VU1 worker; removes the CPU object motion for these draws (0.6 to 0.85 ms per world) | 2 to 3 weeks | medium: DLSS quality depends on the vectors; the owner plays with DLSS on, so step 0 alone does not reach him |
| 2. The same on WebGPU and Vulkan: exact integer arithmetic library in WGSL, the generator of 4.4, the batch drawing in the shared draw code | nothing more; reaches Linux, the browser and the default renderer | 2 to 3 weeks | medium: unmeasured shader cost, browser limits |
| 3. Batch path without clip codes: read the grid, point light and primitive passes to the end, a recogniser across the MSCNTs of a batch, vertex passes as shader stages, index lists to index buffers | 30 to 75 % (most of what is left in every scene; the only step that helps flight and Order of the Flame) | 5 to 8 weeks | high: a multi-run state machine instead of one run; in-place data that persists between batches; the EE edits the flag word between runs; values next to the largest float; nothing of it is shown |
| 4. Batch path with clip codes | 8 to 21 % | not recommended | cannot be exact |

What stays on the CPU at the end (after 0 to 3): the object set-up, the batch runs with clip codes and their clipper
(together 9 to 25 % of today's engine time by section 2.2), the VIF unpack and DMA handling in front of the engine
(about 6 to 15 % of the VU1 worker's samples, `cpu-ceiling.md`), the record building, and on the GS worker one copy of
the raw vertices per draw plus everything per draw (state, texture resolve, targets).

Frame-rate ceiling when done: estimates from the thread times of 5.7, not measurements. Today the frame of a
Surdana still view is the time of its busiest worker: the VU1 worker is busy 95 % of a 5.8 ms frame (and 100 % of a
9.3 ms frame in the fight), the GS worker 72 % (84 %), and the EE thread fills the rest of its frame with the poll
for the two. After steps 0 to 3 the VU1 worker would need about 1.5 to 2.5 ms per frame (set-up, clipped batches,
unpack, record building) and the GS worker about 2.5 to 3.5 ms in Surdana and 4 to 5 ms in the fight (per draw work
and vertex copies). The limit is then the EE thread's own work, which this study did not measure; from the CPU
session's shares (generated guest code 29 to 38 % of the thread, 40 to 70 % waiting) it is about 2.5 to 3.5 ms per
frame in Surdana. That puts the ceiling at roughly 4 ms per frame in Surdana (about 250 world fps against 160 to 180
measured today in this window) and 5 to 6 ms in the fight (170 to 200 against 100). The gain is the same wait that
alternative A of section 7 removes without converting anything, so the two do not add up: A makes the frame the
maximum of the three threads instead of their chain, the conversion shrinks two of the three. In the Order of the
Flame level steps 0 to 2 change nothing; step 3 would take most of the 7.7 ms of VU1 work per frame, and the EE
thread, which costs 10 ms per frame there, is the limit almost at once.

Knock-on work that the records make possible but which is separate: motion vectors from the vertex shader replacing
the CPU object-motion tracking (step 1), per-pixel lighting from real normals and world positions (needs the model
matrix in the record and a lighting model that matches the baked vertex colours), tessellation for silhouettes
(needs mesh adjacency, which a strip record does not have; `gfx-models.md` section 7 stays valid: the clean place is
the model records).

## 7. Cheaper alternatives

| Alternative | What it is | Estimated gain | State |
| --- | --- | --- | --- |
| A. Stop the EE waiting for the workers at the end of the frame | the per-frame 32 x 32 read: 40 to 70 % of the EE thread's wall time is that wait (`perf-cpu.md` 3.1) | up to +30 to +60 % world fps where the CPU side limits (the CPU session's estimate) | another session committed `PS2X_READBACK=eager / late` today (`222bbc97`), off by default; its measurements are not in yet. Independent of everything here |
| B. Evaluate records on several cores | the record makes a strip run independent of every other run, which the microprogram is not (`vu1-multicore.md`: an exact parallel VU was slower because of dependency tracking). Workers evaluate records with the engine's SSE arithmetic and hand finished vertices to the renderer | the same relief of the VU1 worker as step 0, paid with other cores; exact; every backend | not built. 1 to 2 weeks for strips. Needs an SSE evaluation (the double version of this study is the slow reference: 33 world fps when it ran on the GS worker) |
| C. Hand finished vertices to the renderer without the GIF packet in between | the engine writes packets that `gsr_core` parses back into vertices; the vertex path was about a quarter of the GS worker's samples (`cpu-ceiling.md`) | about 1 ms per frame on the GS worker | not built; exact; 1 to 2 weeks |
| D. Wider SIMD | the vertex loops already run 2 or 4 iterations per step with AVX2 / AVX-512 at about 1 ns per pair (`vu1-simd-vertices.md`) | little left in the loops; the primitive pass (16 to 50 %) is branchy and not covered. A native primitive pass might take 10 to 20 % off the VU1 worker | not estimated further |
| E. More cores for the microprogram itself | tried twice, exact but slower (`vu1-multicore.md`, `cpu-vector-guarded-reuse.md`) | none | closed |
| F. Cache transformed static geometry between frames | the matrix of every object contains the camera, so a cached result is only valid while the camera stands still | none in motion; still views are not the problem | not recommended |

## 8. Recommendation

**Go in stages, and do not fund the whole job at once.**

0. Before any of it: one hour on a quiet machine with an idle GPU. Apply the patch, run the fight and the Surdana
   scene with `scene_ab_bench.py` (`cpu` against `DRAKAN_GPU_VERTEX=1,GSR_VERTEX_PROGRAM=1`, DLSS off). The
   prototype removes a third of the VU1 worker's time there; if the frame rate does not move on a quiet machine
   either, the frame is not bound by the workers' CPU time and none of the steps of section 6 will pay. This study
   could not make that measurement.
1. Finish and measure alternative A. It is independent, it is already in the tree behind a switch, and it
   decides how much VU1 and GS worker time is worth: while the EE waits for both workers every frame, time saved on
   a worker can show up as frame rate; once it does not wait, only the slowest of the three threads matters.
2. If step 0 of this list shows a gain: land step 0 of section 6 (strips on Direct3D 11, the patch of this study)
   after the backend restructuring. It is proven identical, it is small, and it is 40 % of a fight's VU1 work.
3. Then step 1, because the owner plays with DLSS on: until the vertex shader also delivers the motion vectors, the
   path is off in his configuration.
4. Before funding step 3, spend one to two weeks reading the batch path to the end with the replay harness of this
   study (recogniser and CPU evaluation only, no shader). That turns the 5 to 8 week estimate into a known quantity
   and is the only part that helps flight and the converted levels. If it turns out as regular as the strip stage,
   go; if the hand-over between runs is as tangled as it looks, alternative B for strips plus alternative C buy most
   of the remaining frame rate for a fraction of the cost.
5. WebGPU (step 2) follows the decision on step 3; until then it takes the CPU evaluation.

## 9. Not established

* Any frame rate or frame time gain (5.7). The worker CPU times fall; the frame rate did not follow outside the
  spread on this shared machine, and the reason given for that is an inference.
* Numbers on a quiet machine, at 3840x2160, with the owner's scale 8, with DLSS on.
* The EE thread's wait and the GPU's cost of the double arithmetic on an idle GPU; any GPU other than the RTX 5080
  and WARP (double support and exact results on AMD and Intel adapters).
* Why the first shader version drew garbage (5.4).
* What the strip runs are that the recogniser declines (3 to 15 % of the strip runs outside Surdana).
* Everything about the batch path beyond its measured shares: the stage descriptions of the grid, point light and
  primitive passes come from a partial reading of the listing; the 5 to 8 weeks of step 3 and the ceilings of
  section 6 are estimates.
* The path with DLSS, a ray effect, terrain detail or material detail on (it switches itself off), motion vectors
  from the vertex shader, the jitter bit.
* WebGPU, Vulkan, Linux, the browser: no shader there; the CPU evaluation of records compiles in those backends and
  was not run. The integer arithmetic library for WGSL does not exist.
* The USA disc. A play-through, level changes, cut scenes, saving and loading, the inventory's 3D previews (drawn
  outside the world phase, so they take the CPU evaluation), spell effects, the Surdana town square. The live
  identity was shown on paused frames in three levels and by capture replays of short walks; frames in motion cannot
  be compared between runs.
* The skip bit of a vertex and flat shaded lit strips never occurred in recorded data; the generated test cases
  cover the bit, nothing covers a flat shaded draw.
* Dependencies through VU memory after a skipped run are covered by the matched live frames only, not by the
  replay (which restores VU memory per run).
* The `runtime-tests` preset as a whole (5.8).

## 10. Reproduce

```sh
# stage shares of a recording (any exe with the committed replay options)
DRAKAN_VU_BENCH=<rec.vur>,5 DRAKAN_VU_BENCH_RUNS=<runs.csv> <play>/drakan.exe
python scripts/perf/vu_run_classes.py <runs.csv>            # --by flags, --by pc
# CPU evaluation against the microprogram, and a dump of the records for the shader test
DRAKAN_VU_BENCH=<rec.vur>,5 DRAKAN_VU_BENCH_GPUPROG=1 DRAKAN_VU_BENCH_GPUPROG_DUMP=<records.bin> <play>/drakan.exe
# shader against CPU evaluation
cmake -S runtime/gs -B build/gs -A x64 && cmake --build build/gs --config Release --target gsr_test_vertex_program
GSR_VP_RECORDS=<records.bin> build/gs/Release/gsr_test_vertex_program.exe     # GSR_VP_WARP=1: software adapter
# with build/review/gpu-vertex/patches/gpu-vertex-integration.patch applied:
DRAKAN_GPU_VERTEX=1 GSR_VERTEX_PROGRAM=1 <play>/drakan.exe                    # the path on
PS2X_GS_CAPTURE=30000 DRAKAN_GPU_VERTEX=1 <play>/drakan.exe                   # a capture with records
gsr_replay <cap.bin> --scale 3 --first 0 --every 1 --no-png --hashes cpu.txt
GSR_VERTEX_PROGRAM=1 GSR_VERTEX_PROGRAM_ANYWHERE=1 gsr_replay <cap.bin> --scale 3 --first 0 --every 1 --no-png --hashes gpu.txt
```
