import { afterEach, describe, expect, it } from 'vitest';
import { beginLoading, isLoading, onLoading, resetLoading } from '../src/editor/loadstate.ts';

afterEach(resetLoading);

describe('loading state', () => {
    it('is on from the first begin until every load has ended', () => {
        expect(isLoading()).toBe(false);
        const a = beginLoading();
        const b = beginLoading();
        a();
        expect(isLoading()).toBe(true);
        b();
        expect(isLoading()).toBe(false);
    });
    it('ending twice does not end another load', () => {
        const a = beginLoading();
        const b = beginLoading();
        a();
        a();
        expect(isLoading()).toBe(true);
        b();
        expect(isLoading()).toBe(false);
    });
    it('tells subscribers when it starts and stops, and stops telling after unsubscribe', () => {
        let n = 0;
        const off = onLoading(() => n++);
        const end = beginLoading();
        end();
        expect(n).toBe(2);
        off();
        beginLoading()();
        expect(n).toBe(2);
    });
});
