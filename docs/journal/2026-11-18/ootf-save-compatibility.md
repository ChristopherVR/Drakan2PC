# Order of the Flame saves across the creature replacement

The installed pack had build `493b53de681b8791`. Several real saves, including the Wartok Canyons save in slot 14,
still used `da1e598e3d3adfdd`. Comparing the installed layout with the archived pack under
`build/review/cpu-offload/mods` found four removed NPC classes and 45 placed objects switching to their disc
counterparts. All other layout keys matched. The disc replacements have the same engine class and field count.

| NPC class | Earlier ID | Disc ID |
| --- | ---: | ---: |
| Crimson Dragon | 7084 | 3040 |
| Blade Dragon | 7096 | 2676 |
| Ice Dragon | 7109 | 5604 |
| Scavenger AnimWay Point | 7130 | 3134 |

The migration runs on a copy of the save opened by the memory card reader. Its build check applies to this pair of
Order of the Flame builds. The reader resolves the database by the save's own table and rewrites the class ID in
each matching record. Slots and references keep their order. The normal pack revision upgrades also run.

The compiled game restored copies of slots 13, 14 and 33 and accepted movement input. Slot 14 replaced nine Blade Dragon
records. Slots 13 and 33 restored the statue while its generator was present. SHA-256 comparisons against the copies
confirmed the original save records, level states and sidecars kept their bytes.

The final build loaded a copy of slot 14, saved it, and loaded it again. It wrote the current build for the new
save and kept the old build in sidecars for three earlier level states, whose contents kept their original bytes.
If a required save upgrade fails, the memory card reader refuses the open. The deployment also preserved all 103
files on the installed memory cards by SHA-256 comparison.

The converter now checks compatibility before replacing a pack. A visual class split can retain the build when the old
class stays available with the same signature and the new class shares that signature. The level's dependency IDs are
explicit and sparse. For example, slot 3 in Ruined Village names Resources even though it is the second dependency
listed. The complete 23-level rebuild for the tree fix retained build `493b53de681b8791`. The guided builder uses the
installed pack as its baseline and the installer rejects a differing build or a lower save revision.

## Earlier packs

The exact 9 November pack was rebuilt from the editor at `09ff9e11` and formats at `48f6c928`, using the locally
extracted PC installation and the Drakan 2 `LEVEL1NA` template. The full conversion reports content build
`d248f6c701888bf8`, matching the older pack stamp in slots 2, 3 and 4. The current pack differs at 1,852 layout keys.
The recovery launcher under `build/legacy-save-recovery/nov09` uses the matching pack and a separate copy of the
memory card. The native Load Game grid and the individual quick-load tests for slots 2, 3 and 4 returned to gameplay
with pack checks enabled.

In a scratch folder, export `editor/scripts/ootf-convert.ts` and `editor/packages/editor-core` from `09ff9e11`, then
the formats package from `48f6c928` at `editor/packages/formats`. Link those packages and the installed `fflate` in the
scratch editor's `node_modules`. The converter command from the repository root uses the player's local inputs:

```powershell
$iso = "C:/Games/Drakan - The Ancients Gates/Drakan - The Ancients' Gates.iso"
$scratch = 'build/historical-save-probe/nov09'
node --experimental-transform-types --no-warnings "$scratch/editor/scripts/ootf-convert.ts" --ootf build/ootf/cab_disc --ps2 build/ootf/ps2/data --template LEVEL1NA --campaign 1 --id ootf-levels --iso $iso --out "$scratch/ootf-levels.zip"
```

### Slot 1 from 14 November

The source pack stamped in slot 1 (`daf6d8d09586c549`) was unavailable. Reconstructing the 14 November editor
(`ee2305c2`, formats `4ef9cb8`) produced different content hashes with ordinary and wide terrain chunks. Both
reconstructions match the actual save's recorded class IDs and databases with every named static object present.
The current layout inserted two falling-tree objects before the church exit relay. All eleven affected objects,
3374 through 3384, match 3376 through 3386 in the current pack after correcting their own IDs and link targets.
Class, transform, event fields and authored parameters all match. This comparison disambiguates the co-located
timers and detectors.

The one-off recovery (`build/historical-save-probe/recover-slot1.py`) used the original files as inputs and made
copies under `build/legacy-save-recovery/nov14-slot1`. Its Village copy changed the IDs in the object tables, retaining
all old slots and saved records. It appended the statue and the two hidden falling-tree companions. The generator
and the standing tree were both present in that save. The tree's serialized link list had the two original targets.
The recovery retained them and added the slots of the new toggler and mover. Loading the copy in the final game,
walking and quicksaving worked. The game-written save still had all four tree links and the statue and generator.

The Book copy's gameplay payload kept its original bytes. Its pack record points to the current build. The original
numbered slot no longer had a Village level state. The paired Village save, made 29 seconds later, provides the
return state. A test that put it only in the Book's numbered slot started a fresh Village scene on return.
Putting that same snapshot at the recovery card root as `OotF 01 - Ruined Village.rlt`, with its pack record, made
the native loader read it. After one Escape during the Book, the Village loading screen closed with gameplay active.
Rynn returned to the church stair at (279392, 14671, 152908) and walking moved her to (280100, 14633, 152754).
The run captured the Book and both Village frames, then quit cleanly.

The Village launcher retains a separate card. The Book launcher clones its template, including the root companion,
to a fresh directory for each replay. Existing progress in the normal card, Village recovery and earlier Book
replays stays in its own folder. The original slot and its archived copy have matching SHA-256 checksums.

The final native Load Game run uses the replay template and leaves its contents alone:

```powershell
python scripts/harness/play_run.py --exe dist/DrakanPC/drakan.exe --name recovered-nov14-book-final --save-ootf build/legacy-save-recovery/nov14-slot1/book-template/mc0_ootf --recipe build/historical-save-probe/book-final.txt --env DRAKAN_GAME=d1 --env DRAKAN_AUTOPRESS=0 --env DRAKAN_FULLSCREEN=0 --env DRAKAN_SAVE_PACK_CHECK=1 --env DRAKAN_SAVE_MENU=native --env DRAKAN_TITLE_MENU=native --env DRAKAN_MENU_MOUSE_LOG=1 --attempts 1 --timeout 65
```

## Normal Load Game import

The installed card's 62 original files are archived under `dist/DrakanPC/save-backups`, alongside a SHA-256
manifest. The recovered Village saves keep slots 1 through 4. Slots 5 and 6 hold the Book saves formerly stored
alongside the Village saves in slots 1 and 2. Each Book save has its own paired Village state. Other saves on the
card retain their bytes. The migrated entries keep their original save times and matching previews.

The November 9 migration compares the exact old pack with the installed pack and checks each input save's hash.
It keeps existing record slots, remaps the moved Rope, Fire Fly, and Atimar's Blade Glow classes, and translates
the changed Village object IDs. The three old decorative pressure plate records and the buried push block use the
current game's unsolved puzzle state. The saved gate, counter, player and character stats retain their original data.
The closed point exit and surviving Book helpers match the native post-Book save layout.

The native Book exit probes the active card root, restores the Village `.rlt`, then reopens it when writing the
Village level-start `.rsg`. The save guard snapshots a compatible companion from the selected Book slot and
serves those reads from the snapshot. When the game consumes the virtual entry, the card reader leaves any
separate physical root file intact. The loader still checks the recorded pack build and applies revision-specific
upgrade rules.

The historical source card no longer carried a contemporaneous return state for either Book save. Slot 5 uses
the migrated Village save made about 30 seconds after its Book save. Slot 6 uses its own paired Village save,
made about four minutes later. These pairs preserve the available follow-on Village progress.

The original Load Game screen showed slots 1 through 4 as unavailable before the import. The final Windows build
loaded the six entries on a copy of the combined card. The Book test kept both an injected unrelated root level-state
file and its pack record byte-for-byte. A separate run loaded slot 33 immediately after the Book return and restored
that save's distinct player position. The private scripts, game captures, and validation logs are under
`build/normal-ootf-recovery` and `build/harness`.

## Automatic migration on load

Restoring the original card brought back the build refusals in the native Load Game menu. The installed
game now checks the exact file name and SHA-256 against the local catalog for the installed OotF pack.
Before the restore hook supplies the converted bytes, it copies the original slot, verifies the copy, and
publishes it under `save-backups/ootf-auto`. An identical backup is reused. The game keeps the restored slot
on the card until the player saves. The known creature-layout migration and save revision upgrades also take
a backup when the game restores an older save. A catalog Book save pins its paired Village state for the exit.

The local catalog contains nine matched files from the November 9 and 14 originals, with both Book return states.
The installed game loaded restored Village slots 1 through 4, both Book saves and their returns, a save without
its pack record, and the Wartok Canyons save from the later creature layout. Every archived slot matched its
source byte-for-byte. A native-menu load and quicksave wrote the current build and revision. The modified,
unstamped Book test stayed unavailable and generated no archive. An ordinary rebuild kept the catalog and
all 77 files on the active card. The private setup and assertions are under `build/ootf-auto` and the game
logs and captures are under `build/harness/ootf-auto-*`.
