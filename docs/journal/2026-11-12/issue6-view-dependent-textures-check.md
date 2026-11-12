# Issue 6 item 2: check of the stale render target fix (2026-11-12)

Second look at [the 2026-11-10 note](../2026-11-10/issue6-view-dependent-textures.md) and its uncommitted fix in
`runtime/gs` (`rt_texture_source.h`, `findRTForTexture` / `rowsFromMemory` in both backends). Runs, logs and images
stay private under `build/review/issue6-2/` (not committed).

## Result

- The empty health orb is reproduced in the live game on the unfixed build and is gone with the fix, with the owner's
  `drakan.ini` and in the reporter's configuration. The cause named in the 2026-11-10 note is confirmed live: in the
  exact frames where the orb is empty the HUD atlas is sampled from an old 128 x 512 target at row 480.
- The fix was incomplete in one place (below); fixed and covered by a test.
- The ray-traced shadows trigger was not reproduced. Toggling the setting did not make anything worse in three runs.
  The reporter's "worse after enabling, stays until restart" is not explained by anything seen here.
- The reporter's blocky hillside was not reproduced as such, and no world texture was shown to change between the
  unfixed and the fixed build in a matched view (see "Not established").

## Review of the fix

- **Content correctness.** Sampling a target and decoding from memory give the same content, as long as the decode
  path flushes dirty targets first: `resolveTexture` calls `flushForRead` (or `aliasedTarget` copies the drawn blocks)
  before decoding, and `syncRT` keeps clean targets equal to memory. So declining a target can only cost resolution
  (draws decoded at native size) and readbacks, never show stale content. Accepting a target wrongly is what breaks:
  rows past its end (empty orb), no mip levels, no wrapping inside the sub-rectangle, no HD image.
- **The half rule** (`rows.second * 2 <= rows.first`) is a heuristic, not derived from anything. Given the point
  above it only decides resolution for textures that are partly drawn and partly uploaded; it is not a correctness
  boundary. In practice every offset sample disappeared with the fix (table below), so the rule never had to pick
  between a mostly drawn and a mostly uploaded target in these runs.
- **`rowFromMemory` was not maintained on one path.** `completeTargetUpload` (and `completePartialUpload` in D3D11)
  patch an upload into a dirty target and stamp every touched row current. A touched row outside the dirty range kept
  `rowFromMemory = 0` when it had been read back earlier, so a texture uploaded there was still sampled from the
  target's native mirror of it. Fixed: those rows are marked as filled from memory
  (`rt_source::rowInRange`, `d3d11_targets.cpp`, `wgpu_targets.cpp`). New case in `staleTargetTextureTest`: draw,
  read back, draw a shaded triangle into the first page row (a solid fill is mirrored into memory and would leave the
  target clean), upload a 128 x 128 image over the base. Without the new lines the check
  `upload over read back rows of a dirty target is decoded` fails (D3D11, checked by removing them); with them it
  passes on D3D11 and WebGPU.
- Left as is, content correct either way: the abandoned-upload path in `completePartialUpload` (readback, then
  `syncRT` of every row) marks all rows as filled from memory, losing the "read back draws" flag, which can only
  lower the resolution of such a target; the solid-fill mirror in `d3d11_draw.cpp` and `copyLocal` do not touch the
  flag (the rows equal memory, or are dirty).
- **Lifetime.** A clean target is erased after 600 frames without use (`d3d11_present.cpp`), but sampling it as a
  texture refreshes `lastUse` (`d3d11_draw.cpp`). A stale target that an uploaded texture keeps landing on therefore
  never retires on the unfixed build. With the fix such a texture no longer samples the target, so it can age out.
  This is read from the code; a stale state that persists for a session was not observed live.

## Live runs

`scripts/harness/play_run.py`, Surdana start (fresh card), window 1280 x 960 instead of borderless fullscreen
(`DRAKAN_FULLSCREEN=0`, internal scale auto = 3), everything else from the play folder's `drakan.ini` (HD textures,
texture_upscale auto, DLSS quality, ray tracing off, mods `drakan-character-motion` and `ootf-levels` on). Reporter
configuration: the same plus `GSR_TEXREPLACE=0` (no HD textures) and `DRAKAN_MODS=<empty folder>`. Note:
`DRAKAN_HD_TEXTURES=0`, as in the `play_run.py` help text, does not switch HD textures off on the D3D11 renderer
(only the CPU backend reads it); the first "reporter" runs used it and are not counted as such.

Exes: unfixed `build/review/baseline-play/drakan.exe` (2026-11-09); fixed `build/review/play/drakan.exe` (full
build with the 2026-11-10 fix) and `build/review/issue6-2/play/drakan.exe` (incremental build with the change above).

Recipes (`build/review/issue6-2/recipes/`): `orb_sweep` (two passes of: pitch down 30 steps, back up, a full turn,
yaw while pitching; a shot every input sample, 372 shots), `ray_menu` (sweep, Esc pause, `setting video.ray_shadows
1`, Esc, sweep, the same with 0, sweep; a shot every second sample), `ui` (Tab inventory, pause menu), `walk`.
`GSR_TEXLOG=1` logs every texture served by a target; `tools/rtstats.py` counts those whose address is not the
target's base ("offset") and those that run past row 512 ("overrun").

| Run | Offset samples | Overruns | Empty orb frames |
| --- | --- | --- | --- |
| unfixed, owner's ini, `orb_sweep` (twice) | 1148 | 10 | `a_014`, `a_050` in both runs |
| fixed, owner's ini, `orb_sweep` (both fixed exes) | 0 | 0 | none of 372 |
| unfixed, reporter config, `orb_sweep` | 1148 | 10 | `a_014`, `a_050` |
| fixed, reporter config, `orb_sweep` | 0 | 0 | none |
| unfixed, owner's ini, `ray_menu` (off / on / off) | 942 / 1367 / 661 | 25 / 0 / 10 | `off1_048`, `off1_050` |
| unfixed, reporter config, `ray_menu` | 975 / 1352 / 671 | 25 / 0 / 10 | `off1_048`, `off1_050`, `off2_050` |
| fixed, both configs, `ray_menu` | 0 / 0 / 0 | 0 | none |
| unfixed, mods off, HD textures on, `walk` | 4253 | 0 | none |
| fixed (both exes), mods off, HD textures on, `walk` | 0 | 0 | none |

The empty-orb frames are exactly the frames whose log shows
`sample render target fbp=435 fbw=2 cls=0 as tbp=14880 psm=00 128x128` (row 480) or `fbp=421 ... tbp=14432` (row
480). They are single frames at fixed camera directions and repeat at the same shot in every run.

Ray-traced shadows were active in the "on" part (`[ray-water] pass N queued ... shadows=1`). The overruns after
switching them off came back at the same camera direction as before (`off2_050` matches `off1_050`), and nothing
new appeared. The 2026-11-10 note's "10, 550, 550" (a texture at tbp 14176 from the target at page 417, row 416, for
the rest of the run) was not seen; its logs no longer exist. The harness cannot open the F1 settings overlay (a
scripted F1 tap does not reach it), so the toggle was applied from the native pause menu instead of the PC settings
page the reporter used.

Regression checks on the fixed exes, looked at side by side with the unfixed exe:

- character shadow on the ground (Surdana, Shadowmire): present and the same;
- HUD orbs: full in every gameplay shot; the slide-in after leaving the pause menu is the same animation in both
  (the fixed run was captured earlier in it);
- Tab inventory, `modern` and `native`: identical, including the first-use help box and the portrait;
- pause menu: identical; the save screen was not reached (the recipe's Enter landed on the help box);
- Shadowmire (`--level 2`) and Ravenshold (`--level 4a`, on Arokh): no differences beyond timing;
- renderer counters at exit (`[gsr] exit`): readbacks per Surdana `orb_sweep` run 296 and 304 unfixed, 188 to 380 fixed
  (runs differ in length, 2681 to 7554 frames); no frame time measurement was made.

Evidence images (`build/review/issue6-2/evidence/`, unfixed left, fixed right):
`real_ini_orb_a014_base_left_fixed_right.png`, `real_ini_orb_a014_orb_closeup.png`, `real_ini_orb_a050_*`,
`rep_nohd_orb_a014_*`, `rep_nohd_after_ray_off2_050_*`, `ui_modern_a.png`, `ui_modern_b.png`, `ui_native_a.png`,
`ui_native_late_base_v2.png`, `lv2_shadowmire.png`, `lv4a_ravenshold.png`; the reporter's screenshot is
`evidence/issue/item2.jpg`.

## Tests

`cmake -S runtime/gs -B build/review/issue6-2/gs -A x64`, Release: 36 tests, 35 pass, `live` skipped (needs a
capture). A first run while the full game build was compiling failed `precise_sleep` (median lateness 7.3 ms, machine
load); it passed on the rerun. WebGPU: `gsr_test_synthetic_webgpu` from the root `runtime-tests` preset
(`build/review/issue6-2/rt`): 331 checks, 0 failed. D3D11 `gsr_test_synthetic`: 365 checks, 0 failed.

## Follow-up: the blocky hillside and the ray-shadow toggle

**Finding the spot.** `analysis/scripts/lvl_objects.py` on LEVEL1NA lists five suspension bridges ("Sus Bridge"). The
pointed stones of the screenshot are level geometry at the bridge ends, not objects. A scouting run placed Rynn 12000
units from each bridge on four sides (`recipes/scout.txt`, `evidence/_scout.png`). Bridge 4 at (745472, 152500,
710656) seen from the west (x 733472) matches the composition best: the bridge between pointed stones, the river
below to the right, cliffs behind. The exact standpoint of the screenshot was not found.

**Sweeps.** I ran dense sweeps with a shot per sample on the unfixed exe in the reporter configuration
(`GSR_TEXREPLACE=0`, no mods). Each sweep starts from a `place`:

- `recipes/hill.txt`: four spots around bridges 2 and 3;
- `recipes/bridge4.txt`: five spots around bridge 4;
- `recipes/walk.txt`: a scripted walk.

I looked at every 6th or 12th shot as contact sheets, and at single shots at full size. I also ran a flat-tile detector
(`tools/blocky.py`). No shot shows large square texels on a world surface. The light patches on the grass slopes
(`runs/hill_base_rep/p1_048.png`) are a low-resolution blend layer drawn with bilinear filtering, smooth, in every
build. After a `place` the camera does not settle in exactly the same pose each run, so frame-by-frame diffs between
two runs are not usable.

**Which world draws used old targets** (private diagnostic build, removed afterwards, see Files). It logged TEX1,
CLAMP, ZBUF and world state for every target sample, with the rule switchable by an env variable. With the rule off,
in `recipes/hill.txt` (15238 target samples):

| Samples | Kind |
| --- | --- |
| 4021 | HUD atlas at an offset (point-sampled, clamp) |
| 1426 | world textures at an offset: REPEAT, depth write, MXL 0, MMAG 1, MMIN 4 (bilinear) |
| 116 | mip-mapped world textures (MXL 4) at an offset: the target path drops their mip levels |
| 1278 | 64 x 64 CT24 point-sampled targets at an offset (the small character targets) |
| rest | targets sampled at their base |

The world draws are blended terrain detail layers. An example is tbp 14176, a 128 x 128 texture drawn at alpha about
17 over the upper left of the screen; with the rule off it came from the old target at its base.

**Matched on/off pairs inside one run.** A second diagnostic mode switched the rule on and off every 30 frames, while
the recipe held the camera still for 60 samples at 20 views (`recipes/alt.txt`, `runs/alt_rep`,
`evidence/alt_rep/`). Comparing a rule-off and a rule-on shot of the same still view, the only differences are Rynn's
idle animation, wind on the trees and one enemy fireball (`evidence/alt_rep/diffx6_*.png`). No world surface
changed visibly at these views.

**Mechanisms in the code that give large square texels on one world surface only:**

1. A texture served from an old render target (the bug the fix addresses). `syncRT` and `uploadTargetRect` copy the
   uploaded texture into the target at internal scale with nearest replication (`expandRenderTargetRows`). The target
   path then samples that copy (`sampleRT`): bilinear between physical taps when the draw asks for linear, otherwise
   point sampling. Each logical texel is a flat scale x scale block, so a strongly magnified surface shows flat squares
   with thin soft edges. It also has no HD image and no mip levels. Covered by the fix for uploaded textures; with the
   fix no target sample away from a target's base remains in any run.
2. GS point filtering requested by the draw (MMAG 0 / MMIN 0), or the user's `filter` override set to point. The
   world draws seen here all ask for bilinear (MMAG 1, MMIN 1 or 4); only the HUD and the CT24 character targets are
   point-sampled. Not covered by the fix, and not a bug if the game or the user asks for it.
3. Nearest mip selection (MMIN 2 / 3) on a coarse level. No world draw seen uses it. Not covered.

Only mechanism 1 can make the look depend on the camera angle (the texture's memory slot depends on what is
visible). This is inferred, not shown: the hillside itself was not reproduced.

**Ray-traced shadows through the player's code path.** The F1 page and the harness `setting` command end in the same
call. The F1 page calls `ModernSettings::applyAll`, which calls `apply` for each action; the harness calls `apply`
directly with `final = false`. Both reach `setWaterRendering` in `drakan_settings_host.cpp`. The only differences are
the INI write and the overlay drawing (Direct3D, not GS memory). The F1 flow pauses through the native pause menu;
the earlier `ray_menu` runs did that too.

I also started the game with `ray_shadows = 1` in a copy of the INI (`DRAKAN_INI`). At the hill I swept with shadows
on, toggled off, swept, toggled on, swept (`recipes/ray_hill.txt`, `runs/rayhill_base_rep`). A control run never
switched them on (`runs/rayhill_base_ctrl`).

| Run | Offset samples per phase | Overruns per phase |
| --- | --- | --- |
| shadows on / off / on | 1538 / 1879 / 2173 | none |
| shadows off throughout | 2283 / 2756 / 2516 | 648 / 567 / 574 (all the HUD atlas: empty orb) |

Turning ray shadows on did not raise stale-target use or bring corruption that persisted. Images:
`evidence/rayhill_on_off_on2.png` and `evidence/_rayhill_cmp.png`.

One visible difference, separate from this issue: with shadows on, trees cast a large, jagged, blocky ray-traced
shadow on the grass (`rayhill_on_off_on2.png`, lower left and lower right), and it is gone after switching them off.
Material textures for the ray pass are reduced to a small fixed size (`surface::Texture::size`, `d3d11_textures.cpp`),
which may explain the jagged edge. Not investigated further.

## Not established

- The reporter's blocky hillside: not reproduced, despite placements around all five suspension bridges and dense
  sweeps. No matched before/after image of a world texture exists. The fix removes every world texture served from
  an old target (counts above), and that path can produce exactly this look, but no frame was shown to look like the
  reporter's.
- Why enabling ray-traced shadows made it worse for the reporter, and why it stayed until a restart. Not reproduced
  through the same apply path, from the pause menu or from an INI started with them on. No second bug was found in
  what the ray path allocates or leaves behind (depth buffers dropped, texture caches flushed, temporal world state
  reset, no render targets created). The overlay drawing of the F1 page itself was not exercised.
- HUD orbs filling and draining (no damage or mana use in the recipes), distant characters (no NPC in view), shops
  and the equipment screen, and the save screens were not checked.
- Borderless fullscreen at the owner's monitor resolution (internal scale above 3) was not run.
- The diagnostic logging and the rule switch used in the follow-up existed only in two private builds
  (`build/review/issue6-2/play-diag`); they are removed from the source again.
