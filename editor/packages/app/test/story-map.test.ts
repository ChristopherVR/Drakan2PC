// Synthetic only: no game bytes. What the story overlay draws (cut scenes, quests, shops, conversations) and the camera
// move a one-click cut scene starts with.
import { describe, expect, it } from 'vitest';
import {
    LevelDocument,
    type ClassSchema,
    type LevelObject,
    type LogicEnv,
    type SchemaField,
} from '@drakan/editor-core';
import { MESSAGE_QUEST_COMPLETE, MESSAGE_QUEST_INITIATE } from '@drakan/formats';
import { buildStoryMarks, countMarks, starterCameraPoints } from '../src/story/storymap.ts';
import { DEFAULT_VIEW_OPTIONS, parseViewOptions } from '../src/scene/viewopts.ts';

interface Stub {
    rfl: number;
    name: string;
    fields: { name: string; type?: number; def: number }[];
}

const CLASSES: Record<string, Stub> = {
    '0x20001': {
        rfl: 0x3d,
        name: 'Trigger Detector',
        fields: [
            { name: 'Trigger Message', type: 7, def: 4 },
            { name: 'Detect Only Once?', type: 7, def: 0 },
        ],
    },
    '0x20003': {
        rfl: 0x33,
        name: 'Stomp Player',
        fields: [
            { name: 'Initial State', type: 7, def: 0 },
            { name: 'Sequence List', type: 0x100b, def: 0 },
        ],
    },
    '0x2000c': { rfl: 0x68, name: 'Find the Farmer', fields: [{ name: 'Type', type: 7, def: 1 }] },
    '0x20010': { rfl: 0x111, name: 'Farmer', fields: [{ name: 'Beginning Dialog Exchange', type: 0x100b, def: 0 }] },
    '0x20011': { rfl: 0xac, name: 'BlackSmith Store', fields: [] },
    '0x20012': { rfl: 0x100, name: 'Barrel', fields: [] },
};

const schemas = new Map<string, ClassSchema>();
const env: LogicEnv = {
    rflOf: (r) => CLASSES[r]?.rfl ?? null,
    schemaFor(r) {
        const c = CLASSES[r];
        if (!c) return undefined;
        let s = schemas.get(r);
        if (!s) {
            s = {
                resource: r,
                classId: Number(r) & 0xffff,
                name: c.name,
                db: 'LEVEL',
                deps: new Map(),
                fields: c.fields.map((f, index): SchemaField => ({
                    index,
                    type: f.type ?? 7,
                    name: f.name,
                    default: f.def,
                    category: 'General',
                })),
            };
            schemas.set(r, s);
        }
        return s;
    },
};

function obj(id: number, resource: string, over: Partial<LevelObject> = {}): LevelObject {
    return {
        id,
        class: CLASSES[resource]!.name,
        type: -1,
        kind: 'prop' as never,
        resource,
        model: '',
        transform: { pos: [id * 1000, 0, 0], rot: [0, 0, 0] },
        props: {},
        origin: id,
        links: [],
        fields: [],
        ...over,
    };
}

const field = (name: string, resource: string, value: number | number[]) => {
    const f = env.schemaFor(resource)!.fields.find((x) => x.name === name)!;
    return { slot: -1, fieldIndex: f.index, type: f.type, name, value } as LevelObject['fields'][number];
};

function level(): LevelDocument {
    const d = new LevelDocument();
    // trigger box 1 plays cut scene player 3 (sequence 44 moves the barrel 9 and the camera)
    d.insert(obj(1, '0x20001', { links: [3], scale: [3, 2, 1] }));
    d.insert(obj(3, '0x20003', { fields: [field('Sequence List', '0x20003', [44])] }));
    d.insert(obj(9, '0x20012'));
    // quest 5: box 6 starts it, box 7 finishes it, the farmer 8 also starts it (talk)
    d.insert(obj(5, '0x2000c'));
    d.insert(
        obj(6, '0x20001', {
            links: [5],
            scale: [2, 2, 2],
            fields: [field('Trigger Message', '0x20001', MESSAGE_QUEST_INITIATE)],
        }),
    );
    d.insert(
        obj(7, '0x20001', {
            links: [5],
            scale: [2, 2, 2],
            fields: [field('Trigger Message', '0x20001', MESSAGE_QUEST_COMPLETE)],
        }),
    );
    d.insert(obj(8, '0x20010', { links: [5] }));
    d.insert(obj(10, '0x20011'));
    return d;
}

const source = {
    sequence: (id: number) =>
        id === 44
            ? {
                  id,
                  name: 'Farmer waves',
                  actors: [
                      { name: 'Camera', ref: 900 },
                      { name: 'Barrel', ref: 9 },
                      { name: 'Ghost', ref: 12345 },
                  ],
                  camera: [
                      [0, 100, 0],
                      [500, 100, 0],
                  ] as [number, number, number][],
                  seconds: 6.5,
              }
            : null,
    questTitle: (classId: number) => (classId === 0x0c ? 'Find the farmer' : null),
    vendor: (o: LevelObject) => (o.class === 'BlackSmith Store' ? { items: 12 } : null),
    conversation: (o: LevelObject) =>
        o.class === 'Farmer' ? { name: 'Farmer talk', hasQuest: true, speakers: ['Farmer'] } : null,
};

describe('story marks', () => {
    const marks = buildStoryMarks(level(), env, source);
    const by = (g: string) => marks.filter((m) => m.group === g);

    it('finds one of each kind', () => {
        expect(countMarks(marks)).toEqual({ cutscene: 1, vendor: 1, quest: 1, talk: 1 });
    });

    it('draws a cut scene with its camera path, actors and trigger box', () => {
        const c = by('cutscene')[0]!;
        expect(c.id).toBe(3);
        expect(c.title).toBe('Farmer waves');
        expect(c.paths).toEqual([
            [
                [0, 100, 0],
                [500, 100, 0],
            ],
        ]);
        // the camera actor and an object not in the level get no line
        expect(c.lines.map((l) => l.target)).toEqual([9]);
        expect(c.areas.map((a) => [a.id, a.tone])).toEqual([[1, 'trigger']]);
        expect(c.areas[0]!.box.half).toEqual([3072, 2048, 1024]);
        expect(c.focus).toEqual({ kind: 'sequence', id: 44 });
        expect(c.detail.join(' ')).toContain('walks into the box');
    });

    it('colours quest start and finish areas and names the quest', () => {
        const q = by('quest')[0]!;
        expect(q.title).toBe('Find the farmer');
        expect(q.areas.map((a) => [a.id, a.tone])).toEqual([
            [6, 'start'],
            [7, 'complete'],
        ]);
        expect(q.areas[1]!.label).toBe('Finishes: Find the farmer');
        // the talking farmer starts it too, drawn as a line
        expect(q.lines.map((l) => [l.target, l.label])).toEqual([[8, 'starts it']]);
        expect(q.focus).toEqual({ kind: 'quest', classId: 0x0c });
    });

    it('labels shops with their item count and conversations with their talk', () => {
        expect(by('vendor')[0]!.title).toBe('BlackSmith Store, 12 items');
        expect(by('vendor')[0]!.focus).toEqual({ kind: 'vendor', object: 10 });
        const t = by('talk')[0]!;
        expect(t.id).toBe(8);
        expect(t.title).toBe('Talk: Farmer');
        expect(t.detail).toContain('Gives a quest.');
        expect(t.focus).toEqual({ kind: 'conversation', classId: 0x10 });
    });

    it('works without names and paths (story files not read yet)', () => {
        const bare = buildStoryMarks(level(), env);
        const c = bare.find((m) => m.group === 'cutscene')!;
        expect(c.title).toBe('sequence 44');
        expect(c.paths).toEqual([]);
        expect(bare.find((m) => m.group === 'quest')!.title).toBe('Find the Farmer'); // the class name
        expect(bare.some((m) => m.group === 'vendor')).toBe(false);
    });

    it('leaves out hidden objects and switched-off groups', () => {
        const some = buildStoryMarks(level(), env, source, { skip: (id) => id === 3, groups: new Set(['quest']) });
        expect(some.map((m) => m.group)).toEqual(['quest']);
    });

    it('a cut scene player with an empty list says so and opens nothing', () => {
        const d = new LevelDocument();
        d.insert(obj(3, '0x20003'));
        const [m] = buildStoryMarks(d, env, source);
        expect(m!.focus).toBeNull();
        expect(m!.detail[0]).toMatch(/empty/);
        expect(m!.detail).toContain('Nothing starts it yet.');
    });
});

describe('starter camera move', () => {
    it('starts at the viewport camera and moves 40% in, both looking at the target', () => {
        const [a, b] = starterCameraPoints([0, 0, 0], [0, 0, 10000]);
        expect(a!.time).toBe(0);
        expect(b!.time).toBe(4);
        expect(a!.pos).toEqual([0, 0, 10000]);
        expect(b!.pos).toEqual([0, 0, 6000]);
        // looking along -Z at the target: yaw 0, level
        expect(a!.yaw).toBeCloseTo(0);
        expect(a!.pitch).toBeCloseTo(0);
    });
    it('without a camera it starts back and above', () => {
        const [a] = starterCameraPoints([100, 0, 0], null);
        expect(a!.pos).toEqual([100, 2500, 6000]);
        expect(a!.pitch).toBeGreaterThan(0);
    });
});

describe('story view options', () => {
    it('are on by default and survive a stored partial value', () => {
        expect(DEFAULT_VIEW_OPTIONS.story).toEqual({ cutscene: true, vendor: true, quest: true, talk: true });
        const o = parseViewOptions({ story: { vendor: false, bogus: true } });
        expect(o.story).toEqual({ cutscene: true, vendor: false, quest: true, talk: true });
        expect(DEFAULT_VIEW_OPTIONS.story.vendor).toBe(true);
    });
});
