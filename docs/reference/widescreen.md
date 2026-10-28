# Widescreen (16:9, 21:9) for the Windows port

Gameplay now retains its established widescreen state while native screen `-1`
remains active. Previously, a camera update gap could expire that state after
six display ticks and switch both projection and presentation to 4:3 during a
slow frame. Actual frontend transitions still clear the world state; pause,
inventory, settings and movies retain their existing policies. The focused
suite passes 4,559 checks, including camera intervals of 20, 60 and 600 ticks.
Build and live validation receipts are in
`docs/journal/2026-10-19/data/aspect-stall-20261019.json`.

The save-in-progress screen 0x1a (between the save browser and gameplay; the card write stalls the camera for
~15 vblanks) keeps an established world like the pause menus (`saveWriteScreen`), so saving no longer flashes
the picture to 4:3 (`docs/journal/2026-10-24/save-aspect-flash.md`).

Native load screen 14 now preserves the configured presentation aspect, using
the same centered foreground canvas as the save browser. Its UI projection
remains native; loading a save no longer switches the display to 4:3 before
returning to wide gameplay. Movies retain their separate presentation policy.

The native loading draw (`0x43d340`) publishes an ordered GUI boundary before
submitting the bar or text. It closes the previous world phase and selects the
centered menu map immediately, without waiting for the host input tick. Loading
foreground therefore ignores gameplay HUD size and previous-frame cluster anchors.
Authored font atlases upload synchronously before their first draw, including
when automatic texture upscaling is off or streaming reuses an animated VRAM slot, so the native font cannot flash first.

Validation: 34 focused WARP pixel/registration checks passed (16:9 and 21:9,
three HUD sizes, cold renderer/font caches, automatic upscaling on/off).
Live save loading at 1280x720 and new-game loading at 1680x720 each yielded
at least 15 captured loading frames with stable bar geometry and typography, then reached
the game. Small final-blit rounding differences were at most two color levels.

## Credits and start-up (2026-10-22)

* The credits (GUI screen 0x13) are a baked 4:3 picture with no widget layout to protect, so in a wide mode the
  presenter now stretches the whole frame to the output aspect (`stretchedImageScreen`, `LayoutPolicy::stretchImage`,
  `drakan_widescreen_policy.h`); projection and HUD scale stay native. Native mode and 4:3 windows are unchanged.
* The presenter used to start in 4:3 and the first host tick widened it a few frames later (the logo screens are world
  pages). `initialPresenter` now starts in the wide layout (and seeds the world policy) when the mode and window are wide.

## Wide menu backdrops

Menu and credits art is a baked 4:3 image, drawn in a centered native canvas (see `menuMap`, `hud_page.h`); the
margins used to be cleared to black. When `<texture_dir>/wide/` holds images they are drawn instead
(`d3d11_backdrop.cpp`, right after the margin clear).

* **Names**: `<name>_<a>x<b>.png`, e.g. `screen8_16x9.png`, `screen8_7x3.png` (21:9), `screen8_32x9.png`.
  `screen<GUI screen id>` (title menu = `screen8`) images are *complete*: the engine's own art tiles for that screen
  are skipped and the image is the whole picture. `default` images only fill the margins (the engine art is drawn over
  their centre), for any menu screen without its own image.
* **Which screen**: the GUI id of the renderer's latest scene packet, then the screen the host saw when it turned
  the native menu canvas on (`setNativeMenu(enabled, screen)`), then `default`. The title's art is drawn a frame
  before its handler-tick packet (`0x43e4c8`) arrives, so the packet id alone still named the logo (30): the first
  title frame showed `default` (4:3 art, filler margins) and stayed up while the `screen8` set loaded, then snapped
  to the full-width `screen8` image (2026-10-23).
* **Selection**: the narrowest image at least as wide as the output aspect, else the widest (`hud::pickWideAspect`).
  It is placed so its embedded 4:3 art lines up exactly with the squeezed native canvas (`hud::widePlacement`), so the
  menu box and text still sit where the game puts them.
* **Making them**: `scripts/assets/make_wide_backdrop.py <4:3 image> --name screen8 --style <look> --out <texture_dir>/wide`.
  Looks: `sky` (flat blue gradient), `dusk`, `clouds` (procedural clouds in the art's palette), `vignette`,
  `stretch` and `template` (flat magenta margins, to feed an external outpainting tool; hand-made or outpainted
  images work the same).
* **The wing problem**: Arokh's wing (and Rynn's sword tip) run off the retail image's edge, so any fill beside the art
  makes them look sliced off, and carrying the edge outward (smeared, mirrored, blurred, row-coloured) smudges them.
  `stretch` instead widens the art itself: its outer ~10% of columns are stretched sideways (1:1 where the stretch
  begins, so faces and the rider stay untouched), so the wing keeps going to the screen edge. At 16:9 it reaches the
  edge; on wider outputs its reach is capped and it fades into the cloud background. It is only valid for complete
  (`screenN`) images, since the engine's unstretched art must not be drawn over it. The extreme outer columns end as
  a soft smear. Real painted extensions (see `template`) remain the better result.
* Not yet verified in a live game session; covered by `hud_page` unit tests only (selection and alignment).

## Inventory canvas

The current native inventory canvas is left-aligned. Its horizontal map is
`x' = x * (4/3)/aspect`: at 16:9 the native X range `0..512` maps to `0..384`.
The original categories, grid, artwork and native preview widgets share that
map; mouse hit testing uses its inverse. There is no added sidebar.

The live world behind the inventory is a separate render phase with a
widescreen projection. The follow-up crop correction expands its coverage to
the mapped panel edge while preserving the camera projection. Forced 21:9 and
32:9 captures no longer show the stale tutorial strip in the inspected scene.
The previous centered-page black-left-margin policy is no longer active.
The crop correction also applies while a message box (screens 1, 2, 3, 0x1b) is stacked on the inventory
(its parent screen at `screen+0x34` is 21), e.g. each section's first-visit help; before 2026-10-24 those
boxes showed a stale strip with a seam at native x 315
(`docs/journal/2026-10-24/inventory-sky-textures.md`).
The Order-of-the-Flame style views (M full-screen map, `docs/reference/mouse-menus.md` section 12) are the exception:
their inventory draw packet carries `SceneGuideCamera::CentredPage`, and `hud::InventoryFrame` then maps that frame's
page about the screen centre, `x' = W/2 + (x - W/2) * (4/3)/aspect`; the input side inverts it with anchor 0.5. Frames
without the flag (the Tab screen) keep the left-anchored map. The world behind a view is not cropped; since the game
draws no sky behind its inventory, the world target is cleared at the first world draw after a view frame.
See `docs/journal/2026-10-18/followup-render-input.md` for current validation and
`docs/journal/2026-10-18/native-inventory.md` for
release evidence and limitations.

## Original projection research

Research + working prototype, 2026-10-17. The prototype is a ~45 line runtime hook (`patches/proposed/ws_anamorphic_projection.patch`, against
`tools/PS2Recomp/ps2xRuntime/src/lib/drakan_overrides.cpp`; **not applied** to `tools/`). It was built in a private copy of the merged runtime
(`build/research/ws_build`, exe `drakan_ws1.exe`) and run on the real game: Level 0 renders with the correct 16:9 look
(`build/screenshots/ws_16x9_level0_subtitle.png`).

## 1. Recommendation

**Anamorphic Hor+**: keep the game's 512x512 frame buffer (displayed as 4:3 by the GS display circuit), squeeze the horizontal projection scale by `(4/3)/aspect`
so the 3D view contains a wider field of view, and let the presenter stretch the frame to the window aspect. This is exactly what PS2 16:9 modes do. It needs **one float
write per camera update**, no GS/VU1 change, keeps culling and the clipper consistent (section 3), and is compatible with the existing `aspect = 16:9` presenter option (gsr letterbox
to a forced aspect already exists: `Config::forcedAspect`, `GsrBridge` reads `[video] aspect` / `PS2X_ASPECT`).
Add internal-width scaling (section 6) later if the horizontal sharpness at 21:9 matters.

## 2. Where the aspect / FOV enters (measured)

The game has one global "render/viewport" object: pointer at **`0x5565c8`** (runtime value `0x6c5d80`, heap). Peeked at run time (`PS2X_PEEK="*5565c8:48,..."`):

| Field | Value (gameplay) | Meaning |
|---|---|---|
| `+0xf8`, `+0xfc` | 512, 512 | frame buffer size used by the GUI/HUD/2D layout and by `FUN_00132d68` |
| `+0x40`, `+0x44` | -256, -256 | -(w/2), -(h/2) |
| `+0xec` | 64.0 (`0x42800000`) | constant |
| `+0xf0` | 44.8 (`0x42333333`) = 64 * **0.7** | `+0xec * aspectFn(w,h)` (vslot `+0xf4` of the object at `+0x18`; 0.7 = pixel aspect of the 512x512 frame shown as 4:3 PAL) |
| `+0x10c` | 76.27 (0x42988b62) in play, 486.1 in the intro cutscene | **`64 / tan(fov/2)`**, written by `FUN_00133fc8(fovDegrees, obj)` (`tan` through the soft-double libm stub) -> fov = 80 deg play / 15 deg cutscene |
| `+0x48` | `-(+0x10c)/(+0xec)` = -1.19 (play) | **x projection scale** (computed in `FUN_00132d68`, "SetViewport") |
| `+0x4c` | `-(+0x10c)/(+0xf0)` | **y projection scale** |
| `+0x50/+0x54/+0x58/+0x5c` | 2048 -/+ 256 | GS guard rectangle (centre 2048 +- half size) |
| `+0x100/+0x104` | 2048 | screen centre in GS units |
| `+0x108` | 128/w | pixel scale used by the unprojection |
| `+0x3c` | `sin(vfov/2)/sin(hfov/2)` | ratio used as a **LOD distance factor** (squared) in `FUN_0013c998`, not a cull |

Pipeline: `FUN_00133fc8` (SetFov) -> vslot `+0x84` = `FUN_00132d68` (SetViewport: computes `+0x48/+0x4c`) ... **`FUN_001480e0`** (once per camera update, called from `FUN_00148498`)
copies the 2x3 view template (`0x4dad58`) into the camera (`cam+0x78..0xa0`) and multiplies row 0 by `obj+0x48` and row 1 by `obj+0x4c`. That matrix is what `FUN_001e8c78`
uploads to VU1 (rows `[0..3]` of the MVP, docs/reference/graphics.md "UNPACK 6c04_0000"). Every EE-side projection (core renderer / HUD markers / particles / lens flares:
`FUN_0017d298`, `FUN_0019e198`, `FUN_00354470`, `FUN_00206aa0` 0x206aa0+16500, `FUN_001b8bf8`) multiplies by `+0x40 * +0x48` / `+0x44 * +0x4c` itself. So **`obj+0x48` is the single place**.

Horizontal FOV for the 4:3 frame: `tan(hfov/2) = 1/(-(+0x48) * ...)`; patch factor `k = (4/3)/aspect` (0.75 for 16:9, 0.5714 for 21:9):
`+0x48' = k * +0x48`. Result at fov 80 deg (4:3): 16:9 shows ~96.6 deg, 21:9 ~111 deg horizontal. Peeks of the prototype confirm the writes (`0xc0b64c50` = -5.696 = 0.75 * -7.595).

## 3. Culling

* **VU1 bbox cull (entry 0x26e0, flag 0x400) and the Sutherland-Hodgman clipper** work on the transformed vertices of the *same* MVP matrix and clip against the +-256 px guard of the (unchanged)
  512x512 frame. Squeezing the x row of the matrix therefore widens the frustum and the clip window together: nothing to patch, nothing pops. (No pop-in was seen in the 16:9 Level 0
  screenshot with trees / terrain reaching both frame edges.)
* **EE-side culling**: all uses of the projection scale go through `+0x48/+0x4c` (above). `+0x3c` is a LOD factor. The only distance test using `+0x10c` (`FUN_00206aa0` 0x206aa0+14043)
  compares a radius, not an angle. No separate frustum-angle culling on the EE was found in the 22 decompiled chunks (`build/research/alldec/`). Residual risk: sector/portal visibility lists
  built on load from the *camera's default FOV objects* (`cam+0x5c/+0x60` = hfov/vfov, vslots `+0x6c/+0x74` of `obj+0x124`, used at `FUN_00206aa0` 12333 and `FUN_002ed34c` 14195 for
  initialisation only). If pop-in appears at the far left/right edge, widen those two getters' results by the same factor (`hfov' = 2*atan(tan(hfov/2)/k)`), see 7.
* Unprojection (screen -> ray, `FUN_0021eb00`, one caller at `0x4416a0` region) uses `+0x108` for both axes and would need `x_ray / k`; not used in normal play.

## 4. Render resolution: anamorphic vs true wide buffer

| | Anamorphic (recommended) | True wider buffer |
|---|---|---|
| Idea | frame stays 512x512 (gsr scale s -> 512 s squared), presented stretched to `aspect` | GS FRAME/scissor/XYOFFSET for e.g. 683x512 or 910x512 |
| Game changes | 1 float (above) | `FRAME_1` FBW (8 -> 11/15, `FUN_001d8df8`/`0x1d8930` set up FBP/FBW and DISPLAY), allocations of the colour/Z buffers and every FBP (the 512x512 buffers are packed at fixed block addresses, a wider one overlaps), scissor + clear sprites, `+0xf8` (GUI layout width!), `+0x50..0x5c`, `+0x100`, warp/screen-effect buffers, the "framebuffer as texture" effects (water, heat), post-fade sprites, screenshot code. FMV quad. |
| Risk | very low | high (dozens of hard coded 512/256/2048 constants in sprite builders `0x1da260..0x1dd2d0`) |
| Quality | horizontal resolution = 512 s columns stretched to the window width | native |

Quality mitigation for the anamorphic mode without game changes: choose the internal scale from the *output width*: the stretched image has `windowWidth / (512 s)` output pixels per buffer column.
At 21:9 3440x1440 the auto scale (height/512 -> 3) gives 1536 columns for 3440 px (2.2x horizontal stretch, soft). Raising `scale` to 5-6 costs GPU only (RTX 5080 fine). Proper fix (phase 2, owner of
`runtime/gs`): `Config::scaleX` (render targets `512 s * sx` wide); the vertex shader maps GS coordinates to NDC through `cb_rt` so only RT/viewport/scissor sizes and the RT-as-texture
copies (`flushToMemory`, integer scale assumptions, ~60 places in `d3d11_backend.cpp`) change.

## 5. HUD, menus, FMV (2D under anamorphic stretch)

All 2D is drawn in the 512x512 frame (GUI layout `+0xf8/+0xfc` = 512), so with a 16:9 present it is stretched 1.333x (21:9: 1.75x): round orbs become ellipses, text is wide
(visible in `build/screenshots/ws_menu_stretch_219.png`: Load Game screen at 4:3 vs 21:9). Options, ordered by effort:

1. **Mode switch (trivial, recommended first)**: present with forced **4:3 pillarbox** whenever the game is not in 3D gameplay: logos, title menu, loading, FMV (4:3 video), credits;
   16:9 only while the world is rendered. The input module already tracks `menu` vs `play` (`drakan_input::setMenuActive`, `[in-log] ... menu|play`). Both the projection factor and the
   present aspect must switch together (set `g_wsAspect` to 4/3 and `gs->setAspect(4/3,false)` -> the world behind a pause menu then re-renders 4:3, acceptable).
2. **HUD de-stretch inside gsr (medium)**: in the sprite path, for 2D draws with the game's UI signature (no Z test, `uiHint` already exists in `setupUpscale`) multiply the x of sprite vertices
   by `k` about the screen centre; exempt sprites that cover the full width (clear/fade/background). This pillarboxes HUD and menu panels into the central 4:3 area with correct aspect (health/mana
   orbs, compass at the 4:3 margins rather than the extreme screen edges - the usual "4:3 HUD" look). Needs a draw log of a gameplay frame to tune the exempt rule
   (`GSR_DRAWLOG=<vsync>-<vsync>`); not done here.
3. **Per element at the source** (large): GUI widgets are laid out from `(+0xf8,+0xfc)` fractions (layouts `0x2300..0x2398`, HUD code around `0x1c1a34`, `FUN_00414280`, `FUN_003877d0`,
   `FUN_002a0d20`); changing `+0xf8` would squeeze layouts but not the sprite pixel size. Not recommended.

The game's own subtitle bar is a full-width band (`FUN_003afd18`); with anamorphic stretch it just spans the screen (see the screenshot).

## 6. Prototype and how to use it

`patches/proposed/ws_anamorphic_projection.patch` adds to `drakan_overrides.cpp`:
* `g_wsAspect` (atomic float), initial value from `drakan.ini [video] aspect` (`16:9`, `21:9`, `2.39`, ...), env `DRAKAN_WS_ASPECT` overrides; for `native`/`stretch` call `g_wsAspect.store(window aspect)` from the presenter.
* `hookCameraUpdateMat` replaces `0x1480e0`: recomputes `obj+0x48 = -(obj+0x10c)/(obj+0xec) * (4/3)/aspect` and calls the original. With aspect 4:3 the value is bit identical to the game's.
* `hookSetViewport` replaces `0x132d68`: original, then the same write (keeps `+0x48` correct between `SetViewport` and the next camera update).
* Installed only when the aspect differs from 4:3.

Run (the presenter option must match): `DRAKAN_WS_ASPECT=16:9 PS2X_ASPECT=16:9 drakan.exe` (or both from `drakan.ini` once wired: one key `aspect` can feed both).
For `drakan.ini`: `aspect = 16:9` should drive `g_wsAspect` and `gsr::setAspect` from one place, `ui aspect_mode = anamorphic|pillarbox_menus`.

Verification done: boot -> New Game -> intro cutscene (book, camera FOV 15 deg) -> Level 0; 16:9 frames (`gs_ws169b.png` raw 1:1 buffer and the stretched `..._final169.png`) show correctly proportioned characters,
wider view, no edge artefacts; peeks match the formula; 4:3 path untouched. Not done: long play-through for pop-in, Arokh flight/bow zoom (SetFov changes handled by the per-frame recompute),
pause-menu overlay look.

## 7. Existing PS2 widescreen patches

Searched (web, PCSX2 patches repo search for `SCES-50006`, general searches for "Drakan Ancients Gates widescreen pnach"): **nothing found**; the PCSX2 patch repository has no
`SCES-50006`/`SCUS-97128` file (the NTSC-U serial is SCUS-97128, not 97105). Nothing to translate. If a pnach ever appears it would patch the same value (the multiply at
`0x1480e0..`): `0x1480e0` loads `lwc1 f?, 0x48(v)` - patching the literal `0x4dad58` template or the SetViewport divide would be equivalent to our hook.

## 8. Optional hook for sector/portal visibility (only if pop-in is observed)
`hfov` getter slot `+0x6c` of the object at `*(0x5565c8)+0x124` is used for the default camera: wrap the callers' results `hfov' = 2*atan(tan(hfov/2)/k)` in a replacement of that vslot
(address in the vtable at `*(obj+0x124)`; find with `PS2X_PEEK`). Untested.

## 9. Integrated in the port (2026-10-17, branch drakan)

* **Code**: `ps2xRuntime/src/lib/drakan_widescreen.{h,cpp}` (projection hooks + state machine), gsr `Renderer::setHudScaleX`, settings overlay (Display > Aspect ratio).
* **`drakan.ini [video] aspect`** = `4:3` | `16:9` | `21:9` | `auto` (window aspect, presenter fills the window) | `native` | `stretch` (any `w:h` / decimal also works). One value drives both the
  projection hook (`obj+0x48 *= (4/3)/aspect`) and the presenter aspect. Live from the F1 overlay (no restart, no keep/revert prompt). Env `DRAKAN_WS_ASPECT` (or `PS2X_ASPECT`) overrides.
* **4:3 pages**: the window thread (`hostTick`, every presented frame) decides: a 3D camera update (`0x1480e0`) within the last 6 vblanks and the active GUI screen (`0x4fb60c`, id at `+0x18`)
  not in {8 title, 0xe new game, 0x13 credits} = *world* -> widened projection and presenter at the target aspect; otherwise 4:3 pillarbox and the original projection. Title menu, logo / outro FMV
  (no camera updates) and the loading screens therefore stay 4:3. The intro book scene and the Options / Load backdrops are in-engine 3D and are widened (the Options backdrop is a narrow 3D viewport
  in the 512x512 buffer on the PS2 as well). `DRAKAN_WS_LOG=1` prints every decision. The F1 menu freezes the decision.
  Correction (2026-10-23): the logo (screen 30) and the intro / outro book (screen 29) are FMV, and the engine does update
  the camera while they play, so they were widened; an IPU frame decoded within the last 15 vblanks now forces 4:3
  (`WorldPolicy::update` movie tick, `fmv.md` section 10). That alone still widened a movie whenever the EE stalled for
  more than 15 vblanks between two IPU frames (disc refill, HD-pack seek, slow host; reproduced on the logo) and for the
  ~50 vblanks the movie screen stays up after its last frame, so the movie screens 0x1d / 0x1e (`movieScreen`) now
  never establish the world; the IPU grace only covers the hand-off after the movie screen closes.
* **`hud_aspect_correct = 1`** (default): while the world is wide gsr scales the x of *UI sprites* by `(4/3)/aspect` about the frame centre (`Config::hudScaleX`, live `setHudScaleX`, env `GSR_HUDSCALE`).
  Rule (d3d11_backend.cpp, `draw`): sprite primitive, colour write enabled (depth-only clears are untouched), not sampling a render target (screen grabs / shadow maps), not depth-tested against the world
  (`ZTST` not GEQUAL/GREATER), drawn into the full display sized frame (scissor >= 480 px) and narrower than 85 % of it (full-width fades, dimmers and the subtitle band stay full width).
  Result at 16:9: health / mana orbs keep their original proportions (measured: raw orb 1.06 w/h at 4:3 -> 0.80 in the buffer = 1.42 on screen in both). Known limit: world-anchored 2D markers
  (target reticle, floating bars) are moved with the HUD and can be off their object near the screen edge; switch the option off if that matters.
* Screen-space text drawn as a fogless textured triangle list (FGE set, fog 255, Q 1) is HUD as well; the top
  tutorial-band map is only for centred text or the full-width band, and a cluster starting at a side margin with
  its centre within 0.45 of that side anchors to it (objective popup, 2026-10-24). A wrapped hint line arrives as
  separate word draws, so any draw inside the rows of a full-width top band seen this frame or the last
  (`hud::TutorialBandRows`) takes the band map too; checking only that each draw was centred split the death hint's
  second line ("key to" ... "go to the main" ... "menu.") into left, centre and right anchors.
* Not done: sector / portal visibility hook (section 8; no pop-in seen), true wide internal buffer.
