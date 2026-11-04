# Save editor item presentation and asset review

Reviewed the existing save editor, the October 31 UI review, November 1 Drakan theme, and November 2 in-game item
proofs. The save editor had no uncommitted changes at the start of this review. Existing stone, pewter and cream-gold
styling already follows Drakan's inventory; no asset replacement or new visual theme was needed.

Read the reference at `D:\Development\uesave` without modifying it. AbioticEditor's `ItemIcon.razor` avoids decoding a
whole catalog before painting its grid; its selected-item inspector pairs local artwork with readable item facts.
Drakan already decodes the player's Resources ODB/models in its worker and renders textured thumbnails with its shared
Three.js renderer. This remains the source of artwork; no extracted game assets are added to the repository.

Findings and changes:

- Every Add/Replace picker metadata lookup previously queued a model decode, even for classes represented only by
  text in a dropdown. `ClassInfo` lookup now accepts an optional thumbnail demand flag; picker lookups request only
  metadata, and visible grid artwork explicitly requests rendering. Existing callers keep their default behavior.
- Pending additions previously always displayed a plus placeholder. They now share the carried-item artwork path,
  including asynchronous image filling, while retaining their pending label and remove action.
- The selected item's inspector now presents its model alongside the disc-derived damage, skill requirements,
  durability and shop-price facts, previously confined to hover tooltips. Its resource identifier follows a pending
  replacement instead of showing the old class. The stone portrait well and cream facts inherit the existing theme.
- Thumbnail and geometry keys now include the disc epoch, preventing the global caches from returning artwork from
  the previous disc after a switch. A mocked renderer regression verifies both keys change after reset.

Validation: all workspace TypeScript/config checks passed; the three existing save UI/item catalog test files passed
(20 tests). Added a demand-loading regression checking that metadata reads do not decode models and that repeated
visible artwork requests decode a class only once (passed). The Vite production app build passed, with its existing
large-chunk advisory. No game build, deployment, save-file writes or reference-repo edits.

Browser QA used the actual Surdana fixture and the player's ISO through file inputs in a local Vite session. All five
carried item images decoded, the selected scimitar portrait and six facts appeared, and a pending Short Wood Bow gained
its own image without saving. Screenshots inspected: `build/scratch/save-editor-review-wide.png` (1600x1100) and
`build/scratch/save-editor-review-narrow.png` (1100x900). The narrow check exposed the existing minimum grid-column
width extending under the right dock; narrow columns now shrink within their panel. No save/export action was taken.
Browser and dev server were closed afterward. The demand/cache regression has two passing tests; typecheck was rerun.

Remaining: the shared thumbnail renderer uses square 96px views; a future portrait renderer could better fill tall weapon slots. Existing
thumbnail polling is retained for level-editor hosts that do not provide the standalone app's art-change events.

## Substantive inventory workflow continuation

The text-only Add/Replace dropdowns are now a persistent visual item catalog. It searches by game name or resource,
sorts by name, renders twelve cards at a time with Show more, and decodes only displayed artwork. Each card shows the
resource id to distinguish classes sharing a game name. Selecting a card opens its artwork, gameplay facts, height
and explicit Add/Replace action. Lack of space disables the action and explains the required contiguous cells.
Search focus and selected catalog details survive asynchronous artwork redraws.

Pending added-item tiles now select their details rather than deleting on click. The inspector has an explicit
Remove added item button, retains the pending/default-values explanation, and follows Undo/Redo. Opening another
document resets the inventory selection and catalog mode.

Found and fixed a substantive preview error: replacement saves already carried their new height, but the grid still
used the original continuation markers. `previewCells` reconstructs the displayed markers using pending replacement
heights, so longer replacements span their actual cells and shorter replacements release the old cells immediately.
A synthetic regression checks both directions and preserves the source inventory.

Actual ISO + Surdana browser workflow:

1. Search Short Wood Bow, inspect its model/facts, Add. Select the new tile: it remains in the pack. Undo removes it;
   Redo restores it.
2. Select the potion in cell 1, Browse replacement, search Rusty Long Sword, inspect, Replace. The unsaved grid shows
   a 3-cell sword and the 2-cell added bow.
3. Export builds 178,859 bytes. The automation browser canceled both the save download and a separate minimal plain
   text Blob download, including with an explicit download directory. Captured the exact Blob created by the actual
   Export button into `build/scratch/catalog-workflow.rsg`, then reopened it using the app's file input. The UI shows
   six items, sword 150/150, bow 200/200 and zero pending changes. Independent SaveDocument parsing confirms class
   0x31fe5 at cell 1 with marker 17/2 and class 0x31bc9 at cell 5 with marker 13/1.
4. Add two 3-cell Wood Long Bows into the remaining full columns. The next Add action is disabled and says there is
   no column of three empty cells. No file write/export was performed for these capacity checks.

Screenshots inspected: `build/scratch/save-catalog-inspect.png` (first layout), `save-catalog-grid.png` (corrected card
padding and twelve actual models), `save-catalog-no-room.png` (capacity refusal). Generic theme button styles initially
overrode catalog-card padding; catalog cards now use the existing slot family so artwork and text occupy separate
areas. Existing UI/core tests passed before this pass; the final focused run includes the new preview regression.

The catalog deliberately offers only template classes present in the current save; it does not fabricate records for
every class on the disc. Editing default values on pending additions still requires exporting/saving and reopening,
which the inspector explains. Native desktop save-dialog delivery was not exercised by this browser check.

## Replacement count edits

A later review found that editing an item's durability/quantity or maximum before replacing it left those edits in
the export queue. The replacement inspector hid them and promised the new template's defaults, but export silently
applied the original item's numbers to the replacement. Pending/exported count edits now exclude replaced cells;
the original edits remain in memory and return when the replacement is undone. New count edits on a pending
replacement are explicitly rejected, matching the existing inspector behavior. Equipment and quick-slot edits remain
independent of item counts.

A regression exports and reopens the replacement, undoes it, exports/reopens the original with its edited counts,
and redoes the replacement. All 33 save document/item tests, editor-core TypeScript checks and formatting checks pass.
This corrects the existing replacement workflow; editing a pending new item's defaults still needs further work.

## Edit new-item counts before saving

The pending-add inspector now edits durability/arrows/quantity and maximum directly. It reads the template's
recognised item-handle layout and uses the existing encoded inventory-field writer after adding the native record.
Overrides belong to the pending item, so removing an earlier addition and shifting its cell does not transfer the
counts to another item. Undo/redo restores values, and invalid fractional, negative or oversized counts are rejected.
The inventory tile displays its current count bar immediately; disc-derived durability is labelled Base durability.
The pending-change summary includes edited counts. This supersedes the earlier save-and-reopen requirement for additions.

Validation: 51 focused save document/item/UI tests, workspace TypeScript/config checks, formatting and Vite production
build pass. The browser used the player's ISO and existing private Surdana fixture: added Short Wood Bow, changed
200/200 to 77/250, undid/redid the maximum change, and inspected the item artwork/count bar at 1600x1100. The actual
Export button produced a 179,191-byte Blob, reopened through the file input; it showed seven items, the new bow at
77/250, and zero pending changes. The reopened grid was also inspected at 1100x900. Screenshots remain private at
`build/scratch/save-pending-counts-final.png` and `save-pending-counts-reopened.png`. Browser and dev server were closed.
Native game loading of this particular export was not retested; serialization uses the already-established add/count paths.
