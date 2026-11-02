# Save editor: added items work in game, tall items and the bow included

Follow-up to `../2026-11-01/save-add-item.md`. Goal: show in the running game that items the save editor adds can be
used and equipped, including items taller than one cell, and that the game's own save and reload keeps them.

All saves were built with the editor's own code, run headless (`SaveDocument.open`, `AddItemCommand`,
`ReplaceItemCommand`, `SetFieldCommand`, `build()`) from a copy of the Surdana sample. Item heights came from the disc's
catalog (`app/src/itemdata.ts` `loadItemCatalog`, read from the player's ISO). Games were run with
`scripts/harness/play_run.py --save <card copy> --recipe <recipe>`. Scripts, cards, recipes and screenshots are in
`build/scratch/saveitems/` (`mk.ts` builder, `cells.ts` cell table + stream dump, `cat.ts` catalog, `flags.ts` and
`state.ts` surveys over 250 saves found under `build/`; runs in `runs/si_*`).

## Results

| Item (class, cells) | Added | Shown in Tab | Used / equipped | Kept by the game's F5 + reload | Evidence (`build/scratch/saveitems/runs/`) |
| --- | --- | --- | --- | --- | --- |
| Health Elixir (0x33752, 1) | yes | yes | drunk, cell empties | yes | `si_a1/d_elixir_used`, `si_h1/c_elixir_used` |
| Rusty Scimitar (0x3372d, 2) | yes | 2 cells tall | equipped right hand (dual wield off), left hand (on); swing shows it | yes, still in the left hand | `si_b1/c_scimitar_equip`, `si_c1/d_scimitar_equip`, `si_d1/b_tab_reloaded` |
| Rusty Long Sword (0x31fe5, 3) | yes | 3 cells tall | equipped right hand once the editor raised Melee to 2 (refused at skill 1: "Requirement not met") | yes | `si_c1/f_longsword_equip`, `si_c1/h_swing`, `si_d1/e_holding` |
| Short Wood Bow (0x31bc9, 2) | yes | 2 cells, 200 arrows | equipped, an arrow fired (199) | yes, 199 after reload, fired again (198) | `si_e1/d_bow_equip`, `si_e1/k_swing`, `si_f1/e_fire`, `si_h1/g_fire` |
| Wood Long Bow (0x32266, 3) | yes | 3 cells, 300 arrows | not equipped (only shown) | yes | `si_e1/g_longbow`, `si_f1/b_tab_reloaded` |
| Steel Club (0x3372b, 2; 606-byte record) | yes | 2 cells | refused at Melee 1 ("Requirement not met", skill 2), not retried | yes | `si_e1/f_club_equip`, `si_h1/e_bow_equipped` |
| Full Mana power-up (0x31b33, 1) | yes | yes, count 100 | used, cell empties | yes (used after reload) | `si_f1/d_mana_used` |
| Replace with a taller class (potion -> Wood Club, 2) | replaced | 2 cells tall | equipped left hand next to the dagger | not run | `si_r1/b_tab`, `si_r1/c_club_equip` |

The final combined check is `si_g1` (elixir, scimitar, long sword, bow, Steel Club added together, Melee 2; scimitar and
long sword equipped, swing) then `si_h1` (reload of the game's own save, elixir drunk, bow equipped and fired).

## Bugs found and fixed (editor-core `save-items.ts`, `save-doc.ts`; app `save/inventory.ts`)

1. **The stream's second word is the item's height, not a flag.** In every save the game wrote, the pair after a
   carried item's slot is `(slot, rows)`: bow, club, scimitar and Steel Club 2, potions and the dagger 1, the long
   sword 3 after the game re-saved it (`flags.ts` over all saves). The editor wrote 1 for every item, so the game
   loaded a tall item as a one-cell item: no markers under it in RAM (its own save of the first test, `si_a1`, had
   none), the Tab screen drew it one cell high, and a later pickup could land on the cells it covers. `addItem` now
   writes the height. With it the game marks the cells itself and its own save keeps them (`si_c1`, `si_e1`, `si_g1`).
2. **Added items were drawn on Rynn's body.** The new record copied the carried state from the first carried item,
   the equipped dagger, including u16 +0x52 (a holder reference: 0xc0, 0xd6, 0x1e4 ... in equipped items, with +0x54
   = 0). Every unequipped carried item in the game's saves has 0xffff, 0xffff there (`state.ts`). The elixir showed in
   her left hand and the Steel Club at her hip while both sat in the pack (`si_c1/h_swing`, `si_e1/k_swing`). The donor
   is now the first unequipped carried item, and the new record gets 0xffff, 0xffff (`si_g1/f_swing`: clean).
3. **Records with the handle further on were ignored.** The editor only accepted item records with the self handle at
   +0x86. Bows (0x112 in shop and world records, 0x140 / 0x148 in carried ones), 606-byte weapons such as the Steel Club
   and the Iron / Steel swords (0x96) and the mana power-ups were never offered. This is why "the bow" could not be
   added: its records were never templates. `itemHandleAt` finds the handle per record (a heap word repeated 0xe0
   later). `replacedBody` keeps the header ranges at their offsets and moves the handle, its copy and the word 0x10
   after it to the template's own offset. The Surdana sample now offers 33 classes instead of 16. The bow exclusion in
   the Add item list is gone.
4. **Replace with... ignored the new class's height.** Replacing a potion by a club kept height 1 (one save under
   `build/` has a club with height 1). `replaceItem(save, cell, template, rows)` and
   `ReplaceItemCommand(cell, template, rows)` now set the height word and the markers. They refuse when the cells under
   the item are taken; pending added items count as taken (`SaveDocument.replaceFits`). Add item's placement sees
   the new height. The UI passes the height when the disc or a carried item gives it, and marks classes that do not
   fit with "- no room".

Tests: `editor-core/test/save-items.test.ts` (synthetic records only): the height word, a taller or shorter replacement
and its refusal, the pending-add interaction, shifted-handle records (`itemHandleAt`, `replacedBody`, add), and the
unequipped donor with the holder words. `npx vitest run` in `editor/`: 67 files, 646 tests passed.

## Other format facts

- The bow is 2 cells (`Icon Slot Size` 2 on the disc, and 2 in the stream and the markers of every save that carries
  it). The 3-cell items are the long bows, the great swords, the long swords and the maces. The earlier notes said
  "bow 3".
- The u16 8 bytes before the cell table counts items gained, not items held. The sample has 3 with 2 items. Each add
  raises it by one, and the game kept 6 after the elixir was drunk.
- Equipped flag: cell table word 0 (1 = equipped, shown as R / L / E). The game sets it on the added items when they
  are equipped and keeps it in its own save.

## Harness notes

- The recipe key for Enter is `ENTER`. `RETURN` is not a key name and was silently ignored. This is why the 2026-11-01
  run "did nothing visible" on Enter.
- `play_run.py` needs absolute `--out-root` / `--recipe` paths. With relative ones the game, which runs in another
  directory, does not find the recipe (`[input] script ... not loaded`) and waits at the title until the timeout.
- With dual wield on (the default play folder), a second one-handed weapon goes to the left hand ("Equip Left Hand").
  It was refused right after drinking the elixir (`si_a1`, "That can't be used right now"): Rynn was still in the
  drinking animation. Without the drink it was accepted (`si_c1`).
- In the modern Tab screen, Right from a 3-cell item moves to the next column's middle row. Use Up to get back to row 0
  (`recF.txt`).

## Not done

- The Steel Club and the long bow were not equipped. The Steel Club was refused only for its skill requirement.
  The long sword shows that raising the skill with the editor is enough.
- The mana power-up is a pickup class. It worked from the pack, but the editor does not tell such classes apart from
  inventory items.
- Classes found with other handle offsets (0x9a: Bitter Touch) were added in no test.

## UI leftovers (Save Game Editor)

1. **Stale "does not fit" message.** The status line kept the no-room text after a later Add item worked. Add item now
   goes through `addChosen` (`app/src/save/inventory.ts`), which sets "Added NAME; pending until you save." on success
   (replacing the old text and clearing the error colour) and the no-room text, as an error, when it does not fit.
2. **Save date.** The save carries no date and no play time: the `.rsg` entries have no such field (see
   `reference/save-slots.md` section 7), and the card folder's `icon.sys` holds none. The game's own load screen shows
   the host file time. The list and the save header therefore say "File changed <date>" (`fileChanged` in
   `app/src/save/dom.ts`), and nothing when the file time is unknown. No change under `packages/formats`.
3. **Power-ups.** The disc's item classes carry an engine class (`rflClass`, low 16 bits of the ODB runtime class).
   The two mana power-ups (Full / Half Mana PwrUp) are class 171 and no other Resources class uses it, so
   `loadItemCatalog` marks them `powerUp` (`POWER_UP_RFL_CLASSES` in `app/src/itemdata.ts`) and Add item lists them
   under "Power-ups (used at once)". Without the disc they cannot be told apart and stay in the normal list. The spell
   power-ups ("#1 Fireball Power Up" and the like, class 57) are not listed because their behaviour was not checked in
   game; add 57 to the set if they turn out to act at once too.

Tests: `app/test/save-ui-leftovers.test.ts`, plus a catalog case in `app/test/itemdata.test.ts`.

## Remaining proofs

Run folders are under `build/scratch/saveitems2/` (builder and cell-table scripts are the ones in `build/scratch/saveitems/`).
Cards: `cardS` (Surdana sample + Melee/Archery/Magic 3 via `SetFieldCommand`, then Add item: Steel Club 2 cells at cell 5,
Wood Long Bow 3 at cell 6, Bitter Touch 2 at cell 7); `cardP` (Melee 3, Replace cell 1 potion -> Rusty Long Sword, height 3).
Recipes `rec1.txt`, `recBow.txt`, `recRep.txt`, `recRel.txt`.

| Proof | Result | Evidence |
| --- | --- | --- |
| Steel Club equipped (Melee 3) and swung | PASS: "Equipped, Left hand", swing trail drawn | `r1/d_club_equip`, `r1/j_swing` |
| Wood Long Bow (3 cells) equipped and fired | PASS: "Equipped", 300 arrows, 298 after two shots | `bow/d_longbow_equip`, `bow/g_fire2`, `bow/h_tab_after` |
| Bitter Touch (handle at +0x9a) added, equipped | PASS: "Equipped, Right hand", Damage 8 AP, Very Fast, 300/300 durability, shown in Rynn's hand | `r1/h_bitter_equip`, `r1/j_swing` |
| Replace with... (potion -> Rusty Long Sword, 3 cells), game's own F5, reload | PASS: 3 cells tall before and after; equipped left hand before the save and still "L" after the reload; unequip then equip again works; swing ok | `rep/d_replaced_equip`, `rep/g_saved`, `rel/b_tab_reloaded`, `rel/e_reequipped`, `rel/g_swing` |

The game's own save (`rep/card/mc0`, loaded in `rel`) has the replaced item as stream `311/3` with markers 9 (1) and 17 (2),
the same as the editor wrote. The game also rewrote the sample's other two blades to heights 2 and 3 (stream `632/2`,
`631/3`; the sample's own stream had 1 for both, with markers for 2 and 3 already present). The icons in the Tab screen
changed from cut off to full size. This is the sample's stale data, not the editor's.

No editor bugs found in this round. Notes:

- Equipping a one-handed weapon in the right hand with a bow equipped unequips the bow (`r1/h_bitter_equip`). Normal game.
- Wood Long Bow: the grid showed 390 arrows while the detail panel said 300/300 and the first shot after equipping read
  298 after two shots. The 390 is unexplained (not checked against the save bytes); the game's count after firing is right.
- Harness: `play_run.py` runs failed for ~1 h at the title (never reaching the F8 quick load) because the harness sets
  `DRAKAN_AUTOPRESS=3000`, which taps Cross every 3 s on the title and bounces between the title and the Load screen
  whenever the run is not already steady/playing. Passing `--env DRAKAN_AUTOPRESS=0` fixes it (all runs above). Whether
  the cause is the current runtime build or the machine was not found; the 10:30 runs of the earlier round did not need it.

## Long bow badge

The "390" on the Wood Long Bow's grid badge was a misreading, not a data fault. The badge reads **300**: the screenshots in
`build/scratch/saveitems2/bow/` are 3840x2160 PNGs that were viewed as 1280-wide JPEGs, and the skin font's old-style
figures draw "0" small, so "300" looks like "390" at that size. Crops of the full-size PNGs
(`build/scratch/bow390/c1.png` for `d_longbow_equip`, `c2.png` for `h_tab_after`) show "300" before firing and "298" after.

Where the number comes from: `runtime/gameui/src/Screen.cpp` (grid cell) and `StripScreen.cpp` print `it.quantity` as the
badge for every item whose `quantityKindFor` is Ammo or Charges, and the detail panel bar uses the same `it.quantity` /
`it.quantityMax`. `Reader.cpp` fills both from the item instance (`a::actor::kQtyCur` / `kQtyMax`, +0x14 / +0x18 of the
instance, decoded). One field feeds both, so they cannot disagree; the panel said 300 / 300 and the badge said 300.
The editor wrote nothing wrong for the bow (no code change, no test change).
