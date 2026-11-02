# "Add shop here": what ties a shop to its keeper, and the new placement (2026-11-02)

The story overlay listed "Add shop here" greyed out because "a shop also needs the character who opens it, a link the
format work hasn't worked out yet" (`story-in-viewport.md`). That link is now understood and proven in game, and the
command works.

## What links a shop to its shopkeeper

Read off the 14 level dumps (`build/lvl-objects/*.json`) and the ODB field dumps (`build/odb-fields`):

- No field. The character class (RFL 157, runtime `0x1009d`, 283 fields) has no shop field and the shop class (RFL 172,
  40 fields) has no character field. The only shop-flavoured character field is `Store open idle animation`.
- The link is the LVL **link list**. On all 8 disc shops the shop's first link is its keeper and the keeper's only link is
  the shop: Surdana Blacksmith #1648 <-> #1650, Mage #7715 <-> #7714, Alchemist #13253 <-> #7713; LEVEL3ES #7665 <->
  Sailor Skinny1 #5043; LEVEL4LD #6684 <-> Atlus #800; LEVEL4RA #6833 <-> Northmen Villager3 #3487; LEVEL6EN #7166 <->
  Blacksmith #2731 and #20078 <-> Male Villager 4 #20079. Further shop links are quest objects and timers.
- The engine resolves it: `DRAKAN_DEBUG_OPEN_SHOP` logs `npc=` (shop object `+0x44`); for an editor-added pair it was the
  new keeper's handle (`shop 4: handle=4677 ... npc=4676`).
- The missing piece was not the link but the keeper's **state**: shopkeepers start switched off. Surdana's keepers each
  get On / Off from two trigger boxes in their shop doorway (Alchemist `15581`/`15582`, Blacksmith `15585`/`15586`, Mage
  `15823`/`15824`; the outer box sends Off, the inner one the class default On). Without On:
  - copy of the Alchemist keeper (LVL flags 0): not drawn, Use beside it does nothing (run `b1`);
  - copy of the Mage keeper (flags 1): drawn, but Use beside it does nothing (run `c1`).
  With a box sending On around the keeper, the keeper appears and Use within its `Player Trigger Radius (lu)` opens the
  shop screen (runs `d1`, `f2`, `g1`). The radius alone does not open it; the Use key (E) does.

Not checked: `Default On Status` 0 shop classes (the smiths) with the shop left off; the new shop is always switched on.
Detector `Load` (23) messages that go to the disc shops (and the Alchemist's timer) were not needed for the shop to open
and sell.

## What was built

- `editor-core/src/vendor.ts`: `SHOPKEEPER_RFL_CLASS`, `shopkeeperOf` (first linked character, preferring one that links
  back), `levelShops`, `KEEPER_BOX` (4 x 2 x 4 cells) and `buildNewShop(ctx, { like?, shopClass?, yaw? })`, which returns
  a template build of three objects: the shop (class stock, `Default On Status` 1 when the class default is off; with
  `like`, a copy of that shop's stock and settings), the keeper (a copy of a keeper of the same shop class, else of any
  keeper, else a character class with defaults plus a warning) and a trigger box (On, not once, Both) linked to the
  keeper. Links: shop -> keeper first, keeper -> shop. Applied with `ApplyTemplateCommand` = one undo step.
- `app/src/story/storyactions.ts`: `addShopAt(ctx, at, like?)`, `selectedShop`, `noShopClass`; the `story.vendor` Place
  entry is available. `app/src/story/register.ts`: right-click the ground "Add shop here", command `story.place.vendor`
  ("Add shop", at the view centre) and the Place panel placer. With one shop selected the new shop copies its stock. A level
  without a shop class (only Surdana, levels 3, 4 and 6 have one) greys it with "This level has no shop class ...". The new
  shop is selected (its Vendor section shows the stock); the toast says walking up to the keeper and talking opens it.
- Tests: `editor-core/test/vendor-shop.test.ts` (7, synthetic classes: keeper lookup, the three objects and their links,
  class choice, `like`, fallback character, no shop class, LVL write-out of the links, undo); `app/test/story-place.test.ts`
  updated (the shop entry is no longer fixed-greyed).

## In-game proof (Surdana, New Game)

Pack built headless with the same editor-core code (`build/scratch/followup-shop/make_shop.ts`: `buildNewShop` +
`ApplyTemplateCommand` + `applyDocumentToLvl` + `writeObjectList`), run with `play_run.py` through
`build/scratch/followup-shop/run.py` (private game folder, absolute `DRAKAN_MODS`). New shop at the ground 1800 units in
front of Rynn's start (446100, 171300, 344205): default shop class Alchemist (class stock: 4 potions), keeper copied from
#7713, box #18256. Recipe `proof3.txt`: walk forward in three short steps, press E beside the keeper, wait for screen 21,
dismiss the first-visit help box, buy item 0 (Square, Cross). `DRAKAN_DEBUG_GOLD=1000` sets the gold; no shop opener.

Result (run `g1`, shots and log in `build/scratch/followup-shop/proof/`): the keeper stands in the meadow
(`p0_keeper.png`, `p1_next_to_keeper.png`), E opens the shop screen with the shopkeeper camera on him (`gui screen -1 ->
21`), the list shows the Alchemist's 4 potions, Health Elixir "Price: 100" (`p2_shop.png`); after buying, gold reads 900
in the screen (`p4_bought.png`) and in RAM (1000 -> 900, `rotr8(*0x4fbae0 ^ *0x4fe86c)`). Control without the box
(`control_no_box_s4.png`, run `b1`): no keeper, E does nothing.

Pitfalls: `DRAKAN_PLAYTEST_POS` sometimes places Rynn before the opening cut scene resets her (run `e1`), so the final run
uses no placement; a keeper placed against the city gate (first try) sits inside the wall.

## Not done / next

- Not clicked through in the browser UI: the command, menu item and placer call the same `addShopAt` -> `buildNewShop` the
  pack used, and the app type-checks, but no Playwright run against the disc was made.
- No runtime change was needed.
- A keeper copied from another level's character class, and shops in levels 3, 4 and 6, were not run in game.
- The keeper's facing is copied from the model keeper; a "face the camera" option would help placement.
