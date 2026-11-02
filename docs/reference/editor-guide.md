# Editor guide

Task guides for the level and save editor (`editor/`). The same text is in the editor: press the Help button in the
top bar, or the "?" on a panel. This page is generated from `editor/packages/editor-core/src/guides.ts`; edit that
file and run `UPDATE_GUIDE=1 npx vitest run packages/editor-core/test/guides.test.ts` in `editor/`. It is published
as https://christophervr.github.io/Drakan2PC/guides/editor-tasks.html; each guide id below is an anchor there, which the editor's "Open on the web" links use.

Contents: [What the editor does](#start), [Place and edit objects](#objects), [Link triggers and exits](#triggers), [Change what a shop sells](#vendors), [Build a campaign from levels](#campaign), [Make a multiplayer map](#multiplayer), [Edit quests, dialogue and cut scenes](#story), [Change the menus and HUD](#menus), [Package and play a mod](#package), [Edit a save](#save), [Browse the raw files](#files), [Shape and paint the ground](#landscape).

A mod project (Project Settings, from the top bar) is a small JSON file: name, id, author, version, description, the levels
it uses in campaign order, the level Play starts in, the checklist ticks and your level edits (the editor's own
object changes, never game data). In the desktop app the project is a folder (`project.drakan.json` plus `mod/` with
the exported pack) that saves in place; in a browser the working project is kept in local storage and "Save project"
downloads the same file, which "Open project" reads back.

<a id="start"></a>

## What the editor does

Know which mode to use for what you want to make.

Path: Open your disc > Pick a template > Edit > Export a mod > Play

1. Open your own Drakan disc image (.iso) with Open ISO in the top bar or the button on the start page. Nothing is uploaded; the editor reads it in place and never changes it.
2. The start page asks "What do you want to make?". Click a template to see what it involves: a campaign level, a multiplayer arena, a new level from a copy, a pressure plate puzzle, a shop, a cut scene, a dialogue line, a quest, story texts, a menu page, a save or an Order of the Flame level. The pane beside the templates says what it is for and lists "What you'll do". Its button does the first step: it asks for your disc image and a level when needed, then opens the editor that makes it. Double-click a template to start it straight away. The + Add button in the toolbar offers the same list at any time, and the Home button at the left of the toolbar (or File > Start page) brings the start page back.
3. A checklist (in the corner of the viewport, or of the window) shows "Step n of m" of what you started, with the next step's button, and ticks the steps off. Its Making list switches to another template.
4. The top bar shows your project (click it for Project Settings, Save, Export and Play) and the open editors: the Level Editor and the editors you opened (Story, Menus, Vendors, ...). Saves are edited in the Save Editor, an application of its own (Tools > Save Editor).
5. Everything you change is kept in a mod project. Export (Ctrl+E) builds one mod pack from the project, which the PC port loads on top of your disc.
6. Play it: the desktop app starts the game for you; in the browser put the zip in the game's mods folder.

* Level Editor: the Place panel on the left, the viewport in the middle, the Outliner, Details and World Settings (multiplayer arena) on the right; the bottom area holds the Content Browser, Output Log and Validation, plus Logic when you open it. Story, Menus, Vendors, Project Settings and Package and Play open as their own tabs (Window menu). Tools > Disc Files: advanced, the raw files on the disc.
* Help: the Help button in the top bar (or Help > Open the help guides) opens these guides; the small "?" on a panel opens the guide for that panel. Ctrl+P finds any command, object, class, level or guide. F1 (or ?) lists the keyboard shortcuts.
* In the desktop app a project is a folder on disk (File > New Project / Open Project, or the project button in the top bar): the project file with your level edits, and the exported mod. It saves in place by itself and with Ctrl+S, and File > Open Recent Project reopens it. In a browser the project file is a download (Save project) that you open again with Open project; it includes the level edits.

<a id="objects"></a>

## Place and edit objects

Add creatures, items and scenery, move them, and change how they behave.

Path: Open a level > Place > Select and move > Change fields > Validate

1. Open a level: Open level... in the toolbar, the "Change a campaign level" template on the start page, or Ctrl+P and the level name.
2. To add something use the Place panel on the left. Pick a group (Characters, Creatures, Items, Chests & breakables, Doors & mechanisms, Lights, Triggers & logic, Cut scenes, Quests, Level exits, Sound, Effects, Scenery and more) and drag a row into the viewport: it lands where the pointer meets the ground. Double-click a row, or select it and press Enter, to put it in the middle of the view. The Search box looks through every group, and Recently placed keeps the last things you used.
3. You can also drag a class from the Content Browser (bottom), or press Ctrl+P and type @ and the class name.
4. Click an object in the viewport or in the Outliner (right side) to select it. The Outliner groups objects the same way as the Place panel. Drag on empty space to box-select.
5. Press W and drag an arrow to move it. E rotates, R scales (where the object allows it). End drops the selection on the ground.
6. Change health, speed, models and sounds in the Details panel (right). Changed fields are marked and can be reset.
7. Ctrl+D duplicates, Delete removes, Ctrl+Z undoes. Edit > Undo History (Ctrl+Shift+H) lists every step and jumps back to one.

* Right-drag and WASD fly the camera. F focuses the selection.
* Creatures and people play their idle animation in the viewport (Display > Play animations turns it off). Only the nearest on-screen characters move, so far ones stay still. To see another clip on the selected one, pick it under Viewport animation in the Details panel.
* Prefer a class the level already uses. A class with no instance in the level gets default settings.
* Locked objects (L) cannot be moved by accident; hidden ones (H) are still in the level.
* Drag the thin bars between the panels to make them bigger or smaller; the sizes are remembered. The Window menu shows or hides the Place panel, the Outliner and Details, and the bottom panel, and the button in the Place header hides that panel.

<a id="triggers"></a>

## Link triggers and exits

Make one object send a message to another, and control where an exit leads.

Path: Select a trigger > Link to... > Choose the message > Check Validation

1. Open Logic (Ctrl+Shift+K, it opens below the viewport) and click an object in the level, or drag one in from the Outliner. Each object is a card named after it (Town Door) with its role on top (Trigger, Door, Timer, Counter, NPC, Quest).
2. Drag from the round pin on the right of a card onto another card. A short list asks what it should do: opens, closes, unlocks, plays the cut scene, starts the quest ... Pick one and the link is made.
3. Drop the link on empty space instead to make something new there. Add node (or press A in the graph) opens a search for objects in the level and new triggers, plates, timers, counters, doors and cut scene players. The Triggers & logic group of the Place panel places the same things straight into the level.
4. Click the word on a link to change what it does, or select the link and press Delete to remove it. Problems show on the card with a yellow mark: a trigger linked to nothing, a message the target ignores, a counter nothing counts down.
5. Right-click an object and choose "Link to..." to link from the level view instead, or use the link handle on the selection.
6. For a whole setup in one go use Ready-made setup in the Logic panel (for example "Trigger volume plays cut scene"), or the ready-made rows of the Place panel.
7. Puzzle templates: "Pressure plates puzzle" (N plates feed a counter that opens a target; plates can be laid along X or Z), "Lever opens door", "Timed trigger" (a volume starts a timer that fires after a delay) and "Key lock opens door" (sends Unlock; the key item needs the same Lock Code). Doors that should only obey the puzzle need Locked on and Auto-Open off.
8. The messages are ordinals of one list; the game converts them to engine ids (On 0x1001, Off 0x1002, Lock 0x1101, Unlock 0x1102, Play Sequence 0x10a1). Unlock only unlocks a door: it still has to be opened by Auto-Open or an On message.
9. Exits: select an exit object (Level exits in the Place panel and the Outliner). Its destination is a level name; the Logic panel lists where it leads and warns when the destination has no matching way back.
10. Open Validation and fix every error before you export.

* A link means: when this object triggers, it sends its message to that one. Arrows in the viewport show them.
* Trigger volumes are boxes or spheres; drag their face handles to resize them.
* If an exit has no matching exit in the destination level, the player arrives with no marker and falls out of the world.

<a id="vendors"></a>

## Change what a shop sells

Edit a shopkeeper's stock, quantities and prices and try it in the game.

Path: Open a level with a shop > Select the vendor > Edit stock and prices > Export > Buy in game

1. Open a level that has shops (Surdana has a blacksmith, an alchemist and a mage), or start from the "Shop and prices" template. Shops have a badge in the viewport: double-click it to open its stock. You can also click the shop object in the Outliner (search "Blacksmith"), or press Ctrl+P and run "Select the vendors of this level".
2. The Details panel shows a Vendor section: "Sale price x" is what the player pays as a multiple of the item price, "Buy-back x" is what the shop pays for items the player sells. Click "Edit stock and prices" to open the Vendors editor.
3. In the Vendors editor pick the vendor, then change each item: Limited or Unlimited stock, the quantity, and the item price. Type in "Add an item" to search every sellable item and click one to add it. The trash button removes an item.
4. Check the "Pays" column: it is the price after the sale modifier. Warnings list items that cost 0 gold (the shop would give them away) and an empty shop.
5. Export (Ctrl+E). The stock and modifiers ship with the level; item prices ship in the story pack of the same export.
6. In the game open the shop (talk to the shopkeeper). Square buys the selected item, Circle sells one of yours.

* An item price belongs to the item, so changing it changes every shop that sells that item. For one shop use "Sale price x".
* Unlimited items never run out. A limited item sells one unit per count.
* Only items of the shared Resources and Magic databases can have their price edited; a shop can only sell items its own class can reach.
* To prove a shop edit without playing to it, run scripts/harness/vendor_shop.py: it builds the pack, opens the shop in the game and buys an item.

<a id="campaign"></a>

## Build a campaign from levels

List your levels in order, see how they connect, copy a level, and test from any of them.

Path: Create a project > Add levels > Check exits > Edit each level > Export

1. Open Project Settings (click the project in the top bar, or Ctrl+,). Under Mod details give the project a name, an author and a version.
2. In Levels press "+ Add level" and pick a level of the disc (type to search the list). To start a new level as a copy of one, use the arrow beside it ("More ways to add") and "Add a copy of a level...", and give the copy its own title.
3. The Levels table lists the project in campaign order: number, level, type, changes, exits and the start flag. Drag a row, or press Alt+Up / Alt+Down, to reorder. Use "Order by route" to sort it the way a player would meet the levels.
4. Expand a level (its arrow, or the Right arrow key) to see its exits: where each leads and whether the player arrives correctly. "Change destination" opens the level with that exit selected and the Logic panel showing where it leads.
5. Hover a row for its actions: open the level in the editor, Play from here, "Start here" (the level the Play button opens) and remove it from the project (its edits are kept). Edits are kept for the project, level by level.
6. Checks lists the problems of the project grouped by level, one line each (click it for the details). The filters in its header show errors, warnings or notes; click a message to select the level or exit it is about.
7. Switch "Show the levels as" from List to Map to see the campaign map: each level is a card, each exit an arrow to the level it leads to (dashed when it leaves the project, orange when it needs fixing). Click a card to open the level, double-click it to play from there, click an arrow to change where that exit leads, and drag cards (or press Auto arrange) to lay the route out. F shows every card.
8. Play from here tests the whole project from that level (desktop app): the game opens straight into it with every project edit, so you can walk through its exits into the next project level.
9. Export from Project Settings or the toolbar. One pack holds every changed level and every copy.
10. To give the game a new entry on its "Choose your game" card, turn on "Make this a campaign" in the Campaign section of Project Settings: a title, a short text, the level New Game starts in (a copy), the save folder (its saves stay apart from Drakan's) and a cover picture (made in the editor, or a picture of your own). "Check the exits" follows every exit from the start level: every level of the campaign must be reached. Your Menus tab changes ship with it and show only while the campaign is chosen.

* A copy starts as the original and ships as a new entry in the level list. Exits inside a copy still lead where the original did.
* To send the player into a copy, an exit must name the copy; change the exit destination in the Logic panel.
* "Start here" sets the level the Play button starts a New Game in (desktop app). The own New Game button of the game is not changed.

<a id="multiplayer"></a>

## Make a multiplayer map

Turn a level into a deathmatch or team arena.

Path: Pick a level > Map details > Spawns > Pickups > Validate > Export

1. Start from the "Multiplayer arena" template on the start page, or open World Settings (the tab beside Details, or Window > World Settings). Pick a level for its terrain and press "Start new map".
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

1. Open Story (Ctrl+Shift+Y), or start from the Cut scene, Dialogue line, Quest or Story texts template. It has four places: Quests, Conversations, Cut scenes, and Text and export. Each has a second row of tabs for the related tools.
2. Quests: pick a quest to edit its title and text. Under Steps in the open level, see what starts and completes it, and add "The player enters an area" or "Something in the level fires". Place a quest marker puts a new quest in the level.
3. Conversations: pick a conversation. Each stage of the talk (first meeting, quest still open, quest finished) is a branch of line cards with the speaker, the subtitle (type to change it), the recording (Play) and up, down and remove buttons. Add a line from the level or use Write a new line.
4. Write a new line: pick a conversation and stage, type the speaker and subtitle, choose a sound of the level (Play previews it), and add the line. The preview shows the order after the insert.
5. Cut scenes: pick a scene. The timeline has a ruler you can scrub and a lane for each actor and kind of step (moves, animation, sound, message). Drag a key sideways to retime it. Below, edit each key and see the camera path. New cut scene places one where the level view looks (a player, the box that starts it and a camera move) and opens its timeline. Build from camera points makes one from points and ordered steps.
6. In the level: cut scenes, shops, quests and the characters you can talk to have badges (Show > Story switches each group). Hover a badge for what it does; click it to select; double-click it to open its editor. A cut scene shows its camera path, dashed lines to the objects it moves and its trigger box; a quest shows its start (green) and finish (amber) areas. Right-click the ground for Add cut scene here, or with a quest selected, Add quest start / finish area here. The Cut scenes and Quests groups of the Place panel place the same things by drag and drop.
7. Text and export: Texts lists every piece of game text; search, edit, and see where it is used.
8. Story pack: review every change, including the new lines, scenes and quests, and export them as one mod pack.

* Story edits are a separate pack from level edits. Enable both in the game's Mods page.
* Text in other languages is edited separately; pick the language in the Disc Files header (Tools > Disc Files).
* A new line plays a sound that is already on the disc; new recordings cannot be added. Its subtitle is stored on the sound, so every line that plays that sound shows it.
* Camera yaw 0 looks along -Z, 90 along +X, and a positive pitch looks down. Existing camera keys show yaw and pitch next to their position in the Cinematics page; editing either one rewrites the key orientation.
* A placed conversation handler keeps its own line lists. In Write a new line pick "Add to" the placed object to put the line there; that edit is saved with the level, not with the Story pack.
* Placement and wiring in the level are undo steps named after the objects they create ("Place quest ...: Quest #12").

<a id="menus"></a>

## Change the menus and HUD

Rename, reorder and restyle the entries of the game menus, and change the HUD colours.

Path: Open Menus > Pick a page > Change entries or theme > Export

1. Open Menus from the "Menu page" template on the start page or the Window menu.
2. Pick a menu page on the left. Each entry has its label (type to change it), a box that shows or hides it, and up and down buttons to reorder it. Pages you changed are marked with a dot.
3. Theme sets the colours and text sizes of every menu, HUD the accent colour of the in-game display, and Layout the positions and sizes. A colour has a picker and a text field; its reset button puts it back to the default.
4. The preview draws the page as the game will show it.
5. Set the mod id, name and author, then Export zip, or Write to mods folder (desktop) to put it straight into the game's mods folder and turn it on.

* Copy INI and Import INI move the skin as ui.ini text, for example to share it in a message. Reset all goes back to the game look.
* The menus pack is separate from the level and story packs; enable it in the Mods page of the game settings.

<a id="package"></a>

## Package and play a mod

Turn your edits into a pack and run it in the game.

Path: Fix Validation > Export > Enable > Play

1. Open Validation (bottom area of the Level Editor) and fix the errors, then the Checks of Project Settings. Warnings are advice.
2. Press Export (Ctrl+E, the toolbar, or File > Export). One dialog builds the pack from the project: it shows what is in it and lets you set the mod name, id, version and author.
3. The pack is written to the project folder's mod folder (desktop), to a place you choose, or downloaded (browser). Tick the box to also install it into the game's mods folder and turn it on.
4. For a zip you copied yourself: put it in the game's mods folder, start the game, and turn it on in the Mods page of the game settings.
5. Start the game and open your level.
6. In the desktop app the Play button (F5) does all of this for you: the game opens straight into the level with the player where the camera looks (no title, intro or save game; a test memory card is used, your own saves are not touched). "Play from here" in the right-click menu starts at that spot. Tick "Player cannot die while testing" in Package and Play (Window menu) to stay alive.

* Your disc and its files are never modified. Remove the pack to get the original game back.
* A pack only works with the disc profile it was made for (shown in Package and Play, which also lists the open level's changes).

<a id="save"></a>

## Edit a save

Change a character, inventory, quests or world state in a .rsg file.

Path: Open a save > Edit > Save or export

1. Open the Save Editor: Tools > Save Editor or the "Edit a save" template on the start page. It is an application of its own (its own window; save.html in a browser) with a Level Editor button to go back; its Disc button reads your disc image for item names, pictures and quest text. "Open save" takes one .rsg file; "Open folder" takes the memory card folder and lists every slot with its screenshot.
2. Pages: Character (stats), Inventory (items with pictures), Story (quests), World (every record of the level, searchable), Transfer, Compare (diff two saves) and Repair.
3. Your changes are listed under Pending changes on the right. Undo and Redo work on them, and Revert all drops them.
4. SAVE writes the file you opened. EXPORT downloads an edited copy and leaves the original alone.

* Keep a copy of the original save first.
* The 3D view shows records that have a position; click one to select it.

<a id="files"></a>

## Browse the raw files

Look inside the disc for textures, text and the structure of a level (advanced).

Path: Pick a folder > Pick a file > Pick an entry > Preview

1. Open Tools > Disc Files. The left pane is the file tree of your disc, in the language you chose.
2. Click a file. Files that hold many resources (.LVL, .TXD, .XDB and so on) list their entries in the middle.
3. Click an entry to preview it: textures are drawn, string tables are listed as text, anything else is shown as hex.
4. Use the buttons above the preview to open the file in the right editor: a level opens in the Level Editor, a string table in the Story editor.

* You do not need this tab to make a mod. It is for finding out what the game data looks like.
* Nothing here changes the disc.

<a id="landscape"></a>

## Shape and paint the ground

Raise hills, dig valleys, smooth and level the ground, paint its textures and make it bigger.

Path: Landscape mode > Sculpt > Paint > Manage layers > Play test

1. Open a level from your disc and click Landscape at the left of the toolbar (or press Shift+L). The Place panel makes way for the Landscape tools and the viewport gets a green frame: a drag on the ground now changes the ground, not objects. Objects or Done goes back.
2. Sculpt: pick Raise, Lower, Smooth, Flatten, Ramp or Noise and drag on the ground. The green circle shows the brush; Size, Strength and Falloff set how wide it is, how fast it works and how soft its edge is. [ and ] change the size; hold Shift to do the opposite (Raise digs, Lower builds up). Flatten levels to the height where you start dragging, or to a fixed height. Ramp makes a straight slope from where you press to where you let go.
3. Paint: the swatches are the ground textures this level already uses, with a name from their colour and how much of the ground they cover. Pick one and drag. Below 100% strength only some cells are painted, scattered, for a soft edge between two kinds of ground. Hole cuts holes in the ground; Shift fills them again.
4. Manage: Pick layer shows one layer of the ground with its outline. Expand grows the picked layer on one side: point near an edge and click, or use the arrows (the new ground continues at the edge height). New layer adds a flat piece of ground where you click, at that height, with the picked texture.
5. Every stroke is one undo step (Ctrl+Z, Ctrl+Y). Your edits are saved with the level edits (autosave and the project file) and show at once in the editor: placing objects and Snap to ground use the new ground.
6. Play test or export the mod to see the new ground in the game.

* The ground of a level is made of many layers stitched together. "Blend into neighbouring layers" (on by default) lets a brush work across the seams so no gaps open; switch it off to change only the layer under the brush.
* A ground cell is 2,048 units. Heights have a limited range per layer (about 65,000 units up or down from the layer height); the brush stops there.
* The game draws at most 10 textures in one small area of ground; the export splits the data so painting many textures close together still loads.
