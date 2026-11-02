import { describe, expect, it } from 'vitest';
import { RESIZE_SETTLE_MS, canvasSize, fullRatio, inBurst, sameSize, settled } from '../src/scene/viewsize.ts';

describe('viewport canvas size', () => {
    it('caps the pixel ratio at 2 and falls back to 1 for nonsense', () => {
        expect(fullRatio(1)).toBe(1);
        expect(fullRatio(1.5)).toBe(1.5);
        expect(fullRatio(3)).toBe(2);
        expect(fullRatio(0)).toBe(1);
        expect(fullRatio(Number.NaN)).toBe(1);
    });

    it('draws at ratio 1 at most while a resize is going on', () => {
        expect(canvasSize(800, 600, 2, false)).toEqual({ w: 800, h: 600, ratio: 2 });
        expect(canvasSize(800, 600, 2, true)).toEqual({ w: 800, h: 600, ratio: 1 });
        expect(canvasSize(800, 600, 0.75, true).ratio).toBe(0.75);
    });

    it('gives a hidden host a 1 x 1 canvas', () => {
        expect(canvasSize(0, 0, 1, false)).toEqual({ w: 1, h: 1, ratio: 1 });
        expect(canvasSize(640.6, 480.2, 1, false)).toEqual({ w: 640, h: 480, ratio: 1 });
    });

    it('skips a resize to the same size', () => {
        const a = canvasSize(800, 600, 1, false);
        expect(sameSize(null, a)).toBe(false);
        expect(sameSize(a, canvasSize(800, 600, 1, false))).toBe(true);
        expect(sameSize(a, canvasSize(801, 600, 1, false))).toBe(false);
        expect(sameSize(canvasSize(800, 600, 2, false), canvasSize(800, 600, 2, true))).toBe(false);
    });

    it('treats resizes close together as one drag', () => {
        expect(inBurst(-Infinity, 0)).toBe(false);
        expect(inBurst(1000, 1016)).toBe(true);
        expect(inBurst(1000, 1000 + RESIZE_SETTLE_MS)).toBe(false);
    });

    it('goes back to the full ratio once the drag has stopped', () => {
        const low = canvasSize(800, 600, 2, true);
        expect(settled(low, 1000, 1000 + RESIZE_SETTLE_MS - 1, 2)).toBe(false);
        expect(settled(low, 1000, 1000 + RESIZE_SETTLE_MS, 2)).toBe(true);
        expect(settled(canvasSize(800, 600, 2, false), 1000, 5000, 2)).toBe(false);
        expect(settled(canvasSize(800, 600, 1, true), 1000, 5000, 1)).toBe(false);
        expect(settled(null, 1000, 5000, 2)).toBe(false);
    });
});
