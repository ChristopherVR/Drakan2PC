# Order of the Flame Level Editor: layout, tools, workflows (2026-10-31)

Source: `Level Editor.exe` (AiO 2.170 build, Level Editor package) run from `build/ootf/install/Editor` on Windows 11
with Alwarren (`Islands World/Alwarren/Alwarren.lvl`) open. Screenshots are kept untracked in `build/ootf/screens/`
(`ed04_alwarren.png`, `ed09_overview.png`, `edmenu_*.png`, `edwin_*.png`, `edtool_*.png`); none are committed.
Tag **[observed]** = seen on screen. **[R]** = our recommendation. Modeler.exe and the 3D view were **not** run (the 3D
view needs DirectDraw, which renders black here; its menu entry stays greyed until a 3D view exists).

## 1. Overall layout [observed]

One main window with a top-down 2D map filling the client area, and **floating tool windows** (separate small windows,
each opened from the Window menu or the toolbar) instead of docked panels.

```
+------------------------------------------------------------------------------------+
| Level Editor - [Alwarren.lvl] (<0x9006>Alwarren)                                   |
| File  Edit  View  Layer  Tools  Window  Help                                       |
| [open][save][save all] [undo][redo] [select][create][sculpt][vertex][paint]        |
| [scroll][zoom][camera] [databases][dependencies][instance props][layers]           |
| [tool options][view options][level props]                                          |
+------------------------------------------------------------------------------------+
|  2D top view (scrollbars both axes). Grey/blue height-shaded landscape (hatched    |
|  where outside the level), object icons on top, bounding boxes and circles,        |
|  cyan lines = object links.                                                        |
|     floating windows sit over it:  [Layers] [Instance Properties] [Tool Options]   |
|     [Databases] [Properties of ...] [View Options] [Level Properties] [Output]     |
+------------------------------------------------------------------------------------+
| Ready          | height under cursor | cursor x,y | selection icon | layer icons TOP ONLY |
+------------------------------------------------------------------------------------+
```

Toolbar order (left to right) [observed]: Open, Save, Save All | Undo, Redo | Select, Create, Sculpt, Vertex, Paint |
Scroll (hand), Zoom, Camera | Databases, Dependencies, Instance Properties, Layers, Tool Options, View Options, Level
Properties. Status bar: message, height under cursor, cursor map x,y, selection-extent icon, layer-state icons and a
"TOP ONLY" checkbox.

## 2. Menus (exact labels and shortcuts) [observed]

- **File**: New Ctrl+N; Open... Ctrl+O; Save Ctrl+S; Save As...; Save All...; Import > (Layers..., Objects/Links...,
  Heights From Bitmap...); Export > (Landscape...); Resource > (Edit Script..., Compile Script...; greyed without a script);
  **Test Level Ctrl+T**; Explore Level Directory Alt+X; Editor Files and Paths...; recent list; Exit.
- **Edit**: Undo Ctrl+Z; Redo Ctrl+Y; Delete Del; Delete Links; Select All Ctrl+A; Select All of Selected Ctrl+E;
  Select All Similar Ctrl+Shift+E; Select All of Class; Select Invalid Objects; Invert Selection Ctrl+I; Move...;
  Remap Databases...; Level Properties... Alt+Enter.
- **View**: Toolbar (check); Status Bar (check); Show All Objects Ctrl+R; Hide Selected Objects Ctrl+H; Zoom In (+);
  Zoom Out (-); Track Camera; Goto Camera Tab; Goto Selected Objects Ctrl+Tab; Level Information...; View Options...
- **Layer**: Editing Rules >; Split >; Join; Flip >; Rotate 90 Degrees >; Set To Height...; Scale Height...; Normalize
  Origin; Match Vertices... Ctrl+M; Mark Outdoor Vertices; Flatten Elements...; Combine Elements; Mark Walkable
  Elements...; Auto-Set Divisions; Replace Texture...; Null-Out All Textures; Group Selected... Ctrl+G.
- **Tools**: Edit Mode > (Objects O, Layers L); Select F2; Create F3; Sculpt F4; Vertex F5; Paint F6; Scroll F9; Zoom F10;
  Camera F11; Tools Options...
- **Window**: 3D View (greyed); Databases; Dependencies; Instance Properties; Class Properties; Sequence Properties;
  Layer Visibility; Level Properties; Output Window; Tools Options; View Options; Hide All Windows.

"Layer" means a terrain/landscape layer (a piece of mesh), not a UI layer; Edit Mode switches between Objects and Layers.

## 3. Windows [observed]

```
Databases (File Edit Build)            Databases and Dependencies         Instance Properties
 tabs: Classes | Models | Textures |    "Databases Used in This Level"     tabs (scroll arrows): General |
       Sounds                           tree: Alwarren > FX, NPCs,          Position | Rotate/Scale | ...
 [ FX v ]  database drop-down             Resources, System; FX; NPCs...   General: Object Class [Select>] name,
 tree of class folders (Blood and       Information: ID, Classes 205,      Settings: Visible in Game, Initial
 Gore, Boil, Break Away Wall,           Models 210, Animations 5,          Event Count; Restrictions: Difficulty
 Explosions, Flame, ...)                Textures 388, Sounds 25,           Level; Get Layer Lighting From
                                        Sequences 7                        [Select>] <default>
                                        buttons Create New, Add File...,   Position: Height, Adjust By
                                        Rename, Remove; "drag databases    Vertex Type (with layer selection)
                                        to each other to create deps"

Layers                                 Tool Options (per tool)             View Options (tabs)
 tree "Group": Ungrouped, Alwarren      Select, Create: empty               Top View | Shading | Colors | 3D View
 Entrance, Enter From5                  Sculpt tabs: Shape (Smooth Slope    Top View: Landscape [Show Textures,
 "Visible Layers" list; buttons         preset, Invert Shape, Offset        Grid, Divisions, Vertices, Show Borders,
 Clear Table, Edit Layer Visibility,    slider Huge..None, brush Thin..     Black Lines, Reset Origin]; Objects
 Calculate Visibility                   Wide, size 20000, Dropoff Fast..    [Show Objects, Direction Marks, Bounding
                                        Slow, Fractalize Lots..None) |      Area, Show Links; Bounding Area:
Level Properties tabs: Defaults |       Drawing | Limits                    Radius / Box]
 Libraries | Auto Save                  Vertex Edit: Select Vertices /
 Defaults: Level Name, Maximum Level    Paint Type Onto Vertices, Type,
 Size W x H (512 x 512), Undo/Redo      Special Vertex Selection
 Remember 20 undo levels                (Select Textures in Range, New
                                        Selection / Add to Existing)
Properties of [..]: Model> and Type>    Texture Paint: Left/Right Button
 pickers + property grid (Class         texture slots, Brush Style
 Properties for the selected object)    (Triangle), SHIFT-Click (Pick
Output Window (log); STOMP Sequence     Texture), Rotate L/R, Flip H/V,
 (animation timeline)                   Randomize Orientation/Over Range
```

## 4. Workflows [observed unless marked]

- **Open a level**: File > Open (standard Windows file dialog, filter "Riot Engine Levels (*.lvl)"), or the recent list.
  Alwarren takes about 10 s (205 classes, 210 models, 388 textures).
- **Select and inspect**: F2 then click an object icon; Instance Properties fills (selected sky object: class
  `AlwarrenSky`, Visible in Game, Initial Event Count 1, layer lighting `<default>`).
- **Place an object [from menu structure, not performed]**: choose a class in Databases > Classes, F3 Create, click the map.
- **Edit properties**: Instance Properties for the instance; Class Properties for the shared class record; both are tabbed
  sheets, windows stay open while the selection changes.
- **Link objects**: links are drawn as cyan lines when View Options > Show Links is on; Edit > Delete Links removes them.
  Creating one was not observed [U].
- **Terrain**: Sculpt (F4) brush with shape/drawing/limits tabs; Vertex (F5) selects vertices; Paint (F6) applies textures
  with two mouse-button slots; the Layer menu has height set/scale, flatten, split/join, match vertices.
- **Triggers and events**: no separate trigger editor window; behaviour lives in object classes, instance properties
  (Initial Event Count) and scripts (File > Resource > Edit/Compile Script) [U for details].
- **Test**: File > Test Level (Ctrl+T) launches the game (registry value `GameEngine` below).
- **Pack/export**: no pack step found; levels are loose `.lvl` + `.odb` files. Export is Landscape only; Import covers
  Layers, Objects/Links and heights from a bitmap.
- Known editor quirks (AiO readme): texture previews break on Windows 8+, 3D view needs "3D sound buffers" enabled.

## 5. Recommendations for our `editor/` [R]

1. Keep the **2D top view as the primary canvas** with height shading and object icons; add the 3D view as a second pane
   rather than a mode switch.
2. Use **dockable panels** (OotF's floating windows overlap and hide each other): Databases/Assets, Instance and Class
   properties, Layers, a single context-sensitive Tool options panel, View options, Output.
3. Preserve the vocabulary and shortcuts: F2 select, F3 create, F4 sculpt, F5 vertex, F6 paint, F9 scroll, F10 zoom,
   F11 camera; O/L edit mode; Ctrl+T test; +/- zoom; Ctrl+E and Ctrl+Shift+E select-similar.
4. Split properties into **instance vs class** with tabs (General, Position, Rotate/Scale, ...).
5. Show links as lines with a toggle, offer link creation by drag plus a link list, and add a Dependencies view
   (database graph with counts) because levels reference shared databases.
6. Include Level Properties (name, max size, undo depth, auto-save) and layer visibility tables.
7. Pack/export: one "Export mod pack" bundling level + databases (our existing mod-pack path), and Test Level launching
   the port.

## 6. Registry written on this machine (user-approved, kept)

All under `HKEY_CURRENT_USER`, created by the game and editor themselves; nothing under HKLM, no `.reg` imported.
- `Software\Surreal\Riot Engine`: `Settings101` REG_BINARY (engine options: graphics, windowed flag, display mode, sound).
  Written when OK was pressed in "Riot Engine Options" (first a 1024x768 windowed run, then a fullscreen retry that
  selected 1920x1080).
- `Software\Surreal\Riot Engine\Tool Paths`: `PathLevels` REG_SZ `...\build\ootf\install\Islands World\Alwarren2`, and
  `PathTextures`, `PathPalettes`, `PathModels`, `PathSounds`, `PathScripts` REG_SZ `2`. These look corrupted (written by
  the editor on exit); fix via File > Editor Files and Paths... or delete the key before real use.
- `Software\Surreal\Drakan Level Editor`: `UndoLevels` 0x14, `ShowMainToolbar` 1, `ShowMainStatusbar` 1, and REG_BINARY
  `Windows`, `ViewOptions`, `Shading`, `ToolsPaint`, `ToolsRidge`, `AutoSave`; `MRULevel0` = the Alwarren path,
  `MRULevel1..8` empty; `Engine` `Engine.exe`, `Control` `Control.rfl`, `PaletteEditor` `paledit.exe`, `GameEngine`
  `Drakan.exe`.

No `Drakan.cfg` was written. To undo: `reg delete "HKCU\Software\Surreal" /f`.
