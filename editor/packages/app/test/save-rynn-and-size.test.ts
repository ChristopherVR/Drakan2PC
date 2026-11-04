import { describe, expect, it } from 'vitest';
import {
    FIELD_INT,
    KNOWN_CLASSES,
    RYNN_CLASS,
    classMatches,
    getField,
    isRynnClass,
    parseSave,
    recordLabel,
    type OdbClass,
} from '@drakan/formats';
import { SaveDocument, itemCells, placeItem } from '@drakan/editor-core';
import { buildSave, saveEntries } from '../../formats/test/savefixture.ts';
import { itemCellCount } from '../src/itemdata.ts';
import { itemHeight, noRoomMessage, previewCells, spans } from '../src/save/inventory.ts';

/** The synthetic save with Rynn's class word replaced (the game-written variant sets bit 27). */
function saveWithRynnWord(word: number): Uint8Array {
    const entries = saveEntries();
    const rynn = entries.find(
        (e) => e.type === 0x102 && new DataView(e.data.buffer).getUint32(8, true) === RYNN_CLASS,
    )!;
    new DataView(rynn.data.buffer).setUint32(8, word, true);
    return buildSave(entries);
}

describe("Rynn's class word", () => {
    it('accepts the sample word and the game-written word with bit 27 set, nothing else', () => {
        expect(isRynnClass(0x17081069)).toBe(true);
        expect(isRynnClass(0x1f081069)).toBe(true);
        expect(isRynnClass(0x17081068)).toBe(false);
        expect(isRynnClass(0x17181069)).toBe(false); // another bit
        expect(isRynnClass(0x1f181069)).toBe(false);
        expect(isRynnClass(0x19001255)).toBe(false); // gold counter
        expect(isRynnClass(undefined)).toBe(false);
        expect(classMatches(0x1f081069, RYNN_CLASS)).toBe(true);
        expect(classMatches(0x11001255, 0x19001255)).toBe(false); // other classes stay exact
    });

    it('finds Rynn in a game-written save (position, label, player marker)', () => {
        const blob = saveWithRynnWord(0x1f081069);
        expect(getField(parseSave(blob), 'pos')).toEqual([1000, 2000, 3000]);
        expect(recordLabel({ classId: 0x1f081069 })).toBe('Rynn (player)');
        expect(KNOWN_CLASSES[0x1f081069]).toBe('Rynn (player)');
        const doc = SaveDocument.open(blob);
        expect(doc.playerObject).not.toBeNull();
    });

    it('still rejects a record whose word differs in another bit', () => {
        const doc = SaveDocument.open(saveWithRynnWord(0x17181069));
        expect(doc.playerObject).toBeNull();
        expect(() => getField(parseSave(saveWithRynnWord(0x17181069)), 'pos')).toThrow(/found 0/);
    });
});

describe('Add item size checks', () => {
    it('previews taller and shorter pending replacements without leaving old occupied cells', () => {
        const original = [
            { cell: 1, resource: 0x33752 },
            { cell: 2, resource: 0x3372d },
            { cell: 10, marker: 1 },
        ];
        const preview = previewCells(
            original,
            new Map([
                [1, 3],
                [2, 1],
            ]),
        );
        expect(spans(preview).get(1)?.rows).toBe(3);
        expect(spans(preview).get(2)?.rows).toBe(1);
        expect(preview.filter((c) => c.marker !== undefined).map((c) => c.cell)).toEqual([9, 17]);
        expect(original).toHaveLength(3);
    });
    it('keeps a tall item inside the grid, spanning down one column', () => {
        expect(itemCells(0, 1)).toEqual([0]);
        expect(itemCells(5, 3)).toEqual([5, 13, 21]);
        expect(itemCells(8, 2)).toEqual([8, 16]);
        expect(itemCells(8, 3)).toBeNull(); // would leave the bottom of the grid
        expect(itemCells(16, 2)).toBeNull();
        expect(itemCells(23, 1)).toEqual([23]);
        expect(itemCells(24, 1)).toBeNull();
        expect(itemCells(0, 0)).toBeNull();
    });

    it('picks the first place where every cell is free', () => {
        const row0 = new Set([0, 1, 2, 3, 4, 5, 6, 7]);
        expect(placeItem(new Set(), 3)).toBe(0);
        expect(placeItem(row0, 1)).toBe(8);
        expect(placeItem(row0, 2)).toBe(8);
        expect(placeItem(row0, 3)).toBeNull(); // no column has three empty cells
        expect(placeItem(new Set([...row0, 16]), 2)).toBe(9); // cell 8 has an item under it
        expect(placeItem(new Set([0, 9, 2, 3, 4, 5, 6, 7]), 2)).toBe(8); // cell 1 is free but cell 9 under it is not
        const full = new Set(Array.from({ length: 24 }, (_, i) => i));
        expect(placeItem(full, 1)).toBeNull();
    });

    it('takes the height from the disc, else from a carried item of the same class', () => {
        const carried = [
            { cell: 0, handle: 0xa00010, resource: 0x3372d },
            { cell: 8, marker: 1 },
            { cell: 1, handle: 0xa00020, resource: 0x33752 },
        ];
        expect(itemHeight(0x3372d, { name: 'Rusty Scimitar', thumb: null, cells: 2 }, [])).toBe(2);
        expect(itemHeight(0x3372d, null, carried)).toBe(2);
        expect(itemHeight(0x33752, null, carried)).toBe(1);
        expect(itemHeight(0x31cbc, null, carried)).toBeUndefined();
    });

    it('reads the height from the class field and ignores out-of-range values', () => {
        const cls = (v: number): Pick<OdbClass, 'fields' | 'data'> => ({
            fields: [{ name: 'Icon Slot Size (1-3)', type: FIELD_INT }],
            data: [v],
        });
        const at = (n: string): number => (n === 'Icon Slot Size (1-3)' ? 0 : -1);
        expect(itemCellCount(cls(2), at)).toBe(2);
        expect(itemCellCount(cls(3), at)).toBe(3);
        expect(itemCellCount(cls(0), at)).toBeUndefined();
        expect(itemCellCount(cls(4), at)).toBeUndefined();
        expect(itemCellCount(cls(1), () => -1)).toBeUndefined();
    });

    it('explains in plain words why an item does not fit', () => {
        expect(noRoomMessage('Health Elixir', 1, 1)).toBe('Health Elixir does not fit: the inventory is full.');
        expect(noRoomMessage('Rusty Scimitar', 2, 2)).toMatch(/2 cells tall and no column has 2 empty cells/);
        expect(noRoomMessage('Axe', 3, undefined)).toMatch(/not known without the game disc/);
    });
});
