import { describe, expect, it } from 'vitest';
import {
    clampSize,
    dragTo,
    keyResize,
    readLayout,
    roomFor,
    shownSize,
    writeLayout,
    type PaneLimits,
} from '../src/editor/splitmath.ts';
import {
    compareNames,
    matchesQuery,
    PLACE_CATS,
    placeCategory,
    pushRecent,
    readRecent,
    templateCategory,
} from '../src/editor/placecats.ts';
import { TEMPLATES } from '@drakan/editor-core';

const lim: PaneLimits = { min: 200, max: 560, def: 280 };

describe('splitter sizes', () => {
    it('clamps, and keeps the minimum when the room is smaller than it', () => {
        expect(clampSize(100, 200, 560)).toBe(200);
        expect(clampSize(900, 200, 560)).toBe(560);
        expect(clampSize(300, 200, 150)).toBe(200);
    });

    it('leaves the viewport its share of the window', () => {
        // 1600 wide, the other side panel takes 340, the viewport keeps 320: at most 560 (the pane's own maximum).
        expect(roomFor(1600, 340, lim, 320)).toBe(560);
        // 1000 wide: 1000 - 340 - 320 = 340.
        expect(roomFor(1000, 340, lim, 320)).toBe(340);
        // Too narrow for anything: the minimum still holds.
        expect(roomFor(600, 340, lim, 320)).toBe(200);
        // Not measured yet (hidden): the pane's maximum.
        expect(roomFor(0, 0, lim, 320)).toBe(560);
    });

    it('draws a hidden pane at 0 and an open one inside its room', () => {
        expect(shownSize({ size: 400, open: false }, lim, 560)).toBe(0);
        expect(shownSize({ size: 500, open: true }, lim, 340)).toBe(340);
        expect(shownSize({ size: 50, open: true }, lim, 560)).toBe(200);
    });

    it('follows a drag on either side of the splitter', () => {
        const prev = { size: 280, open: true };
        // Left pane: dragging right grows it.
        expect(dragTo(280, 60, false, lim, 560, true, prev)).toEqual({ size: 340, open: true });
        // Right pane (after the splitter): dragging right shrinks it.
        expect(dragTo(280, 60, true, lim, 560, true, prev)).toEqual({ size: 220, open: true });
        // Never past the room it has.
        expect(dragTo(280, 900, false, lim, 450, true, prev)).toEqual({ size: 450, open: true });
    });

    it('hides a collapsible pane dragged almost shut and remembers its size', () => {
        const prev = { size: 330, open: true };
        expect(dragTo(330, -260, false, lim, 560, true, prev)).toEqual({ size: 330, open: false });
        // The Outliner cannot be hidden: it stops at its minimum.
        expect(dragTo(330, -260, false, lim, 560, false, prev)).toEqual({ size: 200, open: true });
        // Dragging a hidden pane open again starts from 0.
        expect(dragTo(0, 250, false, lim, 560, true, { size: 330, open: false })).toEqual({ size: 250, open: true });
    });

    it('moves with the keyboard', () => {
        const s = { size: 300, open: true };
        expect(keyResize(s, 'ArrowRight', false, 'x', false, lim, 560, true)).toEqual({ size: 316, open: true });
        expect(keyResize(s, 'ArrowRight', true, 'x', false, lim, 560, true)).toEqual({ size: 364, open: true });
        // Right column: ArrowRight moves the bar toward it, so it shrinks.
        expect(keyResize(s, 'ArrowRight', false, 'x', true, lim, 560, true)).toEqual({ size: 284, open: true });
        // Bottom drawer: ArrowUp grows it.
        expect(keyResize(s, 'ArrowUp', false, 'y', true, lim, 560, true)).toEqual({ size: 316, open: true });
        expect(keyResize(s, 'Home', false, 'x', false, lim, 560, true)).toEqual({ size: 200, open: true });
        expect(keyResize(s, 'End', false, 'x', false, lim, 480, true)).toEqual({ size: 480, open: true });
        expect(keyResize(s, 'Enter', false, 'x', false, lim, 560, true)).toEqual({ size: 300, open: false });
        expect(keyResize(s, 'Enter', false, 'x', false, lim, 560, false)).toBeNull();
        // Arrows across the bar do nothing.
        expect(keyResize(s, 'ArrowUp', false, 'x', false, lim, 560, true)).toBeNull();
        // A hidden pane opens at its minimum on a key that grows it, and stays hidden on one that shrinks it.
        const hidden = { size: 300, open: false };
        expect(keyResize(hidden, 'ArrowRight', false, 'x', false, lim, 560, true)).toEqual({ size: 200, open: true });
        expect(keyResize(hidden, 'ArrowLeft', false, 'x', false, lim, 560, true)).toBeNull();
    });

    it('reads stored sizes defensively and writes them back', () => {
        const limits = { left: lim, right: { min: 240, max: 720, def: 340 } };
        expect(readLayout(null, limits)).toEqual({
            left: { size: 280, open: true },
            right: { size: 340, open: true },
        });
        const odd = readLayout({ left: { size: 'big', open: 'yes' }, right: { size: 9999, open: false } }, limits);
        expect(odd).toEqual({ left: { size: 280, open: true }, right: { size: 720, open: false } });
        const back = readLayout(JSON.parse(writeLayout({ left: { size: 333.4, open: false } })), limits);
        expect(back.left).toEqual({ size: 333, open: false });
    });
});

describe('Place panel groups', () => {
    // No engine class known: the class name (then the stored kind) decides, as for a world dump.
    const cls = (name: string, kind = 'other') => placeCategory({ name, kind: kind as never, rflClass: null });

    it('lists every group once, with a title and an icon', () => {
        const ids = PLACE_CATS.map((c) => c.id);
        expect(new Set(ids).size).toBe(ids.length);
        for (const c of PLACE_CATS) expect(c.title && c.icon && c.hint).toBeTruthy();
    });

    it('sorts classes into plain groups', () => {
        expect(cls('Rynn')).toBe('characters');
        expect(cls('Male Villager', 'creature')).toBe('characters');
        expect(cls('Orc Warrior', 'creature')).toBe('creatures');
        expect(cls('Dragon')).toBe('creatures');
        expect(cls('Battle Bow:  Long')).toBe('items');
        expect(cls('Barrel')).toBe('breakables');
        expect(cls('Catacomb Door PS2')).toBe('doors');
        expect(cls('Wall Torch')).toBe('lights');
        expect(cls('Trigger Detector')).toBe('logic');
        expect(cls('Troll Way Point', 'creature')).toBe('logic');
        expect(cls('Dragon Barrier')).toBe('logic');
        expect(cls('Camera')).toBe('cutscenes');
        expect(cls('Movie Player')).toBe('cutscenes');
        expect(cls('Conversation Handler')).toBe('conversations');
        expect(cls('Weapon Shop')).toBe('vendors');
        expect(cls('Quest Marker')).toBe('quests');
        expect(cls('Level Transition')).toBe('exits');
        expect(cls('Looping Sound')).toBe('sound');
        expect(cls('Fog Switcher')).toBe('sky');
        expect(cls('Birch Tree 01', 'static')).toBe('scenery');
        expect(cls('Misc Thing')).toBe('other');
    });

    it('keeps spell parts and generators with the effects', () => {
        expect(cls('!! Ice Burst !!')).toBe('effects');
        expect(cls('Smoke Puff')).toBe('effects');
        expect(cls('Flesh Mage Roto Missile')).toBe('effects');
    });

    it('files every ready-made set-up under a group that exists', () => {
        const ids = new Set(PLACE_CATS.map((c) => c.id));
        for (const t of TEMPLATES) expect(ids.has(templateCategory(t.id))).toBe(true);
        expect(templateCategory('cutscene-trigger')).toBe('cutscenes');
        expect(templateCategory('lever-door')).toBe('logic');
    });

    it('puts plain names before internal ones', () => {
        const names = ['!! Spike !!', 'boulder', 'Arrow', '4:Snow Orc', 'Barrel 10', 'Barrel 9'];
        expect([...names].sort(compareNames)).toEqual([
            'Arrow',
            'Barrel 9',
            'Barrel 10',
            'boulder',
            '4:Snow Orc',
            '!! Spike !!',
        ]);
    });

    it('searches by every word', () => {
        expect(matchesQuery('Wall Torch Lights', 'torch wall')).toBe(true);
        expect(matchesQuery('Wall Torch', 'torch door')).toBe(false);
        expect(matchesQuery('anything', '  ')).toBe(true);
    });

    it('keeps the recently placed list short, newest first, without repeats', () => {
        let r: string[] = [];
        for (const k of ['a', 'b', 'c', 'a']) r = pushRecent(r, k, 3);
        expect(r).toEqual(['a', 'c', 'b']);
        expect(readRecent(['x', 3, null, 'y'])).toEqual(['x', 'y']);
        expect(readRecent('nope')).toEqual([]);
    });
});
