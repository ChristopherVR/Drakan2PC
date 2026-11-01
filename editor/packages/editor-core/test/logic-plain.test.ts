// Synthetic only: no game bytes. The plain-language layer over the logic model: message words, names, inline problems.
import { logicClass } from '@drakan/formats';
import { describe, expect, it } from 'vitest';
import {
    LevelDocument,
    NEW_NODE_KINDS,
    acceptedMessages,
    allPlainMessages,
    buildLogicGraph,
    defaultLinkMessage,
    graphProblems,
    linkWithMessage,
    matchScore,
    messagesFor,
    nodeName,
    nodeSummary,
    outgoingMessages,
    pinPhrase,
    plainMessage,
    plainRoleName,
    type ClassSchema,
    type LevelObject,
    type LogicEnv,
    type SchemaField,
} from '../src/index.ts';

const CLASSES: Record<string, { rfl: number; name: string; fields: { name: string; def: number }[] }> = {
    trig: {
        rfl: 0x3d,
        name: 'Trigger Detector',
        fields: [
            { name: 'Task', def: 6 },
            { name: 'Initial State', def: 0 },
            { name: 'Trigger Message', def: 1 },
        ],
    },
    door: { rfl: 0x13, name: 'Town Door', fields: [{ name: 'Locked', def: 0 }] },
    cut: {
        rfl: 0x33,
        name: 'Stomp Player',
        fields: [
            { name: 'Initial State', def: 0 },
            { name: 'Message To Play Next', def: 4 },
        ],
    },
    quest: { rfl: 0x68, name: 'Find the Key', fields: [] },
    counter: {
        rfl: 0x07,
        name: 'Countdown',
        fields: [
            { name: 'Decrement (-1) Message', def: 8 },
            { name: 'Message To Send', def: 1 },
        ],
    },
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
                classId: 1,
                name: c.name,
                db: 'SYSTEM',
                deps: new Map(),
                fields: c.fields.map((f, index): SchemaField => ({
                    index,
                    type: 7,
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
        transform: { pos: [0, 0, 0], rot: [0, 0, 0] },
        props: {},
        origin: id,
        links: [],
        fields: [],
        ...over,
    };
}
const msg = (resource: string, name: string, value: number) => {
    const f = env.schemaFor(resource)!.fields.find((x) => x.name === name)!;
    return { slot: -1, fieldIndex: f.index, type: f.type, name, value };
};

describe('plain messages', () => {
    it('uses words from what the target does', () => {
        expect(plainMessage(1, 'door')).toBe('opens');
        expect(plainMessage(0, 'door')).toBe('closes');
        expect(plainMessage(3, 'door')).toBe('unlocks');
        expect(plainMessage(4, 'cutscene')).toBe('plays the cut scene');
        expect(plainMessage(30, 'quest')).toBe('starts the quest');
        expect(plainMessage(18, 'quest')).toBe('completes the quest');
        expect(plainMessage(9, 'door')).toBe('sends "Gib"');
    });
    it('never offers a hex or number as the verb', () => {
        for (const m of allPlainMessages()) expect(m.verb).not.toMatch(/0x|^\d+$/);
        expect(allPlainMessages().some((m) => m.value === 32)).toBe(false);
    });
    it('puts the most useful message first for each target', () => {
        expect(defaultLinkMessage(logicClass(0x13))).toBe(1);
        expect(defaultLinkMessage(logicClass(0x33))).toBe(4);
        expect(defaultLinkMessage(logicClass(0x68))).toBe(30);
        expect(messagesFor('door').map((m) => m.verb)).toEqual(['opens', 'closes', 'unlocks', 'locks']);
        expect(messagesFor(undefined).length).toBeGreaterThan(4);
    });
});

describe('names and pins', () => {
    it('names roles in plain words', () => {
        expect(plainRoleName(logicClass(0x3d)!)).toBe('Trigger');
        expect(plainRoleName(logicClass(0x13)!)).toBe('Door');
        expect(plainRoleName(logicClass(0x9d)!)).toBe('NPC');
        expect(plainRoleName(logicClass(0x31)!)).toBe('Enemy');
        expect(plainRoleName(logicClass(0x68)!)).toBe('Quest');
        expect(plainRoleName(logicClass(0xdc)!)).toBe('Quest marker');
        expect(nodeName({ class: ' Town Door ', id: 5 })).toBe('Town Door');
        expect(nodeName({ class: '', id: 5 }, logicClass(0x13))).toBe('Door');
    });
    it('turns port labels into sentences', () => {
        expect(pinPhrase(logicClass(0x3d)!, 'enters')).toBe('when the player steps in');
        expect(pinPhrase(logicClass(0x3d)!, 'something new')).toBe('when something new');
        expect(nodeSummary(logicClass(0x3d)!)).toBe('Fires when the player steps in');
        expect(nodeSummary(logicClass(0x13)!)).toContain('opens');
    });
});

describe('link edits', () => {
    it('links and sets the message in one go, without duplicating a link', () => {
        const o = obj(1, 'trig');
        expect(linkWithMessage(o, env, 2, 4)).toEqual(['linked', 'message']);
        expect(o.links).toEqual([2]);
        expect(linkWithMessage(o, env, 2, 4)).toEqual(['message']);
        expect(o.links).toEqual([2]);
        expect(linkWithMessage(o, env, 1, null)).toEqual([]);
    });
    it('leaves the message alone for a class that sends none', () => {
        const d = obj(4, 'door');
        expect(linkWithMessage(d, env, 5, 1)).toEqual(['linked']);
    });
});

describe('inline problems', () => {
    function level(): LevelDocument {
        const d = new LevelDocument();
        d.insert(obj(1, 'trig', { links: [2], fields: [msg('trig', 'Trigger Message', 4)] }));
        d.insert(obj(2, 'door'));
        d.insert(obj(3, 'trig'));
        d.insert(obj(4, 'cut'));
        d.insert(obj(5, 'counter'));
        d.insert(obj(6, 'quest'));
        d.insert(obj(7, 'trig', { links: [4], fields: [msg('trig', 'Trigger Message', 4)] }));
        return d;
    }
    it('reports a trigger linked to nothing, a message the target ignores, and orphans', () => {
        const d = level();
        const g = buildLogicGraph(d, env);
        const p = graphProblems(d, env, g);
        expect(p.get(3)?.map((x) => x.code)).toEqual(['no-target']);
        const ign = p.get(1)?.find((x) => x.code === 'ignored');
        expect(ign?.other).toBe(2);
        expect(ign?.text).toContain('Town Door ignores "plays the cut scene"');
        expect(p.get(1)?.some((x) => /0x|message \d/.test(x.text))).toBe(false);
        expect(p.get(5)?.[0]?.code).toBe('never-counts');
        expect(p.get(6)?.[0]?.code).toBe('nothing-starts-quest');
        expect(p.get(4)).toBeUndefined();
    });
    it('is quiet for objects that are wired up', () => {
        const d = level();
        d.get(3)!.links = [2];
        d.get(3)!.fields = [msg('trig', 'Trigger Message', 1)];
        d.get(5)!.links = [2];
        d.get(7)!.links = [5];
        d.get(7)!.fields = [msg('trig', 'Trigger Message', 8)];
        d.get(1)!.links = [4];
        const g = buildLogicGraph(d, env);
        const p = graphProblems(d, env, g);
        expect([...p.keys()].filter((k) => k !== 6)).toEqual([]);
    });
    it('knows what a counter listens for', () => {
        const d = level();
        const set = acceptedMessages(d.get(5)!, logicClass(0x07)!, env);
        expect([...set!]).toEqual([8]);
        expect(outgoingMessages(d.get(1)!, env, logicClass(0x3d))[0]?.value).toBe(4);
    });
});

describe('search and kinds', () => {
    it('ranks name matches above role matches and finds ids', () => {
        expect(matchScore('', 'Town Door', 'Door', 5)).toBe(1);
        expect(matchScore('town', 'Town Door', 'Door', 5)).toBeGreaterThan(matchScore('door', 'Town Door', 'Door', 5));
        expect(matchScore('trigger', 'Zone 1', 'Trigger', 9)).toBeGreaterThan(0);
        expect(matchScore('#9', 'Zone 1', 'Trigger', 9)).toBeGreaterThan(0);
        expect(matchScore('#8', 'Zone 1', 'Trigger', 9)).toBe(0);
        expect(matchScore('zzz', 'Zone 1', 'Trigger', 9)).toBe(0);
    });
    it('lists new node kinds with plain titles', () => {
        expect(NEW_NODE_KINDS.map((k) => k.title)).toContain('Pressure plate');
        for (const k of NEW_NODE_KINDS) expect(logicClass(k.rfl)?.role).toBe(k.role);
    });
});
