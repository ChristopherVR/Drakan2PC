// Synthetic class schemas only; no game bytes.
import {
    FLAG_VISIBLE,
    paramValue,
    parseLevelHeader,
    stringToWords,
    writeLevelHeader,
    type LvlObject,
    type LvlObjectList,
    type OdbIndex,
} from '@drakan/formats';
import { describe, expect, it } from 'vitest';
import {
    CommandStack,
    SchemaProvider,
    applyDocumentToLvl,
    diffDocument,
    documentFromLvl,
    editObjects,
    effectiveValue,
    fieldCategory,
    setObjectField,
} from '../src/index.ts';

const enc = (s: string): Uint8Array => Uint8Array.from([...s].map((c) => c.charCodeAt(0)));
const bits = (v: number): number => {
    const d = new DataView(new ArrayBuffer(4));
    d.setFloat32(0, v);
    return d.getUint32(0);
};

const header = parseLevelHeader(
    writeLevelHeader({
        name: { raw: enc('T'), text: 'T' },
        maxWidth: 1,
        maxHeight: 1,
        dependencies: [
            { dbIndex: 2, zero: 0, path: { raw: enc('..\\..\\Common\\NPC\\NPCs'), text: '' } },
            { dbIndex: 3, zero: 0, path: { raw: enc('..\\..\\Common\\Resources\\Resources'), text: '' } },
        ],
        trailer: new Uint8Array(0),
    }),
);

// NPCS class 10: health (int 35), speed (float 1.5), alert (enum 0), name (string "orc"), weapon (class ref db 2 = RESOURES)
// and a class-ref array. RESOURES class 77 is the sword.
const orcFields = [
    { type: 1, name: 'Initial Health' },
    { type: 2, name: 'Walk Speed' },
    { type: 7, name: 'Alert?' },
    { type: 0x0a, name: 'Display Name' },
    { type: 0x4003, name: 'Weapon Object' },
    { type: 0x1003, name: 'Spawn Object' },
];
// data layout: 6 field dwords, then the string (1 dword) at 6 and the array (1 element) at 7
const orcData = [
    35,
    bits(1.5),
    0,
    ((1 << 16) | 6) >>> 0,
    0x2004d,
    ((1 << 16) | 7) >>> 0,
    ...stringToWords('orc'),
    0x2004d,
];
const odbs: OdbIndex = new Map([
    [
        'NPCS',
        new Map([[10, { id: 10, name: 'Orc', group: 1, runtimeClass: 0x1001c, fields: orcFields, data: orcData }]]),
    ],
    ['RESOURCES', new Map([[77, { id: 77, name: 'Sword', group: 1, runtimeClass: 0x10036 }]])],
]);
const deps = { NPCS: { 2: 'RESOURCES' } };

function mk(id: number, over: Partial<LvlObject> = {}): LvlObject {
    return {
        id,
        classId: 10,
        dbIndex: 2,
        prefix0: 0,
        prefix1: 0,
        pos: [id, 0, 0],
        flags: FLAG_VISIBLE,
        initialEvents: 1,
        radius: null,
        links: [],
        rot: [0, 0, 0],
        scale: null,
        preSentinel: 0,
        sentinel: 0xffffffff,
        data: [],
        params: [],
        ...over,
    };
}

function load() {
    const list: LvlObjectList = { objects: [mk(1), mk(2)], warnings: [] };
    const { doc } = documentFromLvl({ header, list }, odbs, 'T');
    const provider = new SchemaProvider(header, odbs, deps);
    return { list, doc, provider, schema: provider.schemaFor(doc.get(1)!.resource)! };
}

describe('SchemaProvider', () => {
    it('lists every class field with its default and resolves references through the owner deps', () => {
        const { schema, provider } = load();
        expect(schema.fields.map((f) => [f.name, f.default])).toEqual([
            ['Initial Health', 35],
            ['Walk Speed', 1.5],
            ['Alert?', 0],
            ['Display Name', 'orc'],
            ['Weapon Object', 0x2004d],
            ['Spawn Object', [0x2004d]],
        ]);
        const t = provider.resolveRef(schema, 0x4003, 0x2004d);
        expect(t).toMatchObject({ stem: 'RESOURCES', id: 77, name: 'Sword' });
        expect(provider.refValueFor(schema, 'RESOURCES', 77)).toBe(0x2004d);
        expect(provider.refValueFor(schema, 'NPCS', 10)).toBe(10);
        expect(provider.refValueFor(schema, 'SOUND', 1)).toBeNull();
        expect(provider.schemaFor('0x20063')).toBeUndefined();
    });

    it('treats an enum that only holds 0 and 1 as a toggle', () => {
        const { provider } = load();
        expect(provider.isToggle('Alert?', 7)).toBe(true);
        expect(provider.isToggle('Alert?', 7, [2])).toBe(false);
        expect(provider.isToggle('Initial Health', 1)).toBe(false);
    });

    it('groups fields by type and name', () => {
        expect(fieldCategory('Hit Sound', 5)).toBe('Sound');
        expect(fieldCategory('Initial Health', 1)).toBe('Combat');
        expect(fieldCategory('Walk Speed', 2)).toBe('Movement');
        expect(fieldCategory('Something', 1)).toBe('General');
    });
});

describe('editing fields through the command stack', () => {
    it('adds an override, undoes it, and exports it into the object list', () => {
        const { doc, list, schema } = load();
        const health = schema.fields[0]!;
        const stack = new CommandStack(doc);
        stack.run(editObjects(doc, [1], 'Set health', (o) => setObjectField(o, health, 90))!);
        expect(effectiveValue(doc.get(1)!, health)).toEqual({ value: 90, overridden: true });
        expect(diffDocument(list, doc).params).toEqual([1]);
        const out = applyDocumentToLvl(list, doc).list.objects[0]!;
        expect(out.params.map((p) => [p.fieldIndex, p.name])).toEqual([[0, 'Initial Health']]);
        expect(paramValue(out, 0)).toBe(90);
        stack.undo();
        expect(effectiveValue(doc.get(1)!, health).overridden).toBe(false);
        expect(diffDocument(list, doc).params).toEqual([]);
    });

    it('a value equal to the class default drops the override; strings and arrays export', () => {
        const { doc, list, schema } = load();
        const [health, , , name, , spawn] = schema.fields as [
            (typeof schema.fields)[number],
            ...(typeof schema.fields)[number][],
        ];
        const stack = new CommandStack(doc);
        stack.run(editObjects(doc, [1, 2], 'multi', (o) => setObjectField(o, health!, 10))!);
        stack.run(editObjects(doc, [1, 2], 'back', (o) => setObjectField(o, health!, 35))!);
        expect(diffDocument(list, doc).params).toEqual([]);
        stack.run(
            editObjects(doc, [2], 'text', (o) => {
                setObjectField(o, name!, 'grunt');
                setObjectField(o, spawn!, [0x2004d, 0x2004d]);
            })!,
        );
        const out = applyDocumentToLvl(list, doc).list;
        const o2 = out.objects.find((o) => o.id === 2)!;
        expect(o2.params.map((p) => p.name)).toEqual(['Display Name', 'Spawn Object']);
        expect([0, 1].map((i) => paramValue(o2, i))).toEqual(['grunt', [0x2004d, 0x2004d]]);
        expect(out.objects.find((o) => o.id === 1)!.params).toEqual([]);
    });

    it('multi-selection sets the field on every selected object as one undo step', () => {
        const { doc, schema } = load();
        const speed = schema.fields[1]!;
        const stack = new CommandStack(doc);
        stack.run(editObjects(doc, [1, 2], 'speed', (o) => setObjectField(o, speed, 3))!);
        expect([1, 2].map((i) => effectiveValue(doc.get(i)!, speed).value)).toEqual([3, 3]);
        stack.undo();
        expect([1, 2].map((i) => effectiveValue(doc.get(i)!, speed).overridden)).toEqual([false, false]);
    });
});
