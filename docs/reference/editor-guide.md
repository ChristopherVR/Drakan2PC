# Editor guide

Task guides for the level and save editor (`editor/`). The same text is in the editor: press the Help button in the
top bar, or the "?" on a panel. This page is generated from `editor/packages/editor-core/src/guides.ts`; edit that
file and run `UPDATE_GUIDE=1 npx vitest run packages/editor-core/test/guides.test.ts` in `editor/`. It is published
as https://christophervr.github.io/Drakan2PC/guides/editor-tasks.html; each guide id below is an anchor there, which the editor's "Open on the web" links use.

Contents: [What the editor does](#start), [Learn with the tutorials](#tutorials), [Place and edit objects](#objects), [Shape and paint the ground](#landscape), [Link triggers and exits](#triggers), [Change what a shop sells](#vendors), [Add a new shop](#new-shop), [Edit quests, dialogue and cut scenes](#story), [Make a quest start and finish](#quests), [Change what characters say](#conversations), [Make a cut scene](#cutscenes), [Build a campaign from levels](#campaign), [Open Order of the Flame levels](#ootf), [Make your own campaign](#campaign-pack), [Make a multiplayer map](#multiplayer), [Make a multiplayer game mode](#game-modes), [Change the menus and HUD](#menus), [Play test your level](#playtest), [Package and play a mod](#package), [Edit a save](#save), [Browse the raw files](#files).

A mod project (Project Settings, from the top bar) is a small JSON file: name, id, author, version, description, the levels
it uses in campaign order, the level Play starts in, the checklist ticks and your level edits (the editor's own
object changes, never game data). In the desktop app the project is a folder (`project.drakan.json` plus `mod/` with
the exported pack) that saves in place; in a browser the working project is kept in local storage and "Save project"
downloads the same file, which "Open project" reads back.

<a id="start"></a>

## What the editor does

Know where to start and which part of the editor makes what you want.

Path: Open your disc > Pick a template > Edit > Play > Export a mod

1. Open your own Drakan disc image (.iso) with Open ISO in the top bar or the button on the start page. Nothing is uploaded; the editor reads it in place and never changes it.
2. The start page asks "What do you want to make?". Its templates are grouped as Levels and maps (Change a campaign level, Multiplayer arena, New level from a copy, Pressure plate puzzle, Shop and prices), Story and characters (Cut scene, Dialogue line, Quest, Story texts, Menu page) and Saves and imports (Edit a save, Order of the Flame level). Click one to read what it involves and "What you'll do"; press its button (or double-click the template) to start. The button does the first real step: it asks for your disc and a level when needed, then opens the editor that makes it.
3. A checklist shows "Step n of m" of what you started, with the next step's button, and ticks the steps off. Its Making list switches to another template. The Home button at the left of the toolbar (or File > Start page) brings the start page back; over an open level it offers "Back to" that level (Esc does the same).
4. When you pick a level, a loading card in the viewport shows the steps (reading the level, placing objects, building the terrain, loading models). You can use the level as soon as the objects are placed; "Stop loading" keeps what has loaded, Cancel goes back.
5. The top bar shows your project (click it for Project Settings, Save, Export and Play) and the open editors: the Level Editor and the ones you opened (Story, Menus, Vendors, Project Settings, Package and Play). Saves are edited in the Save Editor, an application of its own (Tools > Save Editor).
6. Everything you change is kept in a mod project. Play (F5, desktop app) starts the game straight in the open level. Export (Ctrl+E) builds one mod pack from the project, which the PC version loads on top of your disc.

* Level Editor: the toolbar (Home, Objects | Landscape, + Add, the level picker, Export and Play), the Place panel on the left, the viewport in the middle, the Outliner and Details (with World Settings for a multiplayer map) on the right, and the Content Browser, Output Log and Validation at the bottom, plus Logic and Story when you open them there.
* Help: the Help button in the top bar opens these guides; the "?" on a panel opens the guide for that panel. Help > Start a tutorial... walks you through a task with the right part of the window lit up. Ctrl+P finds any command, object, class, level or guide. F1 (or ?) lists the keyboard shortcuts.
* In the desktop app a project is a folder on disk (File > New Project / Open Project, or the project button in the top bar): the project file with your level edits, and the exported mod. It saves in place by itself and with Ctrl+S, and File > Open Recent Project reopens it. In a browser the project is kept in the browser; Save in Project Settings downloads it as a file and Open reads it back.

<a id="tutorials"></a>

## Learn with the tutorials

Let the editor show you where to click, one step at a time.

Path: Help > Start a tutorial > Follow the lit-up part > Next

1. Open Help > Start a tutorial... (or press Ctrl+P and type "tutorial"). The list has an Editor tour of the whole window and one tutorial per task: Change a campaign level, Level copy, Pressure plate puzzle, Make a cut scene, Shop and prices, Build a campaign, Multiplayer arena and Edit a save.
2. Press Start. The rest of the window dims and the part the step talks about stays sharp and usable. A card beside it says what it is for and what to do.
3. Some steps wait for you: they say "Waiting for you to ..." and a pointer shows where to click or drag. Do it and the tutorial moves on by itself. After a while a hint appears, and Next becomes "Skip this step".
4. Next and Back (or Enter and the arrow keys) move through the steps. Esc or "Leave tour" stops it; the list then offers Resume or Restart.
5. A guide in this Help drawer that has a tutorial shows a "Show me" button; the start page has "Show me how" for its templates.

* Tutorials that need a level suggest one and skip the steps you have already done (disc open, level open, project made).
* With reduced motion turned on in Windows the tutorials draw the path without animating it.
* Finished tutorials are marked Done in the list.

<a id="objects"></a>

## Place and edit objects

Add creatures, items and scenery, move them, and change how they behave.

Path: Open a level > Place > Select and move > Change fields > Validate

1. Open a level: Open level... in the toolbar, the "Change a campaign level" template on the start page, or Ctrl+P and the level name.
2. To add something use the Place panel on the left. Pick a group (Characters, Creatures, Items, Chests & breakables, Doors & mechanisms, Lights, Triggers & logic, Cut scenes, Conversations, Quests, Vendors, Level exits, Sound, Effects, Sky & weather, Scenery) and drag a row into the viewport: it lands where the pointer meets the ground. Double-click a row, or select it and press Enter, to put it in the middle of the view. The Search box looks through every group, and Recently placed keeps the last things you used.
3. You can also drag a class from the Content Browser (bottom), or press Ctrl+P and type @ and the class name.
4. Click an object in the viewport or in the Outliner (right side) to select it. The Outliner groups the objects in plain categories (Characters & creatures, Items & pick-ups, Triggers & logic, Scenery ...). Drag on empty space to box-select.
5. Press W and drag an arrow to move it. E rotates, R scales (where the object allows it). End drops the selection on the ground.
6. Change health, speed, models and sounds in the Details panel (right). Changed fields are marked and can be reset; "Revert to original" (in Details or the right-click menu) puts a whole object back as it is on the disc.
7. Ctrl+D duplicates, Delete removes, Ctrl+Z undoes. Edit > Undo History (Ctrl+Shift+H) lists every step and jumps back to one.

* Keyboard: Tab into the Outliner, then the arrow keys move, Right and Left open and close a group, Home and End jump, Enter selects (Ctrl+Enter adds to the selection). Ctrl+P finds any command, level, class or object.
* Rynn exists once in a level: Ctrl+D skips her and says so. Closing the page right after an edit asks first, because the autosave copy is written a second and a half later; when you come back, a banner offers to restore it.
* Right-drag and WASD fly the camera. F focuses the selection.
* Show > Show categories hides whole groups (for example Scenery) in the viewport; hidden objects are left out of Select all too.
* Creatures and people play their idle animation in the viewport (Display > Play animations turns it off). To see another clip on the selected one, pick it under Viewport animation in the Details panel.
* Prefer a class the level already uses. A class with no instance in the level gets default settings.
* Locked objects (L) cannot be moved by accident; hidden ones (H) are still in the level.
* Drag the bars between the panels to make them bigger or smaller (double-click one for its starting size); the sizes are remembered. Drag a side panel almost shut to hide it, and click its tab on the viewport edge to bring it back. The Window menu shows or hides the Place panel, the Outliner and Details, and the bottom panel.

<a id="landscape"></a>

## Shape and paint the ground

Raise hills, dig valleys, smooth and level the ground, paint its textures, make it bigger, and see it in the game.

Path: Landscape mode > Sculpt > Paint > Manage layers > Play or export

1. Open a level from your disc and click Landscape at the left of the toolbar (or press Shift+L). The Place panel makes way for the Landscape tools and the viewport gets a green frame: a drag on the ground now changes the ground, not objects. Objects (in the toolbar) or Done goes back.
2. Sculpt: pick Raise, Lower, Smooth, Flatten, Ramp or Noise and drag on the ground. The green circle shows the brush; Size, Strength and Falloff set how wide it is, how fast it works and how soft its edge is. [ and ] change the size; hold Shift to do the opposite (Raise digs, Lower builds up). Flatten levels to the height where you start dragging, or to a fixed height. Ramp makes a straight slope from where you press to where you let go.
3. "Move objects with the ground" (on by default) carries trees, rocks and other things standing on the ground up and down with it, so a new hill does not swallow them. Locked objects stay where they are.
4. Paint: the swatches are the ground textures this level already uses, named by their colour, with how much of the ground they cover. Pick one and drag. Below 100% strength only some cells are painted, scattered, for a soft edge between two kinds of ground. Hole cuts holes in the ground; Shift fills them again. The viewport blends the textures at their borders the way the game does.
5. Manage: Pick layer shows one layer of the ground with its outline. Expand grows the picked layer on one side: point near an edge and click, or use the arrows (the new ground continues at the edge height). New layer adds a flat piece of ground where you click, at that height, with the picked texture.
6. Every stroke is one undo step (Ctrl+Z, Ctrl+Y). Your edits are saved with the level edits (autosave and the project file) and show at once in the editor: placing objects and Snap to ground use the new ground.
7. See it in the game: press Play (F5, desktop app) to start the level with the new ground, or Export (Ctrl+E). The pack carries the changed ground next to the object changes; Rynn walks on the new heights and the painted textures draw in the game.

* Closing the editor window saves first: the level autosave and the project folder are written before the window goes. If the ground has not arrived yet the tools are hidden until it has.
* The game sees ground edits only in a play test or an exported mod; the footer of the Landscape panel counts the changed layers.
* The ground of a level is made of many layers stitched together. "Blend into neighbouring layers" (on by default) lets a brush work across the seams so no gaps open; switch it off to change only the layer under the brush.
* A ground cell is 2,048 units. Heights have a limited range per layer (about 65,000 units up or down from the layer height); the brush stops there.
* The game draws at most 10 textures in one small area of ground; the export splits the data so painting many textures close together still loads.
* Aim paint from above: from a low camera a point behind a hill hits the near slope instead.

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
8. Exits: select an exit object (Level exits in the Place panel, Level exits & teleporters in the Outliner). Logic shows its destination; pick another level there. "Only this exit" (on by default) changes this one exit and leaves the others of the same kind alone. Logic warns when the destination has no way back.
9. Open Validation and fix every error before you export.

* Wiring by dragging was checked end to end: place a door and a lever, select both, open Logic (Ctrl+Shift+K), drag from the lever's round pin to the door card and pick Opens. The link is stored on the lever and exported with the level. A class placed from the Place panel with no copy in the level is written with a default body (the export says so); copy an existing lever or door of the level when the exact behaviour matters.
* A link means: when this object triggers, it sends its message to that one. Arrows in the viewport show them.
* Trigger volumes are boxes or spheres; drag their face handles to resize them.
* Unlock only unlocks a door: it still has to be opened by Auto-Open or an On message.
* If an exit has no matching exit in the destination level, the player arrives with no marker and can fall out of the world. An exit out of a level copy uses the way back to the level it was copied from.

<a id="vendors"></a>

## Change what a shop sells

Edit a shopkeeper's stock, quantities and prices and try it in the game.

Path: Open a level with a shop > Select the vendor > Edit stock and prices > Play > Buy in game

1. Open a level that has shops (Surdana has a blacksmith, an alchemist and a mage), or start from the "Shop and prices" template. Shops have a violet bag badge in the viewport ("Blacksmith, 16 items"): double-click it to open its stock. You can also click the shop object in the Outliner (search "Blacksmith"), or press Ctrl+P and run "Select the vendors of this level".
2. The Details panel shows a Vendor section: "Sale price x" is what the player pays as a multiple of the item price, "Buy-back x" is what the shop pays for items the player sells. Click "Edit stock and prices" to open the Vendors editor.
3. In the Vendors editor pick the vendor, then change each item: Limited or Unlimited stock, the quantity, and the item price. Type in "Add an item" to search every sellable item and click one to add it. The trash button removes an item.
4. Check the "Pays" column: it is the price after the sale modifier. Warnings list items that cost 0 gold (the shop would give them away) and an empty shop.
5. Play (F5) to try it, or Export (Ctrl+E). The stock and modifiers ship with the level; item prices ship in the story pack of the same export.
6. In the game walk up to the shopkeeper and press Use to open the shop. Square buys the selected item, Circle sells one of yours.

* An item price belongs to the item, so changing it changes every shop that sells that item. For one shop use "Sale price x".
* Unlimited items never run out. A limited item sells one unit per count.
* Only items of the shared Resources and Magic databases can have their price edited; a shop can only sell items its own class can reach.
* To add a shop where there was none, see "Add a new shop".

<a id="new-shop"></a>

## Add a new shop

Put a shopkeeper with a shop of their own anywhere in a level.

Path: Open a level with shops > Right-click the ground > Add shop here > Edit stock > Talk to the keeper

1. Open a level that already has shops: Surdana and some later levels (chapters 3, 4 and 6) do. Other levels have no kind of shop to copy, so the command is greyed and says why.
2. Right-click the ground where the shopkeeper should stand and choose "Add shop here". Or open the Vendors group of the Place panel and drag Shop onto the ground (double-click puts it in the middle of the view).
3. The editor adds three things in one undo step: the shopkeeper (a copy of a keeper of the same kind of shop), the shop with its default stock, and a small box around the keeper that switches them on when the player comes close. The new shop is selected.
4. To copy the stock of an existing shop, select that shop first (click its badge), then add the new one.
5. Change what it sells in the Vendor section of Details or in the Vendors editor ("Change what a shop sells").
6. Play (F5) from near the new shop. In the game walk up to the keeper and press Use: the shop screen opens and buying takes the gold.

* The keeper faces the same way as the keeper it was copied from; turn them with E if needed.
* Keep the keeper clear of walls: a keeper placed inside a wall cannot be reached.
* The keeper only appears when the player is inside the box around them. Leave the box where it is when you move the keeper, or move both.

<a id="story"></a>

## Edit quests, dialogue and cut scenes

Find your way around the Story editor: quests, conversations, cut scenes and every text of the game.

Path: Open Story > Pick a page > Change or create entries > Play > Export

1. Open Story (Ctrl+Shift+Y), or start from the Cut scene, Dialogue line, Quest or Story texts template. It opens under the viewport, so you see the level while you work ("Open in its own tab" pops it out, "Dock in Level Editor" brings it back).
2. It has four pages: Quests, Conversations, Cut scenes, and Text and export. Each has a second row of tabs for the related tools.
3. In the level, cut scenes, shops, quests and the characters you can talk to have badges (Show > Story switches each group). Hover a badge for what it does; click it to select; double-click it to open its editor. A cut scene shows its camera path, dashed lines to the objects it moves and its trigger box; a quest shows its start (green) and finish (amber) areas.
4. Quests, conversations and cut scenes each have a guide of their own: "Make a quest start and finish", "Change what characters say" and "Make a cut scene".
5. Text and export: Texts lists every piece of game text; search, edit, and see where it is used.
6. Story pack: review every change, including the new lines, scenes and quests. Export (Ctrl+E) puts them in the same pack as your level edits.

* Text in other languages is edited separately; pick the language in the Disc Files header (Tools > Disc Files).
* Placement and wiring in the level are undo steps named after the objects they create ("Place quest ...: Quest #12").

<a id="quests"></a>

## Make a quest start and finish

Write a quest, put it in a level and decide where it starts and where it is done.

Path: Story > Quests > Write the quest > Place a marker > Start and finish areas > Play

1. Open Story > Quests (or the Quest template on the start page). Pick a quest to edit its title and text, or New quest to make one from the selected quest.
2. If the quest is not in the open level yet, use Place a quest marker (or "Place it and start it when the player enters an area" under Steps in the open level). The quest gets a green "!" badge in the viewport.
3. Click the quest's "!" badge to select it. Then right-click the ground and choose "Add quest start area here" or "Add quest finish area here", or drag Quest start area / Quest finish area from the Quests group of the Place panel. Each is a box that starts or finishes the quest when the player walks in.
4. Steps in the open level lists what starts and completes the quest. "Something in the level fires" makes another object (a lever, a trigger, a talking character) start or finish it instead: click the object in the level or the Outliner.
5. Drag the boxes into place and resize them with their face handles. Play (F5) from near the start area to try it.

* The quest rows of the Place panel are greyed until one quest is selected; the reason is shown on the row.
* Items send no messages of their own. To finish a quest when something is picked up, put a finish area around the item.
* Scan levels (in Quests) reads every level and lists where each quest is placed and triggered.

<a id="conversations"></a>

## Change what characters say

Edit subtitles, reorder and remove lines, and add new lines to a conversation.

Path: Story > Conversations > Pick a conversation > Edit the lines > Write a new line > Talk in game

1. Double-click a blue speech-bubble badge in the level ("Talk: Farmer"), or open Story > Conversations and pick a conversation (Load the conversations reads them from your disc).
2. Each stage of the talk is a branch of line cards: First meeting, Quest still open, Quest finished, and After everything. Each card has the speaker, the subtitle (type to change it), the recording (Play) and buttons to move it up or down or take it out of this stage.
3. Write a new line: pick the conversation and stage, type the speaker and subtitle, choose a sound of the level (Play previews it) and press Add this line. The preview shows the order after the insert.
4. Play (F5) from near the character, walk up to them and press Use to hear the conversation.

* A new line plays a sound that is already on the disc; new recordings cannot be added. Its subtitle is stored on the sound, so every line that plays that sound shows it.
* A placed character can keep its own line lists. In Write a new line pick "Add to" that placed object to put the line there; that edit is saved with the level.
* "Undo my changes to this conversation" puts it back as it is on the disc.

<a id="cutscenes"></a>

## Make a cut scene

Place a cut scene where you look, then edit its camera and steps on a timeline.

Path: Look at the spot > Add cut scene here > Timeline > Retime and edit > Play

1. Fly the camera to where the scene should play. Right-click the ground and choose "Add cut scene here", right-click an object for "Add cut scene at this object" (the camera looks at it), or drag Cut scene from the Cut scenes group of the Place panel.
2. The editor places a cut scene player, a box that starts it when the player walks in, and a 4 second camera move from your view towards the spot. The timeline opens under the viewport.
3. The timeline has a ruler you can scrub and a lane for each actor and kind of step (moves, animation, sound, message). Drag a key sideways to retime it. Below the timeline edit each key and see the camera path in the level.
4. To change an existing scene, double-click its pink clapper badge in the level, or open Story > Cut scenes and pick it.
5. Try it: select the cut scene player and choose "Play selected cut scene" from the menu beside Play (desktop app), or Play from near the box and walk in.

* Build from camera points (in Cut scenes) makes a scene from points and ordered steps you type in.
* Camera yaw 0 looks along -Z, 90 along +X, and a positive pitch looks down. Editing yaw or pitch of a key rewrites its orientation.
* Drag the trigger box with its face handles to change where the scene starts.

<a id="campaign"></a>

## Build a campaign from levels

List your levels in order, see how they connect on the campaign map, copy a level, and test from any of them.

Path: Create a project > Add levels > Check exits > Edit each level > Export

1. Open Project Settings (click the project in the top bar, or Ctrl+,). Under Mod details give the project a name, an author and a version; the Id follows the name until you type your own.
2. In Levels press Add level and pick a level of your disc (type to search the list). To start a new level as a copy of one, use the copy button beside it, or the arrow of Add level and "Add a copy of a level...", and give the copy its own title.
3. The Levels table lists the project in campaign order: number, level, type (Disc or Copy), changes, exits and the start flag. Drag a row by its number, or press Alt+Up / Alt+Down, to reorder. "Order by route" sorts it the way a player would meet the levels.
4. Expand a level (its arrow, or the Right arrow key) to see its exits: where each leads and whether the player arrives correctly ("Arrives correctly", or for example "No way back from Andrellian Isles"). "Change destination" opens the level with that exit selected and Logic showing where it leads.
5. Hover or select a row for its actions: Open, Play (desktop app), Move up, Move down and Remove (its edits are kept). Click the flag in the Start column to make Play start there.
6. Checks lists the problems of the project grouped by level, one line each (the arrow shows the details). The counts in its header filter errors, warnings and notes; click a message to select the level or exit it is about.
7. Switch from List to Map to see the campaign map: each level is a card, each exit an arrow to the level it leads to (dashed when it leaves the project, orange when it needs fixing). Click a card to open the level, double-click it to play from there (desktop app), click an arrow to change where that exit leads, and drag cards (or press Auto arrange) to lay the route out. The layout is saved with the project.
8. Play on a row (or a double-click on the map) starts the game straight in that level with every project edit, so you can walk through its exits into the next project level.
9. Export from Project Settings or the toolbar. One pack holds every changed level and every copy. To give it its own entry on the game's "Choose your game" card, see "Make your own campaign".

* A copy starts as the original and ships as a new level. Exits inside a copy still lead where the original did; an exit out of a copy arrives at the way back to the original level.
* To send the player into a copy, an exit must name the copy; change the exit destination in Logic with "Only this exit".
* The Start flag sets the level Play starts in. The game's own New Game is not changed unless the project is a campaign.

<a id="ootf"></a>

## Open Order of the Flame levels

Convert the levels of your own Drakan: Order of the Flame install and open them in the viewport like disc levels.

Path: Open your disc > Choose the install > Convert > Open a level > Download the mod

1. Open your Drakan: The Ancients' Gates disc image first. The converted levels build on one of its levels (the template) and on its class tables.
2. Tools > Import Order of the Flame levels... Press "Choose install folder..." and pick the folder that holds Dragon.rrc (the install itself, not a level folder). The list shows the 23 levels it found.
3. Tick the levels you want (all of them for the campaign and the multiplayer arenas) and keep the default template (Surdana: the intro level as a template drops objects and the game cannot run the result). The template is always a disc level. The importer shows which language your install is in.
4. Press Convert. The window names the level being converted and the editor stays busy for a while: about a minute for all 23 levels.
5. When it finishes, "Open a converted level in the editor" lists the levels. Click one: it opens in the viewport with its terrain, textures and objects, and appears in the level list and in Project Settings > Add level next to the disc levels.
6. Add the converted levels to a project to see how their exits chain on the campaign map. Exits name their destination level; the map follows them across all converted levels.
7. "Download mod (.zip)" (or "Save to mods folder...") writes the pack the game loads. Put it in the game's mods folder, turn it on in the Mods menu, restart and choose Drakan: Order of the Flame.

* Conversion always reads your disc as it is, never the converted levels laid over it, so converting twice (or again after an update) gives the same classes and names.
* Converted levels are kept in this browser or app profile and come back with the disc. File > Remove converted Order of the Flame levels forgets them. Nothing from your install is uploaded.
* Play (F5, desktop app) on a converted level writes the converted levels as a temporary pack, editor-play-ootf.zip, in the game's mods folder and turns it on. Your own Order of the Flame packs (names starting with ootf) are switched off while the game runs, because they share files and class ids with it. When the game closes the editor puts mods.ini back and deletes the temporary pack; if the editor was closed first, it does so the next time it starts. In a browser, Play shows a dialog that points to the desktop app.
* Objects whose class does not exist on the disc are left out; the "conversion notes" under the result list what was changed or skipped.
* Story lists the Order of the Flame quests, cut scenes, conversations and strings of a converted pack under their own scope (the menu or tick beside the list); turn it off to see the disc's as well.

<a id="campaign-pack"></a>

## Make your own campaign

Turn your levels into a campaign of its own, with its own entry on the title card, start level, cover and saves.

Path: Make a campaign > Levels and start > Cover > Check the exits > Export > Choose it in the game

1. On the start page choose "Make a campaign", or use "Make a campaign step by step" in Project Settings > Campaign. The wizard guides you through Name and cover, Levels, Link the exits, Check, Play test, and Export and install. You can also edit these settings directly in Project Settings (Ctrl+,).
2. Use level copies (New level from a copy, or "Add a copy of a level..." in Levels) for every level of the campaign, and start the campaign in one of them. Edits to a level of the disc itself would also show in Drakan, so the checks warn about them.
3. Link the copies to each other: select an exit in each copy and point it at the next copy in Logic with "Only this exit", or click its arrow on the campaign map.
4. The cover: New picture draws a sky, mountains and a dragon from the palette you pick (press it again for another), "Use my own picture..." takes a picture of yours (cut to 16:9), No picture leaves the card plain. The campaign id and the save folder are filled in from the name; saves of the campaign never mix with Drakan's.
5. In the wizard, Link levels in order connects copies whose source levels already connect, including the way back. Otherwise choose an exit yourself. Check follows every exit from the start; fix its errors before exporting. The wizard blocks export until the route has been checked successfully. Exits back into Drakan are warnings: read them before sharing.
6. Changes you make in the Menus tab ship with the campaign and show only while it is chosen.
7. Export (Ctrl+E) and install the pack. In the game the title opens with "Choose your game": your campaign is listed next to Drakan with its cover and text. Choose it, then New Game starts in your start level; Continue and Load Game show only the campaign's saves.

* Play (F5) and Play on a level row test one level straight away, without the title card. Test the whole campaign from New Game after installing the pack.
* The start level must be one of the campaign's own levels (a copy), not a level of the disc.
* Co-op on a campaign starts in its start level.

<a id="multiplayer"></a>

## Make a multiplayer map

Turn a level into a deathmatch or team arena.

Path: Pick a level > Start new map > Spawns > Pickups > Validate > Export

1. Start from the "Multiplayer arena" template on the start page, or open World Settings (the tab beside Details, or Window > World Settings). Pick a level under "Terrain from", type a map name and press "Start new map". Import map... edits a map from a pack instead.
2. On Map: set the name, an id, the modes it offers (deathmatch, team deathmatch and your own game modes), the player count, starting health and loadout, and the bounds of the arena.
3. On Place: choose the spawn tool and click the ground. "Suggest spawns" puts some on open ground; "Alternate teams" shares them out between the teams. Place pickups (weapons, armour, health), and flag, base and zone markers for capture and hold modes, the same way.
4. On World: see which campaign creatures and objects the game removes from the arena (red dots), and keep or remove single objects.
5. On Modes: make game modes of your own (see "Make a multiplayer game mode").
6. Check Validation: a map needs at least two spawns; it also warns about spawns too close together and pickups players cannot reach on foot. Fix every error.
7. On Export: write the pack zip, put it straight into the game's mods folder (desktop), or export the map .ini only. Then pick the map on the Host Game page of the game's Multiplayer menu.

* The menu beside Play has "Play test arena (host)" and "Play test arena (host + second player)" (desktop app) to try the map at once.
* Spawns that sit inside walls are not detected. Walk the map in game once.
* Every player needs the same map pack.

<a id="game-modes"></a>

## Make a multiplayer game mode

Write the rules of a match: teams, lives, score, time, respawn, weapons and pickups.

Path: World Settings > Modes > New mode > Set the rules > Offer it on the map > Export > Host it

1. Open a multiplayer map (World Settings, see "Make a multiplayer map") and its Modes tab. The list shows deathmatch, team deathmatch and your own modes with their goal.
2. Press New mode and start from a template (Last one standing, Frag race, Team battle, Capture the flag, King of the hill) or a Blank mode.
3. Fill in the groups: About (name and description); Players and teams (free for all or 2 to 4 teams with names and colours, friendly fire, points for a team kill); How a match is won (lives, score limit, time limit, capture and hold objectives); Scoring; Respawn (delay, spawn protection, starting health); Weapons and pickups (whether pickups come back, extra starting items, the weapons that can hurt).
4. The "In the game" card shows the words players will see on the Host page and in the HUD. Tick "Offer it on" the map; if the map cannot play the mode (for example capture the flag without flag and base markers) it says why.
5. Fix what "Fix before export" lists, then export the map: the pack holds the map and every mode it offers.
6. In the game choose Multiplayer > Host Game, pick the map and choose your mode on the Game Mode row. The mode sets the limits and teams; the HUD shows its goal, lives and team names.

* Lives: a player out of lives watches until the next match, and the last player (or team) left wins.
* Pickups can come back as the map says, never, or after a number of seconds.
* Every player needs the same pack. The mode is stored in this browser and written into each pack that offers it.

<a id="menus"></a>

## Change the menus and HUD

Rename, reorder and restyle the entries of the game menus, and change the HUD colours.

Path: Open Menus > Pick a page > Change entries or theme > Export

1. Open Menus from the "Menu page" template on the start page or the Window menu.
2. Pick a menu page on the left. Each entry has its label (type to change it), a box that shows or hides it, and up and down buttons to reorder it. Pages you changed are marked with a dot.
3. Theme sets the colours and text sizes of every menu, HUD the accent colour of the in-game display, and Layout the positions and sizes. A colour has a picker and a text field; its reset button puts it back to the default.
4. The preview draws the page as the game will show it.
5. Set the mod id, name and author, then Export zip, or Write to mods folder (desktop) to put it straight into the game's mods folder and turn it on. The project export includes your menu changes too.

* Copy INI and Import INI move the skin as ui.ini text, for example to share it in a message. Reset all goes back to the game look.
* In a campaign project the menu changes belong to the campaign and show only while it is chosen in the game.
* Menus are read when the game starts; restart the game to see a change.

<a id="playtest"></a>

## Play test your level

Start the game straight in the level you are editing, from where you look.

Path: Look at a spot > Play (F5) > Try it > Close the game > Edit again

1. Play needs the desktop app and the game folder (the folder with drakan.exe). The first Play asks for it; File > Game (Play) Folder changes it.
2. Fly the camera so the middle of the view looks at the ground where you want to start, and press Play (F5). The game opens straight in the level with Rynn at that spot, in about 10 seconds: no title, no intro and no save game. With no floor in the middle of the view she starts at the level's own player start.
3. Right-click the ground for "Play from here", or an object for "Play from this object". The menu beside Play also has Play from camera, Play from selected object, Play selected cut scene and the arena play tests.
4. Every project edit is in the test, including other project levels, so you can walk through exits into them. Close the game to come back.
5. Package and Play (Window menu) lists what the mod changes and holds the play options: "New Game starts this level", "Skip title, memory card prompt and intro (test memory card)" and "Player cannot die while testing".

* A play test uses a test memory card of its own, so your saves are never loaded or changed by it.
* A start point right on the edge of an exit trigger can carry Rynn through the exit at once. Start a little away from it.
* In a browser the editor cannot start the game: export the pack and copy it into the game's mods folder.

<a id="package"></a>

## Package and play a mod

Turn your edits into a pack, install it and share it.

Path: Fix Validation > Export > Enable > Play

1. Open Validation (bottom area of the Level Editor) and fix the errors, then the Checks of Project Settings. Warnings are advice.
2. Press Export (Ctrl+E, the toolbar, or File > Export). One dialog builds the pack from the project: it shows what is in it (changed levels, copies, ground edits, story, shops, menus, campaign) and lets you set the mod name, id, version and author.
3. The pack is written to the project folder's mod folder (desktop), to a place you choose, or downloaded (browser). Tick the box to also install it into the game's mods folder and turn it on.
4. For a zip you copied yourself: put it in the game's mods folder, start the game, and turn it on in the Mods page of the game settings (F1), then restart.
5. Start a New Game, or load a save and go to your level. Play (F5) in the desktop app does all of this for you; see "Play test your level".

* Your disc and its files are never modified. Remove the pack to get the original game back.
* A pack only works with the disc it was made for (shown in Package and Play, which also lists the open level's changes and can revert each one).
* Saves remember the state of a level, so object changes are easiest to see from a New Game.

<a id="save"></a>

## Edit a save

Change a character, inventory, quests or world state in a .rsg file.

Path: Open a save > Edit > Save or export

1. Open the Save Editor: Tools > Save Editor or the "Edit a save" template on the start page. It is an application of its own (its own window; save.html in a browser) with a Level Editor button to go back; its Disc button reads your disc image for item names, pictures and quest text. "Open save" takes one .rsg file; "Open folder" takes the memory card folder and lists every slot with its screenshot.
2. Pages: Character (stats), Inventory (items with pictures), Story (quests), World (every record of the level, searchable), Transfer, Compare (diff two saves) and Repair.
3. Inventory: Add item puts a new item in the first free place; items two or three cells tall (swords, bows, maces) need that many free cells in one column, and the list marks the ones with "- no room". Replace with... swaps an item for another. Power-ups (used at once) are listed apart. With the disc open, the names and pictures come from it.
4. Your changes are listed under Pending changes on the right. Undo and Redo work on them, and Revert all drops them.
5. SAVE writes the file you opened and keeps a .bak copy. EXPORT downloads an edited copy and leaves the original alone.

* Keep a copy of the original save first, and close the game while you edit.
* Added items can be used and equipped in the game, and they stay after the game saves again. A weapon still needs the skill it asks for: raise the skill on the Character page.
* Saves carry no date of their own; the list shows when the file last changed.
* The 3D view shows records that have a position; click one to select it.

<a id="files"></a>

## Browse the raw files

Look inside the disc for textures, text and the structure of a level (advanced).

Path: Pick a folder > Pick a file > Pick an entry > Preview

1. Open Tools > Disc Files. The left pane is the file tree of your disc, in the language you chose.
2. Click a file. Files that hold many resources (.LVL, .TXD, .XDB and so on) list their entries in the middle.
3. Click an entry to preview it: textures are drawn, string tables are listed as text, anything else is shown as hex.
4. Use the buttons above the preview to open the file in the right editor: a level opens in the Level Editor, a string table in the Story editor.

* You do not need this to make a mod. It is for finding out what the game data looks like.
* Nothing here changes the disc.
