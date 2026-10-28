# Inventory stale-gap source correction

Source investigation/edit and CPU policy validation. Compilation was deferred
during Averroes's matrix, then completed after release. No viewport game/GPU/WARP
run by this worker; parent supplied the live evidence below. No backend, generated-code, asset,
configuration, dist, commit or push changes. Parent's opt-in setter diagnostic
in `drakan_widescreen.cpp` is preserved.

## Observed defect and source trace

`build/t/inventory-background-repro02/stderr.log` frame3802 draw117745 submits
a masked-colour depth-clear rectangle x315.062..827.062, y0..512. Subsequent
world submissions also begin at315.062, while scissor is0..511. Parent's
left-anchored inventory foreground ends near211 after horizontal factor4/7,
leaving211..315 with previous tutorial pixels. This is actual missing world
coverage before backend UI mapping, not evidence of a texture-cache defect.

The full512x512 setter calls in `inventory-viewport-repro01` do not identify
the final viewport: native code applies a later crop. Read-only source trace
under `build/ps2recomp_try/output` establishes:

- `132d68` saves x/y to renderer+110/+114, dimensions to+118/+11c, resets
  GS offsets+100/+104 to2048, and derives projection and extents.
- `134680` uses integer origin+110/+114 and dimensions+118/+11c to construct
  a renderer rectangle. These integer fields alone do not describe GS centre.
- Inventory `42fd18` temporarily applies its own page viewport at430228
  (return430230) with scale1 and negative offset. That grid/preview route is
  distinct from the background world crop and must remain untouched.
- At430968..430994, inventory computes horizontal offset and animated crop
  scale, writes them to screen manager4fb600+4/+8, and marks renderer dirty.
  `2c4400` reads these fields, calls renderer vtable slot8c, and returns from
  that call at2c4444. Existing documented RAM evidence resolves slot8c to132f60.
- `132f60` sets+100=2048+offset; horizontal extent+40=-W*s/2;
  clip bounds+50/+54=2048+offset +/- W*s/2; projection divisor+ec=64*s;
  projectionX+48=-focal/(64*s); integer width+118=truncate(W*s).
  Vertical projection/extent are not scaled by this horizontal crop.
- `139010` invokes134450 before world rendering. Dirty state causes134450
  to rebuild camera and CPU frustum (133498); the existing widescreen policy
  then keeps CPU side-plane width consistent with actual world projection.

Thus GS XYOFFSET/late crop placement explains why full setter arguments can
coexist with the observed nonzero clipping edge. Parent's subsequent crop logs
confirm the input/output scales and offset in the live inventory path below.

## Authored correction

New pure policy `drakan_inventory_viewport_policy.h` and a132f60 hook in
`drakan_widescreen.cpp` expand only the inventory screen-manager crop. Guards:
function entry132f60, caller2c4444, active screen21, authoritative renderer
identity, full-width input, finite crop0<s<1, rightward nonnegative offset,
and actual foreground compression0<h<1. The full-width guard prevents
reapplying expansion to an already cropped viewport. Native/disabled HUD
compression, other screens, negative grid placement, direct preview calls,
unrelated render objects and interior resumes are unchanged.

The host publishes the same horizontal foreground scale it sends to GSR.
For native framebuffer widthW and requested offsetd:

```
centre = W/2 + d
left = centre - W*s/2
expandedScale = s + 2*left*(1-h)/W
newLeft = h*left
```

This preserves the native overlap behind the compressed foreground. Using
the capture edge315.0625 and factor4/7 gives newLeft180.0357, covering the
observed211..315 stale gap. Centre426 is a numeric fixture reconstructed with
offset170, not independently measured RAM for this exact draw.

Only crop scale changes. Centre stays fixed, and horizontal raster extent
times projectionX stays `(W*s/2)*(focal/(64*s)) = W*focal/128`. Character size
and placement therefore remain invariant under the source arithmetic. This
expands visibility before CPU/VU clipping rather than moving already-clipped
vertices, stretching Rynn, copying stale pixels, or drawing a black strip.
The hook marks the renderer dirty so camera and frustum rebuild together.
The original manager scale/offset are not changed; original setter resets and
native page/preview operations keep their own state.

The existing opt-in `DRAKAN_INVENTORY_VIEWPORT_LOG` additionally reports up
to24 successful `[inventory-world-crop]` corrections with input/output scale,
offset, base width and foreground factor. No diagnostic was enabled here.

## Validation status and integration

`run_inventory_viewport_policy.ps1`: **480 CPU checks PASS**.
Existing `run_widescreen_policy.ps1`: **2,489 checks PASS**, including900
side-plane witnesses. Both compiled and ran after the parent's reservation
was released. The new tests cover the observed edge,
three widescreen aspects, animated crops/offsets, fixed character centre and
size at several focal lengths, newly admitted side-plane witnesses, repeat
guards and invalid/unrelated calls. They contain no guest code/assets.

Parent rebuilt and exercised the hook. Existing private evidence:

- [21:9 stderr log](../../../build/t/inventory-crop21-probe01/stderr.log), line583:
  renderer6c5d80, width512, offset170, scale.43333 ->.960787,
  foreground factor.571429. [Equipment image](../../../build/t/inventory-crop21-probe01/input_ingame_aspect_equipment.bmp).
- [32:9 stderr log](../../../build/t/inventory-crop32-probe02/stderr.log), line574:
  same renderer/width/offset, scale.43333 ->1.20254, factor.375.
  [Equipment image](../../../build/t/inventory-crop32-probe02/input_ingame_aspect_equipment.bmp).

Both existing completion receipts record scriptQuit=true and exitCode0
(48.328s and48.369s respectively). Parent reports the21:9 gap is filled with
coherent world scenery, with no stale tutorial strip; the32:9 split-footprint
run also has coherent gap scenery and removes both orb edges. These are
parent live observations, distinct from this worker's CPU arithmetic tests.
The orb fix is a separate parent draw/sampling change, not a viewport effect.

Broad category/open-close, native/SR and configuration coverage remains with
the parent's final integration checks; no final23+23 receipt is claimed here.
Source arithmetic and these poses do not establish all portal/VU or scene
coverage. The nested source changes
also require the parent's normal runtime-source publication process if used;
no export or manifest changes were made by this worker.
