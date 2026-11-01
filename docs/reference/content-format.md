# Drakan Content Format (DCF) 1.0

Engine-neutral description of one level and the story content that belongs to it. Purpose and architecture:
[`../proposals/engine-portability.md`](../proposals/engine-portability.md). It contains **no game bytes** (no vertices, texels,
samples or code): names, numbers, references and text. Subtitle and journal text is game text, so a package is written to
the user's own disk and never committed.

| Where | What |
| --- | --- |
| `editor/packages/editor-core/schema/drakan-content.schema.json` | the normative JSON Schema (draft-07; Ajv with `strict: true` compiles it) |
| `editor/packages/editor-core/src/content/format.ts` | TypeScript types, one per schema definition |
| `.../content/build.ts` | `buildContentScene(input)`: editor models to a scene. Pure and deterministic |
| `.../content/package.ts` | `buildContentPackage(scenes)`, `zipContentPackage(files)` |
| `.../content/validate.ts` | `validateContent(doc)`: small validator for exactly the keywords the schema uses (no dependency) |
| `editor/packages/app/src/editor/contentexport.ts` | the editor action |
| `editor/packages/editor-core/test/content-format.test.ts` | unit tests on synthetic fixtures |

## Using it in the editor

With a disc level open: command palette (`Ctrl+P`) "Export engine-neutral package...", or the Mod panel's "Export package..."
button. It saves `<level>.drakan-content.zip` through the browser download (desktop app: a save dialog). It exports the live
document, so unsaved edits are included. Nothing is written into the repository or the game folder.

Package layout:

```
manifest.json                      { format, version, kind: "package", generator, source {profile, language}, scenes[], schema }
scenes/<level-slug>.scene.json     one scene (below)
schema/drakan-content.schema.json  the schema the files validate against
README.txt
```

## Versioning

`format: "drakan-content"`, `version: "1.0"` (`major.minor`). A minor bump only adds optional keys (readers ignore unknown
keys of a newer minor); a major bump may change meaning and ships a new schema file. The schema has
`additionalProperties: false` everywhere, so a 1.0 document is checked strictly; a reader of 1.x should validate against the
schema of the minor it knows and skip unknown keys.

## Conventions

* Positions: game units, y up, as the editor shows them (1 terrain cell = 2048 units; `coordinates` repeats this).
* Instance rotation: Euler XYZ radians in the editor's document convention (the LVL yaw offset of -90 degrees is applied,
  level-format.md section 3) plus the same rotation as a quaternion `[x, y, z, w]` (three.js XYZ order). Sequence keyframes are
  the raw game-space values and do **not** carry that yaw offset.
* Ids: instance `id` = LVL object id (stable, unique in the scene). Archetype `name` = `<ODB>/<class name>`, with `#<class id>`
  appended only when that ODB repeats the name. Asset `id` = `<container>:<resource id>` as the disc names it
  (`RESOURCES:128`, `SOUND:4`, level-local containers use the level folder name, e.g. `LEVEL1NA:3012`).
* Numbers are rounded to 9 significant digits (enough for an f32).
* Join with the glTF / Unreal export ([`unreal-export.md`](unreal-export.md)): an instance `id` equals the node's
  `extras.drakan.id`; the mesh name there is `SM_<Class>` / `SK_<Class>`. DCF does not carry mesh paths; `AssetRef.path` is
  only filled when an exporter provides one (`ContentInput.assetPath`, `manifest.assetIndex`). The editor does not set it yet.

## Scene

```
format, version, kind: "scene", generator {name, version}
level        { name, stem?, storyStem?, language?, bounds? }   stem = level folder (LEVEL1NA), storyStem = the container with the story data (LEVEL1ON)
coordinates  { unit: "game-unit", up: "y", cellSize: 2048, rotation: "euler-xyz-radians" }
messages[]   the game's message enum (33 names), indexed by message number
archetypes[] instances[] links[] triggers[] sequences[] dialogue {lines[], handlers[]} quests[]
diagnostics  { objects, unresolvedClasses, danglingLinks, opaqueSequenceActors, missingSubtitles }
```

### archetypes

One per class that an instance uses, plus the classes their class-typed defaults name (so a spawner's projectile class is
known even when nothing of that class is placed). Order: by name.

| Key | Meaning |
| --- | --- |
| `name` | stable name, see Conventions |
| `class` | `{classId, db, rflClass?}`: ODB container, class id, low 16 bits of the runtime class. The PS2 adapter maps the name back to `(db, class id)` with this |
| `category`, `kind` | the editor's ODB label and marker kind (`player camera creature weapon usable static other`; kind is a naming heuristic, not game data) |
| `role` | logic role (`detector timer counter relay switch door cutscene movie conversation exit elevator plate lock visibility sound creature quest chest teleporter mechanism waypoint other`) when the class has one |
| `mesh` | default model: the first model-typed field default, else the class record's own model |
| `properties[]` | the class field list: `key` (unique; `name#index` when a class repeats a field name), `name`, `index` (position in the class, what LVL parameters are keyed by), `type`, `array?`, `default`, `category` (a naming heuristic), `enumNames?` (item names the engine's property tables list), `semantic?: "message"` (the value indexes `messages`) |

Property `type` and value encoding:

| `type` | JSON value |
| --- | --- |
| `int`, `enum`, `unknown` | integer (ints signed) |
| `float` | number |
| `bool` | boolean |
| `string` | string |
| `colour` | `{rgb: "#rrggbb", alpha: 0..255}` |
| `model`, `sound`, `animation`, `sequence`, `texture`, `layer`, `channel` | `null` when unset, else `AssetRef {kind, id, path?, name?}` (kind `mesh sound animation sequence texture layer channel`; sequence ids use the level folder) |
| `class` | `null` or `ClassRef {class: "<container>:<id>", archetype?}` (`archetype` is set when the class is in this scene) |
| any array type | JSON array of the element values above |

### instances

`{id, name, archetype, transform {position, rotation, quaternion, scale?}, properties?, mesh?}`. `properties` holds only the
values that differ from the archetype default (the LVL parameters), keyed by `PropertyDef.key`. `mesh` appears when the
instance's model differs from its archetype's.

### links and triggers

`links[]`: `{from, to, messages[]}` for every link-list entry whose target exists. The messages are what the **source**
sends (a class message field such as "Trigger Message"): `{field, label, message, name}`; the game sends them to every linked
object (level-format.md 15.1). `diagnostics.danglingLinks` counts links to missing objects (not exported).

`triggers[]`: one per instance whose class has one of the core logic roles. `{instance, role, title, volumes?, sends,
listens, accepts?, sequences?}`. `volumes` are box (`center`, `halfExtents` in game units, `rotation`) or sphere (`center`,
`radius`, `field`) shapes derived as in level-format.md 15.3. `listens` are the message-valued input fields (`Trigger On
Message`, ...). `sequences` (cut scene players) are the sequence ids of the "Sequence List" field.

### sequences

From the level's `.SSD`: `{id, name, folder?, length, actors[], partial?}`. An actor is `{name, slot, ref, target, actions[]}`;
`target` is `instance` when `ref` is the id of an instance of this scene and `class` otherwise (inferred: the player and the camera use class ids, and some NPC actors name an id that is not in the object list; on Surdana 222 of 302 actors resolve to instances).
Actions are typed by `kind` with `time` in seconds:

| `kind` | Fields |
| --- | --- |
| `transform` | `position`, `rotation` (quaternion x y z w), `scale` |
| `animation` | `animation` (ADB id), `mode`, `offset`, `speed`, `flags` |
| `sound` | `sound` (`AssetRef`), `volume` (low 16 bits of the params word, 10000 on the disc) |
| `message` | `message` (number as stored, 0x1000 + n for the object message enum), `name`, `param` |
| `camera` | `fov` (inferred), `unknown` (always 1.0 on the disc) |
| `marker`, `hold` | none |
| `unknown` | `type`, `values` (every field of an undecoded action type as a number) |

`partial: true` means at least one actor's bytes did not follow the action grammar (it exports with no actions;
`diagnostics.opaqueSequenceActors`). Every sequence of the PAL disc parses (level-format.md 14).

### dialogue

`lines[]`: every dialog line class of the level ODB: `{name, classId, speaker, text, sounds[], animations[], delay, playMode,
camera {position, transition, transitionTime}}`. `text` is the subtitle of the first sound that has one (empty counts in
`missingSubtitles`). `handlers[]`: every placed conversation handler: `{instance, archetype, initiallyActive, distance,
hasQuest, gold, phases[], items}`. A phase is `{key, label, lines[] (line names), message, messageName}` for
`beginning incomplete complete completeNoBegin done`, with the per-instance parameter overrides applied. `items` lists item
class refs as `<container>:<id>`.

### quests

All quest and map marker classes of System.ODB (a quest is game wide, not level wide; filter by `chapter`): `{id (class id),
name, kind, optional, chapter, chapterName, journal?, mapText?, map? {x, y}}` with text in the disc's language.

## Validating

```
node -e "const A=require('ajv');const a=new A({strict:true});const v=a.compile(require('./drakan-content.schema.json'));
 console.log(v(require('./scenes/<level>.scene.json')))"
```

Any draft-07 validator works (Ajv, Python `jsonschema`). In the editor code `validateContent()` runs the same schema before
a package is written; the unit tests also run it over a round trip through `JSON.stringify`.

## Not in 1.0

Items, vendors, menus, materials, terrain, lighting and music (proposal section 4 "Later"). Sequence camera paths are
exported as the keyframes the game stores, not simplified. The class-reference closure may name classes in other ODBs;
only their `ClassRef.class` id and (when the class is in the level's catalog) archetype are given.
