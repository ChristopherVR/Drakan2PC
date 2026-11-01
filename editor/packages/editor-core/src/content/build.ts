import {
    CORE_ROLES,
    MESSAGE_NAMES,
    actionField,
    describeMessage,
    enumNames,
    isMessageField,
    lineClassId,
    logicClass,
    messageName,
    parseActions,
    parseConversationHandler,
    refParts,
    type LevelStory,
    type QuestClass,
    type SsdAction,
    type XdbString,
} from '@drakan/formats';
import { resourceParts, type AssetClass } from '../catalog.ts';
import type { DocField, LevelDocument, LevelObject } from '../document.ts';
import { logicOf, outgoingMessages, volumesOf, type LogicEnv } from '../logic.ts';
import {
    arrayElements,
    argbToParts,
    effectiveValue,
    isArrayType,
    typeCode,
    type ClassSchema,
    type RefTarget,
} from '../schema.ts';
import {
    CONTENT_FORMAT,
    CONTENT_VERSION,
    type Archetype,
    type AssetKind,
    type AssetRef,
    type ConversationHandler,
    type DcfScene,
    type DialogueLine,
    type Instance,
    type Link,
    type MessageSend,
    type PropertyDef,
    type PropertyType,
    type PropertyValue,
    type DcfQuat,
    type Quest,
    type Sequence,
    type SequenceAction,
    type Trigger,
    type DcfVec3,
} from './format.ts';

/** What the exporter needs from the class schemas of the open level (`SchemaProvider` satisfies it). */
export interface ContentEnv extends LogicEnv {
    resolveRef(owner: ClassSchema, type: number, value: number): RefTarget;
    /** Model of the class record as a reference word (0 = none); `SchemaProvider.modelWord`. Optional for stubs. */
    modelWord?(resource: string): number;
}

export type StoryInput = Pick<LevelStory, 'stem' | 'deps' | 'folders' | 'sequences' | 'sounds' | 'lines' | 'classes'>;

export interface QuestInput {
    classes: readonly QuestClass[];
    strings: readonly XdbString[];
    /** XDB language slot of the disc (0 English, ...). */
    slot: number;
}

export interface ContentInput {
    doc: LevelDocument;
    env: ContentEnv;
    /** Every class the level's ODBs define (names, groups, instance counts). */
    catalog: readonly AssetClass[];
    /** Level title shown in the editor (`Surdana`). */
    levelName: string;
    /** The level folder (`LEVEL1NA`). */
    stem?: string;
    story?: StoryInput | null;
    quests?: QuestInput | null;
    language?: string;
    generator?: { name: string; version: string };
    /** Path of a converted asset (published by a glTF / material exporter), or undefined when there is none. */
    assetPath?: (kind: AssetKind, id: string) => string | undefined;
}

export const GENERATOR = { name: 'drakan-editor', version: '1' };

/** Nine significant digits: enough for an f32 and short in JSON. */
const num = (v: number): number => (Number.isFinite(v) ? Number(v.toPrecision(9)) : 0);
const vec3 = (v: readonly number[]): DcfVec3 => [num(v[0] ?? 0), num(v[1] ?? 0), num(v[2] ?? 0)];

/** Quaternion (x y z w) of an Euler XYZ rotation in three.js order (R = Rx Ry Rz). */
export function eulerToQuat(rot: readonly number[]): DcfQuat {
    const [x, y, z] = [rot[0] ?? 0, rot[1] ?? 0, rot[2] ?? 0];
    const c1 = Math.cos(x / 2);
    const c2 = Math.cos(y / 2);
    const c3 = Math.cos(z / 2);
    const s1 = Math.sin(x / 2);
    const s2 = Math.sin(y / 2);
    const s3 = Math.sin(z / 2);
    const q: DcfQuat = [
        s1 * c2 * c3 + c1 * s2 * s3,
        c1 * s2 * c3 - s1 * c2 * s3,
        c1 * c2 * s3 + s1 * s2 * c3,
        c1 * c2 * c3 - s1 * s2 * s3,
    ];
    return q.map((n) => Number(n.toFixed(7))) as DcfQuat;
}

const PROP_TYPES: Record<number, PropertyType> = {
    1: 'int',
    2: 'float',
    3: 'class',
    4: 'model',
    5: 'sound',
    6: 'bool',
    7: 'enum',
    8: 'channel',
    9: 'animation',
    0x0a: 'string',
    0x0b: 'sequence',
    0x0c: 'layer',
    0x0d: 'unknown',
    0x0e: 'texture',
    0x0f: 'colour',
};
export const propertyType = (type: number): PropertyType => PROP_TYPES[typeCode(type)] ?? 'unknown';

const REF_KIND: Partial<Record<PropertyType, AssetKind>> = {
    model: 'mesh',
    sound: 'sound',
    animation: 'animation',
    sequence: 'sequence',
    texture: 'texture',
    layer: 'layer',
    channel: 'channel',
};

const safeStem = (s: string | null | undefined, db: number): string => (s ? s.replace(/[\s:]+/g, '_') : `DB${db}`);

export const levelSlug = (name: string): string =>
    name
        .toLowerCase()
        .replace(/[^a-z0-9]+/g, '-')
        .replace(/^-+|-+$/g, '') || 'level';

class Exporter {
    readonly env: ContentEnv;
    readonly byResource = new Map<string, AssetClass>();
    readonly dupes = new Map<string, number>();
    readonly archetypes = new Map<string, Archetype>();
    readonly archetypeOf = new Map<string, string>();
    readonly meshKeyOf = new Map<string, string>();
    readonly queue: string[] = [];

    constructor(readonly input: ContentInput) {
        this.env = input.env;
        for (const c of input.catalog) {
            this.byResource.set(c.resource, c);
            const k = `${c.db}/${c.name}`;
            this.dupes.set(k, (this.dupes.get(k) ?? 0) + 1);
        }
    }

    get stem(): string | undefined {
        return this.input.story?.stem;
    }

    /** Stable archetype name of a class resource; registers the class for export. */
    archetypeName(resource: string, fallbackName = ''): string {
        const known = this.archetypeOf.get(resource);
        if (known) return known;
        const c = this.byResource.get(resource);
        let name: string;
        if (c) {
            name = `${c.db}/${c.name}`;
            if ((this.dupes.get(name) ?? 0) > 1) name += `#${c.classId}`;
        } else {
            const { dbIndex, classId } = resourceParts(resource);
            name = `unresolved/${fallbackName || 'class'}#${dbIndex}.${classId}`;
        }
        this.archetypeOf.set(resource, name);
        this.queue.push(resource);
        return name;
    }

    private refId(kind: AssetKind, owner: ClassSchema, t: RefTarget): string {
        const stem = kind === 'sequence' && t.db === 0 && this.stem ? this.stem : t.stem;
        return `${safeStem(stem, t.db)}:${t.id}`;
    }

    asset(kind: AssetKind, id: string, name?: string): AssetRef {
        const out: AssetRef = { kind, id };
        const path = this.input.assetPath?.(kind, id);
        if (path) out.path = path;
        if (name) out.name = name;
        return out;
    }

    refValue(owner: ClassSchema, type: number, word: number): PropertyValue {
        if (word === 0) return null;
        const t = this.env.resolveRef(owner, type, word);
        const pt = propertyType(type);
        if (pt === 'class') {
            const id = `${safeStem(t.stem, t.db)}:${t.id}`;
            return t.resource ? { class: id, archetype: this.archetypeName(t.resource, t.name) } : { class: id };
        }
        const kind = REF_KIND[pt];
        return kind ? this.asset(kind, this.refId(kind, owner, t)) : word;
    }

    value(owner: ClassSchema, type: number, v: DocField['value']): PropertyValue {
        const pt = propertyType(type);
        const isRef = pt === 'class' || REF_KIND[pt] !== undefined;
        if (isArrayType(type)) {
            const words = Array.isArray(v) ? v : [];
            if (isRef) return words.map((w) => this.refValue(owner, type, w));
            if (pt === 'bool') return words.map((w) => w !== 0);
            if (pt === 'float') return arrayElements(type, words).map(num);
            return arrayElements(type, words);
        }
        if (Array.isArray(v)) return v.map((w) => w);
        if (isRef) return typeof v === 'number' ? this.refValue(owner, type, v >>> 0) : null;
        switch (pt) {
            case 'float':
                return num(Number(v));
            case 'bool':
                return Number(v) !== 0;
            case 'string':
                return String(v);
            case 'colour': {
                const { hex, alpha } = argbToParts(Number(v));
                return { rgb: hex, alpha };
            }
            default:
                return typeof v === 'number' ? v : Number(v) || 0;
        }
    }

    /** One archetype from a class resource (needs the schema; classes without one export with no properties). */
    buildArchetype(resource: string): Archetype {
        const name = this.archetypeOf.get(resource)!;
        const c = this.byResource.get(resource);
        const schema = this.env.schemaFor(resource);
        const rfl = this.env.rflOf(resource);
        const counts = new Map<string, number>();
        for (const f of schema?.fields ?? []) counts.set(f.name, (counts.get(f.name) ?? 0) + 1);
        const properties: PropertyDef[] = [];
        let meshKey: string | undefined;
        let mesh: AssetRef | undefined;
        for (const f of schema?.fields ?? []) {
            const key = (counts.get(f.name) ?? 0) > 1 ? `${f.name}#${f.index}` : f.name;
            const pt = propertyType(f.type);
            const def: PropertyDef = {
                key,
                name: f.name,
                index: f.index,
                type: pt,
                default: f.unknownDefault ? null : this.value(schema!, f.type, f.default),
                category: f.category,
            };
            if (isArrayType(f.type)) def.array = true;
            if (isMessageField(f.name, f.type)) def.semantic = 'message';
            else {
                const names = enumNames(rfl, f.name, f.type);
                if (names) def.enumNames = [...names];
            }
            if (pt === 'model' && !def.array && !meshKey && def.default && typeof def.default === 'object') {
                meshKey = key;
                mesh = def.default as AssetRef;
            }
            properties.push(def);
        }
        if (!mesh && schema) {
            const w = this.env.modelWord?.(resource) ?? 0;
            const v = w ? this.refValue(schema, 4, w) : null;
            if (v && typeof v === 'object' && !Array.isArray(v) && 'kind' in v) mesh = v;
        }
        if (meshKey) this.meshKeyOf.set(resource, meshKey);
        const spec = logicClass(rfl);
        const a: Archetype = {
            name,
            class: {
                classId: c?.classId ?? resourceParts(resource).classId,
                db: c?.db ?? schema?.db ?? 'unknown',
                ...(rfl !== null && rfl !== undefined ? { rflClass: rfl } : {}),
            },
            category: c?.category ?? '',
            kind: c?.kind ?? 'other',
            ...(spec ? { role: spec.role } : {}),
            ...(mesh ? { mesh } : {}),
            properties,
        };
        return a;
    }
}

function sendsOf(o: LevelObject, env: ContentEnv, ports: 'out' | 'in'): MessageSend[] {
    const spec = logicOf(o, env);
    if (!spec) return [];
    if (ports === 'out') {
        return outgoingMessages(o, env, spec).map((m) => ({
            field: m.field,
            label: m.label,
            message: m.value,
            name: m.name,
        }));
    }
    const schema = env.schemaFor(o.resource);
    const out: MessageSend[] = [];
    for (const p of spec.in) {
        const f = schema?.fields.find((x) => x.name === p.field);
        if (!f || !isMessageField(f.name, f.type)) continue;
        const v = effectiveValue(o, f).value;
        if (typeof v === 'number') out.push({ field: f.name, label: p.label, message: v, name: messageName(v) });
    }
    return out;
}

const CORE = new Set<string>(CORE_ROLES);

function convertAction(x: SsdAction, ex: Exporter): SequenceAction {
    const t = num(x.time);
    const f = (n: string): number => actionField(x, n);
    switch (x.kind) {
        case 'transform':
            return {
                kind: 'transform',
                time: t,
                position: vec3([f('x'), f('y'), f('z')]),
                rotation: [num(f('qx')), num(f('qy')), num(f('qz')), num(f('qw'))],
                scale: vec3([f('sx'), f('sy'), f('sz')]),
            };
        case 'animation':
            return {
                kind: 'animation',
                time: t,
                animation: f('animation'),
                mode: f('mode'),
                offset: num(f('offset')),
                speed: num(f('speed')),
                flags: f('flags'),
            };
        case 'sound': {
            const { db, id } = soundParts(f('sound'));
            return {
                kind: 'sound',
                time: t,
                sound: ex.asset('sound', `${safeStem(db === 0 ? ex.stem : ex.input.story?.deps[db], db)}:${id}`),
                volume: f('params') & 0xffff,
            };
        }
        case 'message':
            return {
                kind: 'message',
                time: t,
                message: f('message'),
                name: describeMessage(f('message')),
                param: f('param'),
            };
        case 'camera':
            return { kind: 'camera', time: t, fov: f('b'), unknown: num(f('a')) };
        case 'marker':
        case 'hold':
            return { kind: x.kind, time: t };
        default:
            return {
                kind: 'unknown',
                time: t,
                type: x.type,
                values: Object.fromEntries(x.fields.map((fl) => [fl.name, num(fl.value)])),
            };
    }
}

const soundParts = (ref: number): { db: number; id: number } => {
    const p = refParts(ref);
    return { db: p.db, id: p.id };
};

/** Build the engine-neutral scene of one level. Pure: the same input gives the same JSON. */
export function buildContentScene(input: ContentInput): DcfScene {
    const ex = new Exporter(input);
    const { doc, env } = input;
    const objects = [...doc.objects()].sort((a, b) => a.id - b.id);

    // ---- instances (registers their archetypes) ----
    const instances: Instance[] = [];
    for (const o of objects) {
        const arch = ex.archetypeName(o.resource, o.class);
        const schema = env.schemaFor(o.resource);
        const props: Record<string, PropertyValue> = {};
        if (schema) {
            const counts = new Map<string, number>();
            for (const f of schema.fields) counts.set(f.name, (counts.get(f.name) ?? 0) + 1);
            for (const f of o.fields ?? []) {
                const sf = schema.fields.find((x) => x.index === f.fieldIndex);
                if (!sf) continue;
                const key = (counts.get(sf.name) ?? 0) > 1 ? `${sf.name}#${sf.index}` : sf.name;
                props[key] = ex.value(schema, sf.type, f.value);
            }
        }
        const inst: Instance = {
            id: o.id,
            name: `${o.class || 'Object'} #${o.id}`,
            archetype: arch,
            transform: {
                position: vec3(o.transform.pos),
                rotation: vec3(o.transform.rot),
                quaternion: eulerToQuat(o.transform.rot),
                ...(o.scale ? { scale: vec3(o.scale) } : {}),
            },
            ...(Object.keys(props).length ? { properties: props } : {}),
        };
        instances.push(inst);
    }

    // ---- archetypes (instantiated classes plus the classes their class-typed values name) ----
    const archetypes: Archetype[] = [];
    for (let i = 0; i < ex.queue.length && i < 5000; i++) archetypes.push(ex.buildArchetype(ex.queue[i]!));
    archetypes.sort((a, b) => a.name.localeCompare(b.name));
    // instance-level model overrides
    const archByName = new Map(archetypes.map((a) => [a.name, a]));
    for (const inst of instances) {
        const a = archByName.get(inst.archetype);
        const o = doc.get(inst.id)!;
        const key = ex.meshKeyOf.get(o.resource);
        const v = key ? inst.properties?.[key] : undefined;
        if (v && typeof v === 'object' && !Array.isArray(v) && 'kind' in v && a?.mesh && v.id !== a.mesh.id)
            inst.mesh = v as AssetRef;
    }

    // ---- links and triggers ----
    const links: Link[] = [];
    const triggers: Trigger[] = [];
    let dangling = 0;
    for (const o of objects) {
        const sends = sendsOf(o, env, 'out');
        for (const to of o.links ?? []) {
            if (!doc.has(to)) {
                dangling++;
                continue;
            }
            links.push({ from: o.id, to, messages: sends });
        }
        const spec = logicOf(o, env);
        if (!spec || !CORE.has(spec.role)) continue;
        const vols = volumesOf(o, env).map((v) =>
            v.kind === 'box'
                ? ({ shape: 'box', center: vec3(v.center), halfExtents: vec3(v.half), rotation: vec3(v.rot) } as const)
                : ({ shape: 'sphere', center: vec3(v.center), radius: num(v.radius), field: v.field } as const),
        );
        const t: Trigger = {
            instance: o.id,
            role: spec.role,
            title: spec.title,
            ...(vols.length ? { volumes: vols } : {}),
            sends,
            listens: sendsOf(o, env, 'in'),
            ...(spec.accepts?.length ? { accepts: [...spec.accepts] } : {}),
        };
        if (spec.role === 'cutscene') {
            const schema = env.schemaFor(o.resource);
            const f = schema?.fields.find((x) => x.name === 'Sequence List');
            const v = f ? effectiveValue(o, f).value : undefined;
            if (Array.isArray(v)) t.sequences = v.map((w) => w & 0xffff);
        }
        triggers.push(t);
    }

    // ---- sequences ----
    const story = input.story ?? null;
    const sequences: Sequence[] = [];
    let opaque = 0;
    for (const s of story?.sequences ?? []) {
        if (!s.seq) continue;
        let partial = false;
        const folder = story!.folders.find((f) => f.id === s.folder)?.name;
        const actors = s.seq.actors.map((a) => {
            const parsed = parseActions(a);
            if (!parsed) {
                partial = true;
                opaque++;
            }
            return {
                name: a.name,
                slot: a.slot,
                ref: a.ref,
                target: doc.has(a.ref) ? ('instance' as const) : ('class' as const),
                actions: (parsed ?? []).map((x) => convertAction(x, ex)),
            };
        });
        sequences.push({
            id: s.id,
            name: s.seq.name,
            ...(folder ? { folder } : {}),
            length: num(s.seq.length),
            actors,
            ...(partial ? { partial: true as const } : {}),
        });
    }
    sequences.sort((a, b) => a.id - b.id);

    // ---- dialogue ----
    const lines: DialogueLine[] = [];
    let missingSubtitles = 0;
    const stemOfDb = (db: number): string => safeStem(db === 0 ? story?.stem : story?.deps[db], db);
    for (const l of story?.lines ?? []) {
        let text = '';
        const sounds = l.sounds.map((ref) => {
            const { db, id } = soundParts(ref);
            const snd = db === 0 ? story!.sounds.get(id) : undefined;
            if (!text && snd?.text) text = snd.text;
            return ex.asset('sound', `${stemOfDb(db)}:${id}`, snd?.name);
        });
        if (!text) missingSubtitles++;
        lines.push({
            name: l.name,
            classId: l.classId,
            speaker: l.speaker,
            text,
            sounds,
            animations: [...l.animations],
            delay: num(l.delay),
            playMode: l.playMode,
            camera: {
                position: l.camera.position,
                transition: l.camera.transition,
                transitionTime: num(l.camera.transitionTime),
            },
        });
    }
    lines.sort((a, b) => a.classId - b.classId);
    const lineName = new Map((story?.lines ?? []).map((l) => [l.classId, l.name]));
    const handlers: ConversationHandler[] = [];
    if (story) {
        for (const o of objects) {
            const schema = env.schemaFor(o.resource);
            if (!schema || env.rflOf(o.resource) !== 0xb4 || schema.db.toUpperCase() !== story.stem.toUpperCase())
                continue;
            const cls = story.classes.get(resourceParts(o.resource).classId);
            if (!cls) continue;
            const override = new Map<string, number | number[]>();
            for (const f of o.fields ?? []) if (typeof f.value !== 'string') override.set(f.name, f.value);
            const h = parseConversationHandler(cls, override);
            if (!h) continue;
            const itemIds = (refs: number[]): string[] =>
                refs.map((r) => `${stemOfDb(refParts(r).db)}:${refParts(r).id}`);
            handlers.push({
                instance: o.id,
                archetype: ex.archetypeName(o.resource, o.class),
                initiallyActive: h.initiallyActive,
                distance: num(h.distance),
                hasQuest: h.hasQuest,
                gold: h.goldOnComplete,
                phases: h.phases.map((p) => ({
                    key: p.key,
                    label: p.label,
                    lines: p.lines
                        .filter((r) => refParts(r).db === 0)
                        .map((r) => lineName.get(lineClassId(r)))
                        .filter((n): n is string => n !== undefined),
                    message: p.message,
                    messageName: messageName(p.message),
                })),
                items: {
                    getBegin: itemIds(h.itemsToGetBegin),
                    giveBegin: itemIds(h.itemsToGiveBegin),
                    getComplete: itemIds(h.itemsToGetComplete),
                    giveComplete: itemIds(h.itemsToGiveComplete),
                },
            });
        }
    }

    // ---- quests ----
    const quests: Quest[] = [];
    if (input.quests) {
        const text = new Map(input.quests.strings.map((s) => [s.id, s.text[input.quests!.slot] || s.text[0] || '']));
        for (const q of input.quests.classes) {
            const journal = q.journalId ? text.get(q.journalId) : undefined;
            const mapText = q.mapTextId ? text.get(q.mapTextId) : undefined;
            quests.push({
                id: q.classId,
                name: q.name,
                kind: q.kind,
                optional: q.optional,
                chapter: q.chapter,
                chapterName: q.chapterName,
                ...(journal ? { journal } : {}),
                ...(mapText ? { mapText } : {}),
                ...(q.kind !== 'journal' ? { map: { x: q.mapX, y: q.mapY } } : {}),
            });
        }
        quests.sort((a, b) => a.id - b.id);
    }

    // archetypes registered while converting handlers (none: instances already named them) are covered above.
    return {
        format: CONTENT_FORMAT,
        version: CONTENT_VERSION,
        kind: 'scene',
        generator: input.generator ?? GENERATOR,
        level: {
            name: input.levelName,
            ...(input.stem ? { stem: input.stem } : {}),
            ...(story?.stem ? { storyStem: story.stem } : {}),
            ...(input.language ? { language: input.language } : {}),
            ...(doc.bounds ? { bounds: { ...doc.bounds } } : {}),
        },
        coordinates: { unit: 'game-unit', up: 'y', cellSize: 2048, rotation: 'euler-xyz-radians' },
        messages: [...MESSAGE_NAMES],
        archetypes,
        instances,
        links,
        triggers,
        sequences,
        dialogue: { lines, handlers },
        quests,
        diagnostics: {
            objects: objects.length,
            unresolvedClasses: [...ex.archetypeOf.keys()].filter((r) => !ex.byResource.has(r)).length,
            danglingLinks: dangling,
            opaqueSequenceActors: opaque,
            missingSubtitles,
        },
    };
}
