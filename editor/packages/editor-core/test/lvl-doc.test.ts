// Synthetic only: no game bytes. Document <-> LVL mapping, the mod layout and the disc profile.
import { strFromU8, unzipSync } from 'fflate';
import { describe, expect, it } from 'vitest';
import {
    FLAG_SCALED,
    FLAG_VISIBLE,
    SrscReader,
    discProfileHash,
    parseLevelHeader,
    parseObjectList,
    parseSystemCnf,
    readLvl,
    writeLevelHeader,
    writeLvl,
    writeObjectList,
    type LvlObject,
    type LvlObjectList,
} from '@drakan/formats';
import { buildSrsc } from '../../formats/test/fixtures.ts';
import {
    CommandStack,
    DeleteCommand,
    LevelDocument,
    SetScaleCommand,
    TransformCommand,
    applyDocumentToLvl,
    buildManifestToml,
    buildModFiles,
    buildModZip,
    checkEntrySize,
    compareWithWorld,
    diffDocument,
    docRotToLvl,
    documentFromLvl,
    lvlRotToDoc,
    overridePath,
    updateModsIni,
    validateModMeta,
} from '../src/index.ts';

const enc = (s: string): Uint8Array => Uint8Array.from([...s].map((c) => c.charCodeAt(0)));

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
        rot: [0, 45, 0],
        scale: null,
        preSentinel: 0,
        sentinel: 0xffffffff,
        data: [],
        params: [],
        ...over,
    };
}

const baseList = (): LvlObjectList => ({
    objects: [
        mk(2, { links: [5] }),
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
    ['SYSTEM', new Map([[10, { id: 10, name: 'Crate', group: 1, runtimeClass: 0x100d3 }]])],
    ['NPCS', new Map([[11, { id: 11, name: 'Goblin', group: 1, runtimeClass: 0x1001c }]])],
]);

function load(list = baseList()): { doc: LevelDocument; list: LvlObjectList } {
    const { doc, unresolved } = documentFromLvl({ header: header(), list }, odbs, 'Test');
    expect(unresolved).toBe(0);
    return { doc, list };
}

describe('LVL -> document', () => {
    it('keeps real ids, class names and resource ids', () => {
        const { doc } = load();
        expect([...doc.objects()].map((o) => o.id)).toEqual([2, 5, 7, 9].filter((i) => i !== 9).concat(9));
        expect(doc.get(2)!.class).toBe('Crate');
        expect(doc.get(7)!.class).toBe('Goblin');
        expect(doc.get(7)!.resource).toBe('0x3000b');
        expect(doc.get(2)!.transform.pos).toEqual([200, 5, 20]);
        expect(doc.get(5)!.scale).toEqual([1, 2, 3]);
        expect(doc.nextId).toBe(10);
    });
    it('rotation follows the world dump yaw convention and round-trips', () => {
        expect(lvlRotToDoc([0, 90, 0])).toEqual([0, 0, 0]);
        for (const r of [[0, -89, 10] as const, [1, 270, -170] as const, [0, 0, 0] as const]) {
            expect(docRotToLvl(lvlRotToDoc(r))).toEqual([...r]);
        }
    });
    it('unresolved classes are named by id', () => {
        const { doc, unresolved } = documentFromLvl({ header: header(), list: baseList() }, new Map(), 'T');
        expect(unresolved).toBe(4);
        expect(doc.get(2)!.class).toBe('class 10');
    });
});

describe('document -> LVL edits', () => {
    it('an untouched document produces no diff and identical bytes', () => {
        const { doc, list } = load();
        const d = diffDocument(list, doc);
        expect(d).toEqual({ moved: [], rotated: [], scaled: [], added: [], deleted: [] });
        const out = applyDocumentToLvl(list, doc).list;
        expect(writeObjectList(out)).toEqual(writeObjectList(list));
    });

    it('move and rotate map to pos and degrees; undo restores the original bytes', () => {
        const { doc, list } = load();
        const stack = new CommandStack(doc);
        const o = doc.get(2)!;
        const before = structuredClone(o.transform);
        const after = structuredClone(o.transform);
        after.pos[0] += 300;
        after.rot[1] += (30 * Math.PI) / 180;
        stack.run(new TransformCommand(new Map([[2, before]]), new Map([[2, after]])));
        const { list: out, report } = applyDocumentToLvl(list, doc);
        expect(report.diff.moved).toEqual([2]);
        expect(report.diff.rotated).toEqual([2]);
        expect(out.objects[0]!.pos).toEqual([500, 5, 20]);
        expect(out.objects[0]!.rot).toEqual([0, 75, 0]);
        expect(list.objects[0]!.pos).toEqual([200, 5, 20]); // base untouched
        stack.undo();
        expect(writeObjectList(applyDocumentToLvl(list, doc).list)).toEqual(writeObjectList(list));
        stack.redo();
        expect(applyDocumentToLvl(list, doc).list.objects[0]!.pos[0]).toBe(500);
    });

    it('a rotation of a whole turn is not a change', () => {
        const { doc, list } = load();
        doc.get(2)!.transform.rot[1] += 2 * Math.PI;
        expect(diffDocument(list, doc).rotated).toEqual([]);
    });

    it('scale only where flag 0x100 exists', () => {
        const { doc, list } = load();
        const stack = new CommandStack(doc);
        stack.run(new SetScaleCommand(5, [2, 2, 2]));
        stack.run(new SetScaleCommand(2, [3, 3, 3])); // no scale on this object: ignored
        const out = applyDocumentToLvl(list, doc).list;
        expect(out.objects[1]!.scale).toEqual([2, 2, 2]);
        expect(out.objects[0]!.scale).toBeNull();
        stack.undo();
        stack.undo();
        expect(diffDocument(list, doc).scaled).toEqual([]);
    });

    it('duplicate appends a copy with the document id, copied links and the new transform', () => {
        const { doc, list } = load();
        const stack = new CommandStack(doc);
        const cmd = stack.duplicate([2], 400);
        expect(cmd.ids).toEqual([10]);
        const { list: out, report } = applyDocumentToLvl(list, doc);
        expect(report.diff.added).toEqual([{ id: 10, origin: 2 }]);
        const copy = out.objects.at(-1)!;
        expect(copy.id).toBe(10);
        expect(copy.pos).toEqual([600, 5, 420]);
        expect(copy.links).toEqual([5]);
        expect(copy.classId).toBe(10);
        expect(out.objects.length).toBe(5);
        // and the result is a valid, parseable list with ascending ids
        const re = parseObjectList(writeObjectList(out));
        expect(re.warnings).toEqual([]);
        expect(re.objects.map((o) => o.id)).toEqual([2, 5, 7, 9, 10]);
    });

    it('duplicate of a duplicate, then delete of the first copy', () => {
        const { doc, list } = load();
        const stack = new CommandStack(doc);
        const a = stack.duplicate([2], 100);
        const b = stack.duplicate([...a.ids], 100);
        stack.run(new DeleteCommand([...a.ids]));
        const out = applyDocumentToLvl(list, doc).list;
        expect(out.objects.map((o) => o.id)).toEqual([2, 5, 7, 9, b.ids[0]]);
        expect(out.objects.at(-1)!.pos).toEqual([400, 5, 220]);
    });

    it('delete unlinks references by default and reports them', () => {
        const { doc, list } = load();
        new CommandStack(doc).run(new DeleteCommand([5]));
        const { list: out, report } = applyDocumentToLvl(list, doc);
        expect(out.objects.map((o) => o.id)).toEqual([2, 7, 9]);
        expect(out.objects[0]!.links).toEqual([]);
        expect(report.unlinked).toEqual([{ from: 2, to: 5 }]);
        expect(report.diff.deleted).toEqual([5]);
    });

    it("delete with policy 'throw' refuses a referenced object, 'ignore' leaves dangling links", () => {
        const { doc, list } = load();
        new CommandStack(doc).run(new DeleteCommand([5]));
        expect(() => applyDocumentToLvl(list, doc, { onReferenced: 'throw' })).toThrow(/linked from/);
        expect(applyDocumentToLvl(list, doc, { onReferenced: 'ignore' }).list.objects[0]!.links).toEqual([5]);
    });

    it('a delete undone leaves no diff', () => {
        const { doc, list } = load();
        const s = new CommandStack(doc);
        s.run(new DeleteCommand([2, 7]));
        s.undo();
        expect(diffDocument(list, doc).deleted).toEqual([]);
    });

    it('re-packs through writeLvl: only the 0x0020 entry changes', async () => {
        const list = baseList();
        const hdr = writeLevelHeader(header());
        const srsc = buildSrsc([
            { type: 0, id: 0, sub: 0, data: hdr },
            { type: 1, id: 1, sub: 0, data: Uint8Array.of(9, 9, 9) },
            { type: 0x20, id: 3, sub: 4, data: writeObjectList(list) },
        ]);
        const reader = await SrscReader.fromBytes(srsc);
        const lvl = await readLvl(reader);
        const { doc } = documentFromLvl(lvl, odbs, 'T');
        doc.get(7)!.transform.pos = [1, 2, 3];
        const out = applyDocumentToLvl(lvl.list, doc).list;
        const packed = await SrscReader.fromBytes(await writeLvl(reader, out));
        const re = await readLvl(packed);
        expect(re.list.objects.find((o) => o.id === 7)!.pos).toEqual([1, 2, 3]);
        expect(re.header).toEqual(lvl.header);
        const e = packed.entries.find((x) => x.type === 1)!;
        expect([...(await packed.readRaw(e))]).toEqual([9, 9, 9]);
    });
});

describe('world dump comparison', () => {
    it('pairs by resource and position within tolerance', () => {
        const { doc } = load();
        const world = LevelDocument.fromWorld({
            levelName: 'T',
            levelPath: '',
            sequence: 0,
            entities: [
                { resId: '0x2000a', pos: [210, 5, 25] },
                { resId: '0x2000a', pos: [5000, 0, 0] },
                { resId: '0x3000b', pos: [700, 5, 70] },
            ],
        });
        const c = compareWithWorld(doc, world, 100);
        expect(c.matched).toBe(2);
        expect(c.lvlOnly).toBe(2);
        expect(c.worldOnly).toBe(1);
        expect(c.pairs.get(2)).toBeDefined();
    });
});

describe('mod pack layout', () => {
    const meta = {
        id: 'editor.test-pack',
        name: 'Test pack',
        version: '1.0.0',
        author: 'me',
        description: 'a\nb',
        target: { profile: 'pal', hash: 'b9f60ba118125a54' },
    };
    it('manifest matches modding.md', () => {
        const t = buildManifestToml(meta);
        expect(t).toContain('format = 1\nid = "editor.test-pack"\n');
        expect(t).toContain('description = "a b"');
        expect(t).toContain('\n[target]\nprofile = "pal"\nhash = "b9f60ba118125a54"\n');
    });
    it('validates ids and targets', () => {
        expect(() => validateModMeta({ ...meta, id: 'bad id' })).toThrow();
        expect(() => validateModMeta({ ...meta, id: '' })).toThrow();
        expect(() => validateModMeta({ ...meta, target: {} })).toThrow();
        expect(() => validateModMeta({ ...meta, target: { hash: 'xyz' } })).toThrow();
        expect(() => validateModMeta({ ...meta, version: 'v1' })).toThrow();
        expect(() => validateModMeta({ id: 'a', target: { hash: 'B9F60BA118125A54' } })).not.toThrow();
    });
    it('override names use hex type and decimal id/sub', () => {
        expect(overridePath('LEVEL1NA.LVL', { type: 0x20, id: 3, sub: 4 })).toBe('overrides/LEVEL1NA.LVL/0020_3_4.bin');
        expect(overridePath('X.TXD', { type: 0x100, id: 12, sub: 0 })).toBe('overrides/X.TXD/0100_12_0.bin');
    });
    it('zip holds mod.toml at the root and the override', () => {
        const zip = buildModZip(meta, [
            { file: 'LEVEL1NA.LVL', entry: { type: 0x20, id: 3, sub: 4 }, data: Uint8Array.of(1) },
        ]);
        const f = unzipSync(zip);
        expect(Object.keys(f).sort()).toEqual(['mod.toml', 'overrides/LEVEL1NA.LVL/0020_3_4.bin']);
        expect(strFromU8(f['mod.toml']!)).toContain('version = "1.0.0"');
    });
    it('rejects empty and duplicate entries', () => {
        const e = { file: 'A.LVL', entry: { type: 1, id: 1, sub: 1 }, data: Uint8Array.of(1) };
        expect(() => buildModFiles(meta, [e, e])).toThrow(/duplicate/);
        expect(() => buildModFiles(meta, [{ ...e, data: new Uint8Array(0) }])).toThrow(/empty/);
    });
    it('flags growth across the whole-entry limit only', () => {
        expect(checkEntrySize('A', 1000, 0x50000)).toMatch(/past the/);
        expect(checkEntrySize('A', 0x50000, 0x60000)).toBeNull();
        expect(checkEntrySize('A', 1000, 2000)).toBeNull();
    });
});

describe('mods.ini', () => {
    it('creates the file and the section', () => {
        expect(updateModsIni('', 'pack')).toBe('[mods]\npack = on\n');
    });
    it('appends last in [mods], keeps comments and other sections', () => {
        const src = '; hi\n[mods]\na = on\nb = off\n\n[other]\nx = 1\n';
        expect(updateModsIni(src, 'pack')).toBe('; hi\n[mods]\na = on\nb = off\npack = on\n\n[other]\nx = 1\n');
    });
    it('updates an existing entry in place and keeps CRLF', () => {
        expect(updateModsIni('[mods]\r\npack = off\r\nz = on\r\n', 'pack')).toBe('[mods]\r\npack = on\r\nz = on\r\n');
    });
});

describe('disc profile', () => {
    it('hash matches the runtime value for PAL', () => {
        expect(discProfileHash('SCES-50006', 'BESCES-50006')).toBe('b9f60ba118125a54');
    });
    it('parses SYSTEM.CNF', () => {
        const p = parseSystemCnf('BOOT2 = cdrom0:\\SCES_500.06;1\r\nVER = 1.04\r\nVMODE = PAL\r\n')!;
        expect(p.serial).toBe('SCES-50006');
        expect(p.saveId).toBe('BESCES-50006');
        expect(p.profile).toBe('pal');
        expect(p.hash).toBe('b9f60ba118125a54');
        const u = parseSystemCnf('BOOT2 = cdrom0:\\SCUS_971.28;1\n')!;
        expect([u.profile, u.saveId]).toEqual(['usa', 'BASCUS-97128']);
        expect(parseSystemCnf('nothing')).toBeNull();
    });
});
