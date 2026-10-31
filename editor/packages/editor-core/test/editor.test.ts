// Synthetic only: editor commands (add, multi-edit, copy/paste, snapping, arrange, validation, catalog, history).
import { describe, expect, it } from 'vitest';
import {
    addObject,
    FLAG_SCALED,
    FLAG_VISIBLE,
    paramValue,
    parseLevelHeader,
    removeParam,
    setParamScalar,
    writeLevelHeader,
    writeObjectList,
    type LvlObject,
    type LvlObjectList,
} from '@drakan/formats';
import {
    AddCommand,
    CommandStack,
    CompositeCommand,
    LevelDocument,
    alignPositions,
    applyDocumentToLvl,
    argbToParts,
    buildCatalog,
    copyObjects,
    diffDocument,
    distributePositions,
    documentFromLvl,
    editObjects,
    groupCatalog,
    groupFields,
    iconForName,
    moveCommand,
    nearestObjectPlaneY,
    newObjectFromClass,
    partsToArgb,
    pasteObjects,
    resourceParts,
    searchCatalog,
    snapAngle,
    snapPosition,
    snapToGround,
    snapValue,
    validateDocument,
    widgetFor,
    DEFAULT_SNAP,
    type DocField,
    type LevelObject,
} from '../src/index.ts';

const enc = (s: string): Uint8Array => Uint8Array.from([...s].map((c) => c.charCodeAt(0)));
const nameRaw = (s: string): Uint8Array => enc(`${s}\0\0`);

function mk(id: number, over: Partial<LvlObject> = {}): LvlObject {
    return {
        id,
        classId: 10,
        dbIndex: 2,
        prefix0: 0,
        prefix1: 0,
        pos: [id * 100, 5, id * 10],
        flags: FLAG_VISIBLE,
        initialEvents: 1,
        radius: null,
        links: [],
        rot: [0, 90, 0],
        scale: null,
        preSentinel: 0,
        sentinel: 0xffffffff,
        data: [],
        params: [],
        ...over,
    };
}

const f32bits = (v: number): number => {
    const dv = new DataView(new ArrayBuffer(4));
    dv.setFloat32(0, v);
    return dv.getUint32(0);
};

const baseList = (): LvlObjectList => ({
    objects: [
        mk(2, {
            links: [5],
            data: [f32bits(2.5), 3, 0xff112233],
            params: [
                { fieldIndex: 4, type: 2, nameRaw: nameRaw('Speed'), name: 'Speed' },
                { fieldIndex: 5, type: 7, nameRaw: nameRaw('Mode'), name: 'Mode' },
                { fieldIndex: 6, type: 0x0f, nameRaw: nameRaw('Tint'), name: 'Tint' },
            ],
        }),
        mk(5, { flags: FLAG_VISIBLE | FLAG_SCALED, scale: [1, 2, 3], links: [7] }),
        mk(7, { classId: 11, dbIndex: 3 }),
        mk(9),
    ],
    warnings: [],
});

const header = () =>
    parseLevelHeader(
        writeLevelHeader({
            name: { raw: enc('Test'), text: 'Test' },
            maxWidth: 1,
            maxHeight: 1,
            dependencies: [
                { dbIndex: 2, zero: 0, path: { raw: enc('..\\..\\Common\\System\\System'), text: '' } },
                { dbIndex: 3, zero: 0, path: { raw: enc('..\\..\\Common\\NPC\\NPCs'), text: '' } },
            ],
            trailer: new Uint8Array(0),
        }),
    );

const odbs = new Map([
    [
        'SYSTEM',
        new Map([
            [10, { id: 10, name: 'Crate', group: 1, runtimeClass: 0x100d3 }],
            [12, { id: 12, name: 'Birch Tree 01', group: 1, runtimeClass: 0x10036 }],
        ]),
    ],
    ['NPCS', new Map([[11, { id: 11, name: 'Goblin Soldier', group: 1, runtimeClass: 0x1001c }]])],
]);

function load() {
    const list = baseList();
    const lvl = { header: header(), list };
    const { doc } = documentFromLvl(lvl, odbs, 'Test');
    const catalog = buildCatalog(lvl.header, odbs, doc.objects());
    return { doc, list, catalog, lvl };
}

describe('lvl.ts addObject', () => {
    it('templates the body from an instance of the class and takes the next id', () => {
        const list = baseList();
        const r = addObject(list, { classId: 10, dbIndex: 2, pos: [1, 2, 3], rot: [0, 45, 0] });
        expect(r.from).toBe('template');
        expect(r.templateId).toBe(2);
        expect(r.object.id).toBe(10);
        expect(r.object.links).toEqual([]);
        expect(r.object.params.map((p) => p.name)).toEqual(['Speed', 'Mode', 'Tint']);
        expect(r.object.data).toEqual(list.objects[0]!.data);
        expect(r.object.data).not.toBe(list.objects[0]!.data);
        expect(list.objects.at(-1)).toBe(r.object);
        expect(writeObjectList(list).length).toBeGreaterThan(0);
    });
    it('writes a minimal default body when the class has no instance', () => {
        const list = baseList();
        const r = addObject(list, { classId: 99, dbIndex: 2, pos: [0, 0, 0] });
        expect(r.from).toBe('default');
        expect(r.object.flags).toBe(FLAG_VISIBLE);
        expect(r.object.params).toEqual([]);
        expect(writeObjectList(list)[0]).toBe(5);
    });
    it('rejects bad ids, rotations and classes', () => {
        const list = baseList();
        expect(() => addObject(list, { classId: 10, dbIndex: 2, pos: [0, 0, 0], id: 9 })).toThrow();
        expect(() => addObject(list, { classId: 10, dbIndex: 2, pos: [0, 0, 0], rot: [0.5, 0, 0] })).toThrow();
        expect(() => addObject(list, { classId: 70000, dbIndex: 2, pos: [0, 0, 0] })).toThrow();
        expect(() => addObject(list, { classId: 10, dbIndex: 2, pos: [NaN, 0, 0] })).toThrow();
    });
    it('parameters: typed read, scalar write, reset', () => {
        const list = baseList();
        const o = list.objects[0]!;
        expect(paramValue(o, 0)).toBeCloseTo(2.5);
        expect(paramValue(o, 1)).toBe(3);
        setParamScalar(list, 2, 0, 7.25);
        setParamScalar(list, 2, 1, 4);
        expect(paramValue(o, 0)).toBeCloseTo(7.25);
        expect(paramValue(o, 1)).toBe(4);
        removeParam(list, 2, 1);
        expect(o.params.map((p) => p.name)).toEqual(['Speed', 'Tint']);
        expect(o.data).toHaveLength(2);
        expect(() => setParamScalar(list, 2, 9, 1)).toThrow();
    });
});

describe('add object through the document', () => {
    it('a class dropped into the level exports as a templated new object', () => {
        const { doc, list, catalog } = load();
        const cls = catalog.find((c) => c.name === 'Crate')!;
        const stack = new CommandStack(doc);
        const o = newObjectFromClass(doc, cls, [5000, 100, 6000], 0.5);
        expect(o.template).toBe(2);
        expect(o.origin).toBeUndefined();
        const cmd = new AddCommand([o]);
        stack.run(cmd);
        expect(doc.get(cmd.ids[0]!)!.transform.pos).toEqual([5000, 100, 6000]);
        const d = diffDocument(list, doc);
        expect(d.added).toHaveLength(1);
        const { list: out, report } = applyDocumentToLvl(list, doc);
        expect(report.warnings).toEqual([]);
        const added = out.objects.at(-1)!;
        expect(added.classId).toBe(10);
        expect(added.dbIndex).toBe(2);
        expect(added.pos).toEqual([5000, 100, 6000]);
        expect(added.rot).toEqual([0, 29 + 90, 0]);
        expect(added.params).toHaveLength(3);
        expect(added.links).toEqual([]);
        stack.undo();
        expect(diffDocument(list, doc).added).toHaveLength(0);
    });
    it('a class with no instance exports with a default body and a warning', () => {
        const { doc, list, catalog } = load();
        const cls = catalog.find((c) => c.name === 'Birch Tree 01')!;
        const stack = new CommandStack(doc);
        stack.run(new AddCommand([newObjectFromClass(doc, cls, [1, 2, 3])]));
        const { list: out, report } = applyDocumentToLvl(list, doc);
        expect(report.warnings.join()).toMatch(/default body/);
        expect(out.objects.at(-1)!.classId).toBe(12);
        expect(out.objects.at(-1)!.params).toEqual([]);
    });
    it('links and edited parameters of a new object are written; links to other new objects are remapped', () => {
        const { doc, list, catalog } = load();
        const cls = catalog.find((c) => c.name === 'Crate')!;
        const stack = new CommandStack(doc);
        const a = new AddCommand([newObjectFromClass(doc, cls, [1, 1, 1])]);
        stack.run(a);
        const b = new AddCommand([newObjectFromClass(doc, cls, [2, 2, 2])]);
        stack.run(b);
        stack.run(
            editObjects(doc, [b.ids[0]!], 'Edit', (o) => {
                o.links = [a.ids[0]!, 7];
                o.fields!.find((f) => f.name === 'Speed')!.value = 9;
            })!,
        );
        const { list: out } = applyDocumentToLvl(list, doc);
        const nb = out.objects.at(-1)!;
        expect(nb.links).toEqual([out.objects.at(-2)!.id, 7]);
        expect(paramValue(nb, 0)).toBeCloseTo(9);
    });
    it('same-level copies export as duplicates, edits to existing links and params are diffed', () => {
        const { doc, list } = load();
        const stack = new CommandStack(doc);
        stack.run(editObjects(doc, [2], 'Edit', (o) => (o.links = []))!);
        stack.run(editObjects(doc, [2], 'Edit', (o) => (o.fields!.find((f) => f.name === 'Mode')!.value = 8))!);
        const d = diffDocument(list, doc);
        expect(d.linked).toEqual([2]);
        expect(d.params).toEqual([2]);
        const { list: out } = applyDocumentToLvl(list, doc);
        const o = out.objects.find((x) => x.id === 2)!;
        expect(o.links).toEqual([]);
        expect(paramValue(o, 1)).toBe(8);
        // the input list is untouched
        expect(list.objects[0]!.links).toEqual([5]);
    });
    it('resetting a field to the class default removes the parameter', () => {
        const { doc, list } = load();
        const stack = new CommandStack(doc);
        stack.run(editObjects(doc, [2], 'Reset', (o) => (o.fields = o.fields!.filter((f) => f.name !== 'Mode')))!);
        const { list: out } = applyDocumentToLvl(list, doc);
        expect(out.objects.find((x) => x.id === 2)!.params.map((p) => p.name)).toEqual(['Speed', 'Tint']);
    });
});

describe('multi-edit', () => {
    it('applies one mutation to every selected object as a single undo step, and skips no-ops', () => {
        const { doc } = load();
        const stack = new CommandStack(doc);
        const cmd = editObjects(doc, [2, 5, 9, 12345], 'Set height', (o) => (o.transform.pos[1] = 777))!;
        stack.run(cmd);
        expect([2, 5, 9].map((i) => doc.get(i)!.transform.pos[1])).toEqual([777, 777, 777]);
        expect(stack.depth).toBe(1);
        stack.undo();
        expect([2, 5, 9].map((i) => doc.get(i)!.transform.pos[1])).toEqual([5, 5, 5]);
        stack.redo();
        expect(doc.get(9)!.transform.pos[1]).toBe(777);
        expect(editObjects(doc, [2], 'noop', () => {})).toBeNull();
        expect(editObjects(doc, [2], 'same', (o) => (o.transform.pos[1] = 777))).toBeNull();
    });
    it('groups fields across a selection and reports mixed values', () => {
        const { doc } = load();
        const stack = new CommandStack(doc);
        const dup = stack.duplicate([2]);
        stack.run(editObjects(doc, dup.ids, 'x', (o) => (o.fields!.find((f) => f.name === 'Mode')!.value = 1))!);
        const g = groupFields([doc.get(2)!, doc.get(dup.ids[0]!)!, doc.get(9)!]);
        const mode = g.find((x) => x.name === 'Mode')!;
        expect(mode.owners).toHaveLength(2);
        expect(mode.complete).toBe(false);
        expect(mode.mixed).toBe(true);
        expect(g.find((x) => x.name === 'Speed')!.mixed).toBe(false);
    });
    it('composite commands undo in reverse order', () => {
        const { doc } = load();
        const stack = new CommandStack(doc);
        const a = editObjects(doc, [2], 'a', (o) => (o.transform.pos[0] = 1))!;
        const b = editObjects(doc, [2], 'b', (o) => (o.transform.pos[0] = 2))!;
        // Both snapshots start from x=200 so the composite only works if undo runs in reverse.
        stack.run(new CompositeCommand([a, b], 'both'));
        expect(doc.get(2)!.transform.pos[0]).toBe(2);
        stack.undo();
        expect(doc.get(2)!.transform.pos[0]).toBe(200);
    });
});

describe('history and dirty tracking', () => {
    it('lists labels, jumps in history and tracks the saved top', () => {
        const { doc } = load();
        const stack = new CommandStack(doc);
        stack.run(editObjects(doc, [2], 'one', (o) => (o.transform.pos[0] = 1))!);
        const saved = stack.top;
        stack.run(editObjects(doc, [2], 'two', (o) => (o.transform.pos[0] = 2))!);
        stack.run(editObjects(doc, [2], 'three', (o) => (o.transform.pos[0] = 3))!);
        expect(stack.history()).toEqual({ labels: ['one', 'two', 'three'], done: 3 });
        expect(stack.top === saved).toBe(false);
        stack.goTo(1);
        expect(doc.get(2)!.transform.pos[0]).toBe(1);
        expect(stack.top === saved).toBe(true);
        expect(stack.history()).toEqual({ labels: ['one', 'two', 'three'], done: 1 });
        stack.goTo(3);
        expect(doc.get(2)!.transform.pos[0]).toBe(3);
        stack.goTo(0);
        expect(doc.get(2)!.transform.pos[0]).toBe(200);
    });
});

describe('copy / paste', () => {
    it('same level keeps the source, centres the group on the target and drops nothing', () => {
        const { doc, catalog } = load();
        const clip = copyObjects(doc, [2, 5])!;
        expect(clip.center).toEqual([350, 5, 35]);
        const r = pasteObjects(clip, doc, catalog, [1000, 0, 1000]);
        expect(r.skipped).toEqual([]);
        expect(r.objects.map((o) => o.transform.pos)).toEqual([
            [850, 0, 985],
            [1150, 0, 1015],
        ]);
        expect(r.objects[0]!.origin).toBe(2);
        expect(r.objects[0]!.links).toEqual([5]);
        expect(r.objects.every((o) => o.id === 0)).toBe(true);
        const stack = new CommandStack(doc);
        stack.run(new AddCommand(r.objects));
        expect(doc.size).toBe(6);
    });
    it('without a target position the paste is offset', () => {
        const { doc, catalog } = load();
        const r = pasteObjects(copyObjects(doc, [9])!, doc, catalog, null, 100);
        expect(r.objects[0]!.transform.pos).toEqual([1000, 5, 190]);
    });
    it('another level: classes are retargeted by ODB stem and class id, unknown classes are skipped', () => {
        const { doc } = load();
        const clip = copyObjects(doc, [2, 7])!;
        // The target level numbers the same ODBs differently and has no Goblin.
        const other = new LevelDocument();
        other.level = 'Other';
        const hdr = parseLevelHeader(
            writeLevelHeader({
                name: { raw: enc('Other'), text: 'Other' },
                maxWidth: 1,
                maxHeight: 1,
                dependencies: [{ dbIndex: 7, zero: 0, path: { raw: enc('..\\..\\Common\\System\\System'), text: '' } }],
                trailer: new Uint8Array(0),
            }),
        );
        const cat = buildCatalog(hdr, odbs, []);
        const r = pasteObjects(clip, other, cat, [0, 0, 0]);
        expect(r.skipped).toEqual(['Goblin Soldier']);
        expect(r.objects).toHaveLength(1);
        const o = r.objects[0]!;
        expect(o.resource).toBe('0x7000a');
        expect(o.origin).toBeUndefined();
        expect(o.links).toEqual([]);
    });
    it('clipboard payload survives JSON', () => {
        const { doc } = load();
        const clip = copyObjects(doc, [2])!;
        expect(JSON.parse(JSON.stringify(clip))).toEqual(clip);
        expect(copyObjects(doc, [])).toBeNull();
    });
});

describe('snapping math', () => {
    it('snaps values, positions and angles', () => {
        expect(snapValue(130, 64)).toBe(128);
        expect(snapValue(-130, 64)).toBe(-128);
        expect(snapValue(-10, 64)).toBe(0);
        expect(Object.is(snapValue(-10, 64), 0)).toBe(true);
        expect(snapValue(5.5, 0)).toBe(5.5);
        expect(snapPosition([300, 77, 513], { ...DEFAULT_SNAP, gridSize: 256 })).toEqual([256, 77, 512]);
        expect(snapPosition([300, 77, 513], { ...DEFAULT_SNAP, gridSize: 256 }, true)).toEqual([256, 0, 512]);
        expect(snapPosition([300, 77, 513], { ...DEFAULT_SNAP, grid: false })).toEqual([300, 77, 513]);
        const s = { ...DEFAULT_SNAP, angleStep: 15 };
        expect(snapAngle((20 * Math.PI) / 180, s)).toBeCloseTo((15 * Math.PI) / 180);
        expect(snapAngle((23 * Math.PI) / 180, s)).toBeCloseTo((30 * Math.PI) / 180);
        expect(snapAngle(1.234, { ...s, angle: false })).toBe(1.234);
    });
    it('snaps to the ground and leaves objects over no ground', () => {
        const { doc } = load();
        const m = snapToGround(doc, [2, 5, 9], (x) => (x < 250 ? 1000 : x < 600 ? null : 2000), 10);
        expect([...m.keys()]).toEqual([2, 9]);
        expect(m.get(2)!.pos[1]).toBe(1010);
        expect(m.get(9)!.pos[1]).toBe(2010);
        const cmd = moveCommand(doc, new Map([[2, m.get(2)!.pos]]), 'Snap to ground')!;
        new CommandStack(doc).run(cmd);
        expect(doc.get(2)!.transform.pos[1]).toBe(1010);
    });
    it('plane fallback uses the nearest object height', () => {
        const objs: LevelObject[] = [...load().doc.objects()];
        objs[0]!.transform.pos[1] = 50;
        objs[3]!.transform.pos[1] = 900;
        expect(nearestObjectPlaneY(objs, [objs[3]!.transform.pos[0] + 1, 0, objs[3]!.transform.pos[2]])).toBe(900);
        expect(nearestObjectPlaneY([])).toBe(0);
        expect(nearestObjectPlaneY(objs)).toBeGreaterThan(0);
    });
});

describe('align and distribute', () => {
    it('aligns on an axis at min, centre and max', () => {
        const { doc } = load();
        expect([...alignPositions(doc, [2, 5, 9], 0, 'min').values()].map((p) => p[0])).toEqual([200, 200, 200]);
        expect([...alignPositions(doc, [2, 5, 9], 0, 'max').values()].map((p) => p[0])).toEqual([900, 900, 900]);
        expect([...alignPositions(doc, [2, 5, 9], 0, 'center').values()].map((p) => p[0])).toEqual([550, 550, 550]);
        expect(alignPositions(doc, [2], 0, 'min').size).toBe(0);
    });
    it('distributes evenly between the extremes and needs three objects', () => {
        const { doc } = load();
        const stack = new CommandStack(doc);
        stack.run(editObjects(doc, [5], 'x', (o) => (o.transform.pos[2] = 5))!);
        const m = distributePositions(doc, [2, 5, 7, 9], 2);
        const z = [...m.entries()].sort((a, b) => a[1][2] - b[1][2]).map(([, p]) => p[2]);
        expect(z[0]).toBe(5);
        expect(z[3]).toBe(90);
        expect(z[1]! - z[0]!).toBeCloseTo(z[2]! - z[1]!);
        expect(distributePositions(doc, [2, 5], 2).size).toBe(0);
    });
});

describe('validation', () => {
    it('flags dangling links, self links, duplicates, bounds, bad transforms and unresolved classes', () => {
        const { doc } = load();
        const stack = new CommandStack(doc);
        stack.run(
            editObjects(doc, [2], 'x', (o) => {
                o.links = [5, 2, 4040];
            })!,
        );
        stack.run(editObjects(doc, [9], 'x', (o) => (o.transform.pos = [-5, 0, 0]))!);
        stack.run(editObjects(doc, [5], 'x', (o) => (o.transform.pos = [Number.NaN, 0, 0]))!);
        stack.run(editObjects(doc, [7], 'x', (o) => (o.class = 'class 11'))!);
        const issues = validateDocument(doc, { rawIds: [2, 5, 7, 9, 9] });
        const codes = issues.map((i) => i.code);
        expect(codes).toContain('dangling-link');
        expect(codes).toContain('self-link');
        expect(codes).toContain('duplicate-id');
        expect(codes).toContain('outside-bounds');
        expect(codes).toContain('bad-transform');
        expect(codes).toContain('unresolved-class');
        const dl = issues.find((i) => i.code === 'dangling-link')!;
        expect(dl.ids).toEqual([2]);
        expect(dl.message).toMatch(/4040/);
        expect(dl.severity).toBe('error');
        // errors sort before warnings
        const firstWarn = issues.findIndex((i) => i.severity === 'warning');
        expect(issues.slice(firstWarn).every((i) => i.severity === 'warning')).toBe(true);
    });
    it('a clean document has no issues', () => {
        const { doc } = load();
        doc.bounds = undefined;
        expect(validateDocument(doc)).toEqual([]);
    });
    it('object-reference fields are checked only when asked', () => {
        const { doc } = load();
        const f: DocField = { slot: 0, fieldIndex: 1, type: 0x2003, name: 'Target', value: 31337 };
        new CommandStack(doc).run(editObjects(doc, [9], 'x', (o) => (o.fields = [f]))!);
        doc.bounds = undefined;
        expect(validateDocument(doc)).toEqual([]);
        expect(validateDocument(doc, { checkRefs: true }).map((i) => i.code)).toEqual(['dangling-ref']);
    });
});

describe('catalog and schema helpers', () => {
    it('lists every class of the dependency ODBs with instance counts and groups', () => {
        const { catalog } = load();
        expect(catalog.map((c) => c.name)).toEqual(['Goblin Soldier', 'Birch Tree 01', 'Crate']);
        const crate = catalog.find((c) => c.name === 'Crate')!;
        expect(crate.resource).toBe('0x2000a');
        expect(crate.instances).toBe(3);
        expect(crate.rflClass).toBe(0x00d3);
        expect(crate.category).toBe('System');
        expect(catalog.find((c) => c.name === 'Goblin Soldier')!.category).toBe('Characters');
        expect(resourceParts(crate.resource)).toEqual({ dbIndex: 2, classId: 10 });
        const g = groupCatalog(catalog, new Set([crate.resource]));
        expect(g.map((x) => x.title)).toEqual(['Favourites', 'In this level', 'Characters', 'System']);
        expect(g[0]!.items).toEqual([crate]);
        expect(searchCatalog(catalog, 'tree').map((c) => c.name)).toEqual(['Birch Tree 01']);
        expect(searchCatalog(catalog, '0x3000b').map((c) => c.name)).toEqual(['Goblin Soldier']);
        expect(searchCatalog(catalog, 'npcs').map((c) => c.name)).toEqual(['Goblin Soldier']);
        expect(searchCatalog(catalog, '')).toHaveLength(3);
    });
    it('icons and kinds follow class names', () => {
        expect(iconForName('Birch Tree 01')).toBe('tree');
        expect(iconForName('Goblin Soldier')).toBe('character');
        expect(iconForName('Zzz')).toBe('generic');
    });
    it('picks widgets by type word and converts colours', () => {
        expect(widgetFor(1)).toBe('int');
        expect(widgetFor(2)).toBe('float');
        expect(widgetFor(6)).toBe('bool');
        expect(widgetFor(7)).toBe('enum');
        expect(widgetFor(0x0f)).toBe('colour');
        expect(widgetFor(0x0a)).toBe('string');
        expect(widgetFor(0x1001)).toBe('array');
        expect(widgetFor(0x2003)).toBe('object');
        expect(widgetFor(0x3003)).toBe('array');
        expect(widgetFor(3)).toBe('class');
        expect(widgetFor(5)).toBe('id');
        expect(argbToParts(0xff112233)).toEqual({ hex: '#112233', alpha: 255 });
        expect(partsToArgb('#112233', 255)).toBe(0xff112233);
        expect(partsToArgb('#ffffff', 0)).toBe(0x00ffffff);
    });
});
