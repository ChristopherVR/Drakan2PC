# Issue 22: quick bar weapons partly hidden by level geometry

[GitHub issue 22](https://github.com/ChristopherVR/Drakan2PC/issues/22), reported on build `e2138563` (Win11, D3D11,
DLSS SR on, 21:9). With a club and a dagger in the Quick Inventory bar, parts of the club disappear behind a tree trunk
that is nearer to the camera than the bar items. No commit between `e2138563` and `294ef422` touched this path.

## What the video shows

Frames from the issue video (crops of the bar at 6.3 to 11.5 s): the club's pommel and handle are drawn over the trunk,
but its head above the pommel is cut off along a straight horizontal edge where it overlaps the trunk. Where the head
is over the far hillside it is drawn whole. The dagger is drawn over the trunk in full. So the bar does clear the world
depth under its items, but the cleared area ends at the top of the club's handle.

## How the game draws a bar item

Ghidra decompile of the PAL ELF (`build/recomp_ui/alldecomp`, `InputDecompAll.java` on a copy of the project):

| Address | What |
| --- | --- |
| `139010` | frame: world, EndWorld `147690` (ra `139344`), then the game object's HUD callbacks; vtable `52a640` slot `+0x94` = `3b2178` |
| `3b2178` | HUD: `3afa88`, `3af690`, then `3ac540`, which calls the bar draw `3b1288(…, ctl)` |
| `3b1288` | Quick Inventory bar (`ctl+0x10` shown, list `+0x214`, index `+0x220`, scroll `+0x224..+0x234`): for each listed item `3aff40(scale…, angle, alpha, model = 341570(list, i), screen position, colour)`. With the bar closed it shows a picked-up item (`+0x208`, timer `+0x20c`) through the same call |
| `3aff40` | item overlay draw: camera rotation set to identity (`430e90`); unprojects the screen position at 2000 units; clears the item handle's box-valid bit (`+0x104 & 0x2000`) and reads its box (vtable `+0x84`, handle `+0xb8`: origin and three edges, rebuilt by `17f938` from the current matrix); projects the eight corners with `1336a8`; calls the render object vtable `+0x44` = `1342e0` with that rectangle (return address `3b0784`); then sets the new matrix (`3b2800`), draws the model, and draws the item's **part objects** with `3b0868` |
| `1342e0` -> `146df8` | depth clear of a rectangle: `TEST` ZTST ALWAYS, `ZBUF` ZMSK 0, `FRAME` FBMSK `0xffffffff`, 64 x 32 sprites at Z 0, then the context's depth state back (GEQUAL, Z write) |
| `3b0868` | parts: instance `*(*(item+0xfc)+0x10)`, list at instance `+0x108` (`+0xc` u16 registry refs, `+0x14` count); each part gets the item's matrix and is drawn after the item. `346aa8` (item code) walks the same list to find an item's centre, so items are built from several part objects |

So the native clear covers only the projected box of the item handle itself. The parts are drawn with depth test
GEQUAL against whatever the world left in the depth buffer outside that rectangle. The PS2 code has the same gap:
this is not a GS translation error (the clear arrives as the depth-only sprite batch it is, both clear and item are
drawn with the native 4:3 projection after EndWorld, the HUD map leaves both untouched). It shows only when nearer
level geometry is behind a part that sticks out of the handle's box, as the club's head does.

Other callers of `3aff40`: `2c1210` (a GUI list of items). They take the same fix.

## Fix

`drakan_item_overlay.{h,cpp}` wraps `1342e0`. For the call with return address `3b0784` it reads the item handle from
`s5` (the `a0` of `3aff40`, kept for the whole draw), walks the part list like `3b0868`, takes each part's box at
`+0xb8` when its box-valid bit is set, projects its corners with a host copy of `1336a8`, and widens the rectangle on
the guest stack before the original runs. The clear stays the game's own GS packet in its place in the draw order.
A part box is used only when it reaches the item's rectangle grown by the item's own size, and the widening is held to
twice that size and to the frame, so a stale box (a part last drawn somewhere else) cannot clear half the screen. The
parts' boxes are from the previous frame, as the item's own box is in the original.

`DRAKAN_ITEM_OVERLAY_DEPTH=0` turns it off, `DRAKAN_ITEM_OVERLAY_DEPTH_LOG=1` logs widened rectangles
(`[item-overlay] #n item …: depth clear a -> b (k of m part boxes)`).

Tests: `item_overlay_policy` (scripts/tests, runtime-tests preset): the projection against the `1336a8` formula, the
eight-corner rectangle with `cvt.w.s` truncation, a club-like handle and head (the clear grows over the head), stale and
huge boxes, the frame limit, empty rectangles and boxes behind the camera.

## Not verified

* In game: the game was not launched. To check, put the club and another weapon in the quick slots, open the bar with
  near geometry (a tree trunk) behind it, and run once with `DRAKAN_ITEM_OVERLAY_DEPTH_LOG=1`: the log should show the
  club's rectangle growing (`1 of 1 part boxes` or similar). If it shows no widening for the club, the head is not a part
  object (for example the handle box simply leaves it out) and this fix does not reach it.
* That the club's head is one of its part objects is inferred from the decompile and the video (the cut follows the top
  of the handle; single-part items such as the dagger are drawn whole), not read from RAM.
