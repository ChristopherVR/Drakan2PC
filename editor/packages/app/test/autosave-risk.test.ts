import { describe, expect, it } from 'vitest';
import { editsAtRisk } from '../src/editor/autosave.ts';

describe('editsAtRisk', () => {
    it('is false for an empty editor, an untouched level and a level restored clean', () => {
        expect(editsAtRisk(0, 0, false)).toBe(false);
        expect(editsAtRisk(4675, 0, false)).toBe(false);
    });
    it('is true once there is an undoable edit, or edits restored from an autosave', () => {
        expect(editsAtRisk(4675, 1, false)).toBe(true);
        expect(editsAtRisk(4675, 0, true)).toBe(true);
    });
});
