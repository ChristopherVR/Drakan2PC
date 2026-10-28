# Blacksmith / shop screen: the cursor does not select the item under it

Report: in the blacksmith's shop the highlighted item does not follow the cursor, and clicking an item does
nothing. Static analysis of SCES_500.06 found the cause. No code was changed: the fix needs a live check at a
shop, which this session could not reach.

## What the shop screen is

The shop is not a screen of its own. It is the inventory, screen 0x15, opened in shop mode:

* `33be50` (talking to a shopkeeper) calls `345a70(player+0x584, shopkeeper, 0)`, which calls `33c660`. Opening
  the normal inventory (Select) uses `270af0` and passes no shopkeeper.
* `33c660(controller, shopkeeper, ...)`: when a shopkeeper is given, it sets `controller+0x18 = shopkeeper` and
  `controller+0xc = 1`. It opens screen 0x15 through the screen factory `2c4458` and then sets `controller+0x24 = 2`
  (0 when the inventory is opened normally).
* `340508` returns `controller+0xc`. `MenuMouse` already reads this as `player+0x590` to choose 3 category tabs
  instead of 5. The code comment there calls it "Arokh mode", but `33c660` sets it for the shop.
* The section builder `42c9a0` checks `340508()`. In shop mode it creates three sections: 0 Equipment (vtable
  `54e108`), 1 the Spells class (`54f3a8`) and **2 the shop list (vtable `54f488`)**. It opens on section 2
  (`screen+0x154 = 2`). In the normal inventory, index 2 is Skills, and Map and Journal follow it.
* The shop list draws its items with its own routine, **`44a678`** (vtable slot 0x6c). Equipment and Spells use
  `437170` in that slot. `44a678` walks the stock list at `section+0x70 -> +0x18` (count `+0x1c`, scroll `+0x28`,
  selected index `+0x8`). It places each item in (column, row) cells, 8 columns wide, and the item size comes from
  `437d38`. Each icon is drawn through `434e80`, or `4380b0` for one item kind. `434e80` uses the same cell frame
  as Equipment (`section+0x60/+0x64` spacing, `+0x68/+0x6c` origin, `+0xc` transform).

## Why the mouse is out of sync

`MenuMouse::updateInventory` (`runtime/input/src/MenuMouse.cpp`) has an absolute hit test only for the Equipment
grid (section 0, captured in the `437170` hook) and for the Map markers (section 3). For any other section it uses
the old relative adapter. That adapter turns every 36 GUI units of pointer travel into a D-pad step and ignores
left clicks. Two things follow in the shop:

1. Section 2 is the shop list, but the adapter handles it as if it were Skills. Moving the cursor moves the native
   highlight by relative steps, so the highlight drifts away from the item under the cursor. This is the "offset"
   in the report. A left click does nothing, which is deliberate for sections without certified rectangles.
2. `44a678` is not hooked, so nothing captures the shop grid's geometry. `captureInventoryGrid` also returns early
   when `controller+0x24 != 0`, and shop mode sets it to 2. So the Equipment (sell) grid inside the shop is not
   captured either, and section 0 then fails closed: clicks and hovers do nothing.

The display transform is not involved. The shop page uses the same left-anchored inventory page map as the
inventory. The cursor-to-GUI mapping is correct. What is missing is a hit test for the shop's content.

## Proposed fix (not applied)

1. Hook `44a678`. Project its cell frame with the same math as `captureInventoryGrid`, and record each item's
   rectangle as origin cell plus size. Two ways to get the rectangles: hook `434e80`/`4380b0` with return address
   `44aa94`/`44aa0c` and read `{col,row}` from `a3`; or read the caller frame (`sp+0xc0/0xc4` size, `sp+0xd4` list
   index). Allow `controller+0x24 == 2` for this capture.
2. On hover or click, request the target index with native navigation until `list+0x8` equals the target, as the
   Equipment path does. Activate only after that. A purchase opens the native "Buy Item?" confirmation (a normal
   message box, which already has mouse support).

These points need a live session at a shop before the fix is trusted:

* how the D-pad steps change `list+0x8` (linear index or 2D);
* which pad button buys (Cross or Square);
* what `controller+0x24` means (0 / 1 / 2: which pane has focus) and whether Equipment selling with the mouse
  should be allowed in shop mode.

## Resolution

Implemented the same day, with the open points answered by the live runs. The list navigates by index, so
`33e960` is steered like `33eaf8`. Square buys and Circle sells. `controller+0x24` is the focused pane (Equipment 0
while it is the current section), so the Equipment grid capture already worked in a shop. See
[inventory-content-mouse.md](inventory-content-mouse.md) and `docs/reference/mouse-menus.md` section 11.
