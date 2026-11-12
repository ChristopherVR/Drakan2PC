# Terrain quality: what the ground and mountains are drawn from, and two shader settings (2026-11-12)

Request: "improve the overall models and texture quality, especially mountains". This note is the terrain part:
ground, hillsides, cliffs, distant ranges. Everything below was measured on this machine with the owner's `drakan.ini`
(DLSS quality, HD textures, anisotropic 16, FOV 99, ray effects off) in a copy of the play folder. Runs, recipes,
probes and pictures are private under `build/review/gfx-terrain/` (not committed).

Settings that differed from the owner's play in every run: a 1920 x 1080 window instead of borderless fullscreen
(internal scale auto = 3 to 4 instead of 5; the 4K runs below use a 3840 x 2160 window), audio off,
`DRAKAN_NO_ACTIVATE=1`, god mode, a fresh memory card, a direct level start, `material_detail = off` (its default), and
the `elf =` line pointing at a private copy. Other agents ran 7 to 10 game instances on the same GPU during most runs.

## 1. How terrain reaches the renderer (phase 1)

* **Draw stream.** A terrain layer is cut into chunks of up to 12 x 12 cells (a cell is 2048 units, about 4 m). Each
  chunk goes through VU1 as perspective textured triangles (Q = 1 / w, not FST) and is drawn once per texture with the
  blend weight in the vertex alpha: a splat. The renderer sees batches of 6 to about 800 vertices per pass; a Surdana
  frame has about 80 terrain draws. Vertex colours are the baked lighting (per 4 m vertex, Gouraud), FOG is per
  vertex, and there is no normal and no world position in the stream.
* **Three kinds of texture** (GS draw log of one Surdana frame, `probe/dist.py`; distance = 1 / Q):

  | Pass | Texture | Texels per cell | Seen at |
  | --- | --- | --- | --- |
  | far colour map (the level's `..TS.TXD` group texture) | 256 px, 8 bit, 64 cells per tile | 4 | every chunk, 780 to 186000 units; the base under everything |
  | run-time composite of the chunk's layers | 128 px, 32 bit, 2 or 4 cells per tile, drawn twice (texture alpha off, then on) | 64 or 32 | 4900 to 84000 units |
  | ground tiles (disc or mod pack) | 128 or 256 px, 8 bit, 1 cell per tile (64 px: 2 cells) | 128 or 256 (64 px tiles: 32) | most vertices within about 10000 units, some chunks further |

  So what a mountain across a valley shows is the composite at 32 texels per cell (16 per metre) and beyond that the
  colour map at 4 texels per cell (one texel per metre). The tiles the HD pack improves only cover the ground around
  the player. `GSR_TEXUP_LOG=1` at the Surdana start: 236 textures replaced from the pack, 19 upscaled live; of the
  128 and 256 px textures 174 were replaced and 4 (128 px) went to the live xBRZ upscaler, which matches the number of
  composite variants in view. The link "those 4 are the composites" is by size and count, not by a per-draw log.
* **Identifying terrain.** All three passes share one GS state and nothing else in the audited levels uses it:
  `ALPHA` 0x44, alpha test GREATER with reference 0, depth test GEQUAL with depth writes, MODULATE, REPEAT on both
  axes (`TEST` low bits 0x5000d, objects use reference 0x64 or 2). `terrain::classify` tests that.
  `GSR_TERRAIN_AUDIT=1` checks it against geometry: a terrain vertex lies on the 2048 unit grid.

  | Level (one tour each) | classified triangles | of them with all three vertices on the grid |
  | --- | --- | --- |
  | Surdana | 41.3 million | 98.4 % |
  | Ravenshold | 35.3 million | 99.7 % |
  | Northern Tundra (cave start) | 35.9 million | 96.6 % |

  The rest are triangles cut by the engine's clipper. Not classified although on the grid: draws with depth writes
  masked (`TEST` 0x5002d, 95 to 100 % on the grid: water layers), which is intended. One opaque class (`TEST` 0x5064c)
  reads 15 % on the grid in Surdana and under 1 % elsewhere; the audit's tolerance grows with distance, so small far
  triangles pass by chance, and what those are was not looked at. OotF (Wartok Canyons) was run without the audit.
* **Lighting, fog, LOD.** Lighting is baked into the layer's vertex colours by the game; the renderer only
  interpolates it. Fog is the per-vertex GS fog towards `FOGCOL`. Which texture a chunk uses at which distance is
  decided in the guest (the table above is one frame); the routine and its thresholds were not traced. The level view
  distance is the float the models agent found (0x4de968, 75 cells in Surdana).
* **What the scene capture provides.** `SceneGuideCamera` (always published with a world camera) gives the matrices to
  turn a pixel and its Q into a world position. `SceneGeometry` is the whole resident height field of the level
  (Surdana 141000 land triangles, Ravenshold 221000, Tundra 140000), published when a consumer asks (DLSS, ray
  effects, and now `terrain_cliffs`). GS captures (`PS2X_GS_CAPTURE`) hold neither, so `gsr_replay` cannot show the
  terrain features; it proves the off state only.

## 2. Defects in the baseline pictures

`evidence/*_view.jpg` (four panels: off, `terrain_detail`, `terrain_cliffs`, both), panel "off":

* **Surdana**, `surdana_v5_rockface_far`: the rock face behind the tent is a vertical smear: the composite, mapped
  from above, stretched down the cliff. `surdana_v4_cliff_mid`: grass and rock patches pulled into vertical streaks on
  a hillside; `surdana_n0_hill_near`: soft, low-contrast ground with large flat patches.
* **Ravenshold**, `raven_n1_mountain`: featureless snow slopes, a rock band with no structure, hard facets along the
  silhouette and flat shading per 4 m triangle.
* **Northern Tundra**, `tundra_n6_valley`, `tundra_n1_hill`: hills as smooth blurs with visible facets, snow ground a
  single flat colour.
* **OotF Wartok Canyons**, `ootf_v4_canyon`: dark canyon walls, soft, with vertical streaks on the steep parts.

## 3. What was built

Two settings, Direct3D 11 only, both exact no-ops when off. Mechanism: `docs/reference/graphics.md`, "Terrain
detail". Shared policy and maths with unit tests: `runtime/gs/src/terrain_detail.h`
(`runtime/gs/tests/test_terrain_detail.cpp`); shader fragment `gs_terrain.inc`; Direct3D 11 glue
`d3d11_terrain.cpp`.

* **`terrain_detail`** (default 1) and `terrain_detail_strength` (0.25 to 2, default 1): the colour of every terrain
  layer is multiplied by a factor from three octaves of a procedural noise volume sampled at the pixel's world
  position, with a relief term from the volume's second channel. No projection, so nothing stretches on a cliff and
  there is no seam at triangles, cells, chunks or layers; mean 1, so far terrain converges to the old picture.
* **`terrain_cliffs`** (default 0): faces steeper than 50 degrees take their texture from the side, weighted by the
  smooth normal of the height field (a per-level texture built from `SceneGeometry`).

Decisions taken from pictures on the way:

1. A per-triangle normal for the cliff weight showed every triangle edge as a hard line. Replaced by the height
   field's own normal, interpolated per pixel.
2. Leaving the run-time composite out of the side projection (it belongs to one place in the world) kept the layout
   of grass and rock exactly, and then the setting did almost nothing: mid-range faces are drawn from the composite
   alone. With the composite included the smear on rock faces is gone, and patches on steep grass move. That is a
   change of look, so the setting is off by default.
3. The first detail amplitudes turned flat snow in the Tundra into a marbled surface and distant peaks into grain. The
   medium octave and the relief were cut to less than half and fade with distance.
4. Two more inlined copies of the texture sampler (for the two side planes) took the cold compile of the main pixel
   shader from 1.4 to 2.5 s up to 12 to 20 s. All projections are now sampled in one loop.

## 4. Pictures after (same pose, toggled inside one run)

All under `build/review/gfx-terrain/evidence/`: `<name>.png` is the pixel-exact four-panel crop (nearest-neighbour
enlargement where the name says crop), `<name>_view.jpg` a reduced copy. Panels: off, `terrain_detail`,
`terrain_cliffs`, both; the settings were switched with `setting` lines of the input script while Rynn (or Arokh)
kept the pose, 25 to 40 input samples before each shot. Source frames: `runs/ev-*/`, `runs/k4-surdana/`.

| Picture | What it shows |
| --- | --- |
| `surdana_v5_rockface_far`, `surdana_4k_v5_rockface` | A rock face about 60 m away. Detail: a fine grain, hardly visible at this distance. Cliffs: the vertical smear becomes horizontal strata with cracks; this is the clearest gain of the work |
| `surdana_v4_cliff_mid`, `surdana_4k_v4_cliff` | A steep grass and rock hillside. Detail: the patches keep their place and get a rough surface. Cliffs: the streaks are gone, **the green patches sit elsewhere and lie in horizontal bands**, and a straight seam shows at the bottom where a chunk without the side projection begins |
| `surdana_v1_snow_peaks_far`, `surdana_4k_v1_peaks` | Distant snow mountains (colour map only). Detail: slight mottling; silhouettes and facets unchanged |
| `surdana_n0_hill_near` | Ground and a rocky hill next to Rynn: the detail gives the flat patches a surface |
| `raven_n0_full`, `raven_n3_full`, `raven_n1_lower_left` | Ravenshold from Arokh's back (he hovers, so the four panels are not the same frame): rock gets slight texture, snow stays smooth; cliffs alters the rock bands of the mountain on the left |
| `tundra_n2_hill_near`, `tundra_n4_valley`, `tundra_cave_v0` | Northern Tundra: a hill face next to Rynn turns from one flat colour into a rough surface; the cave (terrain ceiling and walls) takes the detail without seams |
| `ootf_v4_canyon`, `ootf_v4_canyon_crop` | OotF Wartok Canyons (converted level), from Arokh's back at dusk: the differences are small at this distance and brightness; nothing breaks |
| `surdana_motion_walk_m0_m3.jpg` | Frames 3 input samples apart while walking and turning with both settings on |

Seen and not fixed: with the detail on, flat snow right under the camera shows a faint cloud pattern (the medium
octave), much weaker than before the amplitudes were cut; at strength 2 it is plain. Objects, characters, foliage,
water, sky and the HUD are untouched in every picture (the classification tint of `GSR_TERRAIN_DEBUG=1` covers ground
and mountains only, `runs/dbg-surdana/sheet.jpg`).

4K: a 3840 x 2160 window is cut to a 3818 x 2032 client area by the desktop; internal scale 8 (auto), DLSS quality.

## 5. Cost

| | `terrain_detail` | `terrain_cliffs` |
| --- | --- | --- |
| Texture fetches per terrain pixel and layer pass (pixels with blend weight 0 skip everything) | 3 trilinear fetches of the noise volume | 1 fetch of the volume (dither) when the detail is off, 16 point reads of the normal grid, and on faces steeper than 50 degrees one or two more samples of the layer texture, each as expensive as the draw's own (up to 16 anisotropic taps) |
| Draw calls | none added | none added |
| GPU memory | noise volume 64^3 x 2 channels with mips: 0.6 MiB; constants 256 bytes | normal grid, 4 slots x 16 bytes per grid vertex: Surdana 372 x 415 = 9.4 MiB, Ravenshold 463 x 392 = 11.1 MiB, Tundra 367 x 450 = 10.1 MiB |
| CPU per level | 64^3 noise built at first use (not timed) | grid built from the height field at level load (not timed; 141000 to 221000 triangles) |
| Host side | nothing | asks the host for the resident height field like DLSS does (motion-only model capture) |

GPU time of the terrain draws, `GSR_TERRAIN_GPU_TIMING=1` (timestamp queries around about 80 terrain draws per
frame), Surdana start looking at the mountains, 1920 x 1080 window, DLSS quality, settings cycled three times in one
run (`recipes/perf.txt`), three 300 frame windows each, ms per frame, median (range):

| Round | off | detail | cliffs | both |
| --- | --- | --- | --- | --- |
| 1 | 1.63 (1.46 to 1.79) | 3.14 (3.02 to 3.58) | 2.81 (2.65 to 3.25) | 4.16 (3.79 to 4.82) |
| 2 | 2.39 (1.85 to 2.58) | 3.82 (3.54 to 3.93) | 3.47 (3.23 to 3.92) | 4.43 (4.36 to 5.75) |
| 3 | 2.54 (2.54 to 2.60) | 5.89 (5.21 to 8.92) | 8.19 (7.57 to 9.85) | 12.40 (11.23 to 12.52) |

Round 3 ran while the GPU was busier (other agents' games), so read rounds 1 and 2: the detail costs about 1.5 ms of
GPU time per frame at this size, cliffs about 1.1 ms, both about 2.3 ms. Not measured at 4K. Frame rate was not used
as a measure: the overlay read between 17 and 93 fps for the same view over the day.

CPU: the same log gives the time inside `prepareTerrain` including its own timing calls: 0.23 to 0.64 ms per frame
with everything off, 0.30 to 0.64 with a setting on. The feature's own share is not separable from that noise.
`cpu_scene_bench.py` and `gs_replay_ab.py` were not used: captures carry no camera, so the features never run there.

Cold start: the main pixel shader compiles in 3.7 to 8.2 s instead of 1.4 to 2.5 s (three variants in parallel, once
per version, then from the disk cache), measured with `gsr_replay` and an empty cache on the loaded machine.

## 6. Off is today's picture

* `gsr_replay` at scale 3, every 20th frame hashed, this tree against the renderer built before the change
  (`build/review/perf-gpu/bin/final2`): Surdana 130 frames, OotF 204, flight 94, none different. A replay has no
  camera, so this covers the shader and draw path with the terrain flags never set, which is what "off" is.
* With both settings off `prepareTerrain` returns before it looks at the draw.
* Not compared: in-game frames off against a build without the change (frames of two runs are never equal:
  foliage wind, DLSS history).

## 7. What the WebGPU backend needs

Shared already: `terrain_detail.h` (classification, unprojection, constants, noise volume, normal grid, CPU
references), settings, ini keys. To port:

1. WGSL twin of `gs_terrain.inc` in `gs_shaders_wgsl.h`: `terrainPoint`, `terrainOctave`, `terrainDetail`,
   `terrainSmoothNormal`, `terrainSide`; the two flag bits; the projection loop around `sampleTexture` (WGSL has no
   derivative restriction problem here: the derivatives are analytic and `textureSampleGrad` takes them).
2. `wgpu_backend_impl.h`: `PF_TERRAIN`, `PF_TERRAIN_SIDE`, a `TerrainGpu` with a uniform buffer
   (`terrain::ShaderConsts`, 256 bytes), a 3D `rg8unorm` texture with 7 mips, a 2D array `rgba32float` texture
   (not filterable: read with `textureLoad`, as the HLSL does), a trilinear repeat sampler.
3. Bind group: one more group or four more entries in the draw group; the pipeline layout changes for every draw
   pipeline. This is the real work on that backend.
4. `wgpu_draw.cpp`: the call of a `prepareTerrain` twin before the constants are uploaded (the body of
   `d3d11_terrain.cpp` is 150 lines of which only the object creation and three bind calls are Direct3D).
5. `applyLive` (`F_TERRAIN`) and `initTexQuality` hooks as on Direct3D 11; set `HostCaps::terrainDetail` for it.

The tree has no mechanism that writes a shader once for both backends: `gs_shaders.h` and `gs_shaders_wgsl.h` are
hand-kept twins. What exists to keep them equal is the CPU reference in the shared header plus tests against it. A
proposal that fits the code: keep each new shader feature as its own fragment file per language next to one shared
header that owns every constant and a CPU reference, and add a GPU test per backend that renders a known input and
compares with the reference (the pattern of `test_rt_sampling_pixels.cpp`). That was not built here: there is no GPU
test of `gs_terrain.inc` against `detailFactor` / `smoothNormal`.

## 8. Options not taken

| Option | Why not | Cost to do |
| --- | --- | --- |
| Keep the ground tiles to a larger distance (the guest decides when a chunk falls back to the composite) | The largest fidelity gain left for mountains: tiles are 128 to 256 texels per cell and in the HD pack, the composite is 32. Needs the guest routine that picks a chunk's passes traced and patched, and more VU1 work per frame | 1 to 2 days, PAL addresses only |
| Build the composite ourselves from the HD tiles at a higher resolution, or sample the HD tiles in world space where the composite is drawn | Needs the per-cell tile and weight data per chunk on the renderer side (the layer blob has it) and a second texture path | 2 to 3 days |
| Far colour maps: exclude the `..TS` textures from the HD pack or rebuild them from tile means with a smooth filter | The materials agent reports brick patterns from the upscaler on them. Not looked at here | hours for an exclude list, a day for a rebuild |
| Detail derived from the layer's own HD texture (its high frequencies sampled again at a finer scale) | Per-material detail instead of one grain for grass, rock and snow. `TexBind::averageColor` exists; composites and colour maps have no tile to take it from | a day |
| Stochastic or hex tiling of the ground tiles | Tiles are placed per cell with rotations and some carry edges that must meet the neighbour; the slow noise octave was taken instead | a day plus a per-tile exclusion rule |
| Per-pixel terrain lighting | The height field normal is now available per pixel (`terrainSmoothNormal`), but the baked vertex colours already contain the light; relighting needs the bake divided out per layer light. Left to the materials work | 1 to 2 days |
| Tessellation or subdivision of terrain patches | Silhouette facets remain. The draw stream is clipped, split per texture and has no adjacency; smooth subdivision would need the terrain drawn from the resident height field instead of from the stream | a week or more |

## 9. Checked claims of the other agents

* "Near terrain is drawn from 256 px run-time composites without a pack image, so the HD tile images never reach the
  ground": not what was found at the Surdana start. The near passes are 8 bit tiles of 128 and 256 px at one cell per
  tile, and 174 of 178 textures of those sizes were replaced from the pack. The run-time composite is the 128 px 32 bit
  texture over 2 or 4 cells, live upscaled, and it is what mid-range terrain is drawn from. No palette index maps were
  in use (`palette index maps=0` in the exit line).
* "Steep and far terrain uses the far-terrain atlases": far yes (the colour map is the base of every chunk and all
  that is left far away). Steep faces near and at mid range use the same passes as level ground.
* Black blotches on paved ground with `hd_textures = 0`: not looked at.

## 10. Not established

* Motion: no video or dense frame sequence was judged. Eight frames while walking and six while turning
  (`evidence/surdana_motion_walk_m0_m3.jpg`) show nothing sliding, but the camera moves too much between them to call
  that verified. The detail is a function of the world position and the unprojection is unit tested to 0.02 units at
  300 units; DLSS ghosting on the new grain was not examined.
* In `tundra_n2_hill_near` Rynn's projected shadow is present in the off and cliffs panels and absent in the two
  detail panels. A repeat at another Tundra spot (`runs/shadow-tundra/feet.jpg`, detail switched four times) shows the
  shadow in all eight shots, and it is there with both settings on in the Surdana walk frames. Why it was missing on
  that slope was not found.
* Close to the camera the grain is plain on smooth snow (`runs/shadow-tundra/feet.jpg`): whether that reads as snow
  or as noise is a matter of taste; `terrain_detail_strength` scales it.
* Borderless fullscreen was not run; 4K was a 3840 x 2160 window (3818 x 2032 client area), Surdana only.
* Ray-traced shadows, water, GI and neural rendering together with the terrain settings: not run.
* The USA disc and levels other than Surdana, Ravenshold, Northern Tundra and OotF Wartok Canyons.
* A level whose height field is wider than 1024 cells gets no normal grid (cliffs then use face normals); none was met.
* The cliff setting leaves a straight seam where a chunk with the side projection meets one without
  (`surdana_v4_cliff_mid`, bottom); which chunks fail the tile period test (audit: "256x256 no-side cells/tile 0.00")
  was not traced.
* GPU time is measured with timestamp queries around the terrain draws on a GPU shared with other game instances.
* Which guest routine composes the 32 bit composite and when a chunk switches passes.

## 11. Later the same day: detail off by default

`terrain_detail` now defaults to 0. The missing shadow in `tundra_n2_hill_near` (section 10) is a visible fault in a
picture of this note and has no explanation yet, so the feature stays opt-in until the cause is found.

## 12. Second round: corrections, the missing shadow, ground texture distance

### Corrections to sections 1, 3, 8 and 9

A trace of the guest terrain draw (code reading of `0x158148` and its callees, one RAM dump at the Surdana start,
checked against the GS draw log; scratch under `build/review/gfx-terrain/trace/`) shows that **nothing is composited
at run time**. What sections 1 to 9 call "the run-time composite" is the outdoor rock, an ordinary disc texture
(128 px, 32 bit with alpha, material "Base Tiling Size" 4 cells, hence 32 texels per cell), drawn a second time with
texture alpha on as the material's noise pass. The "Splat Cache" is a byte budget for draw pages. Also wrong: "4 live
upscaled 128 px textures = the composites". With `GSR_TEXDUMP=1` two of the three hashes were dumped and looked at:
they are menu text and the loading bar. No terrain texture at the Surdana start lacks a pack image.

How the passes are really chosen (`docs/reference/graphics.md` has the full description): the far colour map is the
base of every chunk at full weight; every ground texture of the chunk is drawn on top while the chunk is nearer than
the material's splat fade distance (30 cells = 61440 units for most ground, 45 or 50 cells for rock), with a weight
that falls linearly with view depth from 1 at the camera to 0 at that distance. So "tiles within about 10000 units"
in section 1 was one frame's chunk content, not a threshold, and the softness of mid-range terrain is that linear
fade towards a 4 texel per cell map, plus the stretch on steep faces, plus 32 texels per cell for rock.

For `terrain_cliffs` this means the side projection does not move hand-placed art: the blend weights stay, and what
lands elsewhere is the pattern inside the tile, including the rock's alpha noise that lets the layer below through.
The patches still look rearranged against the stretched original, so the setting stays off by default.

### The missing shadow

Reproduced at the same Tundra slope (`runs/shadow-t/`, detail switched five times in place): the shadow is visible in
all five shots with detail off and cannot be made out in the five with detail on (`runs/shadow-t/sh.jpg`). Findings:

* The shadow is a pass of its own (GS draw log `runs/shadow-log`): 18 vertices, the character's render target as
  texture, clamped, depth writes masked, alpha test GREATER 2, black with a vertex alpha of 21 to 35 of 128, drawn
  after all terrain. It never takes the terrain flags (pixel flags 0x0042434b in the log), so the classification,
  the alpha-0 skip, depth and alpha are not involved. The audit added for this
  (`[terrain-audit] shadow projection`) shows the guest submitting it in 300 of 300 world frames with the texture
  from the target, with detail on and off alike.
* In the zoomed shot with detail on the silhouette is still there (`runs/shadow-t/zoom.jpg`). It is masked: the
  shadow darkens the ground by 16 to 27 %, and the detail's medium octave with its relief put a mottling of the same
  size and the same strength over a surface that is one flat colour without it.
* Fix: the medium octave is cut from 0.06 to 0.025 and its relief weight from 0.3 to 0.1 (fine 0.14 to 0.12, slow
  0.05 to 0.04). Rule and test in `terrain_detail.h`: the peak to peak swing of shadow-sized detail stays under half
  the weakest projected shadow at strength 1 (0.058 against 0.164) and under the shadow at strength 2. Tests also pin
  that the shadow's GS state is never classified as terrain.
* After the fix: `evidence/shadow_surdana_off_on.jpg` (3818 x 2032, same pose, three off / on pairs: the shadow on the
  grass is equally plain in all six) and `runs/sht.jpg` (Tundra snow at the cave mouth, two runs: Rynn's and a
  rock's shadow plain with detail on). **Not re-shot at the slope where it failed**: the walk that reached it is not
  reproducible (creatures push Rynn around) and `place` with the watched coordinates left her in the cave.
  `terrain_detail` therefore stays off by default.
* Other overlays (blood, scorch marks, footprints, water edges, the ray-traced shadow composite): not tested. They
  are separate passes too and cannot take the terrain flags unless they use the splat state exactly; a weak dark
  overlay on a flat surface would be masked the same way by the first amplitudes.
* The detail is now subtle: in the 4K panels it is hard to see beyond a few metres. That is the price of the rule.

### Ground texture distance (`terrain_distance`, 1 to 4, default 1)

`drakan_terrain_distance.cpp` wraps `0x158148` and divides the view scale the cut-off and the fade use by the factor
for the length of the call (`world/TerrainDistance.h`, `world_terrain_distance_test`). In play the log shows the scale
going from 1 to 0.5 at factor 2. It works on every renderer (it is guest side) and is PAL only.

Pictures, 3818 x 2032 client area (a 3840 x 2160 window), internal scale 8, DLSS quality, settings switched in place,
six panels each (original, distance 2, distance 4, detail, distance 2 + detail, distance 2 + detail + cliffs):

| Picture | What it shows |
| --- | --- |
| `s4k_v1_peaks` | Surdana, snow mountain across the valley: at distance 2 and 4 the snow gets its tile's veined structure where the original is a smooth gradient; the rock below changes little |
| `s4k_v5_rockface` | Surdana rock face about 60 m away: distance alone changes the snow slope above it (streaks, because the tile is stretched down the slope); the rock only loses its smear in the panel with cliffs |
| `s4k_v4_cliff` | Surdana cliff about 15 m away: distance and detail make no visible difference (the tiles are already there); only the cliffs panel removes the streaks, with rearranged patches and a straight seam at the bottom |
| `r4k_v0_full` | Ravenshold from Arokh's back (he hovers: six different frames): the mountain on the right shows more structure at distance 2 and 4 |
| `t4k_v0_full` | Northern Tundra start cave (the `place` to the valley did not take): no visible difference between the six panels, nothing breaks. **No Tundra mountain picture at 4K** |
| `o4k_v4_full` | OotF Wartok Canyons from Arokh's back at dusk (he hovers): no difference between the panels that can be told from the frame to frame change; nothing breaks. The overlay reads 5 to 15 fps in this run (4K window, other games on the GPU) |
| `motion_surdana_walk_w00_w07.jpg` | eight frames two input samples apart while walking with detail and distance 2: no trails and no pattern apart from the ground are visible, but the frames are too far apart to follow one feature; `runs/mo-r` (flying) was not looked at |

Honest size of the gain: moderate. Far slopes show the game's own tile texture instead of the colour map; steep
faces show it stretched unless `terrain_cliffs` is on; the rock stays at 32 texels per cell (128 with the HD pack).

Cost: not measured this round. The trace's estimate from the RAM dump at the Surdana start (all pages of the drawn
layers, no frustum cull) is 92 tile passes and 13763 splat vertices at factor 1, 182 and 27161 at factor 2, 225 and
31545 at factor 4, on top of 84 base passes: about twice the terrain VU1 and fill work at factor 2. No texture
memory changes (the textures are loaded regardless of distance).

### Still open after this round

* Tundra and OotF mountain panels at 4K at fixed poses; the Tundra slope after the amplitude fix.
* VU1, GS and frame time cost of `terrain_distance` in play, on the disc levels and on OotF.
* Motion judged from video; DLSS ghosting on the grain.
* What the view float at `+0x3c` is and who writes it (1.0 in play); whether a zoomed view (bow sight) changes it.
* A chunk draw that the scheduler suspends keeps the changed scale until it returns; whether that ever happens was
  not observed.
* Section 4's picture table describes pictures of the first amplitudes; of those, `surdana_4k_v4_cliff`,
  `surdana_4k_v1_peaks`, `surdana_v1_snow_peaks_far`, `raven_n3_full`, `raven_n1_lower_left`, `tundra_n4_valley`,
  `tundra_cave_v0` and `ootf_v4_canyon_crop` were never opened, and all of them are superseded by the amplitudes of
  this round.
