import { describe, expect, it } from 'vitest';
import { progressText } from '../src/editor/ootfimport.ts';

describe('importer progress text', () => {
    it('names the level being converted instead of the converter bookkeeping', () => {
        expect(progressText('sequences OotF 07 - Alwarren: 7 referenced, 7 converted', 'OotF ')).toBe(
            'Converting OotF 07 - Alwarren...',
        );
        expect(progressText('puzzles OotF - Relic (ground): 0 plates -> 0 detectors', 'OotF - ')).toBe(
            'Converting OotF - Relic (ground)...',
        );
    });

    it('turns the language probe into a plain sentence', () => {
        expect(progressText('source language: german (stop words: german 140, english 3)', 'OotF ')).toBe(
            'Install language: German. Converting...',
        );
    });

    it('falls back to a plain line for anything else', () => {
        expect(progressText('retarget Primitive Giant: PC 44 joints', 'OotF ')).toBe('Converting...');
    });
});
