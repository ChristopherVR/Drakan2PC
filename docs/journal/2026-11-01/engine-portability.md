# Engine-neutral content format and the portability plan — 2026-11-01

Direction: separate content and rules from the PS2 engine so the game can later move to another engine (Unreal first).
Plan: [`../../proposals/engine-portability.md`](../../proposals/engine-portability.md). Format reference:
[`../../reference/content-format.md`](../../reference/content-format.md). Phase 1 is implemented and proven below.

## What was built

* `editor/packages/editor-core/schema/drakan-content.schema.json`: draft-07 schema "Drakan Content Format" 1.0 (scene and
  package manifest), strict (`additionalProperties: false`).
* `editor/packages/editor-core/src/content/`: `format.ts` (types), `build.ts` (`buildContentScene`), `package.ts`
  (`buildContentPackage`, `zipContentPackage`), `validate.ts` (small validator for the schema's keywords, used by the editor
  and the unit tests).
* Editor action "Export engine-neutral package..." (command `file.exportContent`, palette search "engine-neutral"; also a
  button in the Mod panel) in `editor/packages/app/src/editor/contentexport.ts`. It exports the live document through the
  normal download / save-as path, so nothing lands in the repository.
* `SchemaProvider.modelWord(resource)` (schema.ts, additive): the model of a class record, so archetypes get a mesh ref even when the
  class has no model-typed field.
* 15 unit tests (`editor-core/test/content-format.test.ts`) on synthetic classes: instances and overrides, quaternions, typed
  defaults (colour, bool, class refs with archetype closure, signed arrays, message semantic), duplicate class names,
  unresolved classes, links with dangling targets, triggers with box volumes, sequences with every action kind and an unreadable
  actor, dialogue lines with subtitles, a placed conversation handler with overrides, quests in the disc language, asset path
  hook, package layout, zip round trip, schema rejection of broken documents.

## Proof on Surdana (the player's disc)

Own Playwright context (playwright-core with Chromium 1248, not the shared MCP browser), the editor built with `vite build`
into a scratch directory and served statically (the dev server reloads whenever another session edits a file), the ISO
`C:\Games\Drakan - The Ancients Gates\Drakan - The Ancients' Gates.iso` set on the file input with `setInputFiles`
(about 13 s to open), `Level 1A - Surdana` (`ENGLISH\LEVELS\LEVEL1NA`) opened, then `Ctrl+P`, "engine-neutral", Enter. The
download was unzipped and both JSON files were validated with **Ajv 8 in strict mode against the `schema/` file from the zip**
(independent of the editor's own validator): `manifest.json` valid, `scenes/level-1a-surdana.scene.json` valid.

| Result | Value |
| --- | --- |
| Package | 841 KB zip: manifest, scene (10.8 MB JSON), schema, README |
| Instances | 4675 (all LVL objects; ids unique; every archetype name resolves; 0 unresolved classes) |
| Archetypes | 1607 (538 used by instances, the rest named by class-typed defaults such as spawners and projectiles; 1157 have a mesh ref) |
| Links | 1712 (0 dangling) |
| Triggers | 565: detector 217, timer 176, conversation 52, door 41, quest 30, cut scene 18, exit 11, switch 10, counter 8, elevator 2 |
| Sequences | 52 (302 actors; 0 partial; actions: transform 1100, animation 544, sound 456, marker 306, message 180, camera 79, unknown 57, hold 26); the 18 cut scene players name sequence ids that all resolve |
| Dialogue | 232 lines (199 with subtitle text; 33 without, mostly generic "Greeting" lines whose sound has no text), 52 placed handlers (51 with lines) |
| Quests | 90 quest / map marker classes (25 of chapter 1) with journal and map text |
| Story container | the level folder is `LEVEL1NA`, but the ODB / SSD / SDB with handlers, lines and sequences are in `LEVEL1ON`; the exporter picks the level's dependency that is a story folder |
| Screenshots | `build/editor-content-export/` (palette, Mod panel with the new section, status after export). The exported JSON stayed in the scratch directory (it contains game text) |

Run time of the export itself: a few seconds on a loaded machine (story files and the System quest catalog are read through
the worker).

## Findings

* A first run produced an empty story section: the story data of `LEVEL1NA` lives under `LEVELS\LEVEL1ON`. Fixed by choosing the
  story stem from the LVL's own dependency list (falls back to the level folder). Other levels were not run; the same rule
  covers any level whose dependency list names a folder with an `.SSD` / `.SDB`.
* Model refs: most classes carry their model in the class record, not in a model-typed field, so `mesh` needed
  `SchemaProvider.modelWord`. Before that fix only 50 archetypes had a mesh, after it 1157.
* The archetype closure over class-typed defaults adds about two thirds of the archetypes (1069 of 1607 have no instance). It
  is what lets an importer know a spawner's projectile class. If size becomes a problem it can be made optional per class
  group; 10.8 MB is acceptable for now.
* 222 of 302 sequence actors resolve to an instance of the level; the other 80 name a player / camera class id or an id that
  is not in the object list. The format calls this `target: "class"` (inferred) rather than guessing.
* Conversation handler phase messages all read 0 (`Off`) on Surdana's first handler: either the fields really default to 0 or
  the message field has another meaning for handlers; exported faithfully, not interpreted.

## Not proven / open

* Only Surdana was exported from the real disc; the other 13 levels and converted Order of the Flame levels were not.
* Nothing imports a package yet (phase 2) and no UE5 project exists; the coordinate conversion belongs to the glTF export
  (`docs/reference/unreal-export.md`) and is unverified in a UE install there.
* `AssetRef.path` is never filled: the glTF / PBR exporters have no asset index yet. The join key is documented (instance id =
  glTF node `extras.drakan.id`).
* `npm run typecheck` currently fails in editor-core on another session's `test/vendor.test.ts` (3 errors about
  `EditObjectsCommand | null`); the editor-app typecheck, formatting check and the new tests pass.
