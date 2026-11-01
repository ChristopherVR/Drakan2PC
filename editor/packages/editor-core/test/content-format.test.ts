// Drakan Content Format: scene export, schema validation and package layout over synthetic classes (no game bytes).
import { strFromU8, unzipSync } from 'fflate';
import {
    addActor,
    joinActions,
    newActionBytes,
    newSequence,
    type DialogLine,
    type OdbClass,
    type QuestClass,
} from '@drakan/formats';
import { describe, expect, it } from 'vitest';
import {
    CONTENT_SCHEMA,
    CONTENT_VERSION,
    LevelDocument,
    buildContentPackage,
    buildContentScene,
    eulerToQuat,
    validateContent,
    zipContentPackage,
    type AssetClass,
    type ClassSchema,
    type ContentEnv,
    type DcfScene,
    type LevelObject,
    type RefTarget,
    type SchemaField,
    type StoryInput,
} from '../src/index.ts';

interface Stub {
    rfl: number;
    name: string;
    db: string;
    fields: { name: string; type: number; def: number | string | number[] }[];
}

const NPC = '0x20001';
const DETECTOR = '0x20002';
const PLAYER = '0x20003';
const HANDLER = '0x30004';
const PROP_A = '0x20005';
const PROP_B = '0x20006';
const CLASSES: Record<string, Stub> = {
    [NPC]: {
        rfl: 0x31,
        name: 'Farmer',
        db: 'NPCS',
        fields: [
            { name: 'Model', type: 4, def: 0x00010080 },
            { name: 'Voice', type: 5, def: 0 },
            { name: 'Tint', type: 0x0f, def: 0xff8040c0 },
            { name: 'Friendly', type: 6, def: 1 },
            { name: 'Name', type: 0x0a, def: 'Joe' },
            { name: 'Weapon', type: 3, def: 0x00010100 },
            { name: 'Speed', type: 2, def: 1.5 },
            { name: 'Gold Drops', type: 0x1001, def: [3, 4294967295] },
            { name: 'Message To Send', type: 7, def: 8 },
        ],
    },
    [DETECTOR]: {
        rfl: 0x3d,
        name: 'Trigger Detector',
        db: 'SYSTEM',
        fields: [
            { name: 'Detect Which?', type: 7, def: 2 },
            { name: 'Trigger Message', type: 7, def: 6 },
        ],
    },
    [PLAYER]: {
        rfl: 0x33,
        name: 'Stomp Player',
        db: 'SYSTEM',
        fields: [
            { name: 'Message To Play Next', type: 7, def: 4 },
            { name: 'Sequence List', type: 0x100b, def: [] },
        ],
    },
    [HANDLER]: {
        rfl: 0xb4,
        name: 'Quest Giver',
        db: 'LEVEL1ON',
        fields: [{ name: 'Initially active?', type: 7, def: 1 }],
    },
    // two classes with the same name in one ODB
    [PROP_A]: { rfl: 0x100, name: 'Barrel', db: 'RESOURCES', fields: [] },
    [PROP_B]: { rfl: 0x100, name: 'Barrel', db: 'RESOURCES', fields: [] },
};
// the weapon the Farmer class names (class reference target), which only the catalog knows
const SWORD = '0x20007';
CLASSES[SWORD] = { rfl: 0x40, name: 'Sword', db: 'NPCS', fields: [{ name: 'Damage', type: 1, def: 5 }] };

const schemaCache = new Map<string, ClassSchema>();
const env: ContentEnv = {
    rflOf: (r) => CLASSES[r]?.rfl ?? null,
    // a class record's own model (no model field): only Barrel A has one
    modelWord: (r) => (r === PROP_A ? 0x10200 : 0),
    schemaFor(r) {
        const c = CLASSES[r];
        if (!c) return undefined;
        let s = schemaCache.get(r);
        if (!s) {
            s = {
                resource: r,
                classId: Number(r) & 0xffff,
                name: c.name,
                db: c.db,
                deps: new Map(),
                fields: c.fields.map((f, index): SchemaField => ({
                    index,
                    type: f.type,
                    name: f.name,
                    default: f.def,
                    category: 'General',
                })),
            };
            schemaCache.set(r, s);
        }
        return s;
    },
    resolveRef(owner, type, value): RefTarget {
        const db = value >>> 16;
        const id = value & 0xffff;
        const stem = db === 0 ? owner.db : db === 1 ? 'RESOURCES' : db === 2 ? 'SOUND' : null;
        const t: RefTarget = { stem, id, db };
        if ((type & 0xff) === 3) {
            t.name = 'Sword';
            t.resource = SWORD;
        }
        return t;
    },
};

const catalog = Object.entries(CLASSES).map(([resource, c]) => ({
    resource,
    classId: Number(resource) & 0xffff,
    dbIndex: 2,
    db: c.db,
    name: c.name,
    group: 0,
    runtimeClass: 0x10000 | c.rfl,
    rflClass: c.rfl,
    category: c.db === 'NPCS' ? 'Characters' : 'System',
    kind: c.rfl === 0x31 ? 'creature' : 'other',
    icon: 'generic',
    instances: 1,
})) as unknown as AssetClass[];

function ov(resource: string, name: string, value: number | string | number[]) {
    const f = env.schemaFor(resource)!.fields.find((x) => x.name === name)!;
    return { slot: -1, fieldIndex: f.index, type: f.type, name, value };
}

function obj(id: number, resource: string, over: Partial<LevelObject> = {}): LevelObject {
    return {
        id,
        class: CLASSES[resource]?.name ?? 'class 9',
        type: -1,
        kind: 'other',
        resource,
        model: '',
        transform: { pos: [id * 100, 5, -id], rot: [0, Math.PI / 2, 0] },
        props: {},
        origin: id,
        links: [],
        fields: [],
        ...over,
    };
}

// ---- story: one sequence with all action kinds, a dialog line and a handler class ----

function story(): StoryInput {
    const seq = newSequence(7, 3, 'Intro');
    const rynn = addActor(seq, 'Rynn', 900);
    const t0 = newActionBytes(0, 0, { x: 1, y: 2, z: 3 });
    const t1 = newActionBytes(1, 0.5, { animation: 3009, mode: 1, speed: 2 });
    const t2 = newActionBytes(3, 1, { sound: 4, params: 0x2710 });
    const t3 = newActionBytes(7, 2, { message: 4097, param: 9 });
    const t4 = newActionBytes(10, 3);
    Object.assign(rynn, joinActions([t0, t1, t2, t3, t4]));
    const cam = addActor(seq, 'Camera', 901);
    Object.assign(cam, joinActions([newActionBytes(9, 0, { a: 1, b: 40 }), newActionBytes(8, 1, { a: 1, g: 1 })]));
    const farmer = addActor(seq, 'Farmer', 1);
    Object.assign(farmer, joinActions([newActionBytes(5, 4)]));
    const broken = addActor(seq, 'Broken', 2);
    broken.actionCount = 2;
    broken.actions = new Uint8Array([9, 9, 9]);

    const line: DialogLine = {
        classId: 11,
        name: 'Farmer - FARM01',
        speaker: 'Farmer',
        sounds: [4, 0x00020009],
        animations: [3009],
        delay: 0.25,
        headAnimation: false,
        playMode: 1,
        camera: { position: 5, transition: 1, transitionTime: 0.5 },
        debugText: '',
        soundAt: -1,
    };
    const empty: DialogLine = { ...line, classId: 12, name: 'Farmer - FARM02', sounds: [0x00020001] };
    // handler class: field 0 Initially active, array fields point into the data area
    const handler: OdbClass = {
        id: 4,
        name: 'Quest Giver',
        group: 0,
        runtimeClass: 0x100b4,
        fields: [
            { type: 7, name: 'Initially active?' },
            { type: 0x1003, name: 'Beginning Dialog Exchange' },
            { type: 7, name: 'Message to send after Beginning dialog' },
            { type: 2, name: 'Distance From Rynn' },
            { type: 7, name: 'Is there a quest?' },
            { type: 1, name: 'Gold to give when complete' },
        ],
        // data[1] = array at offset 6 length 2; elements: line 11 (db 0), line 12
        data: [1, (2 << 16) | 6, 28, 0x40a00000, 1, 15, 11, 12],
    };
    return {
        stem: 'LEVEL1ON',
        deps: { 2: 'SOUND' },
        folders: [{ id: 3, parent: 0, name: 'Intro scenes' }],
        sequences: [{ id: 7, folder: 3, raw: new Uint8Array(0), seq }],
        sounds: new Map([
            [
                4,
                {
                    id: 4,
                    sub: 0,
                    name: 'FARM01A',
                    text: 'Hello, Rynn.',
                    rate: 22050,
                    seconds: 1,
                    descriptor: new Uint8Array(0),
                },
            ],
        ]),
        lines: [line, empty],
        classes: new Map([[4, handler]]),
    };
}

function doc(): LevelDocument {
    const d = new LevelDocument();
    d.level = 'Test Level';
    d.bounds = { minX: 0, maxX: 4096, minZ: 0, maxZ: 4096 };
    d.insert(
        obj(1, NPC, {
            links: [2, 99],
            fields: [
                ov(NPC, 'Model', 0x00010081),
                ov(NPC, 'Tint', 0x80102030),
                ov(NPC, 'Friendly', 0),
                ov(NPC, 'Gold Drops', [7]),
            ],
        }),
    );
    d.insert(obj(2, DETECTOR, { scale: [2, 1, 2], links: [3], fields: [ov(DETECTOR, 'Trigger Message', 4)] }));
    d.insert(obj(3, PLAYER, { fields: [ov(PLAYER, 'Sequence List', [7])] }));
    d.insert(obj(4, HANDLER, {}));
    d.insert(obj(5, PROP_A));
    d.insert(obj(6, PROP_B));
    d.insert({ ...obj(8, '0x99999'), class: 'class 9' });
    return d;
}

const quests: { classes: QuestClass[]; strings: { id: number; sub: number; text: string[] }[]; slot: number } = {
    slot: 1,
    classes: [
        {
            classId: 4167,
            name: 'Speak',
            kind: 'both',
            optional: true,
            journalId: 22,
            mapTextId: 23,
            mapX: 10,
            mapY: 20,
            groupId: 3055,
            groupName: '1 - Surdana',
            chapter: 1,
            chapterName: 'Surdana',
        },
    ],
    strings: [
        { id: 22, sub: 0, text: ['Speak to the farmer', 'Sprich mit dem Bauern'] },
        { id: 23, sub: 0, text: ['Farm', ''] },
    ],
};

function scene(): DcfScene {
    return buildContentScene({
        doc: doc(),
        env,
        catalog,
        levelName: 'Test Level',
        stem: 'LEVEL1NA',
        story: story(),
        quests,
        language: 'ENGLISH',
    });
}

describe('Drakan Content Format scene', () => {
    it('validates against the schema and is deterministic', () => {
        const a = scene();
        expect(validateContent(a)).toEqual([]);
        expect(JSON.stringify(scene())).toBe(JSON.stringify(a));
        expect(a.format).toBe('drakan-content');
        expect(a.version).toBe(CONTENT_VERSION);
        expect(a.level).toMatchObject({ stem: 'LEVEL1NA', storyStem: 'LEVEL1ON', language: 'ENGLISH' });
        // survives a JSON round trip
        expect(validateContent(JSON.parse(JSON.stringify(a)))).toEqual([]);
    });

    it('exports instances with transforms, quaternions and overrides only', () => {
        const s = scene();
        expect(s.instances.map((i) => i.id)).toEqual([1, 2, 3, 4, 5, 6, 8]);
        const farmer = s.instances[0]!;
        expect(farmer.archetype).toBe('NPCS/Farmer');
        expect(farmer.transform.position).toEqual([100, 5, -1]);
        expect(farmer.transform.quaternion[1]).toBeCloseTo(Math.SQRT1_2, 6);
        expect(farmer.transform.quaternion[3]).toBeCloseTo(Math.SQRT1_2, 6);
        expect(farmer.transform.scale).toBeUndefined();
        expect(farmer.properties).toEqual({
            Model: { kind: 'mesh', id: 'RESOURCES:129' },
            Tint: { rgb: '#102030', alpha: 128 },
            Friendly: false,
            'Gold Drops': [7],
        });
        // the model differs from the archetype default, so the instance carries its own mesh
        expect(farmer.mesh).toEqual({ kind: 'mesh', id: 'RESOURCES:129' });
        expect(s.instances[1]!.transform.scale).toEqual([2, 1, 2]);
    });

    it('describes archetypes with typed defaults, stable names and class references', () => {
        const s = scene();
        const names = s.archetypes.map((a) => a.name);
        expect(names).toContain('NPCS/Farmer');
        // the class a class-typed default names becomes an archetype too, even without an instance
        expect(names).toContain('NPCS/Sword');
        // same name twice in one ODB: disambiguated by class id
        expect(names.filter((n) => n.startsWith('RESOURCES/Barrel'))).toEqual([
            `RESOURCES/Barrel#${Number(PROP_A) & 0xffff}`,
            `RESOURCES/Barrel#${Number(PROP_B) & 0xffff}`,
        ]);
        expect(names).toContain('unresolved/class 9#9.39321');
        const farmer = s.archetypes.find((a) => a.name === 'NPCS/Farmer')!;
        const byName = (n: string) => farmer.properties.find((p) => p.name === n)!;
        expect(farmer.mesh).toEqual({ kind: 'mesh', id: 'RESOURCES:128' });
        // the model of the class record itself becomes the archetype mesh
        expect(s.archetypes.find((a) => a.name.startsWith('RESOURCES/Barrel'))!.mesh).toEqual({
            kind: 'mesh',
            id: 'RESOURCES:512',
        });
        expect(byName('Tint').default).toEqual({ rgb: '#8040c0', alpha: 255 });
        expect(byName('Friendly').default).toBe(true);
        expect(byName('Name').default).toBe('Joe');
        expect(byName('Weapon').default).toEqual({ class: 'RESOURCES:256', archetype: 'NPCS/Sword' });
        expect(byName('Gold Drops')).toMatchObject({ array: true, default: [3, -1] });
        expect(byName('Message To Send')).toMatchObject({ semantic: 'message', default: 8 });
        expect(byName('Voice').default).toBeNull();
        expect(farmer.kind).toBe('creature');
        expect(farmer.class.rflClass).toBe(0x31);
        expect(s.archetypes.find((a) => a.name === 'SYSTEM/Trigger Detector')!.role).toBe('detector');
    });

    it('exports links with the messages the source sends, and triggers with volumes', () => {
        const s = scene();
        expect(s.links.map((l) => `${l.from}>${l.to}`)).toEqual(['1>2', '2>3']);
        const l = s.links[1]!;
        expect(l.messages).toEqual([{ field: 'Trigger Message', label: 'enters', message: 4, name: 'Play Sequence' }]);
        expect(s.diagnostics.danglingLinks).toBe(1);
        const t = s.triggers.find((x) => x.instance === 2)!;
        expect(t.role).toBe('detector');
        expect(t.volumes).toEqual([
            {
                shape: 'box',
                center: [200, 5, -2],
                halfExtents: [2048, 1024, 2048],
                rotation: [0, Math.PI / 2, 0].map((n) => Number(n.toPrecision(9))),
            },
        ]);
        expect(s.triggers.find((x) => x.instance === 3)!.sequences).toEqual([7]);
        expect(s.messages[4]).toBe('Play Sequence');
    });

    it('exports sequences with typed actions and flags an actor it cannot read', () => {
        const s = scene();
        expect(s.sequences).toHaveLength(1);
        const q = s.sequences[0]!;
        expect(q).toMatchObject({ id: 7, name: 'Intro', folder: 'Intro scenes', partial: true });
        const rynn = q.actors[0]!;
        expect(rynn).toMatchObject({ name: 'Rynn', ref: 900, target: 'class' });
        expect(rynn.actions.map((a) => a.kind)).toEqual(['transform', 'animation', 'sound', 'message', 'hold']);
        expect(rynn.actions[0]).toMatchObject({ position: [1, 2, 3], rotation: [0, 0, 0, 1], scale: [1, 1, 1] });
        expect(rynn.actions[1]).toMatchObject({ animation: 3009, mode: 1, speed: 2, time: 0.5 });
        expect(rynn.actions[2]).toMatchObject({ sound: { kind: 'sound', id: 'LEVEL1ON:4' }, volume: 10000 });
        expect(rynn.actions[3]).toMatchObject({ message: 4097, name: 'On (4097)', param: 9 });
        expect(q.actors[1]!.actions.map((a) => a.kind)).toEqual(['camera', 'unknown']);
        expect(q.actors[1]!.actions[0]).toMatchObject({ fov: 40, unknown: 1 });
        expect(q.actors[2]).toMatchObject({ name: 'Farmer', target: 'instance', ref: 1 });
        expect(q.actors[3]!.actions).toEqual([]);
        expect(s.diagnostics.opaqueSequenceActors).toBe(1);
    });

    it('exports dialogue lines with subtitles and the placed conversation handler', () => {
        const s = scene();
        expect(s.dialogue.lines.map((l) => l.name)).toEqual(['Farmer - FARM01', 'Farmer - FARM02']);
        const l = s.dialogue.lines[0]!;
        expect(l.text).toBe('Hello, Rynn.');
        expect(l.sounds.map((x) => x.id)).toEqual(['LEVEL1ON:4', 'SOUND:9']);
        expect(l.sounds[0]!.name).toBe('FARM01A');
        expect(s.dialogue.lines[1]!.text).toBe('');
        expect(s.diagnostics.missingSubtitles).toBe(1);
        expect(s.dialogue.handlers).toHaveLength(1);
        const h = s.dialogue.handlers[0]!;
        expect(h).toMatchObject({
            instance: 4,
            archetype: 'LEVEL1ON/Quest Giver',
            hasQuest: true,
            gold: 15,
            distance: 5,
        });
        expect(h.phases[0]).toMatchObject({
            key: 'beginning',
            lines: ['Farmer - FARM01', 'Farmer - FARM02'],
            message: 28,
            messageName: 'Start Conversation',
        });
    });

    it('exports quests with text of the disc language', () => {
        const q = scene().quests;
        expect(q).toEqual([
            {
                id: 4167,
                name: 'Speak',
                kind: 'both',
                optional: true,
                chapter: 1,
                chapterName: 'Surdana',
                journal: 'Sprich mit dem Bauern',
                mapText: 'Farm',
                map: { x: 10, y: 20 },
            },
        ]);
    });

    it('fills asset paths only when an exporter publishes them', () => {
        const s = buildContentScene({
            doc: doc(),
            env,
            catalog,
            levelName: 'Test Level',
            assetPath: (kind, id) => (kind === 'mesh' ? `assets/meshes/${id.replace(':', '/')}.glb` : undefined),
        });
        expect(validateContent(s)).toEqual([]);
        expect(s.archetypes.find((a) => a.name === 'NPCS/Farmer')!.mesh).toEqual({
            kind: 'mesh',
            id: 'RESOURCES:128',
            path: 'assets/meshes/RESOURCES/128.glb',
        });
        expect(s.sequences).toEqual([]);
        expect(s.dialogue).toEqual({ lines: [], handlers: [] });
    });

    it('works without story and quests', () => {
        const s = buildContentScene({ doc: doc(), env, catalog, levelName: 'Bare' });
        expect(validateContent(s)).toEqual([]);
        expect(s.level).toEqual({ name: 'Bare', bounds: { minX: 0, maxX: 4096, minZ: 0, maxZ: 4096 } });
    });
});

describe('schema validation', () => {
    it('rejects broken documents with a path', () => {
        const s = JSON.parse(JSON.stringify(scene())) as DcfScene;
        (s.instances[0] as unknown as { id: string }).id = 'x';
        delete (s as unknown as Record<string, unknown>).links;
        (s.sequences[0]!.actors[0]!.actions[1] as unknown as { kind: string }).kind = 'dance';
        const errs = validateContent(s);
        const text = errs.map((e) => `${e.path}: ${e.message}`).join('\n');
        expect(text).toContain('missing required "links"');
        expect(text).toContain('/instances/0/id: expected integer');
        expect(errs.length).toBeGreaterThanOrEqual(2);
    });
    it('rejects unknown properties and bad versions', () => {
        const s = JSON.parse(JSON.stringify(scene())) as Record<string, unknown>;
        s.version = '2.0';
        s.extra = 1;
        expect(validateContent(s).length).toBeGreaterThan(0);
    });
    it('the shipped schema is a draft-07 document with a definitions section', () => {
        expect(CONTENT_SCHEMA.$schema).toBe('http://json-schema.org/draft-07/schema#');
        expect(Object.keys(CONTENT_SCHEMA.$defs as object)).toContain('Scene');
    });
});

describe('package', () => {
    it('lays out manifest, scenes, schema and a zip that holds the same files', () => {
        const s = scene();
        const files = buildContentPackage([s], { source: { profile: 'pal', language: 'ENGLISH' } });
        expect(Object.keys(files).sort()).toEqual([
            'README.txt',
            'manifest.json',
            'scenes/test-level.scene.json',
            'schema/drakan-content.schema.json',
        ]);
        const manifest = JSON.parse(strFromU8(files['manifest.json']!));
        expect(validateContent(manifest)).toEqual([]);
        expect(manifest.scenes).toEqual([{ name: 'Test Level', path: 'scenes/test-level.scene.json' }]);
        expect(JSON.parse(strFromU8(files['schema/drakan-content.schema.json']!))).toEqual(CONTENT_SCHEMA);
        const back = unzipSync(zipContentPackage(files));
        expect(Object.keys(back).sort()).toEqual(Object.keys(files).sort());
        expect(validateContent(JSON.parse(strFromU8(back['scenes/test-level.scene.json']!)))).toEqual([]);
        // no game bytes: every file is text
        expect(strFromU8(back['manifest.json']!)).toContain('"drakan-content"');
    });
    it('refuses two scenes with the same name', () => {
        const s = scene();
        expect(() => buildContentPackage([s, s])).toThrow(/two scenes/);
    });
});

describe('eulerToQuat', () => {
    it('matches known rotations', () => {
        expect(eulerToQuat([0, 0, 0])).toEqual([0, 0, 0, 1]);
        const q = eulerToQuat([Math.PI, 0, 0]);
        expect(q[0]).toBeCloseTo(1, 6);
        expect(q[3]).toBeCloseTo(0, 6);
        // XYZ order: Rx then Ry then Rz applied as R = Rx Ry Rz
        const a = eulerToQuat([0.3, 0.4, 0.5]);
        expect(Math.hypot(...a)).toBeCloseTo(1, 6);
    });
});
