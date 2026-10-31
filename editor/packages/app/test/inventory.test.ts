import { describe, expect, it } from 'vitest';
import { itemName, resourceKey, spans } from '../src/save/inventory.ts';

describe('inventory helpers', () => {
    it('sizes multi-cell items from the continuation markers', () => {
        const s = spans([
            { cell: 0, handle: 1 },
            { cell: 8, marker: 1 },
            { cell: 9, handle: 2 },
            { cell: 17, marker: 1 },
        ]);
        expect(s.get(0)).toEqual({ cols: 1, rows: 2 });
        expect(s.get(9)).toEqual({ cols: 1, rows: 2 });
        expect(s.has(8)).toBe(false);
    });

    it('names items from the class table, then the known table, then the id', () => {
        expect(itemName(0x31bc9, { name: 'Long_Bow', thumb: null })).toBe('Long Bow');
        expect(itemName(0x3217b, null)).toBe('Wood Club');
        expect(itemName(0x12345, null)).toBe('Item 0x12345');
        expect(resourceKey(0x31bc9)).toBe('0x31bc9');
    });
});
