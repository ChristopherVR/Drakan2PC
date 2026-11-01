// Synthetic only: no game bytes. Per-object "Revert to original" against the disc level.
import { describe, expect, it } from 'vitest';
import {
    FLAG_SCALED,
    FLAG_VISIBLE,
    parseLevelHeader,
    writeLevelHeader,
    writeObjectList,
    type Lvl,
    type LvlObject,
} from '@drakan/formats';
import {
    CommandStack,
    DeleteCommand,
    SetScaleCommand,
    TransformCommand,
    applyDocumentToLvl,
    diffDocument,
    documentFromLvl,
    editObjects,
    objectChange,
    objectChangeVsLvl,
    revertObjects,
    revertToLvl,
    type LevelDocument,
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

const odbs = new Map([['SYSTEM', new Map([[10, { id: 10, name: 'Crate', group: 1, runtimeClass: 0x100d3 }]])]]);

function load(): { doc: LevelDocument; lvl: Lvl; stack: CommandStack } {
    const lvl: Lvl = {
        header: header(),
        list: {
            objects: [mk(2, { links: [5] }), mk(5, { flags: FLAG_VISIBLE | FLAG_SCALED, scale: [1, 2, 3] }), mk(7)],
            warnings: [],
        },
    };
    const { doc } = documentFromLvl(lvl, odbs, 'Test');
    return { doc, lvl, stack: new CommandStack(doc) };
}

const clean = { moved: [], rotated: [], scaled: [], added: [], deleted: [], linked: [], params: [] };

function move(stack: CommandStack, doc: LevelDocument, id: number, dx: number): void {
    const o = doc.get(id)!;
    const after = structuredClone(o.transform);
    after.pos[0] += dx;
    stack.run(new TransformCommand(new Map([[id, structuredClone(o.transform)]]), new Map([[id, after]])));
}

describe('revert one object to the disc version', () => {
    it('a moved, scaled and relinked object goes back; other edits stay; undo and redo work', () => {
        const { doc, lvl, stack } = load();
        move(stack, doc, 5, 300);
        stack.run(new SetScaleCommand(5, [4, 4, 4]));
        stack.run(editObjects(doc, [5], 'Link', (o) => (o.links = [7]))!);
        move(stack, doc, 7, 50);
        doc.get(5)!.props.note = 'keep me';
        expect(objectChange(diffDocument(lvl.list, doc), 5)).toBe('changed');
        expect(objectChangeVsLvl(lvl.list, doc, 5)).toBe('changed');
        expect(objectChangeVsLvl(lvl.list, doc, 2)).toBeNull();

        const cmd = revertToLvl(doc, lvl, odbs, [5])!;
        expect(cmd.label).toBe('Revert Crate #5 to original');
        stack.run(cmd);
        const d = diffDocument(lvl.list, doc);
        expect(objectChange(d, 5)).toBeNull();
        expect(d.moved).toEqual([7]);
        expect(doc.get(5)!.scale).toEqual([1, 2, 3]);
        expect(doc.get(5)!.links).toEqual([]);
        expect(doc.get(5)!.props.note).toBe('keep me');

        stack.undo();
        expect(diffDocument(lvl.list, doc).moved).toEqual([5, 7]);
        expect(doc.get(5)!.scale).toEqual([4, 4, 4]);
        stack.redo();
        expect(objectChange(diffDocument(lvl.list, doc), 5)).toBeNull();
    });

    it('a deleted object comes back with its id and original data', () => {
        const { doc, lvl, stack } = load();
        stack.run(new DeleteCommand([7]));
        expect(objectChange(diffDocument(lvl.list, doc), 7)).toBe('deleted');
        expect(objectChangeVsLvl(lvl.list, doc, 7)).toBe('deleted');
        stack.run(revertToLvl(doc, lvl, odbs, [7])!);
        expect(diffDocument(lvl.list, doc)).toEqual(clean);
        expect(writeObjectList(applyDocumentToLvl(lvl.list, doc).list)).toEqual(writeObjectList(lvl.list));
        stack.undo();
        expect(doc.has(7)).toBe(false);
    });

    it('an added object is removed, with the links other objects had to it', () => {
        const { doc, lvl, stack } = load();
        const dup = stack.duplicate([7]);
        const id = dup.ids[0]!;
        stack.run(editObjects(doc, [2], 'Link', (o) => (o.links = [...o.links!, id]))!);
        expect(objectChange(diffDocument(lvl.list, doc), id)).toBe('added');
        expect(objectChangeVsLvl(lvl.list, doc, id)).toBe('added');

        stack.run(revertToLvl(doc, lvl, odbs, [id])!);
        expect(doc.has(id)).toBe(false);
        expect(doc.get(2)!.links).toEqual([5]);
        expect(diffDocument(lvl.list, doc)).toEqual(clean);

        stack.undo();
        expect(doc.has(id)).toBe(true);
        expect(doc.get(2)!.links).toEqual([5, id]);
    });

    it('several objects at once are one undo step', () => {
        const { doc, lvl, stack } = load();
        move(stack, doc, 2, 10);
        stack.run(new DeleteCommand([5]));
        const id = stack.duplicate([7]).ids[0]!;
        const depth = stack.depth;
        const cmd = revertToLvl(doc, lvl, odbs, [2, 5, id])!;
        expect(cmd.label).toBe('Revert 3 objects to original');
        stack.run(cmd);
        expect(diffDocument(lvl.list, doc)).toEqual(clean);
        expect(stack.depth).toBe(depth + 1);
        stack.undo();
        const d = diffDocument(lvl.list, doc);
        expect(d.moved).toEqual([2]);
        expect(d.deleted).toEqual([5]);
        expect(d.added.map((a) => a.id)).toEqual([id]);
    });

    it('nothing to revert gives no command', () => {
        const { doc, lvl } = load();
        expect(revertToLvl(doc, lvl, odbs, [2, 7])).toBeNull();
        expect(revertToLvl(doc, lvl, odbs, [999])).toBeNull();
        expect(revertObjects(doc, [{ id: 999, original: undefined }])).toBeNull();
    });
});
