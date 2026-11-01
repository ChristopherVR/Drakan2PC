# Engine portability: separating content and rules from the PS2 engine

Status: proposal, 2026-11-01. Phase 1 (the content format and its exporter) is implemented, see "Phases".
Direction from the owner: "We're moving more towards having a game engine and the game coded in a way we can easily port
it to another engine later, like Unreal Engine."

Related: [`level-editor-and-multiplayer.md`](level-editor-and-multiplayer.md) (editor and multiplayer plan),
[`../journal/2026-10-31/engine-reimplementation.md`](../journal/2026-10-31/engine-reimplementation.md) (option (c) "engine as
host", which this builds on), [`../reference/content-format.md`](../reference/content-format.md) (the format),
[`../reference/level-format.md`](../reference/level-format.md), [`../reference/world-model.md`](../reference/world-model.md),
[`../reference/netcode.md`](../reference/netcode.md), [`../reference/portability.md`](../reference/portability.md).

## 1. Goal and non-goals

Goal: a point where **the same content** (levels, objects, story, dialogue, quests) can be loaded by a UE5 project, and the
**game rules** that drive it exist as a specification and a reference implementation that does not depend on the PS2
runtime, so a UE5 (or any other) engine port re-implements a documented contract instead of reverse engineering the
recompiled guest again.

Non-goals:

* Rewriting the renderer, physics or AI in this repository. The recompiled guest stays the playable game and the oracle.
* Shipping any game data. Everything below keeps the existing rule: the player supplies the disc, the repository holds
  code, schemas, names and numbers only (`scripts/repository_guard.py` enforces it).
* A UE5 project in this repository now. Phase 5 is a plugin plus importer that reads a package made locally.

## 2. Where we are

### 2.1 What runs where today

| Concern | Today | Portable? |
| --- | --- | --- |
| Level objects, classes, parameters, links | Disc formats (`.LVL` object list, `.ODB` classes + field lists) read by `editor/packages/formats`, edited through `editor-core` `LevelDocument` + `SchemaProvider` | Yes: pure TypeScript, no PS2 runtime involved |
| Logic vocabulary (messages, roles of trigger / timer / counter / door / cut scene player / conversation handler ...) | `formats/src/logic.ts` (`MESSAGE_NAMES`, `LOGIC_CLASSES`), graph and volumes in `editor-core/src/logic.ts` | Yes as data; the **semantics are only enforced by recompiled guest code** |
| Story: strings, quests, dialogue lines, conversation handlers, cut scene sequences | `formats` (`xdb`, `quests`, `story/*`), edited by `editor-core` `story.ts` / `story-create.ts` | Yes as data; sequence actions are decoded (`story/ssdactions.ts`), their playback is guest code |
| Gameplay, AI, physics, animation, camera, level load, scripting dispatch | 12k recompiled guest functions, state in emulated RDRAM | No. Never reimplemented (`engine-reimplementation.md` 1.1) |
| Multiplayer rules: lobby, phases, damage validation, score, respawn, game modes, arena definitions | `runtime/net` (`MatchServer`, `GameModes`, `ArenaMaps`, `Scoreboard`), std-only, tested headless, no game and no ISO | **Yes, already engine-neutral** |
| Inventory, shop, map, pause, save/load models | `runtime/gameui` models (`Model.h`) filled from guest memory through `IGuestMemory` | The models and layout yes, the **readers** are PS2 specific |
| Entity snapshot of the live world | `runtime/world` (`EntitySnapshot`, JSON dump) | Read-only observer of the guest; the schema is portable, the reader is not |
| Input, settings, audio mixing, video, FMV, disc IO | `runtime/input`, `settings`, `spu2`, `gs`/`gpu`, `fmv`, `hle` | Platform shell; replaced wholesale by a target engine |
| Mod packs (overrides of disc entries) | `runtime/hle` `ModPack`, `editor-core/src/mod.ts` | Specific to the PS2 resource layer |

In one sentence: **content is already portable data, multiplayer rules are already a portable library, and single player
rules are guest code.** The work is to make the first two explicit and stable, and to carve the third into a specification
plus a reference implementation, one rule at a time.

### 2.2 What is missing

1. There is no engine-neutral, versioned way to hand a level to another tool. The editor's own `LevelDocument` JSON
   (`drakan-level-document`) mixes editor state (`origin`, `template`, `slot`) with content, carries LVL parameter indices
   and `0x2000a`-style resource strings that only mean something with the disc's ODB tables, and has no archetypes, no
   story and no stable names.
2. The meaning of messages and triggers lives in `LogicClass.note` strings and in the guest. Nothing executable states it.
3. Vendors, item definitions, menus and materials have no content model at all (they are read from guest memory by
   `gameui` or are fixed in the shader / TXD path).

## 3. Target architecture

Three layers with one-way dependencies, plus adapters.

```
 +--------------------------------------------------------------------------------------------+
 |  Tools: editor (TS), converters (OotF import), exporters (glTF / PBR / DCF)                |
 +------------------------------------------+-------------------------------------------------+
                                            | read / write
 +------------------------------------------v-------------------------------------------------+
 |  CONTENT   Drakan Content Format (DCF)   schema/drakan-content.schema.json                 |
 |  scene: archetypes, instances, links, triggers, sequences, dialogue, quests (+ later       |
 |  items, vendors, menus, materials)       assets by stable id + optional relative path      |
 +------------------------------------------+-------------------------------------------------+
                                            | interpreted by
 +------------------------------------------v-------------------------------------------------+
 |  RULES     message bus, trigger / door / counter / timer / switch semantics, quest and     |
 |  dialogue state, match rules (runtime/net), inventory / shop models (runtime/gameui)       |
 |  std-only C++ + spec tests; no PS2, no renderer, no platform headers                       |
 +------------------------------------------+-------------------------------------------------+
                                            | host interface (entities, time, input, spawn, play)
 +------------------------------------------v-------------------------------------------------+
 |  ENGINE ADAPTERS  PS2 guest adapter (today: hooks in ps2xRuntime/src/lib/drakan_*.cpp,     |
 |  runtime/world readers)  |  UE5 adapter (plugin)  |  headless test host                     |
 +--------------------------------------------------------------------------------------------+
```

Rules of the road:

* **Content never refers to an engine.** It names archetypes and assets by stable ids; engines map those to their own
  classes and meshes. The PS2 adapter maps an archetype name back to `(db, class id)` through the `class` block the
  archetype carries.
* **Rules never include a platform or renderer header** (the same rule `docs/reference/portability.md` already sets for new
  code). They talk to the world through a small host interface.
* **The guest remains the oracle.** Each rule moved out of the guest gets a differential test against RAM dumps / recorded
  message traces from the recompiled game (`docs/reference/stability.md`, `scripts/harness`).
* **Editor and exporters write DCF; engines read DCF.** The editor keeps its richer internal model (`LevelDocument` with LVL
  slots and undo) and serialises into DCF; it does not adopt DCF as its edit format.

## 4. The content model

Concept by concept: where the data lives now, what DCF 1.0 carries, what is next, and the UE5 counterpart.

| Concept | Source today | DCF 1.0 (phase 1, implemented) | Later | UE5 mapping |
| --- | --- | --- | --- | --- |
| Level | `.LVL` header + object list, `LevelDocument` (`bounds`, `level`) | `scene.level` (name, stem, language, bounds), `coordinates` block (unit, up axis, cell size, rotation convention) | terrain layers and lighting as asset refs (phase 2, from the glTF exporter) | `UWorld` / sublevel + a `UDrakanLevelData` data asset |
| Object instance | LVL object: id, class, position, rotation, optional scale, parameters, link list | `instances[]`: `id` (the LVL object id, stable), `archetype`, `transform` (position, Euler, quaternion, scale), `properties` (only differences from the archetype default), `mesh` override | tags, layers, folders, editor groups | `AActor` spawned from a Blueprint class, properties set from the map |
| Class / archetype | ODB class record + 0x0103 field list (`SchemaProvider.schemaFor`) | `archetypes[]`: stable name `<ODB>/<class name>` (`#<id>` only when the ODB repeats a name), `class {classId, db, rflClass}`, kind, logic role, default mesh, typed `properties[]` with defaults, category, enum item names and a `message` marker | inheritance (the ODB has groups, not parents; derive from ODB group ids if useful), behaviour tags | Blueprint class per archetype generated by the importer, or one generic actor + a `UDrakanArchetype` data asset (decision in phase 5) |
| Typed properties | field type word (int, float, class, model, sound, bool, enum, channel, animation, string, sequence, layer, texture, colour, arrays) | `type` as a name, `default`, array flag, refs as `AssetRef` / `ClassRef`, colour as `{rgb, alpha}`, bool as boolean | validation ranges where the game data states them (it rarely does) | `UPROPERTY` of the matching type on the generated class, or a `TMap<FName, FDrakanValue>` |
| Links and messages | per-object link list + the message stored in a class message field | `links[]` (`from`, `to`, the messages the source sends), top-level `messages[]` enum table | message semantics per class as data (phase 3) | message bus component on actors; links become soft references |
| Triggers / logic objects | `LogicClass` roles + volumes (`editor-core/src/logic.ts`) | `triggers[]`: role, box / sphere volumes in game units, `sends`, `listens`, always-accepted messages, played sequences | executable semantics (phase 3) | `ATriggerVolume` + `UDrakanTriggerComponent` |
| Sequences / cinematics | `.SSD` records, actor action grammar (`story/ssdactions.ts`) | `sequences[]`: actors, typed actions (transform, animation, sound, message, camera, hold, marker, unknown with raw values) with times in seconds | camera-path simplification, retargeting hints | `ULevelSequence` built by the importer (transform tracks, audio tracks, event track for messages) |
| Dialogue | dialog line classes, conversation handlers, sound descriptors with subtitle text | `dialogue.lines[]` (speaker, subtitle text, sound refs, animations, camera), `dialogue.handlers[]` (placed handler, five phases, message sent per phase, gold / item refs) | per-language text tables; item names resolved | `UDataTable` rows + `UDialogueWave` / sound cue references; handler actor with a small state machine |
| Quests | System.ODB quest classes + XDB journal strings | `quests[]`: id, name, kind (map / journal / both), optional flag, chapter, journal and map text, map position | quest state transitions (they are driven by messages 30 "Quest Initiate" / 18 "Quest Complete" and handlers), reward items | quest data assets + a quest subsystem |
| Items / inventory | Resources ODB classes (`gameui` `ItemCategory`, footprints, quantities) | not yet; item refs in handlers are `<container>:<id>` strings | `items[]` with category, footprint, quantity kind, details text keys | item data assets + inventory component |
| Vendors | shop stock read by `gameui` from the guest | not yet | `vendors[]` bound to instances | vendor component + data asset |
| Menus | `gameui` `Layout` / `Screen` models, `settings` schema | not yet; already std-only models | `menus[]` as screen graphs (the models in `runtime/gameui` are the starting point) | UMG widgets generated from the models, or hand written against the same model |
| Materials | TXD textures, GS state; the glTF export writes base-colour materials `M_<Scope>_<ref>` and has a resolver hook for the UE5 PBR set (`M_<Name>`, `T_<Name>_BC / _N / _ORM`, `unreal-export.md`) | none: `AssetRef.kind` `texture` / `material` reserved; refs by id, `path` filled when an exporter publishes the asset | material table per mesh, from the PBR exporter | `UMaterialInstance` built from the exporter's output |

### 4.1 Stability and versioning rules

* `format: "drakan-content"`, `version: "major.minor"`. A minor bump only adds optional keys; consumers ignore unknown
  keys of a newer minor. A major bump may change meaning and gets a new schema file (`drakan-content-2.schema.json`).
* Instance ids are LVL object ids. The editor keeps ids stable across edits and duplicates get fresh ones, so links,
  triggers, sequences and handlers stay valid when a level is re-exported.
* Archetype names are `<ODB>/<class name>`; they do not depend on which classes a level uses (the `#id` suffix depends on
  the ODB only). A mod that adds classes adds names; it cannot change an existing one.
* Asset ids are the disc's own `<container>:<resource id>`. They never change between exports of the same disc; a path
  is optional and only present when an asset exporter published that resource.
* DCF holds **no game bytes**: no vertex data, texels, samples or code. Subtitle and journal text is game text and is
  exported to the user's own disk; DCF files are never committed (they sit under the same rule as saves and captures).

## 5. The rules layer

### 5.1 What exists as portable rules

* `runtime/net`: match lifecycle, damage claim validation, score, respawn, game modes (`GameModes`), arena maps
  (`ArenaMaps`), co-op relay rules, LAN discovery. Std-only, deterministic with an explicit clock, tests with a loopback
  transport. This is the model for the rest: a library with a host interface and conformance tests.
* `runtime/gameui` models: inventory grid and footprints, details lines, shop, map, pause and save screens as plain data
  filled by `Reader` from guest memory. Replacing the reader with a DCF-backed source gives the same screens in another
  engine.
* `editor-core/src/logic.ts`: roles, ports, volumes, message names. Executable only as a graph builder and validator.

### 5.2 What still runs only in the recompiled guest

Message dispatch (object receives message N, acts, sends its own to its links), trigger detection, doors, counters, timers,
levers, visibility togglers, conversation handler state, quest state, creature AI, combat resolution, physics, camera,
animation playback, cut scene playback. The documentation of these is partial: roles and ports are known (`LOGIC_CLASSES`,
level-format.md 15), the exact timing and edge cases are not.

### 5.3 Approach: executable specification, one rule at a time

1. **Message bus first.** A std-only `runtime/rules` library with `MessageBus`, `LogicObject` and the host interface
   (`now()`, `playerPosition()`, `spawn()`, `playSequence(id)`, `startConversation(id)`, `setQuest(id, state)`). It loads a DCF
   scene (links, triggers, properties) and implements the CORE roles already described in `LOGIC_CLASSES`: detector,
   timer, counter, relay, switch, door, cut scene player, conversation handler (state machine of the five phases), quest
   sender, visibility toggler, exit.
2. **Conformance by recording.** A guest hook (observer only, like `runtime/world`) logs `(time, source object id, message,
   target object id)` for a played level. The rules library replays the same player input (position trace for detectors,
   death events for counters) over the same DCF scene and must produce the same message trace. Mismatches are either rule
   bugs or undecoded behaviour, and either way become entries in level-format.md 15.
3. **Then the content-driven rules the netcode already has** (arena pickups, objectives) are moved onto the same host
   interface so a UE5 adapter implements one interface.
4. **Combat, AI and physics stay out of scope** for the portability target until the above works. For a UE5 version they are
   re-authored on UE5 systems using the archetype properties (health, damage, speeds) as the tuning data. The properties
   are already in DCF.

## 6. Loading the same content in UE5

* Package layout produced by the editor (phase 1): `manifest.json`, `scenes/<level>.scene.json`,
  `schema/drakan-content.schema.json`.
* **Importer** (phase 5): a UE editor commandlet or Python script (`DrakanContentImporter`) that reads a package plus the
  asset index from the glTF / PBR exporters and creates: a level with one actor per instance, a data asset per archetype,
  level sequences, data tables for dialogue and quests, trigger volumes with the message component. It never needs the disc
  or any `ps2xRuntime` code.
* **Coordinates**: DCF positions are game units, y up, with Euler XYZ radians plus a quaternion, the same numbers the glTF
  level export writes into its node extras (`extras.drakan.gamePos`, `rot`). The conversion to Unreal (axis swap, centimetres)
  is already specified and implemented by the glTF / Unreal export ([`../reference/unreal-export.md`](../reference/unreal-export.md),
  `UE_FROM_GLTF`); the DCF importer reuses that one mapping instead of defining a second one. That document states the
  mapping was **not verified in a UE install**; phase 5 verifies it by importing a level and comparing instance positions.
* **Join key between the two outputs**: a DCF instance `id` equals the glTF node's `extras.drakan.id` (node name
  `<Class>_<id>`), and an archetype's class name is the glTF mesh name suffix (`SM_<Class>`, `SK_<Class>`). So the importer
  can spawn actors from the glTF scene and attach DCF properties, links, triggers and story to them by id, without DCF
  carrying any mesh path.
* **Assets**: meshes, textures and materials come from the exporters; DCF references them by id and, once an exporter
  publishes an index, by relative path. The importer resolves id to path through the exporter's index
  (`manifest.assetIndex`), so the content format does not need to know the exporter's file naming.

## 7. Phases

Each phase ends with something runnable and a stated check.

| Phase | Deliverable | Check |
| --- | --- | --- |
| **1. Content format and exporter** (done 2026-11-01) | `drakan-content.schema.json` + TS types + `buildContentScene` / `buildContentPackage` in `editor-core/src/content`, "Export > Engine-neutral package" in the editor, `docs/reference/content-format.md`. Level instances, archetypes, links, triggers, sequences, dialogue, quests | Unit tests on synthetic fixtures; the real Surdana level exported from the player's disc in a browser and validated against the shipped schema with an independent validator (Ajv); see the journal note |
| **2. Complete the content model** | Items, vendors, menus and level-level data (terrain layers, lights, music) in DCF 1.1; asset index consumed from the glTF / PBR exporters (fills `AssetRef.path`); DCF export of all 14 levels in one package; a DCF reader in `editor-core` so a package can be loaded back as a `LevelDocument` (round trip) | Round trip test: disc level to DCF to `LevelDocument` to LVL equals the original object list byte for byte on all 14 levels |
| **3. Rules extraction** | `runtime/rules`: message bus + CORE roles + conversation and quest state, host interface, a headless host | Message trace conformance against recorded guest traces for Surdana and one more level; unresolved differences documented |
| **4. Headless simulation and content tests** | The rules + a DCF scene run without a guest: scripted player paths trigger doors, counters, conversations and quests; CI runs them on synthetic scenes and, locally, on disc-derived scenes | A level's quest chain can be completed by a script in the headless host |
| **5. UE5 loader** | UE plugin `DrakanContent` (importer, data assets, trigger component, quest and dialogue subsystems), coordinate conversion, level sequence builder | Surdana imported in UE5 with the exporters' meshes: instance positions match the glTF export within tolerance; Rynn can walk to the first trigger and the rules host fires the first conversation |
| **6. Gameplay parity (optional, long)** | Combat, AI and creature behaviour re-authored on UE5 systems using archetype properties; differential comparison against the guest for the numbers that matter (damage, health, speeds) | Per-feature comparison tables in the reference docs |

Phases 2 and 3 are independent and can run in parallel; 4 needs 3; 5 needs 2 and benefits from 3.

## 8. Risks and open questions

* **Undecoded behaviour.** Class fields have names and defaults but not always meaning. DCF exports them faithfully
  (`unknown` types stay `unknown`, undecoded sequence actions keep their raw values), so nothing is lost, but phase 3 will
  find rules the docs do not cover. Mitigation: conformance by recording, and the rule that every mismatch becomes a
  documented finding.
* **Mod content.** Mods that add classes or levels (Order of the Flame converter) need their archetypes in DCF: the
  exporter reads the same ODB tables, so a converted level exports like a disc level. Unverified on converted levels.
* **Sequences use raw game space.** Instance rotations carry the editor's yaw convention; sequence keyframes do not. This
  is stated in the format and must be reconciled by the importer; phase 5 verifies it against a played cut scene.
* **Text and localisation.** Subtitle and journal text come from the disc's language tree; a package is one language.
  Multi-language packages need the per-language tables of phase 2.
* **Size.** A level exports roughly one archetype per class used plus the classes their defaults reference; Surdana's
  numbers are in the journal note. Large levels may want chunked scene files; the manifest already lists scenes by path.
* **Where the rules live in the repo** (phase 3): a new `runtime/rules` library following the `runtime/<lib>` layout, with its
  own tests, so `ps2recomp/ps2xRuntime` links it like every other library.
