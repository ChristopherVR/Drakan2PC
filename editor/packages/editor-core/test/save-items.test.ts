import { describe, expect, it } from 'vitest';
import { listInventory, parseSave } from '@drakan/formats';
import { liveEntries } from '../../formats/test/livefixture.ts';
import { buildSave } from '../../formats/test/savefixture.ts';
import { CommandStack, ReplaceItemCommand, SaveDocument, itemTemplates, replacedBody } from '../src/index.ts';

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
