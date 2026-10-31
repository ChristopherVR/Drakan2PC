# Level logic: graph editor, templates, validation, in-game proof (2026-10-31)

Goal: let people change triggers, puzzles and cut scene trigger points. Reference: `docs/reference/level-format.md` section 15.

## How it was worked out

* Link lists were already decoded (section 2). What was missing was *what travels along a link*. The class field lists (ODB
  0x0102/0x0103, `build/odb-fields`) show message fields (`Trigger Message`, `Message To Send`, ...), all type 7 enums with values 0 to 32.
* The enum item names are in the executable's string pool: the property tables list each enum as one comma separated string
  right after the field name. One 33 item list ("Off,On,Lock,Unlock,Play Sequence,Blow Up,Triggered,Move to WayPoint,I'm
  Dead,...,<none>") follows the generator's "Activate Message" and is the message list; other lists give Task, Detect Which?,
  Start Mode, List Play Order and so on (`analysis/strings_vaddr.csv` around 0x533ec0 and 0x543380 to 0x544500).
* Checked against the data (`analysis/scripts/logic_graph.py`, Trigger Message value x target class): 4 and 23 go to cut scene
  players, 28 and 18 to conversation handlers, 7 to creatures, 3/2 to doors, 8 is what creatures send and counters count. That
  confirms the ordinals; it does not give the engine's numeric message ids, which were not traced (no decompile of the handlers).
* The Trigger Detector model (SYSTEM.MOD 3727) is a unit cube; with scale triple x 2048 it matches the box the editor renders. The
  detector's LVL radius word is unrelated (one use).
* Exits: the destination is a string id in the class default (length 0, so the TS/Python string decoders show an empty string);
  the id indexes the level's own XDB. Arrival markers = exits of the destination whose own destination is this level.
* Surdana: 4675 objects, 654 logic nodes, 608 edges (core roles), 14 triggers without target, 1 unreachable cut scene, 1 ignored
  message. Whole disc: 10453 links, none dangling; the biggest groups are waypoint chains (1317), Vis Toggler to scenery (905),
  detector to timer (484), timer to creature (340).

## Editor

* `formats/src/logic.ts`: message names, `FIELD_ENUMS`, `LOGIC_CLASSES` (role, outgoing and incoming message fields, accepted
  messages, volume flag). The Inspector's enum dropdowns now show names (`fieldsui.ts`, `enumNames`): message fields list all 33.
* `editor-core/src/logic.ts`: `buildLogicGraph` (level or neighbourhood scope), `layoutGraph` (layered, cycles broken),
  `addLink`/`removeLink`/`setMessageField` (use inside `ctx.edit`, so CommandStack, undo and mod export are unchanged), volumes
  (`volumesOf`, `resizeBoxFace`, `setVolumeRadius`), `validateLogic`, `exitArrivalProblem`. `SchemaProvider` gained `rflOf` and
  `rawDefault`.
* `editor-core/src/logic-templates.ts`: six templates and `ApplyTemplateCommand`.
* `app/src/editor/logicpanel.ts`: the **Logic** dock tab (registered through `registerPanel`, installed from `levelui.ts`):
  SVG node graph, pan/zoom, node drag, drag from a node's dot to another node to link, click a link to edit its message (source
  field dropdown with names) or remove it (button, Delete), node click selects in the viewport and the viewport selection
  rebuilds the graph, double click frames, the node's typed fields (existing field controls) in the side panel, template
  wizard, validation provider (Validation tab). `logicview.ts`: wire boxes, circles and drag handles in the viewport (modes selected / all / off).
  `logicdata.ts` + worker call `exitIndex`: where every level's exits lead.
* Commands: "Show Logic" (Ctrl+Shift+L), "Toggle trigger volumes", "Show the whole level logic graph".
* Tests: `editor-core/test/logic.test.ts` (19 tests: vocabulary, graph, layout, edits with undo, volumes incl. rotated resize,
  validation, templates).

Browser check (Surdana, Playwright, ISO via a hard link under `build/scratch/logic`): whole level graph 654 objects / 608 links;
selecting detector #7725 shows it, its cut scene #7809 and timer #15172; the edge editor shows "4 Play Sequence" for the detector's
Trigger Message; the cyan box with face handles sits exactly on the rendered trigger model; template "Trigger volume plays cut
scene" created #18254 with a link to #7809 (4 objects, 3 links in the neighbourhood view after). Screenshots: `build/scratch/logic/`
(`graph-selection.png`, `graph-edge.png`, `volume-3d.png`, `template-form.png`, `template-result.png`).

## In-game proof

Authoring: `build/scratch/logic/make_pack.mjs` (node, the editor's own code: load Surdana, template "Trigger volume plays cut
scene" at Rynn's start (454517.9, 171296.8, 339525.7), volume 10 x 8 x 10 cells, detects Rynn on ground and mount, once, target cut
scene player #7809, export through `applyDocumentToLvl`, pack with `buildModFiles`). Diff: one added object (#18254), nothing
else; LVL entry 452508 -> 452642 bytes.

Run: private folder `build/logic-play` (copy of `build/field-edit-play`, its drakan.exe), pack in `mods/logic.cutscene-trigger`,
`play_run.py --exe build/logic-play/drakan.exe --shots 200,500,900` with `DRAKAN_WORLD=2 DRAKAN_WORLD_DUMP_FRAME=900`; baseline run with
`DRAKAN_MODS` pointing at an empty folder.

* Log: `mods: loaded 'logic.cutscene-trigger'`, `LEVEL1NA.LVL rebuilt with 1 entry patch(es)`.
* World dump (mod): a class `0x1003d` actor at exactly (454517.9, 171296.8, 339525.7). Player at frame 900: baseline (447902, 344205),
  mod (421553, 382730) which is where cut scene #7809 stands (405204, 381824): the cut scene ran.
* Screenshots (`build/scratch/logic/proof/`): `base-200.png` Rynn on the path with the quest hint, `mod-200.png` / `mod-900.png` the
  orc encounter that #7809 plays (sequence 1354), normally triggered by detector #7725 further along.

## Not done / open

* The detector's exact volume test in the engine (box vs. the model's collision shape) was not decompiled; the box is the model.
* Enum names exist only for the fields in `FIELD_ENUMS` (others stay numbers); door On/Off = open/close is inference.
* Exit destinations are read from class defaults and LVL string overrides; editing a destination (a new string id) is not
  possible, you pick another exit class instead. New exit classes cannot be created.
* The graph does not edit the arrays that hold dialog/sequence content (the story agent's area); pressure plates have no instance
  in Surdana (the template reports it). Handle drag and graph drag were checked by unit tests of the maths and by synthetic DOM events only for
  selection, link edit and template creation; a pointer drag of a face handle was not exercised in a real mouse session.
* Viewport scale tool still works on volumes; the new handles are an addition.
* Only Surdana was walked in game.

## Closeout pass (engine ids, pressure plate)

* Message ordinals vs engine ids: see level-format.md section 15.1 (0 to 7 = `0x1000 + n` inferred from the handlers; 8 and up
  live in the `0x1161..0x1183` block, not mapped; no conversion table in the ELF).
* Pressure plates: LEVEL8OS has the class. Template fixed (Send Message When = Reached Bottom, Sink Distance 50, counter message by
  target role) and tested in Stratos with a private play folder (`build/plate-play`, pack `logic.plate-puzzle`, recipe
  `build/scratch/content2/plate_walk.txt`). Plate and counter spawn and the plate sinks; the cut scene effect was not separable from
  the level's own opening effect, so the whole chain is unproven. Screenshots: `build/scratch/content2/plate-*.png`.
