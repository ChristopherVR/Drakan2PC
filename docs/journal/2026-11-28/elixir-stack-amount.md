# Elixir stacks in the modern inventory (issue #23)

Report (build e2138563, D3D11, modern inventory): two Health Elixirs picked up, the inventory showed one elixir with no
amount; drinking one healed and the cell still showed one elixir with "charge 100"; the second drink healed and the
elixir was gone. Nothing between e2138563 and HEAD (294ef422) touched this: the only stack-related change was the quiver
fix `87b67b17`, which hid a quiver's +0x14 value and left its count to the native icon.

## Cause

The game keeps a stack of usables (object type 11: elixirs, potions, quivers) as **one** item in one cell. Its count is
a plain s32 at instance data `+0x2c` (instance data = `*(*(h+0xfc)+0x10)`, `Cam_GetObjFromHandle`):

* `437170` (grid draw), per cell: `if (*(obj+0xc) == 0xb) count = *(Cam_GetObjFromHandle(h) + 0x2c); else count = 1;`
  then `434e80(section, obj, count, cell, ...)`. **[dec]**
* `434e80` (item draw): `if (type == 0xb && count > 1) sprintf(buf, fmt, count); 4315b0(section, buf, pos, 0);`, the
  digits at the cell's top left. `4315b0` is called from nowhere else. **[dec]**
* The 2026-11-18 OotF pickup test saw a potion join a stack: `+0x2c` 9 -> 10 (`ootf-issue-12.md`). **[RAM]**

The modern reader (`runtime/gameui/src/Reader.cpp`) never read `+0x2c`. It read only the `+0x14 / +0x18` pair, which for
consumables is the charge pair (`QuantityKind::Charges`; "Charge" in the native details **[inf]**): 100 / 100 for an elixir, and a
drink does not change it. The Tab grid and the I strip printed that `+0x14` on the cell (bottom right, where an amount
goes) and the card drew a "Charges 100 / 100" bar. So two elixirs looked like one item of "100", and after a drink
nothing changed until the stack was empty. The native count digits, if they reached the icon crop at all, were small
and at the opposite corner.

## Fix

* `ItemEntry::stackable` / `amount` (`Reader.cpp`, `addr::actor::kStackCount = 0x2c`): read for type 11 grid items,
  values outside 1..9999 read as 1. Not read for the shop stock record (what `+0x2c` holds there was not traced).
* `ItemEntry::cellBadge()`: the cell number is the amount when > 1 (as the native grid), else a bow's arrows. Charges are
  no longer printed on cells; they stay on the card's bar.
* Cards (Tab detail panel, strip card): an `Amount` row first for every stackable item.
* `Expectation::before3`: a Use is also confirmed by the amount changing.
* Glue (`drakan_inventory_views.inl`): `4315b0` from `434e80` is skipped while a host view draws the grid, so the native
  digits do not sit in the icon crops next to the host's number (like the quick-slot marker). The native inventory and
  the shop list keep them.

Tests: `gameui_test` `testElixirStack` (two elixirs: one entry, amount 2, "2" on the cell and no "100", card Amount 2;
use: confirmed, amount 1, no badge, card Amount 1; last one gone; garbage words read as 1; a weapon's `+0x2c` ignored),
`gameui_screens_test` `testStripElixirStack` (strip cell and card).

## Not verified

No game run (needs the user's ISO and a real pickup of two elixirs). To check in game: pick up two elixirs, open Tab and
I: "2" bottom right on the elixir cell, the card lists Amount 2; drink one: Amount 1 and no number on the cell. Also
check a quiver (its count now comes from the host, the native digits are skipped in the host views).
