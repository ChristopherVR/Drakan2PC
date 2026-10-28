# Inventory seam, page-switch caps, section icons, intro sky bands, grid lines (24 October 2026)

Report: four screenshots at 3840x2160, 16:9, DLSS Quality, HD pack on, `world_rate = uncapped`. Reproduced with
`scripts/harness/play_run.py` (recipes in the session scratch dir: open the inventory with Tab, dismiss the help with
Enter, Page Down through the sections; the intro cut scene by stopping autopress at 19.8 s with `clock milliseconds`
shots).

## 1. Stale backdrop beside the panel under a help box

Symptom: with a first-visit help box open over the inventory (Skills, Equipment), the area between the squeezed panel
and native x 315 showed an older, unrelated frame with a hard vertical seam at x 315 (`2360 / 3840`).

Cause: the inventory world crop is widened by the `0x132f60` hook (`drakan_inventory_viewport_policy.h`) only while
the active GUI screen is 21. A help box is screen 1 stacked on the inventory (`[ws] ... screen=1` in `DRAKAN_WS_LOG`),
so the world was drawn with the native crop (left edge 315) and the strip kept old pixels.

Fix: `inventoryCropScreen(active, parent)` treats a message box (1, 2, 3, 0x1b) whose parent screen (`screen+0x34`)
is 21 as the inventory. 490 policy checks pass. The help boxes now show continuous scenery behind the panel.

## 2. PGU / PGD caps cut off, section icons not HD

The page-switch arrows sample only part of the tile in `INTERFCE.TXD` 94 (128x128 CT32). UVs from a draw log with
`GSR_DRAWLOGTBP=0x3e7f` (the log now prints STQ with `%.6g`; `%.4f` lost them at q = 0.00025), in upright texels:
right button x 56.1..71.3, y 90.4..105.7 (tip to x 76); left button x 58.5..74.1, y 106.3..123.3 (tip to x 52).
The generated 22 px caps did not fit a 15 px window, and a three-letter 5x7 caption cannot fit at native size.

The atlas also holds the five section icons. It never got the HD pack image for two reasons:

- gsr draws it through the aliased-render-target path. The game streams it every frame into memory of a scratch
  target (base block 0x3e80, 128x192), and `setupUpscale` is skipped for aliased textures.
- Its hash is excluded anyway because the prompt generator rewrites it.

Fix:

- `runtime/ui` draws the native captions inside the measured windows. Captions too wide for a bordered cap are drawn
  with tight glyphs. The old "R1"/"L1" ink is painted out because the arrow tips show the stone beside the caps.
- The prompt hook now passes the texture id, the original disc pixels and the menu-layer captions.
- The host builds a 4x overlay (`buildPageTileOverlay`): the stone tile upscaled, with anti-aliased "PgDn"/"PgUp" caps
  from Arial Bold. The glyphs are 1.4x flatter because the buttons show the window that much taller than wide.
- The host registers it with `gsr::registerRuntimeOverlay`. Outside the tile, gsr takes the pack image of the
  original texture (exact hash, then RGB alias) or the xBRZ upscale.
- Aliased textures whose decoded memory has an exact authored hash (a runtime texture or an exact pack file) now
  go through `setupUpscale`. Render output never produces such a hash.

The section icons are now the AI pack art.

## 3. Rainbow bands in the intro sky

A/B on the moon shot of the intro cut scene: bands with the pack off, with DLSS off and with palette HD maps off; none
with `GSR_TEXUP=off`. The culprit is the 64x64 CT24 sky ramp `48d51591bd210140` (tbp 0x34f3, two full-screen draws,
point sampled, t = 0..0.6 and 32.5). Only rows 0..8 and 33..38 hold sky colours; the other rows are random memory.
The native sampler never reads them, but the 8x smooth upscale blends them into the sampled rows.

Fix: `texup::looksUninitialized` (at least a quarter of the rows, and at least 4, average more than 60 levels between
horizontal neighbours). Such textures stay native in the live upscale path; pack replacements are unaffected.
2 new texup checks.

## 4. Grid lines missing in the inventory page (fixed)

Reproduced (`inv_ab.txt`: open, close, turn the camera, open, shot; close, open, shot). The first open after the
turn shows a plain light stone grid; the reopen is correct. It reproduces only with DLSS on, and only intermittently.

Ruled out, each by A/B or direct readback:

- the SR depth hand-offs (skipping either stretch);
- the page depth clear (it writes 0; depth readback after each draw);
- texture decode and palette (same cache entry and CLUT hash in good and bad frames);
- HD pack and live upscaling (`GSR_TEXREPLACE=0 GSR_TEXUP=off GSR_PALETTE_HD=0` still fails);
- the widescreen map (the draw is mapped);
- the VU1 engines: `DRAKAN_VUCHECK=1` found 0 mismatches in 2.75 M runs.

The real difference is in the game's own submission. The grid quad (`n=351`, same TEX0 as the panel) arrives with
vertex colours (127,127,127) in the bad frame and lit colours (118,116,107) / (86,85,91) in the good one. The page is
lit 3D geometry, and unlit it reads as flat stone.

It depends on timing and on the world rate:

- `DRAKAN_WORLD_RATE=30` and `60` are correct on the first open;
- `uncapped` fails;
- anything that slows frames (a full draw log, `DRAKAN_VU_FUNC=0`, the VU check) hides it.

The uncapped mode (`drakan_frame_rate.h`) wakes the render worker `0x1d78f0` without the original 4-vblank gate. The
likely cause is that the inventory page is built before its light set is in place. Next step: find where the page
geometry's light colours come from (the light list the page draw reads) and gate it like the original cadence.
Workaround: `world_rate = 60`.

Update (later the same day, report: "grid missing with DXR water / ray shadows or DLSS on, fine with both off"). The
renderer features are not the cause. Harness A/B on the shipped exe (Surdana start, open the Items page, dismiss the
help, then 15 cycles of close / turn the camera / reopen / shot; the grid scored by the line contrast of the page):

| config | bad opens |
| --- | --- |
| `world_rate = uncapped`, water + ray shadows off, DLSS off | 2 of 9, 3 of 16 |
| `world_rate = uncapped`, modern water + ray shadows + reflections on | 0 of 9 |
| `world_rate = 60`, all off | 4 of 16, 1 of 8, 3 of 8 |
| `world_rate = 60`, all off, `DRAKAN_RENDER_QUEUE=0` or `PS2X_VU1_THREAD=0` | 4 of 16 (opens 1, 5, 9, 13) |
| `world_rate = 30`, all off | 0 of 16 |

- `world_rate = 60` is not a workaround: it fails as often as `uncapped`. Only the original 30 cadence is clean.
- A bad open stays bad for its whole duration (7 shots from 90 to 200 frames after opening are identical), and a
  good open stays good.
- With the synchronous VU1 path the failures are exactly periodic (every fourth open), so this is not a host thread
  race in the VU1 / render queue. It is game state under the faster cadence.
- The water, shadows and DLSS paths only change the frame timing (they made it look correlated in the report).
- Disabling the 60 FPS velocity hook (`0x325d30`) or wind hook (`0x141d80`) changes nothing (temporary env
  switches, same opens fail).

Mechanism, traced with temporary hooks:

- The grid (the `n=351` draw) is the only page part affected; the other page meshes (`n=108/441/600/897`) are lit
  the same in good and bad opens.
- VU1 data memory at the grid's XGKICK differs in one constant: qword 8 is `(0x23,0x21,0x27,0x80)` in good opens and
  `(0x7f,0x7f,0x7f,0x80)` in bad ones. With 0x7f every vertex saturates to 127, so the lines vanish.
- Qword 8 is written by `1e9fa0` (batch begin, `UNPACK V4-8` to address 8) from its colour pointer `a2`. The default,
  when the pointer is null, is `129370(0xff,0xff,0xff,0xff)`, which packs to `0x807f7f7f`.
- The pointer comes from `1c7410` (called by the scene-object draw `189b38` at `18a84c`): `s7 = object+78`, the
  per-vertex colour array that `186260` builds. On the grid object (vtable `51b8b0`, `+104 = 601780x`), `+74` is 2 and
  `+78` points at colours starting with `0x80272123` in good opens. In bad opens both are 0.
- In bad opens `+74` goes from 2 to 0 inside the grid's first draw, at `189d48..189d70`. The draw collects the
  object's lights itself while `+78` is empty (vtable `+ac` = `185db0` into the list `5697c8`). An empty list clears
  `+74` for good. The grid has no layer (`+7c = -1`) and no light link (`*(obj+a0)` = 0), so the list is always empty
  (`count=0` at x = -2366, the start of the page slide).
- Tracking the grid object by address from its constructor (`178d98`, called at `182eac`) shows who builds `+78`:
  `13af38` (`186260` at `13b258`) lights it once, with a light set it computes, right after the page is built. Its
  colours start with `0x80272123`. Nothing else ever rebuilds them, because the grid has no light link for the draw.
- In every bad open, and only in bad opens, the colours are freed between that one-shot light and the first draw. The
  free is vtable `+11c` (`17e478` -> `1897f0`), called at `18938c` by `189288`, a periodic pass (counter `4e0338`,
  limit `0x259`) over the scene objects. It frees the colours of objects that `18ba28` finds farther from the camera
  than the view distance, to save memory. The grid sat at x = 446910 then (world coordinates), so it counted as far.
- A layered or linked object gets its colours back at its next draw (gather -> vtable `+b4` -> `186260`). The grid
  cannot, so the draw clears `+74` and the grid stays unlit for the whole open.
- At the 30 cadence the light and the first draw fall in one frame and the pass never lands between them (0 of 16).
  At 60/uncapped there are render frames between ticks, and the pass lands in that gap periodically: every fourth open
  with the synchronous VU1 path. Keeping `+74` after the miss, or holding the draw back, did not help, because the
  colours were already gone.

Fix (`drakan_frame_policy.h` `keepUnrebuildableLighting`, `drakan_frame_rate.h` `lightingReleaseHook`, installed for
`world_rate = 60` / `uncapped` only): vtable `+11c` called from `18938c` is skipped when nothing could rebuild the
colours. That means a scene object (vtable `51b8b0`) with colours, no layer (`+7c = -1`) and no light source in its
link. The log line `[frame-rate] kept the lighting of far object ...` marks it. Checks are in
`scripts/tests/frame_cadence_test.cpp` (409 pass).

| config (16 opens each, same recipe) | bad opens before | after |
| --- | --- | --- |
| `world_rate = 60` | 4 | 0 |
| `world_rate = uncapped` | 3 | 0 |
| `world_rate = 60`, `DRAKAN_RENDER_QUEUE=0` (deterministic) | 4 (1, 5, 9, 13) | 0 |
| `world_rate = uncapped`, DLSS quality, modern water, ray shadows and reflections | - | 0 |
| `world_rate = 30` | 0 | 0 |

## 5. Objective popup drawn twice (fixed)

At Surdana start the popup ("Find Lady Myschala at the palace in Surdana." beside the journal icon, "Map Entry (The
Palace)" beside the map icon) showed a small black copy and a large beige copy. Draw log with `GSR_DRAWLOGTBP=0x3000`
(the text atlas): each line is a black shadow sprite batch at +2 px (`flags=237`) and the fill as a textured triangle
list (`flags=037`, FGE set, fog 255, Q 1, ZTST ALWAYS, ZMSK).

- The fill was a `hudWorldTriangle` because of FGE alone, so it was never mapped. It was drawn at native size,
  stretched to 16:9: the large copy.
- The shadow's first line fell in the top tutorial band and took the centred tutorial map. The second line took the
  cluster map. That gave the small black copy, with line 1 pulled away from its icon.

Fix (`hud_page.h`, 3 new checks in `test_hud_page`):

- `screenTextTriangles`: textured, non-perspective triangles with fog 255 everywhere are HUD even with FGE.
  Perspective late world draws (weapon glint) stay excluded through Q.
- `tutorialBandDraw`: the tutorial map applies only to centred draws or the full-width band.
- `clusterAnchorX`: a cluster starting at the left margin with its centre below 0.45 of the width anchors left. The
  first line's centre is at 0.41, just past the old 0.4 threshold. The right margin is handled the same way.

The popup is now one group at the top left, at HUD size, with shadow and fill aligned
(`build/harness/popup-fix2/b.png`). The FPS overlay, when on, covers the start of the first line.
