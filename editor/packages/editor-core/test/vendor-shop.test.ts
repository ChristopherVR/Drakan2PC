// "Add shop here": a new shopkeeper, shop and keeper switch on synthetic classes (no game data).
import {
    FIELD_FLOAT,
    FLAG_SCALED,
    FLAG_VISIBLE,
    parseLevelHeader,
    writeLevelHeader,
    type LvlObject,
    type OdbIndex,
} from '@drakan/formats';
import { describe, expect, it } from 'vitest';
import {
    ApplyTemplateCommand,
    CommandStack,
    KEEPER_BOX,
    SchemaProvider,
    VENDOR_FIELD,
    applyDocumentToLvl,
    buildCatalog,
    buildNewShop,
    documentFromLvl,
    effectiveValue,
    isError,
    levelShops,
    readVendor,
    shopkeeperOf,
    type LevelObject,
    type TemplateBuild,
} from '../src/index.ts';

const enc = (s: string): Uint8Array => Uint8Array.from([...s].map((c) => c.charCodeAt(0)));
const bits = (v: number): number => {
    const d = new DataView(new ArrayBuffer(4));
    d.setFloat32(0, v);
    return d.getUint32(0);
};

// one level ODB (dependency 5) holding a shop class, a character class and a trigger box class
const header = parseLevelHeader(
    writeLevelHeader({
        name: { raw: enc('T'), text: 'T' },
        maxWidth: 1,
        maxHeight: 1,
        dependencies: [{ dbIndex: 5, zero: 0, path: { raw: enc('..\\..\\Levels\\LEVEL1TS'), text: '' } }],
        trailer: new Uint8Array(0),
    }),
);

const shopClass = (id: number, name: string, defaultOn: number) => ({
    id,
    name,
    group: 1,
    runtimeClass: 0x100ac,
    rflClass: 172,
    fields: [
        { type: 7, name: VENDOR_FIELD.defaultOn },
        { type: FIELD_FLOAT, name: VENDOR_FIELD.saleMod },
        { type: 0x1003, name: VENDOR_FIELD.infinite },
        { type: 0x1003, name: VENDOR_FIELD.limited },
    ],
    data: [defaultOn, bits(1), 0, 0],
});
const odbs: OdbIndex = new Map([
    [
        'LEVEL1TS',
        new Map([
            [50, shopClass(50, 'Smithy', 0)],
            [52, shopClass(52, 'Potions', 1)],
            [
                60,
                {
                    id: 60,
                    name: 'Smith',
                    group: 1,
                    runtimeClass: 0x1009d,
                    rflClass: 157,
                    fields: [{ type: FIELD_FLOAT, name: 'Player Trigger Radius (lu)' }],
                    data: [bits(1)],
                },
            ],
            [
                61,
                {
                    id: 61,
                    name: 'Farmer',
                    group: 1,
                    runtimeClass: 0x1009d,
                    rflClass: 157,
                    fields: [{ type: FIELD_FLOAT, name: 'Player Trigger Radius (lu)' }],
                    data: [bits(1)],
                },
            ],
            [
                70,
                {
                    id: 70,
                    name: 'Trigger Detector',
                    group: 1,
                    runtimeClass: 0x1003d,
                    rflClass: 0x3d,
                    fields: [
                        { type: 7, name: 'Detect Which?' },
                        { type: 7, name: 'Trigger Message' },
                        { type: 7, name: 'Detect Only Once?' },
                    ],
                    data: [2, 1, 1],
                },
            ],
        ]),
    ],
]);

function mk(id: number, classId: number, over: Partial<LvlObject> = {}): LvlObject {
    return {
        id,
        classId,
        dbIndex: 5,
        prefix0: 0,
        prefix1: 0,
        pos: [id * 1000, 0, 0],
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

/** Shop #1 (Smithy) <-> keeper #2 (Smith), a farmer #3, a trigger box #4 and a shop #5 (Potions) without a keeper. */
function load(objects?: LvlObject[]) {
    const lvl = {
        header,
        list: {
            objects: objects ?? [
                mk(1, 50, { links: [2] }),
                mk(2, 60, { links: [1], flags: 0 }),
                mk(3, 61),
                mk(4, 70, { flags: FLAG_VISIBLE | FLAG_SCALED, scale: [1, 1, 1] }),
                mk(5, 52),
            ],
            warnings: [],
        },
    };
    const { doc } = documentFromLvl(lvl, odbs, 'T');
    const env = new SchemaProvider(header, odbs, {});
    const catalog = buildCatalog(header, odbs, doc.objects());
    return { lvl, doc, env, catalog };
}

const built = (r: TemplateBuild | { error: string }): TemplateBuild => {
    if (isError(r)) throw new Error(r.error);
    return r;
};
const field = (o: LevelObject, env: SchemaProvider, name: string) =>
    effectiveValue(
        o,
        env.schemaFor(o.resource)!.fields.find((f) => f.name === name)!,
    ).value;

describe('shops and their keepers', () => {
    it('finds the keeper through the shop link, preferring one that links back', () => {
        const { doc, env } = load();
        expect(shopkeeperOf(doc, doc.get(1)!, env)?.id).toBe(2);
        expect(levelShops(doc, env).map((s) => [s.shop.id, s.keeper?.id])).toEqual([
            [1, 2],
            [5, undefined],
        ]);
    });

    it('ignores linked objects that are not characters', () => {
        const { doc, env } = load([mk(1, 50, { links: [4, 2] }), mk(2, 60), mk(4, 70)]);
        expect(shopkeeperOf(doc, doc.get(1)!, env)?.id).toBe(2);
    });
});

describe('buildNewShop', () => {
    it('makes a shop, a keeper copied from the same shop class and a box that switches the keeper on', () => {
        const { doc, env, catalog } = load();
        const r = built(
            buildNewShop({ doc, env, catalog, at: [5000, 10, 20], selection: [] }, { shopClass: '0x50032' }),
        );
        const [shop, keeper, box] = r.objects as [LevelObject, LevelObject, LevelObject];
        expect([shop.class, keeper.class, box.class]).toEqual(['Smithy', 'Smith', 'Trigger Detector']);
        expect(new Set(r.objects.map((o) => o.id)).size).toBe(3);
        expect(r.objects.every((o) => o.id >= doc.nextId)).toBe(true);
        // linked both ways, keeper first in the shop's list; the box sends On to the keeper
        expect(shop.links).toEqual([keeper.id]);
        expect(keeper.links).toEqual([shop.id]);
        expect(box.links).toEqual([keeper.id]);
        expect(r.links).toEqual([]);
        // keeper copied from #2, shop switched on (the class default is off), class stock
        expect(keeper.template).toBe(2);
        expect(keeper.origin).toBeUndefined();
        expect(field(shop, env, VENDOR_FIELD.defaultOn)).toBe(1);
        expect(readVendor(shop, env.schemaFor(shop.resource)!, env).entries).toEqual([]);
        // the box: On, every time, around the keeper
        expect(field(box, env, 'Trigger Message')).toBe(1);
        expect(field(box, env, 'Detect Only Once?')).toBe(0);
        expect(field(box, env, 'Detect Which?')).toBe(2);
        expect(box.scale).toEqual(KEEPER_BOX);
        for (const o of r.objects) expect(o.transform.pos).toEqual([5000, 10, 20]);
    });

    it('defaults to the most used shop class and copies a keeper of another shop when its own has none', () => {
        const { doc, env, catalog } = load();
        // one instance each: the tie goes by name, so Potions (which has no keeper)
        const r = built(buildNewShop({ doc, env, catalog, at: [0, 0, 0], selection: [] }, { yaw: 1.5 }));
        const [shop, keeper] = r.objects as [LevelObject, LevelObject];
        expect(shop.class).toBe('Potions');
        expect(keeper.class).toBe('Smith');
        expect(keeper.transform.rot[1]).toBe(1.5);
        // Potions is on by default: no override written
        expect(shop.fields ?? []).toEqual([]);
    });

    it('copies the stock and settings of a given shop', () => {
        const { doc, env, catalog } = load();
        doc.get(1)!.fields = [];
        const r = built(buildNewShop({ doc, env, catalog, at: [0, 0, 0], selection: [] }, { like: 1 }));
        expect(r.objects[0]!.class).toBe('Smithy');
        expect(r.objects[0]!.template).toBe(1);
        expect(r.notes.join(' ')).toContain('same stock and prices as #1');
        expect(isError(buildNewShop({ doc, env, catalog, at: [0, 0, 0], selection: [] }, { like: 3 }))).toBe(true);
    });

    it('uses a character class when the level has no shopkeeper, and refuses a level without shop classes', () => {
        const a = load([mk(1, 50), mk(3, 61), mk(4, 70)]);
        const r = built(buildNewShop({ doc: a.doc, env: a.env, catalog: a.catalog, at: [0, 0, 0], selection: [] }));
        expect(r.objects[1]!.class).toBe('Farmer');
        expect(r.warnings.join(' ')).toContain('untested in game');
        const b = load([mk(3, 61)]);
        const noShops = b.catalog.filter((c) => c.rflClass !== 172);
        const e = buildNewShop({ doc: b.doc, env: b.env, catalog: noShops, at: [0, 0, 0], selection: [] });
        expect(isError(e) && e.error).toContain('no shop class');
    });

    it('applies and undoes as one step and writes the links into the LVL object list', () => {
        const { lvl, doc, env, catalog } = load();
        const before = doc.size;
        const r = built(buildNewShop({ doc, env, catalog, at: [7000, 0, 0], selection: [] }));
        const stack = new CommandStack(doc);
        const cmd = new ApplyTemplateCommand(r, 'Add shop');
        stack.run(cmd);
        expect(doc.size).toBe(before + 3);
        const [shopId, keeperId, boxId] = cmd.ids as [number, number, number];
        expect(shopkeeperOf(doc, doc.get(shopId)!, env)?.id).toBe(keeperId);
        const { list } = applyDocumentToLvl(lvl.list, doc);
        const out = (id: number) => list.objects.find((o) => o.id === id)!;
        expect(out(shopId).links).toEqual([keeperId]);
        expect(out(keeperId).links).toEqual([shopId]);
        expect(out(boxId).links).toEqual([keeperId]);
        expect(out(keeperId).classId).toBe(60);
        stack.undo();
        expect(doc.size).toBe(before);
        expect(doc.get(shopId)).toBeUndefined();
    });
});
