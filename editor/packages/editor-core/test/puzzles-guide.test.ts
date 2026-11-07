import { describe, expect, it } from 'vitest';
import { GUIDES, TEMPLATES } from '../src/index.ts';

describe('puzzles guide', () => {
    const g = GUIDES.find((x) => x.id === 'puzzles');

    it('exists and names every puzzle template and the locked-door rule', () => {
        expect(g).toBeTruthy();
        const text = [...g!.steps, ...g!.notes].join(' ');
        for (const word of ['Lever opens door', 'Pressure plates', 'Timed trigger', 'Ambush', 'Kill all', 'Key lock']) {
            expect(text, word).toContain(word);
        }
        expect(text).toMatch(/locked door ignores Opens/i);
    });

    it('the lever template tells the author a locked door ignores Opens', () => {
        const door = TEMPLATES.find((t) => t.id === 'lever-door')!.params.find((p) => p.id === 'door')!;
        expect(door.hint).toMatch(/not Locked/);
    });
});
