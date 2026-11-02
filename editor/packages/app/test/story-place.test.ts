import { describe, expect, it } from 'vitest';
import { PLACE_CATS, sentence, storyCategory } from '../src/editor/placecats.ts';
import { grownSize } from '../src/editor/splitmath.ts';
import { placeableReason, storyPlaceables, type StoryPlacer } from '../src/story/storyactions.ts';
import { PLACEMENT } from '../src/editor/ia.ts';

const placer = (why: string | null): StoryPlacer => ({ place: () => undefined, unavailable: () => why });

describe('story placeables in the Place panel', () => {
    it('lists every entry under an existing group', () => {
        const ids = new Set(PLACE_CATS.map((c) => c.id));
        const cats = storyPlaceables().map((p) => storyCategory(p.category));
        expect(cats).toEqual(['cutscenes', 'quests', 'quests', 'vendors']);
        for (const c of cats) expect(ids.has(c)).toBe(true);
        expect(storyCategory('Something new')).toBe('other');
    });

    it('greys an entry with its fixed reason, the placer reason, or no level', () => {
        const [cut, start, , shop] = storyPlaceables();
        expect(placeableReason(cut!, undefined)).toBe('Open a level from the disc first');
        expect(placeableReason(cut!, placer(null))).toBeNull();
        expect(placeableReason(start!, placer('Select one quest first'))).toBe('Select one quest first');
        // A shop can be placed; a level without shop classes greys it through its placer.
        expect(shop!.unavailable).toBeNull();
        expect(placeableReason(shop!, placer(null))).toBeNull();
        expect(placeableReason(shop!, placer('This level has no shop class'))).toBe('This level has no shop class');
        // A fixed reason wins even when a placer exists.
        expect(placeableReason({ ...shop!, unavailable: 'Not yet' }, placer(null))).toBe('Not yet');
    });

    it('writes reasons as sentences', () => {
        expect(sentence('select one quest first')).toBe('Select one quest first.');
        expect(sentence('Edit them in Vendors.')).toBe('Edit them in Vendors.');
        expect(sentence('  ')).toBe('');
    });
});

describe('Story docks beside the viewport', () => {
    it('opens in the bottom drawer by default and can be popped out', () => {
        expect(PLACEMENT.story!.home).toBe('dock');
        expect(PLACEMENT.story!.movable).toBe(true);
        expect(PLACEMENT.story!.dockShare).toBeGreaterThan(0.3);
    });

    it('grows the drawer to its share without shrinking a bigger one', () => {
        const lim = { min: 120, max: 720, def: 280 };
        expect(grownSize(280, 900, 0.5, lim, 600)).toBe(450);
        expect(grownSize(500, 900, 0.5, lim, 600)).toBe(500);
        // never past the room the viewport leaves
        expect(grownSize(280, 1600, 0.5, lim, 600)).toBe(600);
        // not measured yet, or no share: unchanged
        expect(grownSize(280, 0, 0.5, lim, 600)).toBe(280);
        expect(grownSize(280, 900, 0, lim, 600)).toBe(280);
    });
});
