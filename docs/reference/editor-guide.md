# Editor guide

Task guides for the level and save editor (`editor/`). The same text is in the editor: press the help button in the
top bar, or the "?" on a panel. This page is generated from `editor/packages/editor-core/src/guides.ts`; edit that
file and run `UPDATE_GUIDE=1 npx vitest run packages/editor-core/test/guides.test.ts` in `editor/`.

Contents: [What the editor does](#start), [Place and edit objects](#objects), [Link triggers and exits](#triggers), [Build a campaign from levels](#campaign), [Make a multiplayer map](#multiplayer), [Edit quests, dialogue and cut scenes](#story), [Package and play a mod](#package), [Edit a save](#save), [Browse the raw files](#files).

A mod project (Project panel, bottom dock) is a small JSON file: name, id, author, version, description, the levels
it uses in campaign order, the level Play starts in, the checklist ticks and your level edits (the editor's own
object changes, never game data). In the desktop app the project is a folder (`project.drakan.json` plus `mod/` with
the exported pack) that saves in place; in a browser the working project is kept in local storage and "Save project"
downloads the same file, which "Open project" reads back.

<a id="start"></a>

## What the editor does

Know which mode to use for what you want to make.

Path: Open your disc > Pick a task > Edit > Export a mod > Play

1. Open your own Drakan disc image (.iso). Nothing is uploaded; the editor reads it in place and never changes it.
2. On the start page pick a task: edit a campaign level, create a multiplayer map, build a new level from an existing one, change the story, or edit a save.
3. Work in the Level tab (levels, maps, story) or the Save tab (save files). A checklist at the bottom right lists the next steps and ticks them off.
4. Everything you change is kept in a mod project. Export (Ctrl+E) builds one mod pack from the project, which the PC port loads on top of your disc.
5. Play it: the desktop app starts the game for you; in the browser copy the zip into the game folder.

* Level tab: place and edit objects, triggers, multiplayer maps, story. Save tab: edit .rsg save files. Files tab: advanced, to look at the raw files on the disc.
* Ctrl+P finds any command, object, class or level. Press ? for the keyboard shortcuts.
* In the desktop app a project is a folder on disk (File > New Project / Open Project, or the Project tab): the project file with your level edits, and the exported mod. It saves in place by itself and with Ctrl+S, and File > Open Recent Project reopens it. In a browser the project file is a download (Save project) that you open again with Open project; it includes the level edits.

<a id="objects"></a>

## Place and edit objects

Add creatures, items and scenery, move them, and change how they behave.

Path: Open a level > Select > Move or place > Change fields > Validate

1. Open a level from the start page or with Ctrl+P and the level name.
2. Click an object in the viewport or in the Outliner (right side). Drag on empty space to box-select.
3. Press W and drag an arrow to move it. E rotates, R scales (where the object allows it). End drops the selection on the ground.
4. To add something, drag a class from the Content Browser (bottom) into the world, or press Ctrl+P and type @ and the class name.
5. Change health, speed, models and sounds in the Details panel (right). Changed fields are marked and can be reset.
6. Ctrl+D duplicates, Delete removes, Ctrl+Z undoes. History (bottom) lists every step and jumps back to one.

* Right-drag and WASD fly the camera. F focuses the selection.
* Creatures and people play their idle animation in the viewport (Display > Play animations turns it off). Only the nearest on-screen characters move, so far ones stay still. To see another clip on the selected one, pick it under Viewport animation in the Details panel.
* Prefer a class the level already uses. A class with no instance in the level gets generic defaults that are not verified in game.
* Locked objects (L) cannot be moved by accident; hidden ones (H) are still in the level.

<a id="triggers"></a>

## Link triggers and exits

Make one object send a message to another, and control where an exit leads.

Path: Select a trigger > Link to... > Choose the message > Check Validation

1. Select the object that should act (a trigger volume, a timer, a switch).
2. Right-click it and choose "Link to...", or use the link handle on the selection, then click the target object.
3. Open the Logic panel (Ctrl+Shift+L). Each link shows the message it sends; click one to change the message or remove it.
4. To build a common setup quickly, use a template in the Logic panel (for example "Trigger volume plays cut scene").
5. Exits: select an exit object. Its destination is a level name; the Logic panel lists where it leads and warns when the destination has no matching way back.
6. Open Validation and fix every error before you export.

* A link means: when this object triggers, it sends its message to that one. Arrows in the viewport show them.
* Trigger volumes are boxes or spheres; drag their face handles to resize them.
* If an exit has no matching exit in the destination level, the player arrives with no marker and falls out of the world.

<a id="campaign"></a>

## Build a campaign from levels

List your levels in order, see how they connect, copy a level, and test from any of them.

Path: Create a project > Add levels > Check exits > Edit each level > Export

1. Open the Project panel (bottom dock) and give the project a name, an author and a version.
2. Add levels from the disc with "Add level", or make a copy of one with "Add copy" and give the copy its own title.
3. Each level lists its exits and where they lead. A warning means the destination is missing or has no way back to this level.
4. Open a level from the list to edit it. Its edits are kept for the project, level by level.
5. Use "Order by route" to sort the list the way a player would meet the levels, and "Start here" to choose the level the Play button opens.
6. Export from the Project panel. One pack holds every changed level and every copy.

* A copy starts as the original and ships as a new entry in the level list. Exits inside a copy still lead where the original did.
* To send the player into a copy, an exit must name the copy; change the exit destination in the Logic panel.
* "Start here" sets the level the Play button starts a New Game in (desktop app). The own New Game button of the game is not changed, and starting inside a copy has not been tested in game.

<a id="multiplayer"></a>

## Make a multiplayer map

Turn a level into a deathmatch or team arena.

Path: Pick a level > Map details > Spawns > Pickups > Validate > Export

1. Open the Multiplayer Map tab (bottom dock) and choose a level. Its terrain becomes the arena.
2. On Map: set the name, an id, the modes (deathmatch, team deathmatch), the player count, starting health and loadout.
3. On Place: choose the spawn tool and click the ground. "Suggest spawns" puts a few on open ground; "Alternate teams" colours them.
4. Place pickups (weapons, armour, health) the same way. Use the bounds tool to draw the area players may use.
5. On World: choose which campaign creatures and objects stay or are removed from the arena.
6. Check Validation: a map needs at least two spawns. Fix every error.
7. On Export: write the pack zip, put it straight into the game's mods folder (desktop), or export the .ini only. Then pick the map in the multiplayer menu.

* Spawns that sit inside walls are not detected. Walk the map in game once.
* Custom game modes are written on the Modes page.

<a id="story"></a>

## Edit quests, dialogue and cut scenes

Change what characters say, what quests ask for, and how cut scenes play, or write new ones.

Path: Open Story > Pick a page > Change or create entries > Story pack > Play

1. Open Story from the bottom dock (Ctrl+Shift+Y). Its pages are Strings, Quests, Dialogue, Cinematics, New dialogue, New cut scene, Quest marker and Story pack.
2. Strings: every piece of game text. Search, edit, and see where it is used. Changed rows are marked.
3. Quests: edit a quest's title and text.
4. Dialogue: edit conversations and subtitles line by line, and play the voice line next to it.
5. Cinematics: edit the sequence of a cut scene, its camera path and sounds. The Logic panel decides when it plays.
6. New dialogue: pick a conversation and phase, type the speaker and subtitle, choose a sound of the level (Play previews it), and add the line. The preview shows the phase order after the insert.
7. New cut scene: add camera points (from the viewport camera or the selected object) and ordered sound, animation and message steps, then create it. Tick the option to place a trigger volume and cut scene player in the level as well.
8. Quest marker: write a new quest (journal text, map dot) or pick one, then place it in the level with volumes that start and complete it. Pick a placed conversation handler as quest giver to wire it to the quest.
9. Story pack: review every change, including the new lines, scenes and quests, and export them as one mod pack.

* Story edits are a separate pack from level edits. Enable both in the game's Mods page.
* Text in other languages is edited separately; pick the language in the Files tab header.
* A new line plays a sound that is already on the disc; new recordings cannot be added. Its subtitle is stored on the sound, so every line that plays that sound shows it.
* Camera orientation of a new cut scene (yaw and pitch) is written from an inferred convention and the stock game did not show camera key edits; positions and the step order are the reliable part.
* A placed conversation handler keeps its own line lists. In New dialogue pick "Add to" the placed object to put the line there; that edit is saved with the level, not with the Story pack.
* Placement and wiring in the level are undo steps named after the objects they create ("Place quest ...: Quest #12").

<a id="package"></a>

## Package and play a mod

Turn your edits into a pack and run it in the game.

Path: Fix Validation > Export > Enable > Play

1. Open the Validation tab and fix the errors. Warnings are advice.
2. Press Export (Ctrl+E, the top bar, or File > Export). One dialog builds the pack from the project: it shows what is in it and lets you set the mod name, id, version and author.
3. The pack is written to the project folder's mod folder (desktop), to a place you choose, or downloaded (browser). Tick the box to also install it into the game's mods folder and turn it on in mods.ini.
4. For a zip you copied yourself: put it in the game's mods folder, start the game once so mods.ini lists it, then set its line to "on" (the Mods page in the game settings does the same).
5. Start the game. The log shows "mods: loaded" and your mod id.
6. In the desktop app the Play button (F5) does all of this for you, and "Play from here" in the right-click menu starts at that spot.

* Your disc and its files are never modified. Remove the pack to get the original game back.
* A pack only works with the disc profile it was made for (shown in the Mod tab, which also lists the open level's changes).

<a id="save"></a>

## Edit a save

Change a character, inventory, quests or world state in a .rsg file.

Path: Open a save > Edit > Save or export

1. Open the Save tab. "Open save" takes one .rsg file; "Open folder" takes the memory card folder and lists every slot with its screenshot.
2. Pages: Character (stats), Inventory (items with pictures), Story (quests), World (every record of the level, searchable), Transfer, Compare (diff two saves) and Repair.
3. Your changes are listed under Pending changes on the right. Undo and Redo work on them, and Revert all drops them.
4. SAVE writes the file you opened. EXPORT downloads an edited copy and leaves the original alone.

* Keep a copy of the original save first.
* The 3D view shows records that have a position; click one to select it.

<a id="files"></a>

## Browse the raw files

Look inside the disc for textures, text and the structure of a level (advanced).

Path: Pick a folder > Pick a file > Pick an entry > Preview

1. Open the Files tab. The left pane is the file tree of your disc, in the language you chose.
2. Click a file. Files that hold many resources (.LVL, .TXD, .XDB and so on) list their entries in the middle.
3. Click an entry to preview it: textures are drawn, string tables are listed as text, anything else is shown as hex.
4. Use the buttons above the preview to open the file in the right editor: a level opens in the Level tab, a string table in the Story editor.

* You do not need this tab to make a mod. It is for finding out what the game data looks like.
* Nothing here changes the disc.
