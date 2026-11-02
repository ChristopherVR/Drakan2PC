// Synthetic only.
import type { LevelObject } from '@drakan/editor-core';
import { describe, expect, it } from 'vitest';
import { objectFieldsPrint, scrollAfterRebuild } from '../src/editor/inspectorsync.ts';

describe('details panel sync', () => {
    it('scrolls to the top for a new selection and keeps the place for the same one', () => {
        expect(scrollAfterRebuild('4', '9', 320)).toBe(0);
        expect(scrollAfterRebuild('4,5', '4', 320)).toBe(0);
        expect(scrollAfterRebuild('4', '4', 320)).toBe(320);
    });
    it('prints differently after a field edit and the same after an undo back', () => {
        const base = { fields: [{ slot: 0, fieldIndex: 1, type: 1, name: 'a', value: 5 }] } as unknown as LevelObject;
        const edited = { fields: [{ slot: 0, fieldIndex: 1, type: 1, name: 'a', value: 6 }] } as unknown as LevelObject;
        expect(objectFieldsPrint(base)).not.toBe(objectFieldsPrint(edited));
        expect(objectFieldsPrint(base)).toBe(objectFieldsPrint(structuredClone(base)));
        expect(objectFieldsPrint({} as LevelObject)).toBe('[]');
    });
});
