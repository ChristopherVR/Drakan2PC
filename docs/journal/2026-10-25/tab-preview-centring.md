# Modern Tab screen: keeping Rynn centred in her preview column — 2026-10-25

Follows `tab-rework-phase2.md` (its "Not done" list had: Rynn's preview turns and steps with her idle animation, so
she sometimes stands off the column's centre). Reference: `docs/reference/mouse-menus.md` section 13, "Preview
centring".

## Cause: the joints, not the root

The crop (`tabWorldCrop`, `132f60` from `2c4444`) put the projection centre at a fixed offset, (x - 0.5) W, and the
native camera (`42fd18`) aims once at Rynn's root. RAM dumps every 8 s over 30 s on each page (`tabc-before/ram_*`):

* root transform `player+0x80` (`+0x14` position, `+0x20` matrix): x / z and heading identical in all dumps (only y
  bobs by ~20 units); in view space the root stays on the camera axis (view x / depth ~0);
* the camera object moves by under 10 units;
* a scan for world positions near her found the animated joints: `player+0x480` -> 33 positions (12 bytes,
  `+0x484` capacity 40, `+0x488` count 33), world space. In the dump taken while she turned her back and bent
  (`char_08`) the spine / head joints moved 0.10-0.15 units of x / depth to the right, the picture by +67 px.

So the drift is entirely skeletal: idle turns, look-backs and bends.

## Projection

`132f60(s, renderer, offset)`: `+0x40` = -W s / 2, `+0x48` = -f / (64 s) (widescreen then multiplies by the aspect /
FOV factor), `+0x100` = offset + 0x800. The product `+0x40 * +0x48` does not depend on s. Fitting the joint
directions against the screenshots (a warm-pixel centroid of the column, below) gave 229 frame px per unit; the
product at the world camera update is 427.1 x 0.7143 x 0.75 (16:9) = 228.8, and the joints overlaid with that scale
land on her shoulders / arms / head. Positive view x (camera right axis) = right on screen. The widescreen module now
records the product at each world camera update (`drakan_ws::worldProjectionX()`).

## Fix

Each crop call: u = mean view x / depth of all joints (the skeleton's centre; model-agnostic, no bone indices), eased
by a critically damped follow (0.35 s, frame-rate independent, snaps on open and after a gap > 0.5 s), offset =
(x - 0.5) W - 228.8 u (shift limited to 0.2 W), crop s = 1 + 2 |offset| / W + 0.01 + 2 / W. The whole pixels go to
`132f60`, the fraction is added to `+0x100 / +0x50 / +0x54` afterwards so the follow moves smoothly. Fallback (no
skeleton / projection): the fixed offset. Vertical framing unchanged (no vertical drift reported; bends lower her
head a little, as natively). Of the candidate points the joint mean matched the visible-body centroid best on the
before-run dumps (residual std 7 px vs 16 for the spine chain at full strength and 22 uncorrected). Code:
`drakan_tab_preview_policy.h` (tests `tab_preview_policy_tests.cpp`), `drakan_tab_screen.inl` `tabWorldCrop`,
`drakan_inventory_views.inl` `hookViewViewport`; `DRAKAN_TAB_PREVIEW_LOG=1` logs u, the eased u and the offset.

## Measurements (16:9 game picture, 3840 x 2160 output, save `build/harness/polish-save/mc0`)

Recipe `build/tabc/before.txt`: Tab, 16 screenshots 2 s apart (120 frames) on Inventory, E, 16 on Character, Tab.
Rynn's horizontal position = centroid of warm (skin / hair / leather) pixels in the column, in 1080p pixels:

| Run | Inventory spread / std | Character spread / std |
| --- | --- | --- |
| `tabc-before` (fixed offset) | 149 / 34 px | 148 / 31 px |
| `tabc-after` | 50 / 13 px | 34 / 13 px |

The remaining spread is mostly her arms and hair (the skeleton centre is on target): from the log of `tabc-after`
(483 samples), the skeleton centre's own excursion would have been 234 px (std 38) with the fixed offset; with the
follow its distance from the column centre is std 6.6 px, at most 41 px for a moment during a fast turn (the ease
lag).

Open / close (`tabc-anim`, recipe `build/tabc/anim.txt`, shots every 12 frames): the first crop call of a session
comes before the host publishes its preview target (offset 0, as before); the next one snaps the follow and Rynn
stays in the column through the zoom-in; the close ends with the ordinary gameplay view. projX stays 228.8 through the
open animation (the zoom moves the camera, not the focal length). Game-picture aspects (`tabc-43`, `tabc-219`,
`DRAKAN_WS_ASPECT`): projX 305.1 at 4:3 (no correction) and 174.3 at 21:9, Rynn centred in the column in both; at 4:3
the pillarbox still cuts the left of her column (unchanged, `tab-rework-phase3.md`).

## Notes

* The runs after the build were at ~6-7 fps from the title menu on (the window was presented across adapters; the
  before run happened to be at 60 fps); the follow uses wall-clock time, so it behaves the same.
