/**
 * Drakan Content Format (DCF), version 1.0: an engine-neutral description of one level and the story content that
 * belongs to it. It contains no game bytes: no meshes, textures, audio or code, only names, numbers, references and
 * text. Heavy assets are referenced by stable id (`AssetRef.id`) and, when an exporter produced them, by relative
 * path (`AssetRef.path`). The normative description is `schema/drakan-content.schema.json`; the architecture and the
 * phase plan are in docs/proposals/engine-portability.md, the field-by-field reference in docs/reference/content-format.md.
 *
 * Conventions:
 * - Positions are game units, y up, exactly as the editor and the world dump show them (1 cell = 2048 units).
 * - Instance rotations are Euler XYZ radians in the editor's document convention (the LVL yaw offset of -90 degrees
 *   is already applied) plus the same rotation as a quaternion (x y z w). Sequence keyframes are the raw game space values.
 * - Every cross reference between objects uses a number (instance id = LVL object id) or a string key (archetype name).
 * - Names that matter to other engines (archetypes, assets, sequences) are stable across editor sessions and levels.
 */

export const CONTENT_FORMAT = 'drakan-content';
/** `major.minor`: a minor bump only adds optional keys; a major bump may change meaning. */
export const CONTENT_VERSION = '1.0';

export type DcfVec3 = [number, number, number];
export type DcfQuat = [number, number, number, number];

export type AssetKind = 'mesh' | 'texture' | 'material' | 'sound' | 'animation' | 'sequence' | 'channel' | 'layer';

/**
 * A reference to something that lives outside this file. `id` is `<container>:<resource id>` as the disc names it
 * (`RESOURES:128`, `SOUND:4`, `LEVEL1ON:3012`); it is stable and independent of any converted file. `path` is relative to
 * the package root and is only present when an asset exporter published that resource (glTF meshes, PBR materials).
 */
export interface AssetRef {
    kind: AssetKind;
    id: string;
    path?: string;
    /** Informational: the name the game data gives the resource, when it has one. */
    name?: string;
}

/** A reference to an archetype (class). `archetype` is present when the class is part of this document. */
export interface ClassRef {
    class: string;
    archetype?: string;
}

export interface ColourValue {
    /** `#rrggbb` */
    rgb: string;
    /** 0 to 255. */
    alpha: number;
}

export type PropertyType =
    | 'int'
    | 'float'
    | 'bool'
    | 'enum'
    | 'string'
    | 'colour'
    | 'class'
    | 'model'
    | 'sound'
    | 'animation'
    | 'sequence'
    | 'texture'
    | 'layer'
    | 'channel'
    | 'unknown';

export type PropertyValue = number | boolean | string | ColourValue | AssetRef | ClassRef | null | PropertyValue[];

export interface PropertyDef {
    /** Unique inside the archetype (`name`, or `name#index` when the class repeats a name). */
    key: string;
    /** The field name the game data gives it. */
    name: string;
    /** Position in the class field list (what the LVL parameters are keyed by). */
    index: number;
    type: PropertyType;
    array?: boolean;
    default: PropertyValue;
    category: string;
    /** Item names of an enum, when the game's property table lists them. */
    enumNames?: string[];
    /** `message`: the value indexes `DcfScene.messages`. */
    semantic?: 'message';
}

export type EntityKindName = 'player' | 'camera' | 'creature' | 'weapon' | 'usable' | 'static' | 'other';

export interface Archetype {
    /** Stable name, `<ODB>/<class name>`, with `#<class id>` appended when the ODB repeats the name. */
    name: string;
    class: {
        classId: number;
        /** ODB container the class lives in (`NPCS`, `RESOURCES`, or the level's own). */
        db: string;
        /** Low 16 bits of the runtime class (the engine's code class), when known. */
        rflClass?: number;
    };
    /** Display label of the ODB. */
    category: string;
    kind: EntityKindName;
    /** Logic role (trigger, door, conversation handler ...) when the game logic gives the class one. */
    role?: string;
    /** The default model, when the class has one. */
    mesh?: AssetRef;
    properties: PropertyDef[];
}

export interface DcfTransform {
    position: DcfVec3;
    rotation: DcfVec3;
    quaternion: DcfQuat;
    scale?: DcfVec3;
}

export interface Instance {
    /** LVL object id: unique in the level and used by links, triggers, sequences and dialogue. */
    id: number;
    name: string;
    archetype: string;
    transform: DcfTransform;
    /** Only the values that differ from the archetype default, by `PropertyDef.key`. */
    properties?: Record<string, PropertyValue>;
    /** Present when the instance's model differs from its archetype's. */
    mesh?: AssetRef;
}

export interface MessageSend {
    field: string;
    label: string;
    message: number;
    name: string;
}

export interface Link {
    from: number;
    to: number;
    /** What the source sends to every object it links to. */
    messages: MessageSend[];
}

export interface BoxVolumeOut {
    shape: 'box';
    center: DcfVec3;
    /** Half extents in game units. */
    halfExtents: DcfVec3;
    rotation: DcfVec3;
}
export interface SphereVolumeOut {
    shape: 'sphere';
    center: DcfVec3;
    radius: number;
    /** The property key the radius comes from. */
    field: string;
}

export interface Trigger {
    instance: number;
    role: string;
    title: string;
    volumes?: (BoxVolumeOut | SphereVolumeOut)[];
    sends: MessageSend[];
    listens: MessageSend[];
    /** Message numbers the class always understands. */
    accepts?: number[];
    /** Cut scene players: the sequence ids they play (`DcfScene.sequences`). */
    sequences?: number[];
}

export type SequenceAction =
    | { kind: 'transform'; time: number; position: DcfVec3; rotation: DcfQuat; scale: DcfVec3 }
    | { kind: 'animation'; time: number; animation: number; mode: number; offset: number; speed: number; flags: number }
    | { kind: 'sound'; time: number; sound: AssetRef; volume: number }
    | { kind: 'message'; time: number; message: number; name: string; param: number }
    | { kind: 'camera'; time: number; fov: number; unknown: number }
    | { kind: 'marker' | 'hold'; time: number }
    | { kind: 'unknown'; time: number; type: number; values: Record<string, number> };

export interface SequenceActor {
    name: string;
    slot: number;
    /** The actor's `ref`: an instance id when `target` is `instance`, a class id for `class` (the player, the camera). */
    ref: number;
    target: 'instance' | 'class';
    actions: SequenceAction[];
}

export interface Sequence {
    id: number;
    name: string;
    folder?: string;
    /** Header length of the sequence (inferred to be seconds). */
    length: number;
    actors: SequenceActor[];
    /** The action bytes of at least one actor did not follow the known grammar (kept out of this file). */
    partial?: boolean;
}

export interface DialogueLine {
    /** The class name (`Farmer - C103FA01`), unique in the level. */
    name: string;
    classId: number;
    speaker: string;
    /** Subtitle of the first sound that has one. */
    text: string;
    sounds: AssetRef[];
    animations: number[];
    delay: number;
    playMode: number;
    camera: { position: number; transition: number; transitionTime: number };
}

export interface ConversationPhase {
    key: string;
    label: string;
    /** Names of `DcfScene.dialogue.lines`, played in order. */
    lines: string[];
    message: number;
    messageName: string;
}

export interface ConversationHandler {
    /** Instance that holds the handler. */
    instance: number;
    archetype: string;
    initiallyActive: boolean;
    distance: number;
    hasQuest: boolean;
    gold: number;
    phases: ConversationPhase[];
    /** Item class refs (`<container>:<id>`) the handler takes from / gives to the player. */
    items: { getBegin: string[]; giveBegin: string[]; getComplete: string[]; giveComplete: string[] };
}

export interface Quest {
    /** Stable key: the quest class id. */
    id: number;
    name: string;
    kind: 'map' | 'journal' | 'both';
    optional: boolean;
    chapter: number;
    chapterName: string;
    /** Objective sentence of the journal, in the language of the disc the package was made from. */
    journal?: string;
    /** Place name on the map. */
    mapText?: string;
    map?: { x: number; y: number };
}

export interface Diagnostics {
    objects: number;
    unresolvedClasses: number;
    danglingLinks: number;
    opaqueSequenceActors: number;
    missingSubtitles: number;
}

export interface DcfScene {
    format: typeof CONTENT_FORMAT;
    version: string;
    kind: 'scene';
    generator: { name: string; version: string };
    level: {
        name: string;
        /** The level folder holding the `.LVL` (`LEVEL1NA`), when known. */
        stem?: string;
        /** The container that holds the level's story data (`LEVEL1ON`: its ODB, SSD and SDB), when it has one. */
        storyStem?: string;
        language?: string;
        /** Playable extent in game units (x, z), when known. */
        bounds?: { minX: number; maxX: number; minZ: number; maxZ: number };
    };
    coordinates: {
        unit: 'game-unit';
        up: 'y';
        cellSize: 2048;
        rotation: 'euler-xyz-radians';
    };
    /** The game's message enum, indexed by message number (`MessageSend.message`). */
    messages: string[];
    archetypes: Archetype[];
    instances: Instance[];
    links: Link[];
    triggers: Trigger[];
    sequences: Sequence[];
    dialogue: { lines: DialogueLine[]; handlers: ConversationHandler[] };
    quests: Quest[];
    diagnostics: Diagnostics;
}

export interface DcfManifest {
    format: typeof CONTENT_FORMAT;
    version: string;
    kind: 'package';
    generator: { name: string; version: string };
    /** Disc the content was read from; the package carries no part of it. */
    source?: { profile?: string; language?: string };
    scenes: { name: string; path: string }[];
    schema: string;
    /** Where an asset exporter's index lives, relative to the package root, when the package has one. */
    assetIndex?: string;
}
