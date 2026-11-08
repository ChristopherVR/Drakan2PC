// Synthetic data only; no game bytes.
import { describe, expect, it } from 'vitest';
import { choiceToOverride, defaultChoice, isOffMap, limitImage, normalizePicture, normalizeWorld, previewPoints } from '../src/editor/levelmap.ts';
import { markerLabel } from '../src/editor/ootfmaps.ts';

const base = { world: { x0: 0, z0: 0, x1: 100, z1: 100 }, picture: { x0: 0.1, y0: 0.1, x1: 0.9, y1: 0.9 } };
const img = { width: 2, height: 2, rgba: new Uint8Array(16) };

describe('map choice', () => {
    it('sends nothing for an untouched level', () => {
        expect(choiceToOverride(defaultChoice(base), base, () => null)).toBeUndefined();
    });
    it('sends only the rectangle that changed', () => {
        const c = defaultChoice(base);
        c.world = { x0: 10, z0: 0, x1: 100, z1: 100 };
        expect(choiceToOverride(c, base, () => null)).toEqual({ world: c.world });
    });
    it("uses another level's parchment with its picture rectangle", () => {
        const c = defaultChoice(base);
        c.source = { kind: 'level', path: 'a/b.lvl' };
        c.picture = { x0: 0, y0: 0, x1: 1, y1: 1 };
        const o = choiceToOverride(c, base, (p) => (p === 'a/b.lvl' ? img : null))!;
        expect(o.image).toBe(img);
        expect(o.picture).toEqual({ x0: 0, y0: 0, x1: 1, y1: 1 });
    });
    it("sends a player's own picture", () => {
        const c = defaultChoice(base);
        c.source = { kind: 'file', name: 'mine.png', image: img };
        c.picture = { x0: 0, y0: 0, x1: 1, y1: 1 };
        expect(choiceToOverride(c, base, () => null)!.image).toBe(img);
    });
    it('keeps rectangles valid', () => {
        expect(normalizeWorld({ x0: 5, z0: 5, x1: 5, z1: 4 })).toEqual({ x0: 3, z0: 2, x1: 5, z1: 4 });
        const p = normalizePicture({ x0: -1, y0: 0.2, x1: 2, y1: 0.2 });
        expect(p.x0).toBe(0);
        expect(p.x1).toBe(1);
        expect(p.y1 - p.y0).toBeGreaterThanOrEqual(0.05);
    });
});

describe('preview points', () => {
    it('places the start and the quest places through the calibration', () => {
        const pts = previewPoints(base, { x: 50 * 2048, z: 50 * 2048 }, [{ label: 'Find the key', x: 0, z: 0 }]);
        expect(pts[0]).toMatchObject({ kind: 'start', px: 128, py: 128 });
        expect(pts[1]).toMatchObject({ kind: 'quest', label: 'Find the key', px: 26, py: 26 });
    });
    it('knows when a place is outside the shown area', () => {
        expect(isOffMap(base.world, 50 * 2048, 50 * 2048)).toBe(false);
        expect(isOffMap(base.world, 150 * 2048, 50 * 2048)).toBe(true);
    });
});

describe('helpers', () => {
    it('shrinks big pictures and keeps small ones', () => {
        const big = { width: 2048, height: 1024, rgba: new Uint8Array(2048 * 1024 * 4) };
        const s = limitImage(big, 1024);
        expect([s.width, s.height]).toEqual([1024, 512]);
        expect(limitImage(img)).toBe(img);
    });
    it('names a marker like the converter does', () => {
        expect(markerLabel('Map Trigger - Find Rimril Icon')).toBe('Find Rimril');
        expect(markerLabel('Map Trigger')).toBe('');
    });
});
