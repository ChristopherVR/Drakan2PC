import { unzipSync, strFromU8 } from 'fflate';
import { describe, expect, it } from 'vitest';
import { SrscReader } from '@drakan/formats';
import { buildSrsc } from '../../formats/test/fixtures.ts';
import {
    CommandStack,
    DeleteCommand,
    LevelDocument,
    SetPropCommand,
    TransformCommand,
    buildModZip,
    classifyType,
    parseWorldDump,
    srscRoundTrips,
    type Transform,
} from '../src/index.ts';

const dump = JSON.stringify({
    levelName: 'Test',
    levelPath: 'x.lvl',
    sequence: 1,
    entities: [
        { handle: '0x1', class: '0x10009', type: 3, resId: '0x20a50', model: '0x3', pos: [1, 2, 3], yaw: 1.5, hp: 100 },
        { handle: '0x2', class: '0x100d3', type: 8, pos: [10, 20, 30], yaw: 0 },
        { handle: '0x3', class: '0x10025', type: 7, pos: [5, 5, 5], yaw: 0, hp: 20 },
    ],
});

describe('world dump', () => {
    it('parses and classifies', () => {
        const d = LevelDocument.fromWorld(parseWorldDump(dump));
        expect(d.size).toBe(3);
        expect(d.get(1)!.kind).toBe('player');
        expect(d.get(2)!.kind).toBe('static');
        expect(d.get(3)!.kind).toBe('creature');
        expect(d.get(1)!.transform.rot[1]).toBe(1.5);
        expect(classifyType(7, false)).toBe('other');
    });
    it('rejects non-dumps', () => {
        expect(() => parseWorldDump('{"a":1}')).toThrow();
    });
});

describe('commands', () => {
    const mk = () => new CommandStack(LevelDocument.fromWorld(parseWorldDump(dump)));
    it('transform undo/redo', () => {
        const s = mk();
        const before = new Map<number, Transform>([[2, structuredClone(s.doc.get(2)!.transform)]]);
        const after = new Map<number, Transform>([[2, { pos: [1, 1, 1], rot: [0, 2, 0] }]]);
        s.run(new TransformCommand(before, after));
        expect(s.doc.get(2)!.transform.pos).toEqual([1, 1, 1]);
        s.undo();
        expect(s.doc.get(2)!.transform.pos).toEqual([10, 20, 30]);
        s.redo();
        expect(s.doc.get(2)!.transform.rot[1]).toBe(2);
        expect(s.canRedo).toBe(false);
    });
    it('delete and duplicate with stable ids', () => {
        const s = mk();
        s.run(new DeleteCommand([1, 2]));
        expect(s.doc.size).toBe(1);
        s.undo();
        expect(s.doc.size).toBe(3);
        const dup = s.duplicate([2, 3]);
        expect(dup.ids).toEqual([4, 5]);
        expect(s.doc.get(4)!.transform.pos[0]).toBe(510);
        s.undo();
        expect(s.doc.has(4)).toBe(false);
        s.redo();
        expect(s.doc.get(5)!.class).toBe('0x10025');
        expect(s.doc.get(5)!.id).toBe(5);
    });
    it('new redo branch clears redo, props undo', () => {
        const s = mk();
        s.run(new SetPropCommand(1, 'hp', 50));
        s.undo();
        expect(s.doc.get(1)!.props.hp).toBe(100);
        s.run(new SetPropCommand(1, 'tag', 'a'));
        expect(s.canRedo).toBe(false);
        s.undo();
        expect(s.doc.get(1)!.props.tag).toBeUndefined();
    });
});

describe('document json', () => {
    it('round trips', () => {
        const d = LevelDocument.fromWorld(parseWorldDump(dump));
        d.insert({ ...d.get(1)!, id: 0 });
        const d2 = LevelDocument.fromJSON(JSON.stringify(d));
        expect(d2.toJSON()).toEqual(d.toJSON());
        expect(d2.nextId).toBe(5);
        expect(() => LevelDocument.fromJSON('{"format":"x"}')).toThrow();
    });
});

describe('mod export', () => {
    const file = buildSrsc([
        { type: 1, id: 2, sub: 0, data: Uint8Array.of(1, 2, 3) },
        { type: 4, id: 5, sub: 6, data: Uint8Array.of(9, 9, 9, 9, 9) },
    ]);
    it('SRSC round-trip self-test passes', async () => {
        const r = await SrscReader.fromBytes(file);
        expect(await srscRoundTrips(r, [0, 1])).toBe(true);
    });
    it('writes mod.toml and override entries in the runtime layout', () => {
        const zip = buildModZip({ id: 'test.t', target: { profile: 'pal' } }, [
            { file: 'LEVEL.RRC', entry: { type: 4, id: 5, sub: 6 }, data: Uint8Array.of(7, 7) },
        ]);
        const f = unzipSync(zip);
        expect([...f['overrides/LEVEL.RRC/0004_5_6.bin']!]).toEqual([7, 7]);
        expect(strFromU8(f['mod.toml']!)).toContain('id = "test.t"');
    });
});
