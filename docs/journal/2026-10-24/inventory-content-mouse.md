# Mouse on the shop, Spells, Skills and Journal — 2026-10-24

Request: fix the blacksmith shop (the item under the cursor could not be clicked; the highlight drifted away from
the cursor), and the other modes not yet fixed for the mouse. Cause of the shop problem:
[shop-mouse.md](shop-mouse.md). Design and verification: `docs/reference/mouse-menus.md` section 11.

## What changed

- The relative pointer-to-D-pad adapter is gone. It produced the drift in every section without absolute hit tests:
  the shop list, Spells, Skills and Journal. `[menu] inventory_step` is still accepted in old ini files but ignored.
- Each section's items are captured at its native draw and selected through the game's own navigation:
  - **Shop list:** `434e80` / `4380b0` icon draws, steered through `33e960`.
  - **Spells:** `4481e8`, steered through `3422c8`.
  - **Skills:** row frames at `445b78`, with Up / Down taps.
  - **Journal:** page clip and arrow glyphs.
  - The Equipment projection is shared as `MenuMouse::projectPlane`.
- **In a shop:**
  - An Equipment click sells (Circle), and a shop-list click buys (Square).
  - The game's own Yes / No box confirms either one.
  - The category tabs accept the shop's 3-tab state.
  - The shop flag (`controller+0xc` / `player+0x590`) was labelled "Arokh mode" in MenuMouse; the label is corrected.
- An action waits while the inventory controller is still busy with the selection change (`*controller != 0`);
  before, a click that both moved the selection and acted could lose its action.
- A left click continues the widget-less pages:
  - text page 0x10 and movies 0x1d / 0x1e: START;
  - credits 0x13 and memory-card warning 0x1c: Cross.
  - 0x10 and 0x13 joined the menu-context screens, so the cursor shows there.
- Test aids:
  - `DRAKAN_DEBUG_OPEN_SHOP` opens the n-th shop of the level through `33be50`, tail-called on the real context like
    the title Quit box.
  - `DRAKAN_DEBUG_SKILL_POINTS` writes unspent skill points.

## Findings from the live runs

- Surdana has three shops: 1 the blacksmith (16 weapons, a scrolling strip of 8), 2 Jade's Magic Emporium (9 spells
  in rows), 3 potions (4 items).
- `44a678` is called more than once per frame; one pass draws no icons. Clearing captures on every call emptied the
  list, so a frame's captures now last until the next input update.
- A shop refuses what it does not trade in without any message: the blacksmith refuses the Health Elixir, Jade's
  refuses the bow, and a spell above the magic skill (Ice Blast needs 2) is not sold. A pad Circle does the same, so
  these are native.
- Summon (Square in Spells) sets `player+0x1040` to 1 and then 4. While it runs, the native selector refuses
  navigation, and so does the mouse.
- Right click does not dismiss the Yes / No boxes (native: Circle / START do nothing there); clicking No does.

## Verification

- Unit tests (`runtime/input/tests/menu_mouse_inventory_tests.cpp`):
  - captures, hover steering, click actions, busy waits and refusals for Spells, Skills, the shop list (all drawn
    and strip), Journal arrows and wheel, Equipment sell, and the click-to-continue pages;
  - the two old relative-navigation expectations now assert that nothing is emitted.
  - All 44 runtime tests and the Python tests pass.
- Live (harness runs under `build/harness/`, see the reference section):
  - blacksmith buy and sell with Yes; the gold changed as expected;
  - magic shop hover, and two spell purchases;
  - Spells hover steering and Summon;
  - Skills hover, click and wheel;
  - Journal page wheel.

## Not verified

- The Journal arrow glyphs, which need a save with a second region or overflowing text.
- The click-to-continue pages.
- Shop items larger than one cell wide (the blacksmith's are 1x2, Jade's 1x1).
