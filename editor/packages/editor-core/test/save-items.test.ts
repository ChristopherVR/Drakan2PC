import { describe, expect, it } from 'vitest';
import { assembleSave, listInventory, parseSave } from '@drakan/formats';
import { liveEntries } from '../../formats/test/livefixture.ts';
import { buildSave } from '../../formats/test/savefixture.ts';
import {
    AddItemCommand,
    CommandStack,
    RemoveAddedItemCommand,
    ReplaceItemCommand,
    SaveDocument,
    addItem,
    findCellStream,
    itemTemplates,
    replacedBody,
} from '../src/index.ts';

const CELL_BASE = 0x302;
const HANDLE_A = 0xa59e30;
const HANDLE_B = 0x9edd30;
const HANDLE_GROUND = 0x7a0010;

/** An item record the way the real ones are shaped: resource - 0x20000 at +0, handle at +0x86 and +0x166. */
function item(resource: number, handle: number, marker: number, state: number, len = 0x200): Uint8Array {
    const b = new Uint8Array(len);
    const dv = new DataView(b.buffer);
    dv.setUint32(0, resource - 0x20000, true);
    dv.setUint32(8, 0x19001041, true);
    dv.setFloat32(0x0c, marker, true); // position x
    dv.setUint32(0x48, 0x100 + marker, true); // serial
    dv.setUint32(0x56, state, true);
    dv.setUint32(0x86, handle, true);
    dv.setUint32(0x86 + 0xe0, handle, true);
    dv.setUint32(0x96, state === 2 ? 2 : 1, true);
    b.fill(marker, 0x180, len); // the class body
    return b;
}

function rynn(): Uint8Array {
    const b = new Uint8Array(0x800);
    const dv = new DataView(b.buffer);
    dv.setUint32(8, 0x17081069, true);
    dv.setInt32(CELL_BASE - 4, -1, true);
    dv.setInt32(CELL_BASE - 0x14, -1, true);
    dv.setUint32(CELL_BASE - 0x10, 1, true);
    [1, 0, HANDLE_A, 0].forEach((v, i) => dv.setUint32(CELL_BASE + 4 * i, v, true));
    [0, 0, HANDLE_B, 0].forEach((v, i) => dv.setUint32(CELL_BASE + 16 + 4 * i, v, true));
    return b;
}

function fixture(): Uint8Array {
    const base = liveEntries().filter((e) => e.type !== 0x102);
    return buildSave([
        ...base,
        { type: 0x102, id: 56, data: rynn() },
        { type: 0x102, id: 328, data: item(0x33753, HANDLE_A, 1, 2) }, // carried potion
        { type: 0x102, id: 615, data: item(0x33cbc, HANDLE_B, 2, 2) }, // carried dagger
        { type: 0x102, id: 262, data: item(0x31cbc, HANDLE_GROUND, 3, 0, 0x260) }, // ground club, a bigger record
        { type: 0x102, id: 270, data: item(0x33cbc, 0x7a0050, 4, 0) }, // second dagger on the ground
    ]);
}

describe('Replace with... (item class swap)', () => {
    it('offers one template per class and prefers a record lying in the world', () => {
        const d = SaveDocument.open(fixture());
        expect(itemTemplates(d.save)).toEqual([
            { resource: 0x31cbc, entry: 262, size: 0x260 },
            { resource: 0x33753, entry: 328, size: 0x200 }, // carried only: still listed
            { resource: 0x33cbc, entry: 270, size: 0x200 }, // the ground dagger, not the carried one
        ]);
    });

    it('keeps the identity fields and takes the rest from the template', () => {
        const carried = item(0x33753, HANDLE_A, 1, 2);
        const tpl = item(0x31cbc, HANDLE_GROUND, 3, 0, 0x260);
        const out = replacedBody(carried, tpl);
        const dv = new DataView(out.buffer);
        expect(out.length).toBe(0x260);
        expect(dv.getUint32(0, true) + 0x20000).toBe(0x31cbc); // class
        expect(dv.getUint32(0x48, true)).toBe(0x101); // serial kept
        expect(dv.getUint32(0x56, true)).toBe(2); // carried state kept
        expect(dv.getUint32(0x86, true)).toBe(HANDLE_A);
        expect(dv.getUint32(0x86 + 0xe0, true)).toBe(HANDLE_A);
        expect(dv.getUint32(0x96, true)).toBe(2);
        expect(dv.getFloat32(0x0c, true)).toBe(1);
        expect(out[0x200]).toBe(3); // body of the template
    });

    it('is an undoable pending edit that rewrites only the item record', () => {
        const blob = fixture();
        const d = SaveDocument.open(blob);
        const s = new CommandStack(d);
        s.run(new ReplaceItemCommand(1, 270));
        expect(d.pendingEdits().map((e) => [e.name, e.label, e.text?.after])).toEqual([
            ['item.1', 'cell 1 item', '0x33cbc (record 270)'],
        ]);
        expect(d.itemResourceOf(1)).toBe(0x33cbc);
        const out = parseSave(d.build().bytes);
        const inv = listInventory(out);
        expect(inv.map((c) => [c.cell, c.resource, c.handle])).toEqual([
            [0, 0x33753, HANDLE_A],
            [1, 0x33cbc, HANDLE_B], // cell 1 is now a dagger and keeps its handle
        ]);
        const orig = parseSave(blob);
        const changed = out.entries
            .filter((e, i) => e.data.some((v, k) => v !== orig.entries[i]!.data[k]))
            .map((e) => e.id);
        expect(changed).toEqual([615]); // only the replaced item's record changes
        expect(out.entries.find((e) => e.id === 615)!.data[0x1f0]).toBe(4);
        s.undo();
        expect(d.pendingEdits()).toEqual([]);
        expect(d.itemResourceOf(1)).toBe(0x33cbc);
    });

    it('swaps to a different class and back', () => {
        const d = SaveDocument.open(fixture());
        const s = new CommandStack(d);
        s.run(new ReplaceItemCommand(0, 262));
        const out = parseSave(d.build().bytes);
        expect(listInventory(out).find((c) => c.cell === 0)!.resource).toBe(0x31cbc);
        expect(out.entries.find((e) => e.id === 328)!.data.length).toBe(0x260);
        s.run(new ReplaceItemCommand(0, undefined));
        expect(d.pendingEdits()).toEqual([]);
    });
});

describe('Add item (new carried record)', () => {
    const K = 12;
    const STATICS = [5020, 5018, 5016, 5014];
    const SLOT_A = 3; // carried potion, entry id 4
    const SLOT_B = 4; // carried dagger, entry id 5

    function words(ws: number[]): Uint8Array {
        const b = new Uint8Array(ws.length * 4);
        const dv = new DataView(b.buffer);
        ws.forEach((v, i) => dv.setUint32(4 * i, v >>> 0, true));
        return b;
    }

    /** Rynn with the cell table at CELL_BASE and the cell word stream (potion in cell 0, dagger in cell 1) after it. */
    function rynnWithStream(): Uint8Array {
        const base = rynn();
        const stream: number[] = [];
        // 24 positions, column-major: cell 0 = position 0, cell 1 = position 3
        for (let p = 0; p < 24; p++) {
            if (p === 0) stream.push(SLOT_A, 1);
            else if (p === 3) stream.push(SLOT_B, 1);
            else stream.push(0xffffffff);
        }
        const out = new Uint8Array(0x700 + stream.length * 4 + 0x40);
        out.set(base.subarray(0, 0x700), 0);
        out.set(words(stream), 0x700);
        out.fill(0x5a, 0x700 + stream.length * 4); // the rest of the record
        new DataView(out.buffer).setUint16(CELL_BASE - 8, 2, true); // item count
        return out;
    }

    function addFixture(): Uint8Array {
        const base = liveEntries().filter((e) => e.type !== 0x102 && e.type !== 0x21 && e.type !== 0x24);
        const recs = [];
        for (let id = 1; id <= K; id++) {
            if (id === 4) recs.push({ type: 0x102, id, data: item(0x33753, HANDLE_A, 1, 2) });
            else if (id === 5) recs.push({ type: 0x102, id, data: item(0x33cbc, HANDLE_B, 2, 2) });
            else if (id === 7) recs.push({ type: 0x102, id, data: item(0x31cbc, HANDLE_GROUND, 3, 0, 0x260) });
            else if (id === 8) recs.push({ type: 0x102, id, data: rynnWithStream() });
            else recs.push({ type: 0x102, id, data: new Uint8Array(0x60) });
        }
        const pairs = words([4000 + K, 8, 9, STATICS.length, ...STATICS.flatMap((u, i) => [u, K + i])]);
        return buildSave([
            ...base,
            { type: 0x24, id: 0, data: words([STATICS.length, ...STATICS]) },
            { type: 0x21, id: 0, data: pairs },
            ...recs,
        ]);
    }

    it('appends the record, drops the first static, grows Rynn by one word', () => {
        const save = parseSave(addFixture());
        const out = addItem(save, 7);
        expect(out.slot).toBe(K);
        expect(out.cell).toBe(2); // first free cell
        expect(out.droppedStatic).toBe(5020);
        const e = out.save.entries;
        const added = e.find((x) => x.type === 0x102 && x.id === K + 1)!;
        const dv = new DataView(added.data.buffer, added.data.byteOffset);
        expect(dv.getUint32(0, true) + 0x20000).toBe(0x31cbc); // the club class
        expect(added.data.length).toBe(0x260);
        expect(dv.getUint32(0x56, true)).toBe(2); // carried state of the donor (the potion)
        expect(dv.getUint32(0x48, true)).toBeGreaterThan(0x102); // a new serial
        expect(dv.getUint32(0x86, true)).toBe(dv.getUint32(0x86 + 0xe0, true));
        expect([HANDLE_A, HANDLE_B, HANDLE_GROUND]).not.toContain(dv.getUint32(0x86, true));
        // 0x24: first uid gone, count down; 0x21: its pair gone, count down
        const st = e.find((x) => x.type === 0x24)!.data;
        expect([...new Uint32Array(st.buffer.slice(st.byteOffset, st.byteOffset + st.length))]).toEqual([
            3, 5018, 5016, 5014,
        ]);
        const pr = e.find((x) => x.type === 0x21)!.data;
        const pw = [...new Uint32Array(pr.buffer.slice(pr.byteOffset, pr.byteOffset + pr.length))];
        expect(pw[3]).toBe(3);
        expect(pw.slice(4)).toEqual([5018, K + 1, 5016, K + 2, 5014, K + 3]);
        // Rynn: +4 bytes, the cell-2 word became (slot, 1), count 3
        const oldR = save.entries.find((x) => x.id === 8)!.data;
        const newR = e.find((x) => x.type === 0x102 && x.id === 8)!.data;
        expect(newR.length).toBe(oldR.length + 4);
        const rv = new DataView(newR.buffer, newR.byteOffset);
        expect(rv.getUint16(CELL_BASE - 8, true)).toBe(3);
        const at = 0x700 + 4 * (2 + 1 + 1 + 2 + 1 + 1); // words before stream position 6
        expect(rv.getUint32(at, true)).toBe(K);
        expect(rv.getUint32(at + 4, true)).toBe(1);
        expect(newR.subarray(at + 8)).toEqual(oldR.subarray(at + 4));
        // ids stay contiguous and the file round-trips
        const re = parseSave(assembleSave(out.save).bytes);
        expect(re.entries.filter((x) => x.type === 0x102).map((x) => x.id)).toEqual(
            Array.from({ length: K + 1 }, (_, i) => i + 1),
        );
    });

    it('fills the next free cell and refuses a non-item template', () => {
        const once = addItem(parseSave(addFixture()), 7).save;
        const twice = addItem(once, 7);
        expect(twice.slot).toBe(K + 1);
        expect(twice.droppedStatic).toBe(5018);
        expect(() => addItem(parseSave(addFixture()), 1)).toThrow(/not an item record/);
    });

    it('places a tall item where all of its cells are free and marks the cells under it', () => {
        const once = addItem(parseSave(addFixture()), 7, 3); // cells 0 and 1 hold items: column 2 is the first free one
        expect(once.cell).toBe(2);
        const inv = listInventory(once.save);
        expect(inv.filter((c) => c.marker !== undefined).map((c) => [c.cell, c.marker])).toEqual([
            [10, 1],
            [18, 2],
        ]);
        // the stream keeps the covered cells as single empty words: Rynn grows by one word only
        const oldR = parseSave(addFixture()).entries.find((x) => x.type === 0x102 && x.id === 8)!.data;
        const newR = once.save.entries.find((x) => x.type === 0x102 && x.id === 8)!.data;
        expect(newR.length).toBe(oldR.length + 4);
        // the next item does not land on a covered cell
        const next = addItem(once.save, 7, 2);
        expect(next.cell).toBe(3);
        expect(() => addItem(parseSave(addFixture()), 7, 4)).toThrow(/1 to 3 cells/);
    });

    it('refuses a tall item when no column has room, but a short one still fits', () => {
        const d = SaveDocument.open(addFixture());
        const s = new CommandStack(d);
        for (let i = 0; i < 6; i++) s.run(new AddItemCommand(7, 3)); // columns 2..7
        expect(d.addedCells()).toEqual([2, 3, 4, 5, 6, 7]);
        expect(d.placeFor(3)).toBeNull();
        expect(() => s.run(new AddItemCommand(7, 3))).toThrow(/no free column with 3 empty cells/);
        expect(d.itemAdds).toHaveLength(6); // the refused add left nothing behind
        expect(d.placeFor(2)).toBe(8); // under the potion
        s.run(new AddItemCommand(7, 2));
        expect(d.addedPlaces().at(-1)).toEqual({ cell: 8, cells: [8, 16] });
        expect(d.placeFor(1)).toBe(9);
    });

    it('builds the same placement it shows', () => {
        const d = SaveDocument.open(addFixture());
        const s = new CommandStack(d);
        s.run(new AddItemCommand(7, 2));
        s.run(new AddItemCommand(7, 1));
        expect(d.addedCells()).toEqual([2, 3]);
        const inv = listInventory(parseSave(d.build().bytes));
        expect(inv.map((c) => [c.cell, c.marker ?? null])).toEqual([
            [0, null],
            [1, null],
            [2, null],
            [3, null],
            [10, 1],
        ]);
    });

    it('works on a save the game wrote, whose Rynn class word has bit 27 set', () => {
        const blob = addFixture();
        const save = parseSave(blob);
        const r = save.entries.find((x) => x.type === 0x102 && x.id === 8)!;
        new DataView(r.data.buffer, r.data.byteOffset).setUint32(8, 0x1f081069, true);
        const out = addItem(save, 7);
        expect(out.cell).toBe(2);
        expect(listInventory(out.save).map((c) => c.cell)).toEqual([0, 1, 2]);
    });

    it('findCellStream matches the stream by the carried slots', () => {
        const r = rynnWithStream();
        expect(
            findCellStream(
                r,
                new Map([
                    [0, SLOT_A],
                    [1, SLOT_B],
                ]),
            ),
        ).toBe(0x700);
        expect(() => findCellStream(r, new Map([[0, 99]]))).toThrow(/not found/);
    });
});

describe('Add item through the document', () => {
    it('is an undoable pending edit and builds a file with one more item', () => {
        // reuse the fixture of the previous block through a fresh builder
        const K = 12;
        const w = (ws: number[]): Uint8Array => {
            const b = new Uint8Array(ws.length * 4);
            ws.forEach((v, i) => new DataView(b.buffer).setUint32(4 * i, v >>> 0, true));
            return b;
        };
        const stream: number[] = [];
        for (let p = 0; p < 24; p++) stream.push(...(p === 0 ? [3, 1] : p === 3 ? [4, 1] : [0xffffffff]));
        const r = new Uint8Array(0x700 + stream.length * 4 + 0x40);
        r.set(rynn().subarray(0, 0x700), 0);
        r.set(w(stream), 0x700);
        new DataView(r.buffer).setUint16(CELL_BASE - 8, 2, true);
        const recs = [];
        for (let id = 1; id <= K; id++) {
            recs.push({
                type: 0x102,
                id,
                data:
                    id === 4
                        ? item(0x33753, HANDLE_A, 1, 2)
                        : id === 5
                          ? item(0x33cbc, HANDLE_B, 2, 2)
                          : id === 7
                            ? item(0x31cbc, HANDLE_GROUND, 3, 0, 0x260)
                            : id === 8
                              ? r
                              : new Uint8Array(0x60),
            });
        }
        const base = liveEntries().filter((e) => e.type !== 0x102 && e.type !== 0x21 && e.type !== 0x24);
        const blob = buildSave([
            ...base,
            { type: 0x24, id: 0, data: w([2, 5020, 5018]) },
            { type: 0x21, id: 0, data: w([4000, 8, 9, 2, 5020, K, 5018, K + 1]) },
            ...recs,
        ]);
        const d = SaveDocument.open(blob);
        const s = new CommandStack(d);
        s.run(new AddItemCommand(7));
        expect(d.addedCells()).toEqual([2]);
        expect(d.pendingEdits().map((e) => [e.name, e.label])).toEqual([['add.0', 'new item 1 (cell 2)']]);
        const out = parseSave(d.build().bytes);
        expect(out.entries.filter((e) => e.type === 0x102)).toHaveLength(K + 1);
        s.undo();
        expect(d.pendingEdits()).toEqual([]);
        expect(d.itemAdds).toEqual([]);
        s.run(new AddItemCommand(7));
        s.run(new RemoveAddedItemCommand(0));
        expect(d.itemAdds).toEqual([]);
    });
});
