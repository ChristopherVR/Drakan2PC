# Model quality: PS2-era limits, detail distance and draw distance (2026-11-12)

Scope: characters, creatures, props, buildings, vegetation and the dragon (terrain and materials are other sessions'
work). Two player settings were added, `[video] lod_distance` and `draw_distance`. Tessellation was assessed and not
prototyped. Scratch data, tools, runs and pictures: `build/review/gfx-models/` (not tracked).

## 1. How models are drawn (what was read, what is inferred)

* One VU1 microprogram transforms, lights and clips everything (graphics.md section 3). Lighting is per vertex: three
  directional lights plus ambient and point lights, from per-vertex normals, output as Gouraud colours. **[disc]** The
  0x0203 record carries one normal per vertex (signed bytes). So shading is already smooth across facets wherever the
  authored normals are shared; the facets players see are silhouettes and low vertex density, not flat shading.
* The GS stream that reaches the renderer backends holds screen-space vertices (ST, RGBAQ, FOG, XYZ2). There are no
  normals and no object identity in it.
* Static props draw from 0x0203 / 0x0204, skinned models from the strip record 0x0211 (level-format.md section 11).
  **[inferred]** Skinning runs on the EE: the scene capture reads posed vertex buffers of animated models from guest
  RAM (`drakan_scene_models.h`). The exact guest function was not traced.
* The visibility path is `0x18b0c8` (per object, per world frame). It calls the draw-distance test `0x18ba28`, a
  sector / frustum test, and `0x18b5d0`, which computes the squared camera distance and calls the LOD selector
  `0x102c90` (skinned) or `0x18b760` (static object with a far model). The render method is `0x189b38`.

## 2. PS2-era budgets found, with numbers

Audit of every MOD of the English tree (`build/review/gfx-models/tools/modaudit.ts`, 3700 models) and the PAL code.

| Limit | Where | Numbers |
| --- | --- | --- |
| Character LOD distances | 0x0208 LOD table, selector `0x102c90` | 57 of 153 skinned models have 2 or 3 LODs, no static model has any. Villagers, guards, northmen, concubines: 0 / 6000 / 12000 units (24 models). Wartoks, Soulless, dragons of the NPC file, Storm Jinn: 0 / 8000 / 16000 (9). **Rynn, all seven armours: 0 / 2300** (1628 to 2056 vertices down to 208 to 560). Arokh 0 / 16000. Orcs, Scavenger, skeleton dragon 0 / 12000. Giants 0 / 25000 / 50000. Gryphons 0 / 16000 / 35000. A typical villager: 1968 / 692 / 337 polygons. |
| LOD hysteresis | `0x102c90` | 2^20 on the squared distance: a 6000 switch happens at 6087 going out and 5912 coming in. One step per call. No cross-fade. |
| Look-changing LODs | 0x0204 / 0x0211 texture indices | The eight models of the 2026-11-10 audit use sheets in lower LODs that LOD 0 does not use. The two fat villagers' projected last LOD is a separate fault (`hle::CharacterLodFixOverride`, unchanged). |
| Object draw distance, model value | 0x0200 float when `shading & 0x20000`, copied to object `+0xf8` at `0x17b3b4` | 601 of 3700 models. NPCs 35000 units (a few 25000), dragons and gryphons 110000, giants 70000, gulls and bats 10240, props 15000 to 140000. |
| Object draw distance, by size | `0x17f8d8`: `+0xf8 = radius * 70.71` (times the object scale) when the model has no value and `shading & 0x10000` is clear | Most props and all small plants. Surdana RAM dump: median 7241 units over the 161 evaluated objects. A flower of radius 60 disappears at 4200 units. |
| Object draw distance, set by code | setter `0x17d2c8` (mode bits 0x30000000 at object `+4`) | 508 objects in Surdana; who sets it was not traced. |
| Level view distance | float at `0x4de968`, in cells, times 2048 | 75 cells = 153600 units in Surdana and at the flesh-eater spot. It caps every object's distance inside `0x18ba28`. |
| Distance fade | `0x18ba28` sets object flag 0x200 beyond 0.75 of the draw distance; the draw call passes the distance to VU1 (`0x1e9bb0`, microcode flag 0x800) | The last quarter of the draw distance fades. |
| Projected character shadow | seen in pictures only | A villager at 7500 units has no shadow on the original LOD 1 and has one when kept on LOD 0 (`evidence/lod/F_07500_zoom.png`). The rule in the code was not traced. |
| Counts at one Surdana spot | RAM dump, `tools/objstats.py` | 4712 world objects resident, 244 skinned; 220 of the 388 objects with a model draw distance were hidden by distance. |

Not established: animation update rates of distant actors, the shadow caster limit, vegetation density rules beyond the
size-based draw distance, and what `0x15cba0` (another loop that reads `+0xf8`) is for.

## 3. What was done

`drakan_model_detail.{h,cpp}` wraps four guest functions (PAL addresses only) and changes the numbers they compare for
the length of the call. Rules: `runtime/world/include/world/ModelDetail.h`, test `world_model_detail_test`.

* **Character detail distance** (`[video] lod_distance`, env `DRAKAN_LOD_DISTANCE`, 1 to 8, default 4): the squared
  distance handed to `0x102c90` and `0x18b760` is divided by factor squared. Above 1, a model whose lower LOD uses a
  material that LOD 0 does not use never reaches that LOD. The rule is read from the resident model (per LOD, the
  material indices of its polygons), so it follows mod packs and the USA data without an id table. The scene capture
  reads the controller's active slot and follows by itself.
* **Object draw distance** (`[video] draw_distance`, env `DRAKAN_DRAW_DISTANCE`, 1 to 4, default 1 = original): object `+0xf8`
  is multiplied for the calls of `0x18ba28` and `0x189b38` and put back when the call returns (also across a scheduler
  unwind and resume). The stored value never changes, so saves keep the disc's numbers. The level view distance still
  caps it in the guest code.
* At factor 1 the hooks call the original functions with untouched arguments.
* `DRAKAN_MODEL_DETAIL_LOG=1` prints, every 5 s, the characters seen by the selector per LOD, their polygon and vertex
  sums, and how many objects the distance test hid.
* PC Settings: Display page, section "Models". Both settings apply at once.

No backend code was touched: Direct3D 11, WebGPU and Vulkan get the same geometry from the same VU1 stream.

## 4. Pictures (owner's ini; a 1280x960 window unless marked 4K)

All under `build/review/gfx-models/evidence/lod/`, "orig" = both factors 1, "new" = LOD 4 and draw distance 1.5 (the
defaults at the time of the runs; the shipped default for the draw distance is 1), same exe.

* `F4k_30000_guard.png` (3840x2160, crop x3): a Surdana guard at middle distance. Original: LOD 1, flat chest sheet,
  no knee and shin plates. New: the full model.
* `F4k_07500_villager.png` (4K), `F_07500_zoom.png`, `F_12500_zoom.png`: the fat villager at 7500 and 12500 units.
  Rounder arms and trousers; at 1280x960 the difference is a few pixels.
* `rav_rider_zoom.png`: Rynn riding Arokh in Ravenshold. The original draws her 387-polygon LOD (the camera is more
  than 2300 units away); new draws the 3068-polygon model. She is about 30 pixels tall in the window.
* `F_12500_full.png`, `F4k_07500_small.png`: more flowers and grass tufts on the slope with draw distance 1.5.

Honest limits of these pictures: one villager type, one guard, Rynn mounted; no creature, no prop close-up, no frame
sequence in motion, no OotF picture comparison (the OotF start view has Rynn alone). The village centre view was
blocked by buildings (`runs/v-*`).

## 5. Cost

Exact counts from `DRAKAN_MODEL_DETAIL_LOG` (still views, same poses):

| View | Characters, polygons orig | new | Objects hidden by distance orig | new |
| --- | --- | --- | --- | --- |
| Surdana, villager at 7500 | 3: 4832 | 5878 | 267 of 371 | 233 |
| Surdana, 20000 stop | 3: 4748 | 4 (one more drawn): 7024 | 284 of 410 | 251 |
| Ravenshold start, mounted | 2: 3184 | 5865 | 159 of 176 | 139 |
| Northern Tundra start | 5: 11950 | 6: 12742 | 71 of 474 | 40 |
| OotF Ruined Village start | 1: 3068 | 3068 | 32 of 98 | 25 |

Frame rate and CPU per frame: section 8.

## 6. Defaults and why

* `lod_distance = 4`: a villager then switches at 24000 and 48000 units. At 24000 units a character is about 45 pixels
  tall at 2160 lines with the owner's field of view, and NPCs stop being drawn at 35000 (52500 with the new draw
  distance), so the coarse LODs are rarely seen and the switch is hard to see. Rynn switches at 9200, beyond the riding
  camera. Always-LOD-0 (8 on the slider for most models) costs polygons for figures under 45 pixels with no visible gain.
* `draw_distance = 1` (original): 1.5 draws only 20 to 35 more objects per view, mostly grass and flowers, yet costs
  42 % more VU1 work in the Surdana meadow and 3 costs 2.8 times (section 8). That is not a default; the slider goes
  to 4 for players with CPU to spare.

## 7. Options not taken

* **Tessellation (PN triangles, Phong tessellation, offline subdivision): not prototyped.** A backend cannot do it:
  the GS stream has no normals and no mesh identity, and the scene capture and DLSS motion data would no longer match
  the raster. The only clean place is the model records, through a locally generated pack: subdivide 0x0203 / 0x0204,
  rebuild the 0x0211 strips within the 225-slot window, interpolate the 0x0208 weights, keep 0x0207 untouched. The
  TypeScript encoders for all four records exist (`editor/packages/formats`, used by the OotF converter). Cost if
  done: four times the polygons of each treated model in guest heap and VU1 time; risks: the 32 MB guest heap, weight
  interpolation at joints, hard edges and UV seams, attachment nodes. This is a multi-day item and no picture exists.
* **LOD cross-fade**: not done. It needs two LODs of one skeleton drawn in one frame, which the controller does not
  support. With factor 4 the remaining switches are small on screen.
* **Persistent override of the 0x0208 distances** (generalising `CharacterLodFixOverride`): rejected in favour of the
  selector hook, which is live-adjustable and needs no record parsing.
* **Raising the level view distance**: terrain agent's area; not touched.

## 8. Benchmark, tests and builds as they were

`scripts/perf/cpu_scene_bench.py`, scene `surdana` (meadow start, still view), owner's ini in a 1280x960 window
(`build/review/gfx-models/owner-windowed.ini`), one exe, 2 interleaved repeats of 15 s, 5 to 9 other games running.
World fps moved between 19 and 84 with the machine load and is not a result; the VU1 pair count is exact.

| Variant | VU1 Mpairs per frame | VU1 worker CPU ms per frame | EE CPU ms per frame | world fps (range) |
| --- | --- | --- | --- | --- |
| series 1: original | 3.82 | 7.77 | 10.68 | 58.8 (46.8 to 70.8) |
| series 1: LOD 4 + draw 1.5 | 5.47 | 10.95 | 14.37 | 38.4 (26.5 to 50.3) |
| series 1: LOD 4 + draw 3 | 10.56 | 20.38 | 15.23 | 30.2 (19.3 to 41.2) |
| series 2: original | 3.82 | 8.01 | 12.36 | 38.9 (34.3 to 43.5) |
| series 2: LOD 4 only | 3.85 | 7.77 | 12.60 | 32.2 (27.0 to 37.4) |
| series 2: LOD 8 only | 3.92 | 8.29 | 11.78 | 53.5 (28.5 to 78.5) |
| series 2: draw 1.5 only | 5.41 | 10.03 | 10.32 | 68.3 (52.6 to 83.9) |

So the LOD factor costs 0.8 % (4) to 2.6 % (8) more vertex work here and the draw distance 42 % (1.5). In series 1
both pairs were slower with draw 1.5 (70.8 to 50.3, 46.8 to 26.5 fps); in series 2 the load dominated.

Tests: `world_model_detail_test` passes; the standalone `runtime/settings` tree (31 tests incl. `settings_test` and
the schema tests, 2 data tests skipped) passed before the default of the draw distance was changed and was rerun
after it (see the session report). clang-format is clean on the new files. Game builds: one failed on a compile
error of the new file, then incremental builds passed. Runs ended clean in Surdana, Ravenshold, Northern Tundra and
the OotF Ruined Village with both settings on and off.

## 9. Not verified

* "Off equals today's picture" rests on the code path and the unit test (identity at factor 1), not on a pixel
  comparison: animated scenes are not bit-repeatable between runs.
* The look-changing rule was not seen in a picture (no run had one of the eight models in view; the log showed
  "look rule on 2" once at the Surdana start).
* No combat scene, no co-op, no USA disc (hooks are not installed there), WebGPU and Vulkan not run.
* The draw-distance scope across a scheduler unwind was not exercised on purpose.
