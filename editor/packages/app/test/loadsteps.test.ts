import { describe, expect, it } from 'vitest';
import {
    LOAD_STEPS,
    LoadCancelled,
    estimate,
    headline,
    isCancelled,
    levelShown,
    loadFinished,
    loadFraction,
    newLoad,
    readyLine,
    stepDetail,
} from '../src/editor/loadsteps.ts';

describe('level load steps', () => {
    it('starts on reading the level, with an empty bar', () => {
        const s = newLoad('Level 1A - Surdana');
        expect(s.steps.read).toBe('active');
        expect(loadFraction(s)).toBe(0);
        expect(headline(s)).toBe('Reading the level...');
        expect(levelShown(s)).toBe(false);
        expect(loadFinished(s)).toBe(false);
    });

    it('fills the bar by step weight and by model batches', () => {
        const s = newLoad('x');
        const total = LOAD_STEPS.reduce((a, st) => a + st.weight, 0);
        s.steps.read = 'done';
        s.steps.objects = 'done';
        s.objects = 4675;
        expect(levelShown(s)).toBe(true);
        expect(loadFraction(s)).toBeCloseTo(25 / total);
        s.steps.terrain = 'done';
        s.layers = 483;
        s.steps.models = 'active';
        s.models = { done: 269, total: 538, missing: 0 };
        expect(loadFraction(s)).toBeCloseTo((40 + 30) / total);
        expect(headline(s)).toBe('Loading models: 269 of 538');
        expect(stepDetail(s, 'objects')).toBe('4,675 placed');
        expect(stepDetail(s, 'terrain')).toBe('483 layers');
        expect(stepDetail(s, 'models')).toBe('269 of 538');
    });

    it('uses an elapsed-time estimate for steps without a count, never reaching the end', () => {
        expect(estimate(0, 700)).toBe(0);
        expect(estimate(700, 700)).toBeGreaterThan(0.6);
        expect(estimate(1e6, 700)).toBeLessThan(1);
        const s = newLoad('x');
        s.partial = 0.5;
        expect(loadFraction(s)).toBeCloseTo(0.1);
    });

    it('says how it ended', () => {
        const s = newLoad('x');
        for (const st of LOAD_STEPS) s.steps[st.id] = 'done';
        s.models = { done: 538, total: 538, missing: 101 };
        expect(loadFinished(s)).toBe(true);
        expect(loadFraction(s)).toBe(1);
        expect(readyLine(s, 'Surdana')).toBe('Surdana is ready. 101 object types have no model and show as markers.');
        s.models.missing = 0;
        expect(readyLine(s, 'Surdana')).toBe('Surdana is ready.');
        s.steps.models = 'stopped';
        s.models.done = 100;
        expect(stepDetail(s, 'models')).toBe('Stopped at 100 of 538');
        expect(readyLine(s, 'Surdana')).toMatch(/^Surdana is open\. Models that were not loaded show as markers/);
    });

    it('tells a cancel apart from a failure', () => {
        expect(isCancelled(new LoadCancelled())).toBe(true);
        expect(isCancelled(new Error('no .LVL file'))).toBe(false);
    });
});
