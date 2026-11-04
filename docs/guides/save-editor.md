# Save Game Editor

The Save Game Editor opens the PC version's save files (`.rsg`) and lets you change Rynn's health, gold, experience and skills, her inventory, quest progress and the state of the level, or copy values from one save into another. Changes are only written when you press **Save** or **Export**, and the original can always be kept.

<!-- media:save-editor-tour -->

## Before you start

- The game can stay running. Avoid saving to the same slot from the game and editor at the same time.
- **Back up your saves.** Copy the whole `mc0` folder (see [Saves & updates](saves.md) for where it is) before editing. Saving from the editor also keeps a backup beside the file, but a copy of your own is the safest undo.
- Your disc image is optional. Without it the editor shows values and short names; with it (**Disc** button in the header) it shows item names, pictures and quest text.

## Install

Download **DrakanSaveEditor-Setup.exe** or **DrakanSaveEditor-Portable.zip** from the [latest release](https://github.com/ChristopherVR/Drakan2PC/releases/latest). The level editor can also open it: **Tools > Save Editor**, or the **Edit a save** card on its start page. In a browser build it is the `save.html` page next to the editor.

## Open a save

1. Click **Open folder** and choose your memory card folder (`dist/DrakanPC/mc0`). Every slot is listed on the left with its screenshot, level and date; use the search box to filter.
2. Or click **Open save** and pick one `.rsg` file inside a slot folder.
3. Pick a slot. Its picture, level, health and file details appear on the **Character** page.

## Edit

The pages along the top hold everything the editor can change:

| Page | What you can do |
| --- | --- |
| **Character** | Health (with **Heal to full**), gold, experience, level and the melee, archery and magic skills. |
| **Inventory** | The item grid. Select an item to change its durability, quantity and flags. **Add item** and **Browse replacement** open a searchable illustrated catalog with item facts and space checks before you confirm. |
| **Story** | Quests and their progress. |
| **World** | Every record of the level, searchable. Records with a position appear in the 3D view on the right; click one to select it. |
| **Transfer** | Take values (stats, items) from another save, for example to carry a character into an older slot. |
| **Compare** | List the differences between two saves. |
| **Repair** | Check the save, with your pending changes applied, and list any problems before you write it. |

<!-- media:save-editor-inventory -->

Every change is listed under **Pending changes** on the right with its old and new value. The arrow beside a change reverts just that one, **Revert all** drops them all, and **Undo** / **Redo** step through them. Nothing touches the file until you save.

The item catalog offers classes with a usable record in the current save. Open your disc for names, artwork and gameplay
facts. Select a catalog card, inspect it, then press its **Add** or **Replace** button. Tall items need contiguous empty
cells in a column; the catalog explains when an item does not fit. Newly added items remain marked **New** until saved.
Select one to inspect it or use **Remove added item**; selecting its tile keeps it in the pack. You can edit a new
item's quantity/arrows/durability and maximum before saving. Its count bar updates immediately, and Undo/Redo also
works for these values. Replacing an existing item uses the replacement's defaults; undoing the replacement restores
any count edits you made to the original item.

An empty pack still shows its inventory grid. Adding items currently needs an existing carried item to copy the
game's ownership state from; if none is recognised, **Add item** is disabled with an explanation. Pick up an item
in the game, save, and reopen that save to enable it.

## Save or export

- **Save** writes the file you opened and keeps the original beside it as `NAME.bak` (`.bak.1`, `.bak.2` and so on for later saves). In a browser that cannot write files in place, Save downloads the edited file instead; copy it over the original yourself.
- **Export** downloads an edited copy and leaves the original untouched. Use it when you want to try something first.

Start the game and load the slot to see the result. If something looks wrong, restore your backup or the `.bak` file.

## Tips

- Keep values within what the game itself allows (for example, skill levels up to their maximum), and open **Repair** before saving to see any problem it finds.
- Level and experience are separate values in the save. Change both if you want them to agree.
- Quicksaves (F5) are ordinary saves in their own slot (`BESCES-50006Slot33`), so the editor can open them too.
