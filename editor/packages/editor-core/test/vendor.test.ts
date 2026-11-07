// Vendor stock, settings and price edits on synthetic classes (no game data).
import {
    FIELD_INT,
    FIELD_FLOAT,
    FLAG_VISIBLE,
    ODB_CLASS_TYPE,
    joinClassRecord,
    parseClassBody,
    parseLevelHeader,
    splitClassRecord,
    writeLevelHeader,
    type ClassFieldDef,
    type LvlObject,
    type OdbIndex,
} from '@drakan/formats';
import { describe, expect, it } from 'vitest';
import {
    ITEM_PRICE_FIELD,
    ModError,
    SchemaProvider,
    VENDOR_FIELD,
    buyBackPrice,
    documentFromLvl,
    editObjects,
    effectiveValue,
    itemPrice,
    priceChanges,
    priceProblem,
    readVendor,
    salePrice,
    stockEntries,
    stockLists,
    stockSlots,
    vendorIssues,
    vendorSchemaOf,
    writeSetting,
    writeStock,
    CommandStack,
    type StockEntry,
} from '../src/index.ts';

const enc = (s: string): Uint8Array => Uint8Array.from([...s].map((c) => c.charCodeAt(0)));
const bits = (v: number): number => {
    const d = new DataView(new ArrayBuffer(4));
    d.setFloat32(0, v);
    return d.getUint32(0);
};

// Level ODB "SHOPLVL" is dependency 5, RESOURCES is 3 and MAGIC is 4 of the level; the shop class sees RESOURCES as its
// own db 4 and MAGIC as db 6 (references are relative to the owning ODB's .DB list, like the real data).
const header = parseLevelHeader(
    writeLevelHeader({
        name: { raw: enc('T'), text: 'T' },
        maxWidth: 1,
        maxHeight: 1,
        dependencies: [
            { dbIndex: 3, zero: 0, path: { raw: enc('..\\..\\Common\\Resources\\Resources'), text: '' } },
            { dbIndex: 4, zero: 0, path: { raw: enc('..\\..\\Common\\Magic\\Magic'), text: '' } },
            { dbIndex: 5, zero: 0, path: { raw: enc('..\\..\\Levels\\LEVEL1TS'), text: '' } },
        ],
        trailer: new Uint8Array(0),
    }),
);

// vendor class: 0 saleMod, 1 buyMod, 2 infinite (array of class), 3 limited (array of class), 4 purchased
const shopFields = [
    { type: FIELD_FLOAT, name: VENDOR_FIELD.saleMod },
    { type: FIELD_FLOAT, name: VENDOR_FIELD.buyMod },
    { type: 0x1003, name: VENDOR_FIELD.infinite },
    { type: 0x1003, name: VENDOR_FIELD.limited },
    { type: 7, name: VENDOR_FIELD.purchased },
];
const ref = (db: number, id: number): number => ((db << 16) | id) >>> 0;
// data: 5 field dwords, then infinite [res 10] at 5, limited [res 11, res 11, magic 20] at 6..8
const shopData = [
    bits(1),
    bits(0.7),
    ((1 << 16) | 5) >>> 0,
    ((3 << 16) | 6) >>> 0,
    4,
    ref(4, 10),
    ref(4, 11),
    ref(4, 11),
    ref(6, 20),
];
const itemFields: ClassFieldDef[] = [
    { type: 1, name: 'Quantity' },
    { type: FIELD_INT, name: ITEM_PRICE_FIELD },
];
const item = (id: number, name: string, price: number) => ({
    id,
    name,
    group: 7,
    runtimeClass: 0x1000e,
    fields: itemFields,
    data: [1, price],
});
const odbs: OdbIndex = new Map([
    [
        'LEVEL1TS',
        new Map([
            [
                50,
                {
                    id: 50,
                    name: 'Blacksmith',
                    group: 1,
                    runtimeClass: 0x100ac,
                    rflClass: 172,
                    fields: shopFields,
                    data: shopData,
                },
            ],
            [
                51,
                {
                    id: 51,
                    name: 'Villager',
                    group: 1,
                    runtimeClass: 0x10029,
                    fields: [{ type: 1, name: 'Health' }],
                    data: [5],
                },
            ],
        ]),
    ],
    [
        'RESOURCES',
        new Map([
            [10, item(10, 'Rusty Sword', 165)],
            [11, item(11, 'Wood Shield', 135)],
            [12, item(12, 'War Bow', 850)],
        ]),
    ],
    ['MAGIC', new Map([[20, item(20, 'Fireball Unlocker', 270)]])],
]);
const deps = { LEVEL1TS: { 4: 'RESOURCES', 6: 'MAGIC' } };

function mk(id: number, classId: number, over: Partial<LvlObject> = {}): LvlObject {
    return {
        id,
        classId,
        dbIndex: 5,
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
    const { doc } = documentFromLvl({ header, list: { objects: [mk(1, 50), mk(2, 51)], warnings: [] } }, odbs, 'T');
    const provider = new SchemaProvider(header, odbs, deps);
    const shop = doc.get(1)!;
    return { doc, provider, shop, schema: provider.schemaFor(shop.resource)! };
}

const run = (stack: CommandStack, cmd: ReturnType<typeof editObjects>): void => {
    if (!cmd) throw new Error('the edit changed nothing');
    stack.run(cmd);
};

describe('vendor detection and stock', () => {
    it('recognises the shop class by its stock and price fields only', () => {
        const { doc, provider } = load();
        expect(vendorSchemaOf(doc.get(1)!, provider)?.name).toBe('Blacksmith');
        expect(vendorSchemaOf(doc.get(2)!, provider)).toBeNull();
        expect(vendorSchemaOf(doc.get(1)!, null)).toBeNull();
    });

    it('reads stock from the class defaults, grouping repeated limited items into a count', () => {
        const { shop, schema, provider } = load();
        const v = readVendor(shop, schema, provider);
        expect(v.entries).toEqual([
            { stem: 'RESOURCES', id: 10, mode: 'infinite', count: 1 },
            { stem: 'RESOURCES', id: 11, mode: 'limited', count: 2 },
            { stem: 'MAGIC', id: 20, mode: 'limited', count: 1 },
        ]);
        expect(v.settings.saleMod).toBe(1);
        expect(v.settings.buyMod).toBeCloseTo(0.7, 5);
        expect(v.unresolved).toEqual([]);
    });

    it('round-trips entries through the two lists', () => {
        const entries: StockEntry[] = [
            { stem: 'RESOURCES', id: 10, mode: 'infinite', count: 5 },
            { stem: 'RESOURCES', id: 11, mode: 'limited', count: 3 },
            { stem: 'MAGIC', id: 20, mode: 'limited', count: 0 },
        ];
        const lists = stockLists(entries);
        expect(lists.infinite).toEqual([{ stem: 'RESOURCES', id: 10 }]);
        // a limited entry always sells at least one unit
        expect(lists.limited.map((x) => `${x.stem}:${x.id}`)).toEqual([
            'RESOURCES:11',
            'RESOURCES:11',
            'RESOURCES:11',
            'MAGIC:20',
        ]);
        expect(stockEntries(lists.infinite, lists.limited).map((e) => [e.id, e.mode, e.count])).toEqual([
            [10, 'infinite', 1],
            [11, 'limited', 3],
            [20, 'limited', 1],
        ]);
    });

    it('ignores repeated infinite items', () => {
        const e = stockEntries(
            [
                { stem: 'RESOURCES', id: 10 },
                { stem: 'RESOURCES', id: 10 },
            ],
            [],
        );
        expect(e).toHaveLength(1);
    });

    it('writes the stock as object overrides (one undo step) and reads it back', () => {
        const { doc, shop, schema, provider } = load();
        const stack = new CommandStack(doc);
        const next: StockEntry[] = [
            { stem: 'RESOURCES', id: 12, mode: 'infinite', count: 1 },
            { stem: 'RESOURCES', id: 10, mode: 'limited', count: 2 },
            { stem: 'MAGIC', id: 20, mode: 'limited', count: 1 },
        ];
        run(
            stack,
            editObjects(doc, [1], 'Edit stock', (o) => {
                expect(writeStock(o, schema, provider, next).unreachable).toEqual([]);
            }),
        );
        const infinite = schema.fields.find((f) => f.name === VENDOR_FIELD.infinite)!;
        expect(effectiveValue(doc.get(1)!, infinite).value).toEqual([ref(4, 12)]);
        const limited = schema.fields.find((f) => f.name === VENDOR_FIELD.limited)!;
        expect(effectiveValue(doc.get(1)!, limited).value).toEqual([ref(4, 10), ref(4, 10), ref(6, 20)]);
        expect(readVendor(doc.get(1)!, schema, provider).entries.map((e) => [e.id, e.mode, e.count])).toEqual([
            [12, 'infinite', 1],
            [10, 'limited', 2],
            [20, 'limited', 1],
        ]);
        stack.undo();
        expect(readVendor(doc.get(1)!, schema, provider).entries).toHaveLength(3);
        expect(readVendor(shop, schema, provider).entries[0]!.id).toBe(10);
    });

    it('drops the override when the stock equals the class default again', () => {
        const { doc, shop, schema, provider } = load();
        const same = readVendor(shop, schema, provider).entries;
        // an unchanged stock is no edit at all (editObjects finds nothing to do)
        expect(editObjects(doc, [1], 'noop', (o) => void writeStock(o, schema, provider, same))).toBeNull();
        expect(doc.get(1)!.fields ?? []).toHaveLength(0);
    });

    it('reports items the shop cannot name instead of writing a wrong reference', () => {
        const { doc, schema, provider } = load();
        const bad: StockEntry = { stem: 'SOUND', id: 3, mode: 'limited', count: 1 };
        let unreachable: StockEntry[] = [];
        run(
            new CommandStack(doc),
            editObjects(doc, [1], 'x', (o) => {
                unreachable = writeStock(o, schema, provider, [bad]).unreachable;
            }),
        );
        expect(unreachable).toEqual([bad]);
        expect(readVendor(doc.get(1)!, schema, provider).entries).toEqual([]);
    });

    it('writes price modifiers as floats and keeps unknown fields untouched', () => {
        const { doc, schema, provider } = load();
        run(
            new CommandStack(doc),
            editObjects(doc, [1], 'mod', (o) => {
                writeSetting(o, schema, 'saleMod', 1.5);
                writeSetting(o, schema, 'buyMod', 0.25);
            }),
        );
        const s = readVendor(doc.get(1)!, schema, provider).settings;
        expect(s.saleMod).toBe(1.5);
        expect(s.buyMod).toBe(0.25);
        expect(() => writeSetting(doc.get(1)!, schema, 'repairQuality', 1)).toThrow(/no field/);
    });
});

describe('prices', () => {
    it('scales and rounds', () => {
        expect(salePrice(165, 1)).toBe(165);
        expect(salePrice(165, 2)).toBe(330);
        expect(salePrice(135, 0.5)).toBe(68);
        expect(buyBackPrice(250, 0.7)).toBe(175);
        expect(itemPrice(new Map([['RESOURCES:10', 99]]), 'RESOURCES', 10, 165)).toBe(99);
        expect(itemPrice(new Map(), 'RESOURCES', 10, 165)).toBe(165);
    });

    it('validates a price', () => {
        expect(priceProblem(100)).toBeNull();
        expect(priceProblem(-1)).toMatch(/negative/);
        expect(priceProblem(1.5)).toMatch(/whole/);
        expect(priceProblem(Number.NaN)).toMatch(/whole/);
        expect(priceProblem(1e9)).toMatch(/not supported/);
    });

    const recOf = (price: number): Uint8Array => {
        const data = [1, price];
        const body = new Uint8Array(30 + data.length * 4);
        const dv = new DataView(body.buffer);
        dv.setUint32(22, data.length, true);
        dv.setUint32(26, data.length, true);
        data.forEach((v, i) => dv.setUint32(30 + i * 4, v, true));
        return joinClassRecord('Rusty Sword', body);
    };
    const src = (stem: string, id: number) =>
        stem === 'RESOURCES' && id === 10
            ? { file: 'RESOURES.ODB', group: 7, record: recOf(165), defs: itemFields }
            : null;

    it('turns a price edit into a patched class record and skips unchanged prices', () => {
        const r = priceChanges(new Map([['RESOURCES:10', 40]]), src);
        expect(r).toHaveLength(1);
        const c = r[0]!;
        expect([c.file, c.type, c.id, c.sub, c.isNew]).toEqual(['RESOURES.ODB', ODB_CLASS_TYPE, 10, 7, false]);
        expect(parseClassBody(splitClassRecord(c.data).body)!.data).toEqual([1, 40]);
        expect(priceChanges(new Map([['RESOURCES:10', 165]]), src)).toEqual([]);
    });

    it('refuses a price it cannot carry', () => {
        expect(() => priceChanges(new Map([['RESOURCES:99', 5]]), src)).toThrow(ModError);
        expect(() => priceChanges(new Map([['RESOURCES:10', -5]]), src)).toThrow(/negative/);
        expect(() =>
            priceChanges(new Map([['RESOURCES:10', 5]]), () => ({
                file: 'X.ODB',
                group: 1,
                record: recOf(1),
                defs: [{ type: 1, name: 'Quantity' }],
            })),
        ).toThrow(/no "Cost to Buy/);
    });
});

describe('class queries', () => {
    it('finds classes by field and reads a number default', () => {
        const { provider } = load();
        const all = provider.classesWithField(ITEM_PRICE_FIELD);
        expect(all.map((c) => [c.stem, c.id, c.value]).sort()).toEqual([
            ['MAGIC', 20, 270],
            ['RESOURCES', 10, 165],
            ['RESOURCES', 11, 135],
            ['RESOURCES', 12, 850],
        ]);
        expect(provider.classNumber('RESOURCES', 12, ITEM_PRICE_FIELD)).toEqual({ index: 1, value: 850 });
        expect(provider.classNumber('RESOURCES', 99, ITEM_PRICE_FIELD)).toBeNull();
    });
});

describe('vendor issues', () => {
    it('flags an empty shop, free items and double listing', () => {
        const { shop, schema, provider } = load();
        const v = readVendor(shop, schema, provider);
        expect(vendorIssues(v, () => 10)).toEqual([]);
        expect(vendorIssues({ ...v, entries: [] }, () => 10)[0]!.message).toMatch(/nothing for sale/);
        expect(vendorIssues(v, (e) => (e.id === 11 ? 0 : 10)).map((i) => i.message)).toEqual([
            expect.stringMatching(/RESOURCES #11 costs 0/),
        ]);
        expect(vendorIssues(v, (e) => (e.id === 20 ? null : 5))[0]!.message).toMatch(/no price field/);
        const dup = {
            ...v,
            entries: [...v.entries, { stem: 'RESOURCES', id: 10, mode: 'limited' as const, count: 1 }],
        };
        expect(
            vendorIssues(dup, () => 5).some((i) => /limited and as infinite.*keeps only the unlimited/.test(i.message)),
        ).toBe(true);
        expect(vendorIssues({ ...v, settings: { ...v.settings, saleMod: -1 } }, () => 5)[0]!.level).toBe('error');
    });
});

describe('stock slots', () => {
    const e = (id: number, mode: 'infinite' | 'limited', count = 1): StockEntry => ({
        stem: 'RESOURCES',
        id,
        mode,
        count,
    });
    it('counts what the game keeps: one entry per item class, however it is listed', () => {
        const four = [e(1, 'infinite'), e(2, 'infinite'), e(3, 'infinite'), e(4, 'infinite')];
        // an extra item takes a slot, as the five-entry Alchemist with an Iron Mace did (stock=5 in the game log)
        expect(stockSlots([...four, e(9, 'limited')])).toBe(5);
        expect(stockSlots([...four, e(9, 'infinite')])).toBe(5);
        // a limited unit of an unlimited item is absorbed (stock=4 in the game log)
        expect(stockSlots([...four, e(1, 'limited')])).toBe(4);
        // repeated limited units share one entry
        expect(stockSlots([e(9, 'limited', 3)])).toBe(1);
        expect(stockSlots([])).toBe(0);
    });
    it('has no per-shop limit on how many entries are written', () => {
        // the guest accepts 5 (new Alchemist) and 17 (Blacksmith with the Mace) entries; the editor must not clamp them
        const many = Array.from({ length: 40 }, (_, i) => e(100 + i, 'infinite'));
        expect(stockLists(many).infinite).toHaveLength(40);
        expect(stockSlots(many)).toBe(40);
    });
});
