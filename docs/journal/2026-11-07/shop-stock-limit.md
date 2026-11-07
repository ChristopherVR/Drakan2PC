# Iron Mace in a new Alchemist: there is no stock limit (2026-11-07)

Correction of the "Shop" finding in `editor-round9.md`. Own exe copy `build/gaps5/play`, packs built with
`editor/scripts/vendor-pack.ts --new-shop` (edit aimed at the new shop), runs through `scripts/harness/vendor_shop.py --no-pack
--no-buy --shop 4` (Surdana, new Alchemist = 4th shop). Log line `stock=` is the shop object's entry count (`+0x1c`).

## What the earlier failure really was

`vendor_shop.py` always passed `--vendor 1648` (the Surdana blacksmith) to the pack builder, also with `--new-shop`, so the
"five entries written" went into the Blacksmith (its log line read `stock=17`) and the new Alchemist kept its class stock
(`stock=4`). The pack of `ed-a-shop` confirms it: the new shop object 18255 carries no stock override at all. The screen that
"never drew" in that run was not caused by the stock. Fixed: the harness passes `--vendor` only when asked or without
`--new-shop`.

## Runs with the edit really on the new Alchemist

| Run | Edit | `stock=` | Shop screen |
| --- | --- | --- | --- |
| g5lim (2 runs) | new Alchemist, Iron Mace limited x1 | 5 | draws in the second run; the first run never got past the help box (input flake, same pack) |
| g5unl | new Alchemist, Iron Mace unlimited | 5 | draws, Iron Mace listed with price 660 |
| g5ex | existing Alchemist #13253, Iron Mace limited | 5 | draws |
| g5sh | new Alchemist, Wood Shield limited | 5 | draws |
| g5sw | new Alchemist, Iron Short Sword limited | 5 | draws |
| g5pot | new Alchemist, Health Potion (Red) limited (already unlimited there) | 4 | draws |

## Guest code

* Shop class `0x100ac`: class table `0x4fd0c4`, handler `0x40e710` (message 1 = load, `0x40a330`). The load reads the field
  container of the shop (class defaults merged with the object's override) in `0x40a128`: first the unlimited list (field 1),
  then the limited list (field 2), creating one item entity per element through `0x409990`. That function returns 0, and the
  slot is not counted, only when the item factory returns nothing; it has no per-class slot count and no weapon check.
* `0x40ab00` then compares every pair of slots with `0x40a910` (same item class) and merges identical ones: a limited unit of
  an item that is also unlimited is absorbed (g5pot: 4), repeated limited units share one slot with a count. So `stock=` is the
  number of distinct item classes, not the number of written elements.
* The only per-class differences between Alchemist, Blacksmith and Mage in the data are `Items Purchased` (what the shop buys
  back), `Display items in Rows?`, repair modifiers and the stock lists. None of them limits or filters the sold stock: the
  Ravenshold smith class mixes potions, weapons, armour and shields in one list, and the new Alchemist sells a mace.

## Editor

No slot limit exists, so the editor writes everything it is given. Added: `stockSlots(entries)` in
`editor-core/src/vendor.ts` (distinct item classes, what the game shows) and a clearer vendor issue for the one case where the
written elements and the shown entries differ: an item listed both limited and unlimited (the game keeps only the unlimited
entry). Tests in `editor-core/test/vendor.test.ts` ("stock slots", "vendor issues").
