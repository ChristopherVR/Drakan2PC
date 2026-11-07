import { describe, expect, it } from 'vitest';
import { isRynnStart } from '../src/level.ts';
import type { LevelObject } from '@drakan/editor-core';

const obj = (kind: string, cls: string) => ({ kind, class: cls }) as unknown as LevelObject;

describe('Rynn start', () => {
    it('recognises Rynn the player and nothing else', () => {
        expect(isRynnStart(obj('player', 'Rynn'))).toBe(true);
        expect(isRynnStart(obj('player', 'rynn'))).toBe(true);
        expect(isRynnStart(obj('character', 'Rynn'))).toBe(false);
        expect(isRynnStart(obj('player', 'Arokh'))).toBe(false);
        expect(isRynnStart(undefined)).toBe(false);
    });
});
