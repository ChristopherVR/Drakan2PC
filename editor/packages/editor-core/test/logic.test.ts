// Synthetic only: no game bytes. Logic graph, layout, volumes, validation and templates over stub classes.
import { MESSAGE_NAMES, enumNames, isMessageField, logicClass, messageName } from '@drakan/formats';
import { describe, expect, it } from 'vitest';
import {
    ApplyTemplateCommand,
    CELL,
    CommandStack,
    LevelDocument,
    TEMPLATES,
    addLink,
    buildLogicGraph,
    destinationLevelName,
    editObjects,
    exitArrivalProblem,
    isError,
    layoutGraph,
    outgoingMessages,
    removeLink,
    resizeBoxFace,
    rotateEulerXYZ,
    setMessageField,
    setVolumeRadius,
    templateById,
    validateLogic,
    volumesOf,
    type AssetClass,
    type ClassSchema,
    type ExitIndex,
    type LevelObject,
    type LogicEnv,
    type SchemaField,
} from '../src/index.ts';

interface StubClass {
    rfl: number;
    name: string;
    fields: { name: string; type?: number; def: number }[];
}

const CLASSES: Record<string, StubClass> = {
    '0x20001': {
        rfl: 0x3d,
        name: 'Trigger Detector',
        fields: [
            { name: 'Task', type: 7, def: 6 },
            { name: 'Detect Which?', type: 7, def: 2 },
            { name: 'Initial State', type: 7, def: 0 },
            { name: 'Trigger Message', type: 7, def: 5 },
            { name: 'Detect Only Once?', type: 7, def: 0 },
        ],
    },
    '0x20002': {
        rfl: 0x3e,
        name: 'Timer',
        fields: [
            { name: 'Time Until Trigger', type: 2, def: 5 },
            { name: 'Start Mode', type: 7, def: 1 },
            { name: 'Trigger Message', type: 7, def: 1 },
        ],
    },
    '0x20003': {
        rfl: 0x33,
        name: 'Stomp Player',
        fields: [
            { name: 'List Play Order', type: 7, def: 0 },
            { name: 'Initial State', type: 7, def: 0 },
            { name: 'Message To Play Next', type: 7, def: 4 },
            { name: 'Sequence List', type: 0x100b, def: 0 },
        ],
    },
    '0x20004': {
        rfl: 0x13,
        name: 'Town Door',
        fields: [
            { name: 'Locked', type: 7, def: 0 },
            { name: 'Auto-Open Distance (lu)', type: 2, def: 2 },
        ],
    },
    '0x20005': { rfl: 0x31, name: 'Orc Center', fields: [{ name: 'Message To Send', type: 7, def: 8 }] },
    '0x20006': {
        rfl: 0x07,
        name: 'Countdown to Zero',
        fields: [
            { name: 'Initial Counter Value', type: 1, def: 1 },
            { name: 'When Triggered?', type: 7, def: 1 },
            { name: 'Decrement (-1) Message', type: 7, def: 8 },
            { name: 'Message To Send', type: 7, def: 1 },
            { name: 'Destroy After Counter Hits Zero ?', type: 7, def: 0 },
        ],
    },
    '0x20007': { rfl: 0x70, name: 'Orc WP', fields: [{ name: 'Message to send', type: 7, def: 0 }] },
    '0x20008': { rfl: 0x79, name: 'Vis Toggler', fields: [{ name: 'Show Message', type: 7, def: 1 }] },
    '0x20009': { rfl: 0x100, name: 'Book', fields: [] },
    '0x2000a': {
        rfl: 0x0d,
        name: 'Lever 2000',
        fields: [
            { name: 'Message to send when on', type: 7, def: 1 },
            { name: 'Message to send when off', type: 7, def: 0 },
        ],
    },
    '0x2000b': {
        rfl: 0x37,
        name: 'Level Transition',
        fields: [{ name: 'Send This Message', type: 7, def: 0 }],
    },
    '0x2000d': {
        rfl: 0x22,
        name: 'Key Lock',
        fields: [
            { name: 'Lock Code', type: 1, def: 0 },
            { name: 'Trigger To Send', type: 7, def: 1 },
        ],
    },
    '0x2000c': { rfl: 0x68, name: 'Kill the Inquisitor', fields: [{ name: 'Type', type: 7, def: 1 }] },
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
                db: 'SYSTEM',
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

const catalog = Object.entries(CLASSES).map(([resource, c]) => ({
    resource,
    classId: Number(resource) & 0xffff,
    dbIndex: 2,
    db: 'SYSTEM',
    name: c.name,
    group: 0,
    runtimeClass: 0x10000 | c.rfl,
    rflClass: c.rfl,
    category: 'x',
    kind: 'prop',
    icon: 'rock' as never,
    instances: 1,
})) as unknown as AssetClass[];

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

const field = (name: string, resource: string, value: number) => {
    const f = env.schemaFor(resource)!.fields.find((x) => x.name === name)!;
    return { slot: -1, fieldIndex: f.index, type: f.type, name, value };
};

function level(): LevelDocument {
    const d = new LevelDocument();
    d.insert(obj(1, '0x20001', { links: [2], scale: [3, 2, 1], fields: [field('Trigger Message', '0x20001', 4)] }));
    d.insert(obj(2, '0x20002', { links: [3], fields: [field('Trigger Message', '0x20002', 23)] }));
    d.insert(obj(3, '0x20003', {}));
    d.insert(obj(4, '0x20004', { fields: [field('Locked', '0x20004', 1)] }));
    d.insert(obj(5, '0x20005', { links: [6] }));
    d.insert(obj(6, '0x20006', { links: [4] }));
    d.insert(obj(7, '0x20007', { links: [8] }));
    d.insert(obj(8, '0x20007', { links: [7] }));
    d.insert(obj(9, '0x20008', { links: [10, 11] }));
    d.insert(obj(10, '0x20009'));
    d.insert(obj(11, '0x20009'));
    return d;
}

describe('message vocabulary', () => {
    it('has the 33 engine names and the indices the disc data relies on', () => {
        expect(MESSAGE_NAMES).toHaveLength(33);
        expect(messageName(4)).toBe('Play Sequence');
        expect(messageName(8)).toBe("I'm Dead");
        expect(messageName(23)).toBe('Load');
        expect(messageName(99)).toBe('message 99');
    });
    it('recognises message fields but not yes/no questions or arrays', () => {
        expect(isMessageField('Trigger Message', 7)).toBe(true);
        expect(isMessageField('Msg Sent When Opened', 7)).toBe(true);
        expect(isMessageField('Can be Triggered by Message ?', 7)).toBe(false);
        expect(isMessageField('Message List', 0x1007)).toBe(false);
        expect(isMessageField('Message String', 0x0a)).toBe(false);
        expect(isMessageField('Initial Health', 1)).toBe(false);
    });
    it('names enum items', () => {
        expect(enumNames(0x3d, 'Detect Which?', 7)?.[1]).toBe('Rynn On Mount');
        expect(enumNames(0x3e, 'Trigger Message', 7)).toBe(MESSAGE_NAMES);
        expect(enumNames(0x3d, 'Unknown', 7)).toBeUndefined();
        expect(logicClass(0x33)?.role).toBe('cutscene');
    });
});

describe('graph', () => {
    it('draws logic objects, resolves edge messages and counts hidden targets', () => {
        const g = buildLogicGraph(level(), env);
        expect([...g.nodes.keys()].sort((a, b) => a - b)).toEqual([1, 2, 3, 4, 5, 6]);
        const e = g.edges.find((x) => x.from === 1)!;
        expect(e.messages).toHaveLength(1);
        expect(e.messages[0]).toMatchObject({
            field: 'Trigger Message',
            value: 4,
            name: 'Play Sequence',
            overridden: true,
        });
        // creature -> counter -> door are pulled in; waypoint chain and props are not nodes
        expect(g.edges.some((x) => x.from === 5 && x.to === 6)).toBe(true);
        expect(g.nodes.has(7)).toBe(false);
        expect(g.nodes.has(9)).toBe(false);
    });
    it('neighbourhood scope follows links both ways to a depth', () => {
        const g = buildLogicGraph(level(), env, { focus: [2], depth: 1 });
        expect([...g.nodes.keys()].sort((a, b) => a - b)).toEqual([1, 2, 3]);
        const g2 = buildLogicGraph(level(), env, { focus: [9], depth: 1 });
        expect(g2.nodes.has(9)).toBe(true);
    });
    it('lays out sources left of targets and survives cycles', () => {
        const g = buildLogicGraph(level(), env);
        const p = layoutGraph(g);
        expect(p.get(1)!.layer).toBeLessThan(p.get(2)!.layer);
        expect(p.get(2)!.layer).toBeLessThan(p.get(3)!.layer);
        expect(p.get(5)!.layer).toBeLessThan(p.get(6)!.layer);
        const d = level();
        d.get(3)!.links = [1];
        expect(layoutGraph(buildLogicGraph(d, env)).size).toBe(6);
    });
});

describe('edits', () => {
    it('adds and removes links and edits the source message as one undo step', () => {
        const d = level();
        const stack = new CommandStack(d);
        const cmd = editObjects(d, [2], 'Link', (o) => addLink(o, 4))!;
        stack.run(cmd);
        expect(d.get(2)!.links).toEqual([3, 4]);
        const set = editObjects(d, [2], 'Message', (o) => setMessageField(o, env, 'Trigger Message', 3))!;
        stack.run(set);
        expect(outgoingMessages(d.get(2)!, env, undefined)).toEqual([]);
        const g = buildLogicGraph(d, env);
        expect(g.edges.find((e) => e.from === 2 && e.to === 4)!.messages[0]!.name).toBe('Unlock');
        stack.undo();
        stack.undo();
        expect(d.get(2)!.links).toEqual([3]);
        expect(d.get(2)!.fields![0]!.value).toBe(23);
        expect(removeLink(d.get(2)!, 3)).toBe(true);
        expect(addLink(d.get(2)!, 2)).toBe(false);
    });
    it('dropping a message back to the class default removes the override', () => {
        const d = level();
        const o = d.get(1)!;
        setMessageField(o, env, 'Trigger Message', 5);
        expect(o.fields).toHaveLength(0);
        expect(setMessageField(o, env, 'Task', 1)).toBe(false);
    });
});

describe('volumes', () => {
    it('a detector is a box of scale cells, a door has an opening radius', () => {
        const d = level();
        const [v] = volumesOf(d.get(1)!, env);
        expect(v).toMatchObject({ kind: 'box', half: [(3 * CELL) / 2, CELL, CELL / 2] });
        const door = volumesOf(d.get(4)!, env)[0]!;
        expect(door).toMatchObject({ kind: 'sphere', radius: 2 * CELL, field: 'Auto-Open Distance (lu)' });
        if (door.kind === 'sphere') {
            setVolumeRadius(d.get(4)!, env, door, 5 * CELL);
            expect(volumesOf(d.get(4)!, env)[0]).toMatchObject({ radius: 5 * CELL });
        }
    });
    it('dragging a face keeps the opposite face fixed, also when rotated', () => {
        const o = obj(1, '0x20001', { scale: [2, 2, 2], transform: { pos: [0, 0, 0], rot: [0, 0, 0] } });
        // +x face from 1 cell to 3 cells: box now spans -1..3 cells in x
        resizeBoxFace(o, 0, 1, 3 * CELL);
        expect(o.scale![0]).toBeCloseTo(4);
        expect(o.transform.pos[0]).toBeCloseTo(CELL);
        // rotated a quarter turn about y: the local x axis is world -z
        const r = obj(2, '0x20001', { scale: [2, 2, 2], transform: { pos: [0, 0, 0], rot: [0, Math.PI / 2, 0] } });
        resizeBoxFace(r, 0, 1, 3 * CELL);
        expect(r.transform.pos[2]).toBeCloseTo(-CELL);
        expect(r.transform.pos[0]).toBeCloseTo(0);
        // cannot collapse
        resizeBoxFace(o, 0, -1, 100 * CELL);
        expect(o.scale![0]).toBeGreaterThan(0);
        expect(rotateEulerXYZ([0, 0, Math.PI / 2], [1, 0, 0])[1]).toBeCloseTo(1);
    });
});

describe('validation', () => {
    it('flags triggers without targets, unreachable cut scenes and ignored messages', () => {
        const d = level();
        d.insert(obj(20, '0x20001', { fields: [field('Trigger Message', '0x20001', 4)] }));
        d.insert(obj(21, '0x20003', {}));
        d.insert(obj(22, '0x20001', { links: [4], fields: [field('Trigger Message', '0x20001', 4)] }));
        const issues = validateLogic(d, env);
        const by = (code: string) => issues.filter((i) => i.code === code).flatMap((i) => i.ids);
        expect(by('trigger-no-target')).toContain(20);
        expect(by('trigger-no-target')).not.toContain(1);
        expect(by('unreachable-cutscene')).toContain(21);
        expect(by('unreachable-cutscene')).not.toContain(3);
        expect(by('message-ignored')).toEqual(expect.arrayContaining([22, 4]));
    });
    it('a cut scene that starts on counts as reachable; a disabled detector with nothing feeding it does not start a chain', () => {
        const d = new LevelDocument();
        d.insert(obj(1, '0x20003', { fields: [field('Initial State', '0x20003', 1)] }));
        d.insert(obj(2, '0x20001', { links: [3], fields: [field('Initial State', '0x20001', 1)] }));
        d.insert(obj(3, '0x20003'));
        const un = validateLogic(d, env).filter((i) => i.code === 'unreachable-cutscene');
        expect(un.map((i) => i.ids[0])).toEqual([3]);
    });
    it('checks exit destinations against the other levels', () => {
        const index: ExitIndex = {
            levels: new Map([
                ['Level 1B - Kragmor', [{ id: 1, name: 'Transition', dest: 'Level 1A - Surdana', portal: false }]],
                ['Level 2 - Shadowmire', [{ id: 2, name: 'Exit', dest: 'Level 5 - Shiverbane', portal: false }]],
            ]),
        };
        expect(exitArrivalProblem('Level 1A - Surdana', 'Level 1B - Kragmor', false, index)).toBeNull();
        expect(exitArrivalProblem('Level 1A - Surdana', 'Level 2 - Shadowmire', false, index)).toMatch(
            /no exit leading back/,
        );
        expect(exitArrivalProblem('Level 1A - Surdana', 'Level 9', false, index)).toMatch(/not on this disc/);
        // a copy of Surdana arrives at Kragmor's way back to Surdana
        expect(exitArrivalProblem('Surdana Copy', 'Level 1B - Kragmor', false, index)).toMatch(/no exit leading back/);
        expect(exitArrivalProblem('Surdana Copy', 'Level 1B - Kragmor', false, index, 'Level 1A - Surdana')).toBeNull();
        expect(exitArrivalProblem('Level 1A - Surdana', 'Level 1B - Kragmor', true, index)).toMatch(/portal/);
        expect(destinationLevelName('Levels\\Level 2 - Shadowmire\\Level 2 - Shadowmire.lvl')).toBe(
            'Level 2 - Shadowmire',
        );
        const d = new LevelDocument();
        d.insert(obj(1, '0x2000b'));
        const out = validateLogic(d, env, {
            level: 'Level 1A - Surdana',
            exits: index,
            exitDestination: () => ({ dest: 'Level 2 - Shadowmire', portal: false }),
        });
        expect(out.map((i) => i.code)).toEqual(['exit-no-arrival']);
        const none = validateLogic(d, env, { level: 'x', exits: index, exitDestination: () => null });
        expect(none.map((i) => i.code)).toEqual(['exit-no-destination']);
    });
});

describe('templates', () => {
    const ctxFor = (doc: LevelDocument, selection: number[] = []) => ({
        doc,
        env,
        catalog,
        at: [5000, 100, 7000] as [number, number, number],
        selection,
    });

    it('trigger volume plays an existing cut scene', () => {
        const d = level();
        const r = templateById('cutscene-trigger')!.build(ctxFor(d), {
            cutscene: 3,
            w: 4,
            h: 2,
            d: 1,
            once: true,
            who: '0',
        });
        if (isError(r)) throw new Error(r.error);
        expect(r.objects).toHaveLength(1);
        const det = r.objects[0]!;
        expect(det.links).toEqual([3]);
        expect(det.scale).toEqual([4, 2, 1]); // taken from the level's existing detector body, then resized
        expect(det.template).toBe(1);
        const byName = Object.fromEntries(det.fields!.map((f) => [f.name, f.value]));
        expect(byName).toMatchObject({ 'Trigger Message': 4, 'Detect Only Once?': 1, 'Detect Which?': 0 });
        const stack = new CommandStack(d);
        const before = d.size;
        stack.run(new ApplyTemplateCommand(r, 'Template'));
        expect(d.size).toBe(before + 1);
        expect(validateLogic(d, env).filter((i) => i.ids.includes(det.id))).toEqual([]);
        stack.undo();
        expect(d.size).toBe(before);
        stack.redo();
        expect(d.has(det.id)).toBe(true);
    });

    it('creates a new cut scene player with ids that link up', () => {
        const d = level();
        const r = templateById('cutscene-trigger')!.build(ctxFor(d), { sequence: 77 });
        if (isError(r)) throw new Error(r.error);
        expect(r.objects).toHaveLength(2);
        const [cs, det] = r.objects as [LevelObject, LevelObject];
        expect(cs.class).toBe('Stomp Player');
        expect(det.links).toEqual([cs.id]);
        expect(cs.fields!.find((f) => f.name === 'Sequence List')!.value).toEqual([77]);
        expect(new Set([cs.id, det.id]).size).toBe(2);
        expect(d.has(cs.id)).toBe(false);
    });

    it('lever opens a door, or unlocks it', () => {
        const d = level();
        const open = templateById('lever-door')!.build(ctxFor(d), { door: 4 });
        if (isError(open)) throw new Error(open.error);
        const lever = open.objects[0]!;
        expect(lever.links).toEqual([4]);
        // class defaults (On / Off) mean no overrides
        expect(lever.fields).toEqual([]);
        const unlock = templateById('lever-door')!.build(ctxFor(d), { door: 4, mode: 'unlock' });
        if (isError(unlock)) throw new Error(unlock.error);
        expect(Object.fromEntries(unlock.objects[0]!.fields!.map((f) => [f.name, f.value]))).toEqual({
            'Message to send when on': 3,
            'Message to send when off': 2,
        });
        expect('error' in templateById('lever-door')!.build(ctxFor(d), {})).toBe(true);
    });

    it('kill all enemies: counter counts the selected creatures and the creatures link to it', () => {
        const d = level();
        d.insert(obj(30, '0x20005'));
        d.insert(obj(31, '0x20005', { fields: [field('Message To Send', '0x20005', 1)] }));
        const r = templateById('kill-all-gate')!.build(ctxFor(d, [30, 31]), { gate: 4 });
        if (isError(r)) throw new Error(r.error);
        const counter = r.objects[0]!;
        expect(Object.fromEntries(counter.fields!.map((f) => [f.name, f.value]))).toMatchObject({
            'Initial Counter Value': 2,
            'Message To Send': 3,
        });
        expect(counter.links).toEqual([4]);
        expect(r.links).toEqual([
            { from: 30, to: counter.id },
            { from: 31, to: counter.id },
        ]);
        expect(r.warnings.join()).toMatch(/#31 sends "On" on death/);
        const stack = new CommandStack(d);
        stack.run(new ApplyTemplateCommand(r, 'Template'));
        expect(d.get(30)!.links).toEqual([counter.id]);
        const g = buildLogicGraph(d, env);
        expect(g.edges.some((e) => e.from === 30 && e.to === counter.id)).toBe(true);
        stack.undo();
        expect(d.get(30)!.links).toEqual([]);
        expect('error' in templateById('kill-all-gate')!.build(ctxFor(d), { gate: 4 })).toBe(true);
    });

    it('reports a missing class instead of building a half setup', () => {
        const d = level();
        const r = templateById('pressure-plates')!.build(ctxFor(d), { target: 4, count: 3 });
        expect(isError(r) && r.error).toMatch(/no pressure plate class/);
    });

    it('timed trigger: volume starts a timer that fires at the target', () => {
        const d = level();
        const r = templateById('timer-door')!.build(ctxFor(d), { target: 4, delay: 2, then: 'Unlock' });
        if (isError(r)) throw new Error(r.error);
        const det = r.objects.find((o) => o.class === 'Trigger Detector')!;
        const timer = r.objects.find((o) => o.class === 'Timer')!;
        expect(det.links).toEqual([timer.id]);
        expect(timer.links).toEqual([4]);
        const fv = (o: LevelObject, n: string) => o.fields!.find((f) => f.name === n)!.value;
        expect(fv(timer, 'Time Until Trigger')).toBe(2);
        expect(fv(timer, 'Trigger Message')).toBe(3);
        expect(fv(det, 'Trigger Message')).toBe(1);
        expect('error' in templateById('timer-door')!.build(ctxFor(d), {})).toBe(true);
    });

    it('key lock sends Unlock to the door', () => {
        const d = level();
        const k = templateById('key-lock')!.build(ctxFor(d), { door: 4, code: 49 });
        if (isError(k)) throw new Error(k.error);
        const lock = k.objects[0]!;
        expect(lock.links).toEqual([4]);
        expect(Object.fromEntries(lock.fields!.map((f) => [f.name, f.value]))).toEqual({
            'Lock Code': 49,
            'Trigger To Send': 3,
        });
    });

    it('places a quest object and its start / complete volumes', () => {
        const d = level();
        d.insert(obj(40, '0x2000c'));
        const r = templateById('quest-object')!.build(ctxFor(d), { classId: 4000, start: true, complete: true });
        if (isError(r)) throw new Error(r.error);
        const q = r.objects.find((o) => o.class !== 'Trigger Detector')!;
        expect(Number(q.resource) & 0xffff).toBe(4000);
        expect(Number(q.resource) >>> 16).toBe(2);
        const vols = r.objects.filter((o) => o.class === 'Trigger Detector');
        expect(vols).toHaveLength(2);
        expect(vols.every((v) => v.links!.join() === String(q.id))).toBe(true);
        const msgs = vols.map((v) => v.fields!.find((f) => f.name === 'Trigger Message')!.value);
        expect(msgs).toEqual([1, 18]);
        const plain = templateById('quest-object')!.build(ctxFor(d), { start: false });
        if (isError(plain)) throw new Error(plain.error);
        expect(plain.objects).toHaveLength(1);
        expect(plain.objects[0]!.resource).toBe('0x2000c');
    });

    it('volume message and level exit', () => {
        const d = level();
        const r = templateById('volume-message')!.build(ctxFor(d, [4]), { message: 'Unlock' });
        if (isError(r)) throw new Error(r.error);
        expect(r.objects[0]!.links).toEqual([4]);
        const x = templateById('level-exit')!.build(ctxFor(d), {});
        if (isError(x)) throw new Error(x.error);
        expect(x.objects[0]!.class).toBe('Level Transition');
        expect(TEMPLATES.map((t) => t.id)).toContain('pressure-plates');
    });
});
