import { describe, expect, it } from 'vitest';
import {
    OBJECT_CATEGORIES,
    categoryIndex,
    categoryInfo,
    categoryOf,
    classCategory,
    countByCategory,
    hasRflCategory,
    type ObjectCategory,
} from '../src/index.ts';

describe('object categories', () => {
    it('lists every category once, with a label, hint and icon', () => {
        const ids = OBJECT_CATEGORIES.map((c) => c.id);
        expect(new Set(ids).size).toBe(ids.length);
        expect(ids.at(-1)).toBe('other');
        for (const c of OBJECT_CATEGORIES) {
            expect(c.label.length).toBeGreaterThan(2);
            expect(c.hint.length).toBeGreaterThan(5);
            expect(c.icon).toBeTruthy();
        }
        expect(categoryInfo('trigger').label).toBe('Triggers & logic');
        expect(categoryInfo('nope' as ObjectCategory).id).toBe('other');
    });

    it('uses the engine class before the name', () => {
        // A tree named without "tree" (the old name heuristic made it Other) is scenery by its engine class.
        expect(categoryOf({ name: 'Snowy Pine', rfl: 0xd3, kind: 'other' })).toBe('scenery');
        // A creature whose name the old heuristic missed.
        expect(categoryOf({ name: 'Green Trog : Center', rfl: 0x31, kind: 'other' })).toBe('character');
        // The cut scene player is called "Stomp Player": not the player.
        expect(categoryOf({ name: 'Stomp Player', rfl: 0x33, kind: 'player' })).toBe('cutscene');
        // A dragon barrier is not a creature.
        expect(categoryOf({ name: 'Dragon Barrier', rfl: 0x76, kind: 'creature' })).toBe('barrier');
        expect(categoryOf({ name: 'Sound Trigger', rfl: 0x3d })).toBe('trigger');
        expect(categoryOf({ name: 'AmbientTransition', rfl: 0x83 })).toBe('sound');
        expect(categoryOf({ name: 'Savage Waypoint', rfl: 0x70, kind: 'creature' })).toBe('waypoint');
        expect(categoryOf({ name: 'Level Transition from 1 to 2', rfl: 0x37 })).toBe('exit');
        expect(categoryOf({ name: 'Wartok Torch', rfl: 0x14, kind: 'usable' })).toBe('light');
        expect(categoryOf({ name: 'Barrel', rfl: 0x11 })).toBe('breakable');
        expect(categoryOf({ name: 'Town Door', rfl: 0x13 })).toBe('mechanism');
        expect(categoryOf({ name: 'Conversation Handler', rfl: 0xb4 })).toBe('quest');
        expect(categoryOf({ name: 'Procedural Sky', rfl: 0x1a })).toBe('sky');
        expect(categoryOf({ name: 'Level Properties', rfl: 0x36 })).toBe('settings');
        expect(categoryOf({ name: 'WaterFall Mist', rfl: 0x8c })).toBe('effect');
        expect(categoryOf({ name: 'Camera', rfl: 0x1b })).toBe('camera');
        expect(categoryOf({ name: 'Rynn', rfl: 0x09 })).toBe('player');
    });

    it('treats invisible scenery as a barrier', () => {
        expect(categoryOf({ name: 'Invisible Static Wall', rfl: 0xd3 })).toBe('barrier');
        expect(categoryOf({ name: 'Invisible jump plug', rfl: 0x11 })).toBe('barrier');
        expect(categoryOf({ name: 'Boulder Wide (Light)', rfl: 0xd3 })).toBe('scenery');
    });

    it('falls back on the name, then the stored kind, when the engine class is unknown', () => {
        expect(categoryOf({ name: 'Looping Sound', rfl: null })).toBe('sound');
        expect(categoryOf({ name: 'Trigger Detector' })).toBe('trigger');
        expect(categoryOf({ name: 'Static (Vertex) Light' })).toBe('light');
        expect(categoryOf({ name: 'Boulder Wide (Light)', kind: 'static' })).toBe('scenery');
        expect(categoryOf({ name: 'Mystery', rfl: 0x7777, kind: 'creature' })).toBe('character');
        expect(categoryOf({ name: 'Mystery', kind: 'usable' })).toBe('other');
        expect(categoryOf({ name: 'Mystery' })).toBe('other');
        expect(hasRflCategory(0x3d)).toBe(true);
        expect(hasRflCategory(0x7777)).toBe(false);
        expect(hasRflCategory(null)).toBe(false);
    });

    it('debug helpers stay Other', () => {
        expect(categoryOf({ name: 'Waypoint Generator', rfl: 0x19 })).toBe('other');
    });

    it('classifies catalog classes and indexes objects by resource', () => {
        expect(classCategory({ name: 'Snowy Pine', rflClass: 0xd3, kind: 'other' })).toBe('scenery');
        const idx = categoryIndex([
            { resource: '0x10001', rflClass: 0x3e },
            { resource: '0x10002', rflClass: null },
        ]);
        expect(idx({ class: 'timer', resource: '0x10001', kind: 'usable' })).toBe('trigger');
        expect(idx({ class: 'Looping Sound', resource: '0x10002', kind: 'other' })).toBe('sound');
        // Not in the catalog (a world dump object): name and kind.
        expect(idx({ class: 'Thing', resource: '0x99', kind: 'weapon' })).toBe('item');
    });

    it('counts per category in display order', () => {
        const rows = countByCategory(['trigger', 'player', 'trigger', 'other'] as ObjectCategory[], (x) => x);
        expect(rows.map((r) => [r.info.id, r.count])).toEqual([
            ['player', 1],
            ['trigger', 2],
            ['other', 1],
        ]);
    });
});
