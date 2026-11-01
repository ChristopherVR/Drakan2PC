# Vendors: data, editor panel and in-game proof (2026-11-01)

Goal: shops (stock, prices, buy/sell rules) editable in the editor and proven in the game. Reference text:
`docs/reference/level-format.md` section 16, `world-model.md` section 12b, `modding.md` "Shops (vendors)".

## What the disc says

* Shop = object of class `0x100ac` (RFL 172), one ODB class per shop. Stock = two class-reference arrays (`Objects For Infinite
  Sale`, `Objects For Sale`; repeats = units). Price = `Cost to Buy (in Gold)` of the **item class** (RESOURES / MAGIC ODB), scaled by
  the shop's `Item Sale Price Mod` (buy) and `Item Buy Price Mod` (sell back). 8 placed shops on the disc.
* Found by scanning `build/odb-fields/*.json` (from `analysis/scripts/odb_fields.py`) for field names containing shop / price / sell /
  store, and `build/lvl-objects/*.json` for runtime class `0x100ac`. The shop screen is inventory screen 0x15 in shop mode
  (`2026-10-24/shop-mouse.md`).

## Editor

* `editor-core/src/vendor.ts` (+ `SchemaProvider.classNumber/classesWithField`): read/write stock (entries with mode and count),
  settings, price edits as class-record overrides (`priceChanges`), checks (`vendorIssues`). `test/vendor.test.ts`: 14 tests on
  synthetic classes (grouping, round trip through the two arrays, undo, unreachable databases, price rounding/validation, record
  patch, issues).
* `app/src/editor/vendor.ts`: dock tab **Vendors** (registered through `registry.ts`, plus the command "Select the vendors of this
  level") and a **Vendor** section in Details (sale / buy-back modifiers, summary, "Edit stock and prices"). `vendorprices.ts` holds the
  pending item price edits (no DOM) and feeds the story pack through `StorySession.addChangeSource` (wired in `main.ts`); they are saved in
  the project file (`ProjectStory.prices`, optional).
* `editor/scripts/vendor-pack.ts`: builds the pack from the same code (used by the harness).
* Guide "Change what a shop sells" in `guides.ts`; `editor-guide.md` regenerated.

## In-game proof (`scripts/harness/vendor_shop.py`)

Pack: Surdana blacksmith #1648 gets Iron Mace (RESOURES 8569, disc price 660) added to its stock; Rusty Short Sword (RESOURES 7869) costs
7 instead of 165. New Game in Surdana, `DRAKAN_DEBUG_GOLD=1000` (new test aid) and `DRAKAN_DEBUG_OPEN_SHOP=1` open the shop (the game's
own `33be50`). Result of the first full modded run (`build/vendor-proof/v1/modded`): the shop shows "Rusty Short Sword ... Price: 7"
and, at the end of the strip, "Iron Mace ... Price: 660" (17 stock entries in the log: `stock=17`, 16 on the disc); Square + Yes buys
the sword; gold decoded from RAM dumps `rotr8(*0x4fbae0 ^ *0x4fe86c)` goes 1000 -> 993 (exactly the edited price); the Equipment pane
then lists the sword. The full harness run (`build/vendor-proof/v2`, exit 0, `pass: true`): baseline (no pack) gold 1000 -> 835 (165, the disc price), modded 1000 -> 993 (7). The modded Equipment pane shows Rynn's Dagger plus the bought Rusty Short Sword and 993 gold.

Findings / pitfalls:
* A playtest placement next to the shopkeeper on foot was unreliable (the placement ran before the real start, then the player was reset;
  the NPC room geometry is tight), so the proof uses the game's own shop opener, as the mouse work did.
* The first shop visit shows a "Store:" help box that eats pad input; the recipe dismisses it with Cross, and the Spells / Equipment
  tabs show theirs the first time.
* `play_run.py` deletes its run folder, so the harness keeps the recipe beside it.
* Rounding of `price x saleMod` is **inferred** (round to nearest); not tested with a non-integer result in game.
* `npm run typecheck` currently fails in `app/src/editor/startpage.ts` (another session's edit), not in the vendor files; `vite build`
  was not reached.

## Not proven

* The editor panel itself was type-checked and its logic unit-tested, but not driven in a browser against the real ISO (the panel DOM
  was not clicked through). The pack in the proof was made with the same editor-core functions through `vendor-pack.ts`, not by
  clicking in the panel.
* Selling, repair prices and the `Item Sale Price Mod` of a placed shop were not exercised in game.
