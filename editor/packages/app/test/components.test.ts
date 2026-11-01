import { describe, expect, it } from 'vitest';
import { activatesRow, gridArea } from '../src/shared/components.ts';

describe('shared grid and list rows', () => {
    it('places a cell in an 8-wide grid and spans only when larger than one cell', () => {
        expect(gridArea(0, 8)).toBe('grid-column:1;grid-row:1');
        expect(gridArea(11, 8)).toBe('grid-column:4;grid-row:2');
        expect(gridArea(3, 8, { cols: 1, rows: 2 })).toBe('grid-column:4;grid-row:1 / span 2');
        expect(gridArea(16, 8, { cols: 2, rows: 1 })).toBe('grid-column:1 / span 2;grid-row:3');
    });

    it('activates a focused row with Enter or Space only', () => {
        expect(activatesRow('Enter')).toBe(true);
        expect(activatesRow(' ')).toBe(true);
        expect(activatesRow('a')).toBe(false);
        expect(activatesRow('Tab')).toBe(false);
    });
});
