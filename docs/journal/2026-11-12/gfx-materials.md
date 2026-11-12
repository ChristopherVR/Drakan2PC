# Texture and material quality: pack audit, refit to the source, material maps, relief shading (2026-11-12)

Request: "improve the overall models and texture quality, especially mountains ... we should be able to improve graphics
based on today's standards", with engine-neutral content and UE5-style PBR textures as the standing direction. This
entry is the texture and material part (two other agents had terrain and model geometry). Scratch, runs and pictures
are under `build/review/gfx-materials/` (not committed): `runs/` (game runs), `evidence/` (pictures and crops),
`work/` (index, audit tables, logs), `tools/` (the audit scripts), `tex_refit/` (the refitted pack with its
`materials/`), `recipes/`.

Machine: RTX 5080, shared with six to eight other game instances and other agents' builds during every run; the GPU
sat at 99 % and the disk was at times so busy that listing a folder took two minutes. Frame rates in the shots
(14 to 65 fps) say nothing about the change.

## 1. What was there

* Every disc texture (2,962 unique) and every texture of the installed Order of the Flame pack (2,409 more) has a
  Real-ESRGAN x4 image in the owner's folder. Models used (`tools/hdpack/realesrgan/models`): `realesrgan-x4plus`
  (33 MB) for everything except the HUD and effect files (INTERFCE, SYSTEM, DEBUG, FX, MAGIC, PS2MUSIC), which take
  `realesrgan-x4plus-anime` (9 MB); every non-constant alpha plane goes through the anime model as a grey image.
  Also on the machine and unused: `realesr-animevideov3` x2 / x3 / x4 (1.2 MB each, a video model, softer). No other
  model was downloaded; candidates are listed in section 9.
* The GS path draws pre-lit textured triangles: per draw one texture, per vertex a colour (the game's lighting), ST,
  Q and fog. No normal, tangent, world position or light. Separately the host publishes the world camera always and,
  with DLSS or a ray effect on, a lighting snapshot (sun vector, colour and ambient of the terrain layer under the eye
  and its point lights, `drakan_scene_models.h`).
* `analysis/scripts/pbr_pack.py` (2026-11-01) already wrote UE5-named BaseColor / Normal / ORM sets for the editor and
  the export, by material name; the renderer did not use them.
* The HD index is keyed by a 64 bit hash of the decoded guest texture (pixels, width, height), with `alias.txt` for
  pictures whose palette alpha the game edits. The key depends on the picture only: not on the disc region, the level
  file or the pack. (The USA disc was not hashed here; the statement follows from how the key is made.)

## 2. Audit of the pack

Two parts: the pack against its sources, pixel exact (`tools/pack_audit.py`, `tools/refit_explore.py`, tables in
`work/audit_disc.json`), and the game at the owner's settings (`runs/audit_surdana_4k`, 3840 x 2160 borderless, DLSS
quality, internal scale 8, `texture_upscale = auto`, anisotropic 16, HD on and off at the same poses).

Pack against sources, disc pack, per category (source file): colour shift = largest channel difference of the mean
after averaging the image back down; detail = gradient energy against a bicubic enlargement of the source.

| Category | Textures | Colour shift median / p90 / max | Detail median / p10 / p90 | Worst seam ratio p90 (tiling) |
| --- | --- | --- | --- | --- |
| characters (NPCS) | 331 | 3.1 / 7.2 / 14.1 | 1.60 / 1.11 / 2.36 | 1.38 |
| effects (FX, MAGIC) | 174 | 2.8 / 7.7 / 19.5 | 0.87 / 0.01 / 1.93 | 2.53 |
| items (RESOURES) | 420 | 3.6 / 9.0 / 21.2 | 1.41 / 0.67 / 2.73 | 1.51 |
| far-terrain atlases (`*TS`) | 757 | 3.8 / 6.0 / 10.4 | 1.95 / 1.21 / 4.21 | 1.28 |
| terrain tiles (PROCTEES) | 142 | 4.1 / 12.2 / 18.5 | 1.47 / 0.89 / 2.86 | 1.47 |
| HUD (INTERFCE, SYSTEM) | 135 | 3.6 / 15.2 / 31.0 | 1.37 / 0.83 / 2.66 | 2.43 |
| world and level objects | 1003 | 3.5 / 8.3 / 23.8 | 1.67 / 0.85 / 3.68 | 1.45 |

Findings (pictures in `evidence/audit/`):

1. **Colour drift.** 573 of 2,962 images differ from their source by more than 6 levels in mean colour, 177 by more
   than 10 (`pack_colour_shift.png`). Terrain tiles drift most (p90 12 levels) and their far-terrain atlases drift
   differently, so the two no longer agree in colour.
2. **Invented content on small textures.** On 32 and 64 px noise-like sources the network draws something else at the
   right mean colour: sand becomes a stone wall, rust becomes brickwork, a dirt tile a cracked wall with a tree line
   (`pack_colour_shift.png`, lower row). 401 non-HUD images have more than three times the gradient energy of their
   source, 173 of them on sources of 64 px or less outside the atlases. This changes the art, not its fidelity.
   Not fixed: see section 9.
3. **Lost grain.** 231 images are flatter than their source (snow, sand: `refit_src_before_after.png`, first column).
4. **Soft alpha.** Smoke, cloud and glow textures outside the HUD files have their picture in alpha; the anime model
   draws shards and strokes into it (`pack_highrelerr.png` third column, `pack_lowcorr_a.png` last column). 360 such
   planes in the disc pack.
5. **Far-terrain atlases.** The 757 `*TS` textures are low-resolution mosaics of ground tiles with a magenta filler.
   The network turns the blurred tile thumbnails into brick patterns and smears the filler border into streaks
   (`pack_ge_2p5_3p5.png`). These are what distant terrain and the mountain faces use. Not changed here (terrain
   agent).
6. Not faults: garbage in the colour under fully transparent texels (never sampled; an earlier version of the audit
   counted it), seams on textures that only looked tileable to the edge test (atlases of items).
7. **Tiling seams and cut-out fringes**: no seam beyond p90 1.5 on textures that really tile. Cut-out coverage
   (share of texels at or above half alpha) differs from the source by more than 2 % on 89 of the 495 textures with
   alpha and by more than 5 % on 28; not looked at in game, not changed.

In the game (`evidence/audit/s1_*`, `s2_*`):

| Category | With the pack at 4K | Note |
| --- | --- | --- |
| Stone walls (tower) | good: clean stones, crisp mortar | |
| Bark, wood | good | |
| Rynn (skin, cloth, leather) | good | not compared texel for texel |
| Grass tiles | sharp but stringy: long painted strokes | the upscaler's look, not a fault of fidelity |
| Leaves | painterly blobs with hard cut-out edges | |
| Paved floor near Rynn | soft, mottled | the ground is a run-time splat composite, see below |
| Steep rock faces | smeared streaks | texture stretched along the slope (terrain agent) |
| Mountain face next to Rynn | a blur: one far-terrain atlas texel covers a hand's width | the pack cannot help (terrain agent) |
| HUD | good | |

Two facts behind the terrain rows, from the texture log: (a) near terrain is drawn from 256 px textures the game
composites at run time from its tile set (its "splat cache"); they are palette variants without a pack image and take
the HD index map path with the live upscaler as guide, so the Real-ESRGAN tile images never reach the ground;
(b) steep and distant terrain uses the far-terrain atlases of finding 5.

Found on the way, not fixed: with `hd_textures = 0` the live upscaler leaves black blotches on the paved ground in
Surdana (`evidence/audit/s2_a_floor_hd_vs_off.png`, right half), where a splat layer has black texels with alpha 0.
It does not show with HD textures on.

## 3. What per-pixel shading is possible on the GS path

Q is 1 / w and linear in screen space, so the view-space position of a pixel is the view ray through it divided by Q.
Its screen derivatives give the face normal; with the UV derivatives they give the directions of u and v on the
surface. That is a full tangent frame per pixel with no change to the vertex stream (`GSR_MATERIAL_DEBUG=1` and `4`:
`runs/dbg1`, `runs/dbg4`; faces come out flat and correctly oriented, u runs along the walls).

Lighting must not be applied twice: the vertex colour already carries the game's light. So the shader only moves
light between the slopes inside a texture: factor = 1 + strength * (n.x * T.L + n.y * B.L), with L the level's sun.
A flat normal gives exactly 1, the mean of a texture does not move, and the result still goes through the vertex
colour and fog. `runs/dbg3` shows the term alone: stones raised and lit from the same side on both visible faces of a
tower.

## 4. What was done

1. **Refit of the pack to its sources** (`hd_pack.py refit`, also the last step of `upscale`): colour drift removed,
   grain restored where it was lost, soft alpha planes taken from the source. Reference: `hd-textures.md` 11.
2. **Material maps** (`analysis/scripts/material_maps.py`): `<hash>_n.png`, `_orm.png`, `_h.png`, `materials.json`
   and `materials.txt` in `<textures>/materials`. Reference: `hd-textures.md` 12.
3. **`material_detail`** in the Direct3D 11 renderer: relief shading from those maps. Reference: `hd-textures.md` 12,
   `graphics.md` "Material detail".
4. Builder: `extras.py` runs the maps after the upscale (step `hd-materials`); the setup texts carry the new totals.

## 5. Results

Settings of every game run: the owner's `drakan.ini` (Direct3D 11, DLSS quality, scale auto = 8 at 3840 x 2160,
`texture_upscale = auto`, anisotropic 16, HD textures on, foliage wind on, borderless fullscreen 3840 x 2160). What
differed: the window was kept behind the others (`DRAKAN_NO_ACTIVATE`), audio off, an empty memory card, a direct
level start; `GSR_TERRAIN_DETAIL=0` and `GSR_TERRAIN_CLIFFS=0` (the terrain agent's new settings default to on in the
tree and are not in the owner's ini); the `elf =` line points at a private copy; `GSR_TEXDIR` points at the refitted
pack for the "after" runs. Shots are the presented 3840 x 2160 image.

Matching: two shots 44 frames apart with identical settings differ by 0.3 to 2.8 levels on average (wind in the
foliage, the idle animation of Rynn, a slow camera drift, DLSS). Within one run a pair is therefore near-matched, not
pixel-matched; between runs the same `place` often ends in another camera position. Material on / off pairs were taken
within one run by switching `material_detail_strength` between 0 and 1 (strength 0 is the same arithmetic result as
off and needs no texture reload).

**Refit, offline (exact):** `evidence/audit/refit_src_before_after.png` (source, owner's image, refitted image): snow
gets its grain back, two smoke puffs lose the shards, a foliage tile and a medallion return to the source's colour,
cobbles stay as they were. Disc pack: 2,962 images refitted, 231 had lost their grain, 360 soft alpha planes, largest
colour drift 30.7 levels; both packs: 5,371 images, 290 and 538.

**Refit, in game:** `evidence/refit/tower_*_owner_vs_refit.png` (Surdana tower, the owner's pack against the refitted
one, material detail off, two runs with slightly different camera positions). The stone wall and roof look the same
to the eye; mean colour of the crops moves by 1 to 7 levels, which includes the pose difference. No before / after of
a texture that changed strongly (snow, smoke) was taken in game: the offline pictures are the evidence for those.

**Material detail, Surdana** (`evidence/range/`, `evidence/surdana/`):

* `range/tower_strength.png`: strength 0, 1, 1.5, 2 on the tower wall within 100 frames. At 1 the stones gain a little
  surface relief and their upper edges catch light; clean, easy to miss without the pair. At 1.5 it is plainly
  visible and still reads as stone. At 2 it starts to look gritty and embossed.
* `range/tower_modes.png`: strength 0 against mode off (mean difference 0.35 levels in the crop, the noise floor),
  specular 0.5 (no visible difference on rough stone), mode all.
* `surdana/s_village_c_wall_x3_off_on.png`: a house wall at three times zoom, off and on at strength 1: more grain on
  the stones, slightly deeper joints. The camera drifted a few pixels between the two.
* `surdana/s_village_b_floor.png`: the paved ground. No difference between the three columns: the ground is a
  run-time composite without a pack image, so neither the refit nor the material reaches it.
* `range/village_strength.png`: a cliff in front of a wall: the cliff (terrain) does not change, the wall does.
* `range/tower_rynn_all.png`: mode `all` with Rynn in view. She changed pose between the shots, so this only shows
  that nothing breaks on her textures; skin and leather were not compared texel for texel.
* Debug views: `runs/dbg1` (face normals), `runs/dbg3` (detail term), `runs/dbg4` (u direction), crops
  `evidence/work_dbg23_tower.png`.

**Material detail, Order of the Flame, Ruined Village** (`evidence/ootf_rv/`): 334 maps loaded and 80 % of the draws
shaded (the converted ground uses pack textures, unlike the disc's). In the night scene the effect is close to
nothing: `rv_houses_wall_brightened_x3_off_on.png` (brightened three times for inspection) shows marginally sharper
stone edges and no artefact. The largest differences in `rv_start` and `rv_hall` are an orc and Rynn moving.

**Kragmor and Ravenshold** (`evidence/kragmor/`, `evidence/ravenshold/`, start position, four headings, a short
forward flight, four more): both levels start on Arokh in flight, so the pairs differ by wing beats and drift and
show landscape from the air. 96 and 103 maps were loaded and 18 % of the draws shaded; no artefact is visible in the
full frames that were looked at (`k_1`, `r_5`), and no material change can be told from the motion. These runs do not
count as matched evidence.

**Motion:** not obtained. `evidence/motion/sequence_16_frames.jpg` (running through the village at 2 to 9 frames per
second on the loaded machine: no blotches or flicker on shaded bark and wall, but the frames are too far apart to
judge shimmer); two strafe sequences of 12 consecutive frames turned the camera away from the wall
(`strafe*_crops.png`).

**What it looks like, plainly:** on masonry and roofs in daylight the relief is a modest gain at strength 1 and a
clear one at 1.5; it does not look plastic because there is no highlight and the mean brightness of a texture does not
move. It does nothing for the ground and the mountains of the disc levels, which are what the request named first.
Painted lettering and stains would be embossed like stones (seen in the maps of signs,
`work/mat_test/contact.png`); none was in view in the game shots.

Seen during the runs, not caused by this work: in 4 of 9 long runs the game returned to the title screen a few
seconds after a `place` (or during the Kragmor flight), once with `GSR_MATERIAL_DETAIL=off`. With an empty memory
card a death ends at the title; a teleport that drops Rynn is the likely cause. The shots after such a point were
discarded (`runs/ev_surdana`, `ev_surdana2`: the tower poses).

## 6. Cost

| What | Measured |
| --- | --- |
| Refit, time | 307 s for the 2,962 disc images on one core; 1,178 s for all 5,371 while the disk was saturated by other work |
| Refit, disk | pack PNG 2,117 MB before, 2,194 MB after (+77 MB): restored grain compresses worse |
| Material maps, time | 179 s for the disc pack (1,540 materials) on six processes; 307 s for both packs (3,108 materials of 5,371 textures) |
| Material maps, disk | 462 MB for both packs: normal 233 MB, ORM 143 MB, height 86 MB (the game does not read the height) |
| Material maps, VRAM | 59 MB (124 maps, Surdana village), 95 MB (334 maps, Ruined Village), 43 and 46 MB (Kragmor, Ravenshold), 121 MB (235 maps, mode `all` included). Texture cache peak in that last run 451 MB of the 6,144 MB budget. Upper bound with every map of both games resident: 1.2 GB |
| Load | two more PNG decodes per texture on the worker pool, in the job that loads the HD image; not timed |
| Draws | Surdana 161,531 of 558,265 draws shaded (29 %), Ruined Village 80 %, Kragmor and Ravenshold 18 % |
| Shader | the material variant adds one anisotropic (8x) sample and about 60 arithmetic instructions per shaded pixel; with the setting off the other variant is bound and nothing is added |
| Frame time | not measured. A GS capture replay has no world camera, so the shading does not run in `gsr_replay` and `gs_replay_ab.py` cannot compare it; live frame rates were 14 to 65 fps with eight games on the GPU |

## 7. Defaults

* **Refit: on** in the pack build (`--no-refit` turns it off). It moves every image toward its source and nothing
  else; the unit tests pin that a faithful image changes by at most one level. A player gets it by running the HD step
  again (the upscale skips what exists, the refit then runs once over the whole pack) or `hd_pack.py refit`.
* **Material maps: built** with the HD pack (they are files; nothing uses them unless asked).
* **`material_detail`: off.** The relief is clean in the still pictures and bounded by construction, but its look in
  motion at a real frame rate was not obtained, it was matched in one disc level and one converted level only, and it
  adds shading that is not in the art. Until someone has looked at it in play it is opt-in: PC Settings > Video >
  Material detail > Hard surfaces. `hard` rather than `all` is the first choice because bump from luminance on faces
  and cloth was not examined.
* **`material_detail_strength`: 1.0**, the conservative end of what reads as stone; 1.5 is the value to try for a
  visible change.
* **`material_specular`: 0.** Roughness is a per-class guess; on the rough classes it does nothing visible, and no
  smooth material was examined.

## 8. What the WebGPU backend needs

Shared already (`material_detail.h`): the table, the class mask, `eligible`, `makeConsts`, `packTexels`,
`downsample`, the light choice and the CPU mirror of the shader. To do in `wgpu_*`:

1. `initMaterial` / `scanMaterials` / `materialFor` equivalents (copy of the first 60 lines of `d3d11_material.cpp`;
   no API in them except the log).
2. `UpDone` / `UpEntry` / `TexBind` in `wgpu_backend_impl.h`: the material levels, texture and class, as in
   `d3d11_backend_impl.h`; in `wgpu_textures.cpp` the same five hooks as in `d3d11_textures.cpp` (key bit 20, load in
   the job, create the texture in `pushDone`, adopt in `pumpUp`, hand over in `resolveTexture`).
3. A WGSL port of `gs_material.inc` in `gs_shaders_wgsl.h` (`dpdx` / `dpdy`, `textureSampleGrad`), called in
   `sourcePixel` where the HLSL calls it, as a second pipeline variant; a uniform block for `material::Consts`
   (80 bytes), one more texture and one more sampler binding (wrap / clamp per axis, anisotropic).
4. The pipeline key of `wgpu_draw.cpp` needs the variant bit; the draw calls `eligible` and `makeConsts` like
   `prepareMaterial`.
5. The lighting snapshot (`nativeModels->lighting`) is already delivered to that backend.
6. Run `gsr_test_material_detail` there too (it has no D3D part except the shader compile).

Not started: none of the above.

## 9. Options not taken

* **Another upscaler model for small noise-like textures** (finding 2). Not downloaded; from memory, to be checked
  before use: `realesr-general-x4v3` with its denoise-strength companion (xinntao/Real-ESRGAN release v0.2.5.0,
  BSD-3-Clause, about 5 MB each as `.pth`; an ncnn conversion is needed, the release has none); community ESRGAN
  models such as `4x-UltraSharp` (about 67 MB, CC BY-NC-SA 4.0: non-commercial, probably not usable here) or the
  `Nomos` family (about 67 MB, CC BY 4.0). Cost: a conversion step, 30 to 60 min of GPU for a full pack per model, and
  a review of several hundred textures per model. The cheaper route is a guard: where the network's image has more
  than about three times the gradient energy of a 32 or 64 px source, keep a Lanczos enlargement with the network's
  image blended in at low weight; half a day with a review sheet.
* **Material maps for the run-time ground composites** (near terrain has no pack image, section 2). The same filter
  could run in the texture job on the upscaled composite (a C++ port of about 150 lines, about 10 ms per 1024 px
  texture on a worker). Belongs with the terrain work; see section 10.
* **Parallax / height-based contact shading.** The height maps are written, the shader does not use them: with
  heights guessed from luminance, parallax slides painted detail sideways and breaks at triangle edges without
  smoothed normals. Not tried.
* **Point lights and the ambient colour in the relief term.** Only the sun (or the fallback direction) is used. Indoor
  scenes lit by torches get relief from a direction that is not the visible light's. Cost: the snapshot already has
  the three nearest point lights; a view-space light list in the constants and a loop in the shader, half a day plus
  pictures.
* **x8 images** and **repainting**: as in `ootf-texture-quality.md` section 8.

## 10. For the terrain and model work

* Interface for terrain: a texture's material travels with its HD copy. `TexBind::materialSrv` (D3D11) is the packed
  map (R, G = normal x, y with 128 = 0, +y toward +v; B = occlusion with 200 = the map's mean; A = roughness), with
  mips, sampled with the colour texture's own coordinates divided by the logical size; `TexBind::materialClass` is
  `material::Class`. `material::Table::find(hash)` answers for any pack hash. In the material variant of the shader
  the map is `MatTex` (t3) with `SmpMat` (s3), constants `CBM` (b4); terrain detail uses b3 / t2 / s2, so both can be
  bound for one draw. Draws flagged `PF_TERRAIN_SIDE` are left out of material shading because their colour is
  sampled with other coordinates; if the side projection should carry relief, sample `MatTex` with those coordinates.
* The terrain tile set (PROCTEES, 142 textures, class `terrain`) has maps, but the ground rarely samples those
  textures directly (splat composites). To give the ground relief from its tiles, either derive the map from the
  composite at load time or have the terrain shader sample a tile's map in world space.
* Far-terrain atlases have no maps on purpose (class `none`).
* For the model work: characters and foliage have maps (`character`, `foliage`) but are only shaded in mode `all`.
  The frame is per face; a smoothed normal from the model would not change the detail term (it only uses the tangent
  directions) but would be needed for a clean specular highlight across faces.

## 11. Tests and builds

As they ran, on this machine:

* `gsr_test_material_detail` (new): 68 checks pass, including the compile of both pixel shader variants with and
  without `MATERIAL`.
* Private `runtime/gs` build (`build/review/gfx-materials/gs`, DLSS module off): `ctest` 44 tests, 43 pass, `live`
  skipped; includes `synthetic` and `terrain_detail`.
* Private `runtime/settings` build: `ctest` 31 tests, 29 pass, `gameui_ram_test` and `hle_test` skipped;
  `settings_test` 457 checks; the `ctest` run was before the default of `material_detail` changed from `hard` to
  `off`, `settings_test` alone was built and run again afterwards (463 checks, another agent added some).
* `python scripts/run_tests.py`: exit 0. New: `test_material_maps.py` (7 tests), `test_hd_pack_refit.py` (8 tests);
  changed: `test_builder_extras.py` (plan, estimate text, the material step after the upscale).
* `python -m ruff check`, `ruff format --check` on the touched Python files, `scripts/repository_guard.py`: clean.
* `clang-format`: the three new C++ files are formatted; in files shared with other work only the hunks of this
  change were formatted.
* Game: incremental builds under the build lock into `build/review/gfx-materials/play`. One failed in
  `d3d11_backend.cpp` on a line of this change (a newline inside a string literal) and was fixed.
* The root `runtime-tests` preset and the WebGPU test targets were not run.

## 12. Not established

* The look of material detail in motion (shimmer, DLSS ghosting, specular flicker): no usable sequence.
* Frame cost of material detail: not measured (section 6).
* Matched material pictures exist for Surdana (walls, roofs) and one wall of the Ruined Village only. Kragmor and
  Ravenshold were run but show flight over terrain. Interiors, caves and torch-lit scenes were not run; there the
  relief is lit by the outdoor sun direction or the fallback, not by the visible light.
* Characters, foliage and metal in mode `all`, and any specular highlight: not examined beyond "nothing breaks".
* In-game before / after of the refit on a texture it changed strongly; the effect on effects (FX, MAGIC), whose soft
  alpha planes it replaces, was not seen in game at all.
* Whether the classes are right: counts only (both packs: wood 975, terrain 853, surface 591, character 319, stone
  297, foliage 61, metal 12, none 2,263). "wood" certainly holds brown things that are not wood.
* The direction convention of the captured sun (`SceneLight::vector` points to the light) is taken from the ray shader
  and the terrain code; the detail-term view agrees with a light from above, which does not separate east from west.
* The USA disc: the key is region independent by construction; no USA texture was hashed.
* With DLSS off and no ray effect the lighting snapshot is not delivered and the fallback light is used; not run.
* Ray-traced shadows together with material detail: not run.
* The title-screen returns in the harness runs: cause inferred (death after a teleport), not shown.
* Windowed mode and resolutions other than 3840 x 2160: not run.
* The builder end to end (`Build-Drakan.ps1 -HdTextures on`) with the new step: only the unit test with a fake runner;
  the two scripts were run by hand on the real packs.
