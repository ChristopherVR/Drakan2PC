import { describe, expect, it } from 'vitest';
import { assembleSave, enc, listInventory, parseSave } from '@drakan/formats';
import { liveEntries } from '../../formats/test/livefixture.ts';
import { buildSave } from '../../formats/test/savefixture.ts';
import {
    AddItemCommand,
    CommandStack,
    RemoveAddedItemCommand,
    ReplaceItemCommand,
    SaveDocument,
    SetFieldCommand,
    SetAddedItemCountCommand,
    SetReplacementItemCountCommand,
    addItem,
    findCellStream,
    itemHandleAt,
    itemRows,
    itemTemplates,
    replaceItem,
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

/** An item record whose self handle sits further on (the bow: 0x112 / 0x148, the Steel Club: 0x96). */
function shiftedItem(resource: number, handle: number, at: number, state: number, len = 0x280): Uint8Array {
    const b = new Uint8Array(len);
    const dv = new DataView(b.buffer);
    dv.setUint32(0, resource - 0x20000, true);
    dv.setUint32(8, 0x11000045, true);
    dv.setUint32(0x48, 0x300, true);
    dv.setUint32(0x56, state, true);
    dv.setUint32(at, handle, true);
    dv.setUint32(at + 0xe0, handle, true);
    dv.setUint32(at + 0x10, 7, true);
    return b;
}

function bowItem(resource: number, handle: number, at: number, ownerSlot: number): Uint8Array {
    const data = shiftedItem(resource, handle, at, 2, at + 0x180);
    const dv = new DataView(data.buffer);
    dv.setUint16(at - 0x0e, 0x4f, true);
    dv.setUint32(at + 0x50, 0x51c030, true);
    dv.setUint32(at + 0x138, 0x521378, true);
    dv.setUint32(at + 0x154, ownerSlot, true);
    dv.setUint32(at + 8, enc(resource === 0x31bc9 ? 200 : 300), true);
    dv.setUint32(at + 12, enc(resource === 0x31bc9 ? 200 : 300), true);
    dv.setUint32(at + 0x160, resource === 0x31bc9 ? 0x1379e : 0x137aa, true);
    dv.setUint32(at + 0x164, ownerSlot === 0xffffffff ? 0xffffffff : 0x19a, true);
    dv.setUint32(at + 0x168, ownerSlot === 0xffffffff ? 0 : 2, true);
    return data;
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
    it('edits replacement counts independently through switching, undo, redo and export', () => {
        const doc = SaveDocument.open(fixture());
        const stack = new CommandStack(doc);
        stack.run(new SetFieldCommand('inv.0.durability', 12));
        stack.run(new ReplaceItemCommand(0, 262));
        stack.run(new SetReplacementItemCountCommand(0, 'durability', 77));
        stack.run(new SetReplacementItemCountCommand(0, 'max', 250));
        expect(doc.value('inv.0.durability')).toBe(77);
        const exported = () => SaveDocument.open(doc.build().bytes).inventory[0]!;
        expect([exported().durability, exported().max]).toEqual([77, 250]);
        expect(doc.pendingEdits().find((e) => e.name === 'item.0')!.text!.after).toContain('quantity/durability 77');
        stack.run(new ReplaceItemCommand(0, 270));
        expect(doc.itemReplacementCounts.has(0)).toBe(false);
        stack.undo();
        expect([exported().durability, exported().max]).toEqual([77, 250]);
        stack.undo();
        stack.undo();
        stack.undo();
        expect(exported().durability).toBe(12);
        stack.redo();
        stack.redo();
        stack.redo();
        expect(exported().durability).toBe(77);
        for (const bad of [-1, 1.5, 0x100000000, NaN])
            expect(() => stack.run(new SetReplacementItemCountCommand(0, 'max', bad))).toThrow();
        expect(exported().max).toBe(250);
    });

    it('keeps replacement defaults instead of exporting hidden count edits from the original item', () => {
        const doc = SaveDocument.open(fixture());
        const stack = new CommandStack(doc);
        stack.run(new SetFieldCommand('inv.0.durability', 77));
        stack.run(new SetFieldCommand('inv.0.max', 99));
        stack.run(new ReplaceItemCommand(0, 262));
        expect(doc.checkEdit('inv.0.durability', 88)).toMatch(/Undo the item replacement/);
        const expected = listInventory(replaceItem(doc.save, 0, 262))[0]!;
        const replaced = SaveDocument.open(doc.build().bytes).inventory[0]!;
        expect(replaced.resource).toBe(0x31cbc);
        expect(replaced.durability).toBe(expected.durability);
        expect(replaced.max).toBe(expected.max);
        expect(doc.pendingEdits().map((e) => e.name)).not.toContain('inv.0.durability');
        expect(doc.pendingEdits().map((e) => e.name)).not.toContain('inv.0.max');
        stack.undo();
        const restored = SaveDocument.open(doc.build().bytes).inventory[0]!;
        expect(restored.resource).toBe(0x33753);
        expect(restored.durability).toBe(77);
        expect(restored.max).toBe(99);
        stack.redo();
        expect(SaveDocument.open(doc.build().bytes).inventory[0]!.durability).toBe(expected.durability);
    });
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

describe('item records with the handle further on (bow, Steel Club)', () => {
    it('finds the handle per record and lists those classes', () => {
        expect(itemHandleAt(item(0x33753, HANDLE_A, 1, 2))).toBe(0x86);
        expect(itemHandleAt(shiftedItem(0x31bc9, 0x7b0010, 0x112, 0))).toBe(0x112);
        expect(itemHandleAt(new Uint8Array(0x200))).toBeNull();
        const blob = buildSave([
            ...liveEntries().filter((e) => e.type !== 0x102),
            { type: 0x102, id: 56, data: rynn() },
            { type: 0x102, id: 328, data: item(0x33753, HANDLE_A, 1, 2) },
            { type: 0x102, id: 615, data: item(0x33cbc, HANDLE_B, 2, 2) },
            { type: 0x102, id: 700, data: shiftedItem(0x31bc9, 0x7b0010, 0x112, 0) },
        ]);
        expect(itemTemplates(SaveDocument.open(blob).save).map((t) => t.resource)).toContain(0x31bc9);
    });

    it('moves the carried handle block to the template’s own offset', () => {
        const carried = item(0x33753, HANDLE_A, 1, 2);
        const out = replacedBody(carried, shiftedItem(0x31bc9, 0x7b0010, 0x112, 0));
        const dv = new DataView(out.buffer);
        expect(out.length).toBe(0x280);
        expect(dv.getUint32(0, true) + 0x20000).toBe(0x31bc9);
        expect(dv.getUint32(0x56, true)).toBe(2); // header: carried state of the donor
        expect(dv.getUint32(0x112, true)).toBe(HANDLE_A);
        expect(dv.getUint32(0x112 + 0xe0, true)).toBe(HANDLE_A);
        expect(dv.getUint32(0x122, true)).toBe(2); // the carried slot kind, 0x10 after the handle
        expect(dv.getUint32(0x86, true)).toBe(0); // nothing written at the usual offset
        // and back: a shifted carried item turned into a usual one
        const back = new DataView(replacedBody(out, item(0x33cbc, 0x7a0050, 4, 0)).buffer);
        expect(back.getUint32(0x86, true)).toBe(HANDLE_A);
        expect(back.getUint32(0x96, true)).toBe(2);
    });
});

describe('equipped Bow replacement ownership', () => {
    const sourceAt = 0x112;
    const targetAt = 0x148;

    function bowFixture(change?: (source: Uint8Array, target: Uint8Array, player: Uint8Array) => void) {
        const source = bowItem(0x31bc9, HANDLE_A, sourceAt, 55);
        const target = bowItem(0x32266, HANDLE_GROUND, targetAt, 0xffffffff);
        const player = rynn(); // record 56; serialized owner slot is therefore 55
        change?.(source, target, player);
        return parseSave(
            buildSave([
                ...liveEntries().filter((e) => e.type !== 0x102),
                { type: 0x102, id: 56, data: player },
                { type: 0x102, id: 328, data: source },
                { type: 0x102, id: 615, data: item(0x33cbc, HANDLE_B, 2, 2) },
                { type: 0x102, id: 700, data: target },
            ]),
        );
    }

    it('keeps the verified Rynn owner slot across shifted Bow prefixes without inheriting the old ammunition', () => {
        const save = bowFixture();
        const source = save.entries.find((e) => e.id === 328)!.data;
        const target = save.entries.find((e) => e.id === 700)!.data;
        const before = replacedBody(source, target);
        const out = replaceItem(save, 0, 700).entries.find((e) => e.id === 328)!.data;
        const expected = before.slice();
        new DataView(expected.buffer).setUint32(targetAt + 0x154, 55, true);
        expect(out).toEqual(expected); // the only additional preservation is the appended owner reference
        expect(new DataView(out.buffer).getUint32(targetAt + 0x160, true)).toBe(0x137aa);
        expect(new DataView(out.buffer).getUint32(targetAt + 0x164, true)).toBe(0xffffffff);
        expect(new DataView(out.buffer).getUint32(targetAt + 0x168, true)).toBe(0);
        expect(out.subarray(targetAt + 8, targetAt + 16)).toEqual(target.subarray(targetAt + 8, targetAt + 16));
    });

    it.each(['foreign-owner', 'source-class', 'target-class', 'source-vtable', 'target-vtable', 'not-equipped'])(
        'does not preserve ownership for %s',
        (reason) => {
            const save = bowFixture((source, target, player) => {
                if (reason === 'foreign-owner') new DataView(source.buffer).setUint32(sourceAt + 0x154, 54, true);
                if (reason === 'source-class') new DataView(source.buffer).setUint16(sourceAt - 0x0e, 0x2d, true);
                if (reason === 'target-class') new DataView(target.buffer).setUint16(targetAt - 0x0e, 0x2d, true);
                if (reason === 'source-vtable') new DataView(source.buffer).setUint32(sourceAt + 0x50, 0x51c034, true);
                if (reason === 'target-vtable') new DataView(target.buffer).setUint32(targetAt + 0x138, 0x52137c, true);
                if (reason === 'not-equipped') new DataView(player.buffer).setUint32(CELL_BASE, 0, true);
            });
            const out = replaceItem(save, 0, 700).entries.find((e) => e.id === 328)!.data;
            expect(new DataView(out.buffer).getUint32(targetAt + 0x154, true)).toBe(0xffffffff);
        },
    );

    it('does not confer equipped ownership through the identity helper used by Add', () => {
        const source = bowItem(0x31bc9, HANDLE_A, sourceAt, 55);
        const target = bowItem(0x32266, HANDLE_GROUND, targetAt, 0xffffffff);
        const addedBody = replacedBody(source, target);
        expect(new DataView(addedBody.buffer).getUint32(targetAt + 0x154, true)).toBe(0xffffffff);
    });

    it('leaves a truncated target layout alone', () => {
        const save = bowFixture();
        const target = save.entries.find((e) => e.id === 700)!;
        target.data = target.data.slice(0, targetAt + 0x154);
        const source = save.entries.find((e) => e.id === 328)!.data;
        const out = replaceItem(save, 0, 700).entries.find((e) => e.id === 328)!.data;
        expect(out).toEqual(replacedBody(source, target.data));
    });
});

describe('idle PAL equipped Bow reload preparation', () => {
    const sourceAt = 0x112;
    const targetAt = 0x148;
    const cameraRaw = 0x7e;
    type FixtureParts = { player: Uint8Array; source: Uint8Array; target: Uint8Array; camera: Uint8Array; raw: number };

    function reloadFixture(sight: boolean, change?: (parts: FixtureParts) => void, raw = 0x200) {
        const player = new Uint8Array(raw + 0x1300);
        const dv = new DataView(player.buffer);
        dv.setUint32(8, 0x17081069, true);
        dv.setUint16(raw - 2, 9, true);
        dv.setUint32(raw + 0x90, 0x10062000, true); // unrelated state bits must survive
        dv.setUint32(raw + 0xab0, 0x40100040, true);
        dv.setUint32(raw + 0xe90, 5, true);
        dv.setUint32(raw + 0xeb4, 6, true);
        const cells = raw + 0x5b4;
        dv.setInt32(cells - 4, -1, true);
        dv.setInt32(cells - 0x14, -1, true);
        dv.setUint32(cells - 0x10, 0, true);
        [1, 0, HANDLE_A, 0].forEach((value, i) => dv.setUint32(cells + i * 4, value, true));
        const camera = new Uint8Array(734);
        const cv = new DataView(camera.buffer);
        cv.setUint32(0, 2, true);
        cv.setUint32(8, 0x10001049, true);
        cv.setUint16(cameraRaw - 2, 0x1b, true);
        cv.setUint32(cameraRaw + 0x148, 0x7e0000, true);
        cv.setUint32(cameraRaw + 0x14c, 6, true);
        cv.setUint32(cameraRaw + 0x48, sight ? 0x220 : 0x200, true);
        const source = bowItem(0x31bc9, HANDLE_A, sourceAt, 55);
        const target = bowItem(0x32266, HANDLE_GROUND, targetAt, 0xffffffff);
        change?.({ player, source, target, camera, raw });
        return parseSave(
            buildSave([
                ...liveEntries().filter((e) => e.type !== 0x102),
                { type: 0x102, id: 56, data: player },
                { type: 0x102, id: 57, data: camera },
                { type: 0x102, id: 328, data: source },
                { type: 0x102, id: 700, data: target },
            ]),
        );
    }

    it.each([false, true])(
        'queues the appropriate native reload branch with sight=%s and preserves other state',
        (sight) => {
            const raw = 0x2a2; // a different actor prefix must not change the native offset mapping
            const save = reloadFixture(sight, undefined, raw);
            const before = save.entries.find((e) => e.id === 56)!.data;
            const out = replaceItem(save, 0, 700);
            const after = out.entries.find((e) => e.id === 56)!.data;
            const expected = before.slice();
            const dv = new DataView(expected.buffer);
            dv.setUint32(raw + 0xab0, 0x40300040, true);
            if (sight) dv.setUint32(raw + 0x94, 0x80, true);
            expect(after).toEqual(expected);
            expect(out.entries.find((e) => e.id === 57)!.data).toEqual(save.entries.find((e) => e.id === 57)!.data);
            const bow = out.entries.find((e) => e.id === 328)!.data;
            const bv = new DataView(bow.buffer, bow.byteOffset);
            expect(bv.getUint32(targetAt + 0x160, true)).toBe(0x137aa);
            expect(bv.getUint32(targetAt + 0x164, true)).toBe(0xffffffff); // the engine must create the correct new arrow
            const again = replaceItem(out, 0, 700);
            expect(assembleSave(again).bytes).toEqual(assembleSave(out).bytes);
        },
    );

    const negativeCases: [string, (parts: FixtureParts) => void][] = [
        ['empty ammunition', ({ target }) => new DataView(target.buffer).setUint32(targetAt + 8, enc(0), true)],
        ['zero maximum', ({ target }) => new DataView(target.buffer).setUint32(targetAt + 12, enc(0), true)],
        ['no ammunition object', ({ target }) => new DataView(target.buffer).setUint32(targetAt + 0x160, 0, true)],
        [
            'missing ammunition reference',
            ({ target }) => new DataView(target.buffer).setUint32(targetAt + 0x160, 0xffffffff, true),
        ],
        [
            'unknown ammunition family',
            ({ target }) => new DataView(target.buffer).setUint32(targetAt + 0x160, 0x237aa, true),
        ],
        ['already nocked', ({ target }) => new DataView(target.buffer).setUint32(targetAt + 0x164, 0x309, true)],
        ['wrong player layout', ({ player, raw }) => new DataView(player.buffer).setUint16(raw - 2, 8, true)],
        ['non-bow mode', ({ player, raw }) => new DataView(player.buffer).setUint32(raw + 0xe90, 1, true)],
        ['other ammunition mode', ({ player, raw }) => new DataView(player.buffer).setUint32(raw + 0xeb4, 4, true)],
        ['jumping', ({ player, raw }) => new DataView(player.buffer).setUint32(raw + 0x94, 0x10, true)],
        ['falling', ({ player, raw }) => new DataView(player.buffer).setUint32(raw + 0x94, 0x20, true)],
        ['attacking', ({ player, raw }) => new DataView(player.buffer).setUint32(raw + 0x94, 0x80, true)],
        ['script lock', ({ player, raw }) => new DataView(player.buffer).setUint32(raw + 0x90, 0x66000, true)],
        ['queued attack', ({ player, raw }) => new DataView(player.buffer).setUint32(raw + 0x90, 0x62002, true)],
        ['forward movement', ({ player, raw }) => new DataView(player.buffer).setFloat32(raw + 0x504, 1, true)],
        ['strafe movement', ({ player, raw }) => new DataView(player.buffer).setFloat32(raw + 0x508, -1, true)],
        ['queued jump', ({ player, raw }) => new DataView(player.buffer).setUint32(raw + 0x50c, 1, true)],
        ['owned torso action', ({ player, raw }) => new DataView(player.buffer).setUint32(raw + 0xab0, 0x100042, true)],
        ['existing reload', ({ player, raw }) => new DataView(player.buffer).setUint32(raw + 0xab0, 0x300040, true)],
        ['foreign owner', ({ source }) => new DataView(source.buffer).setUint32(sourceAt + 0x154, 54, true)],
        ['non-Bow source', ({ source }) => new DataView(source.buffer).setUint16(sourceAt - 0x0e, 0x2d, true)],
        ['unknown camera class', ({ camera }) => new DataView(camera.buffer).setUint32(8, 0x10001048, true)],
        ['unknown camera payload', ({ camera }) => new DataView(camera.buffer).setUint16(cameraRaw - 2, 0x1a, true)],
        ['alternate camera mode', ({ camera }) => new DataView(camera.buffer).setUint32(cameraRaw + 0x14c, 1, true)],
        ['scripted camera', ({ camera }) => new DataView(camera.buffer).setUint32(cameraRaw + 0x48, 0x100200, true)],
        ['invalid camera self', ({ camera }) => new DataView(camera.buffer).setUint32(cameraRaw + 0x148, 0, true)],
    ];
    it.each(negativeCases)('leaves player state untouched for %s', (_name, change) => {
        const save = reloadFixture(true, change);
        const before = save.entries.find((e) => e.id === 56)!.data;
        const out = replaceItem(save, 0, 700);
        expect(out.entries.find((e) => e.id === 56)!.data).toEqual(before);
    });

    it.each(['no camera', 'duplicate camera', 'short camera', 'short player'])(
        'rejects %s without changing player state',
        (kind) => {
            const save = reloadFixture(true);
            const camera = save.entries.find((e) => e.id === 57)!;
            const player = save.entries.find((e) => e.id === 56)!;
            if (kind === 'no camera') save.entries = save.entries.filter((e) => e.id !== 57);
            if (kind === 'duplicate camera') save.entries.push({ ...camera, id: 58 });
            if (kind === 'short camera') camera.data = camera.data.slice(0, cameraRaw + 0x18b);
            if (kind === 'short player') player.data = player.data.slice(0, 0x200 + 0x111b);
            const before = player.data.slice();
            expect(replaceItem(save, 0, 700).entries.find((e) => e.id === 56)!.data).toEqual(before);
        },
    );
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

    it('distinguishes an empty pack from a missing table and refuses additions before creating pending edits', () => {
        const save = parseSave(addFixture());
        const player = save.entries.find((e) => e.type === 0x102 && e.id === 8)!;
        player.data.fill(0, CELL_BASE, CELL_BASE + 24 * 16);
        player.data.fill(0xff, 0x700, 0x700 + 24 * 4);
        const doc = SaveDocument.open(buildSave(save.entries));
        expect(doc.hasInventory).toBe(true);
        expect(doc.inventory).toEqual([]);
        const stack = new CommandStack(doc);
        expect(doc.canAddItems).toBe(false);
        expect(() => stack.run(new AddItemCommand(7, 3))).toThrow(/Pick up an item/);
        expect(doc.itemAdds).toEqual([]);
        expect(SaveDocument.open(doc.build().bytes).inventory).toEqual([]);
        player.data.fill(0, CELL_BASE - 0x14, CELL_BASE);
        expect(SaveDocument.open(buildSave(save.entries)).hasInventory).toBe(false);
    });

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

    it('does not copy an equipped Bow donor owner when adding a ground-template Bow', () => {
        const save = parseSave(addFixture());
        save.entries.find((e) => e.type === 0x102 && e.id === 4)!.data = bowItem(0x31bc9, HANDLE_A, 0x112, 7);
        save.entries.find((e) => e.type === 0x102 && e.id === 7)!.data = bowItem(
            0x32266,
            HANDLE_GROUND,
            0x148,
            0xffffffff,
        );
        const player = save.entries.find((e) => e.type === 0x102 && e.id === 8)!.data;
        new DataView(player.buffer, player.byteOffset).setUint32(CELL_BASE + 16, 1, true);
        const out = addItem(parseSave(buildSave(save.entries)), 7);
        const added = out.save.entries.find((e) => e.type === 0x102 && e.id === out.slot + 1)!.data;
        expect(new DataView(added.buffer, added.byteOffset).getUint32(0x148 + 0x154, true)).toBe(0xffffffff);
        expect(listInventory(out.save).find((c) => c.cell === out.cell)!.equipped).toBe(0);
    });

    it('fills the next free cell and refuses a non-item template', () => {
        const once = addItem(parseSave(addFixture()), 7).save;
        const twice = addItem(once, 7);
        expect(twice.slot).toBe(K + 1);
        expect(twice.droppedStatic).toBe(5018);
        expect(() => addItem(parseSave(addFixture()), 1)).toThrow(/not an item record/);
    });

    it('edits new item counts before export and preserves them when another addition is removed', () => {
        const doc = SaveDocument.open(addFixture());
        const stack = new CommandStack(doc);
        stack.run(new AddItemCommand(7, 2));
        const defaults = doc.addedItemCounts(0)!;
        stack.run(new SetAddedItemCountCommand(0, 'durability', 77));
        stack.run(new SetAddedItemCountCommand(0, 'max', 200));
        stack.run(new AddItemCommand(7, 2));
        stack.run(new SetAddedItemCountCommand(1, 'durability', 88));
        const exported = () => SaveDocument.open(doc.build().bytes).inventory.filter((c) => c.resource === 0x31cbc);
        expect(exported().map((c) => [c.cell, c.durability])).toEqual([
            [2, 77],
            [3, 88],
        ]);
        expect(exported()[0]!.max).toBe(200);
        expect(doc.pendingEdits().find((e) => e.name === 'add.0')!.text!.after).toContain('quantity/durability 77');
        stack.run(new RemoveAddedItemCommand(0));
        expect(exported().map((c) => [c.cell, c.durability])).toEqual([[2, 88]]);
        stack.undo();
        expect(exported().map((c) => [c.cell, c.durability])).toEqual([
            [2, 77],
            [3, 88],
        ]);
        stack.undo(); // second item's count
        expect(doc.addedItemCounts(1)!.durability).toBe(defaults.durability);
        stack.redo();
        expect(exported()[1]!.durability).toBe(88);
        expect(() => stack.run(new SetAddedItemCountCommand(1, 'max', -1))).toThrow();
        expect(() => stack.run(new SetAddedItemCountCommand(1, 'max', 1.5))).toThrow();
        expect(() => stack.run(new SetAddedItemCountCommand(1, 'max', 0x100000000))).toThrow();
        expect(doc.addedItemCounts(1)!.max).toBe(defaults.max);
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

    it('writes the item height as the second word of its stream pair', () => {
        // In every save the game wrote the word is the height in cells (bow, club, scimitar 2; potions 1), and the
        // loader marks the cells under the item from it: a tall item written with 1 loads as a one-cell item.
        const tall = addItem(parseSave(addFixture()), 7, 2);
        expect(itemRows(tall.save, tall.cell)).toBe(2);
        const r = tall.save.entries.find((x) => x.type === 0x102 && x.id === 8)!.data;
        const rv = new DataView(r.buffer, r.byteOffset);
        const at = 0x700 + 4 * (2 + 1 + 1 + 2 + 1 + 1); // stream position 6 = cell 2
        expect([rv.getUint32(at, true), rv.getUint32(at + 4, true)]).toEqual([K, 2]);
        const short = addItem(tall.save, 7, 1);
        expect(itemRows(short.save, short.cell)).toBe(1);
        expect(itemRows(short.save, 0)).toBe(1); // the carried potion
    });

    it('copies the carried state from an unequipped item and holds the new item in the pack, not in a hand', () => {
        // cell 0 (the potion) is flagged equipped in the fixture; the dagger in cell 1 is not
        const save = parseSave(addFixture());
        const potion = save.entries.find((x) => x.type === 0x102 && x.id === 4)!;
        new DataView(potion.data.buffer, potion.data.byteOffset).setUint32(0x52, 0x000000c0, true); // a holder ref
        const out = addItem(save, 7);
        const added = out.save.entries.find((x) => x.type === 0x102 && x.id === K + 1)!;
        const dv = new DataView(added.data.buffer, added.data.byteOffset);
        expect(dv.getFloat32(0x0c, true)).toBe(2); // position of the dagger (the unequipped donor)
        expect([dv.getUint16(0x52, true), dv.getUint16(0x54, true)]).toEqual([0xffff, 0xffff]);
    });

    it('adds a bow-shaped record with its new handle at the record’s own offset', () => {
        const save = parseSave(addFixture());
        const withBow = {
            ...save,
            entries: save.entries.map((e) =>
                e.type === 0x102 && e.id === 9 ? { ...e, data: shiftedItem(0x31bc9, 0x7b0010, 0x112, 0) } : e,
            ),
        };
        const out = addItem(withBow, 9, 2);
        const added = out.save.entries.find((x) => x.type === 0x102 && x.id === K + 1)!;
        const dv = new DataView(added.data.buffer, added.data.byteOffset);
        const h = dv.getUint32(0x112, true);
        expect(dv.getUint32(0x112 + 0xe0, true)).toBe(h);
        expect([HANDLE_A, HANDLE_B, HANDLE_GROUND, 0x7b0010]).not.toContain(h);
        expect(listInventory(out.save).find((c) => c.cell === out.cell)!.resource).toBe(0x31bc9);
        expect(itemRows(out.save, out.cell)).toBe(2);
    });

    it('Replace with a taller class takes the cells under the item, or refuses when they are taken', () => {
        const save = parseSave(addFixture());
        const out = replaceItem(save, 1, 7, 3); // the dagger in cell 1 becomes a 3-cell club
        expect(itemRows(out, 1)).toBe(3);
        expect(listInventory(out).map((c) => [c.cell, c.marker ?? null])).toEqual([
            [0, null],
            [1, null],
            [9, 1],
            [17, 2],
        ]);
        // the record grows by nothing: only the height word changes
        const len = (s: typeof save): number => s.entries.find((x) => x.type === 0x102 && x.id === 8)!.data.length;
        expect(len(out)).toBe(len(save));
        // back to one cell: the markers go again
        const back = replaceItem(out, 1, 7, 1);
        expect(listInventory(back).map((c) => c.cell)).toEqual([0, 1]);
        // a tall item under the potion's column blocks a 2-cell potion
        const blocked = addItem(save, 7, 1); // cell 2
        const under = addItem(blocked.save, 7, 1); // cell 3
        expect(() => replaceItem(under.save, 0, 7, 4)).toThrow(/1 to 3 cells/);
        const tallNext = addItem(save, 7, 3); // column 2
        expect(() => replaceItem(tallNext.save, 2, 7, 2)).not.toThrow(); // shrinking its own column is fine
        // without a height the item keeps its height (the old behaviour)
        expect(itemRows(replaceItem(save, 1, 7), 1)).toBe(1);
    });

    it('refuses a taller replacement when a pending added item takes the cell under it', () => {
        const d = SaveDocument.open(addFixture());
        const s = new CommandStack(d);
        for (let i = 0; i < 6; i++) s.run(new AddItemCommand(7, 1)); // cells 2..7
        s.run(new AddItemCommand(7, 1)); // cell 8: under the potion
        expect(d.addedCells().at(-1)).toBe(8);
        expect(d.replaceFits(0, 2)).toBe(false);
        expect(() => s.run(new ReplaceItemCommand(0, 7, 2))).toThrow(/cells under cell 0 are taken/);
        expect(d.itemReplacements.size).toBe(0);
        expect(d.replaceFits(1, 3)).toBe(true);
        for (let i = 0; i < 7; i++) s.undo(); // the fixture has only four static objects to give up for new records
        s.run(new ReplaceItemCommand(1, 7, 3));
        expect(d.placeFor(1)).toBe(2);
        expect(d.placeFor(3)).toBe(2); // 9 and 17 now belong to the replaced item
        s.run(new AddItemCommand(7, 1));
        s.run(new AddItemCommand(7, 3));
        expect(d.addedCells()).toEqual([2, 3]);
        const out = parseSave(d.build().bytes);
        expect(itemRows(out, 1)).toBe(3);
        expect(
            listInventory(out)
                .filter((c) => c.marker !== undefined)
                .map((c) => c.cell),
        ).toEqual([9, 11, 17, 19]);
        for (let i = 0; i < 3; i++) s.undo();
        expect(d.itemReplacementRows.size).toBe(0);
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
