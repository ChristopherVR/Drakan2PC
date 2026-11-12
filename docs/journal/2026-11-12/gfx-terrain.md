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
  frame has about 40 to 90 terrain draws. Vertex colours are the baked lighting (per 4 m vertex, Gouraud), FOG is per
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
* **OotF Wartok Canyons**, `ootf_v3_canyon`: dark canyon walls with vertical streaks.

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

PICTURES

## 5. Cost

COST

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
* Borderless fullscreen was not run; 4K was a 3840 x 2160 window.
* Ray-traced shadows, water, GI and neural rendering together with the terrain settings: not run.
* The USA disc and levels other than Surdana, Ravenshold, Northern Tundra and OotF Wartok Canyons.
* A level whose height field is wider than 1024 cells gets no normal grid (cliffs then use face normals); none was met.
* The cliff setting leaves a straight seam where a chunk with the side projection meets one without
  (`surdana_v4_cliff_mid`, bottom); which chunks fail the tile period test (audit: "256x256 no-side cells/tile 0.00")
  was not traced.
* GPU time is measured with timestamp queries around the terrain draws on a GPU shared with other game instances.
* Which guest routine composes the 32 bit composite and when a chunk switches passes.
