import { describe, expect, it } from 'vitest';
import { unresolvedClassName } from '../src/lvl-doc.ts';

describe('unresolvedClassName', () => {
    it('names the kind of database instead of a bare "class N"', () => {
        expect(unresolvedClassName('FX', 12180)).toBe('Effects class 12180');
        expect(unresolvedClassName('NPCS', 5)).toBe('Character class 5');
        expect(unresolvedClassName('LEVEL1ON', 2368)).toBe('Level1on class 2368');
    });
    it('copes with a database the level does not list', () => {
        expect(unresolvedClassName(undefined, 7)).toBe('Unknown class 7');
    });
});
