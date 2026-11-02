// Synthetic only: titles and exit tables are made up.
import { describe, expect, it } from 'vitest';
import {
    MAP_CARD_W,
    MAP_GAP_X,
    MAP_HEAD_H,
    MAP_ROW_H,
    MAP_STUB_W,
    addDiscLevel,
    addLevelCopy,
    autoLayout,
    buildCampaignGraph,
    mapBounds,
    mapCardHeight,
    mapExitPinY,
    mapPositions,
    newProject,
    parseProject,
    removeLevel,
    serializeProject,
    validateProject,
    type ExitIndex,
    type ModProject,
} from '../src/index.ts';

const exits: ExitIndex = {
    levels: new Map([
        [
            'Level A - One',
            [
                { id: 5, name: 'To Two', dest: 'Level B - Two', portal: false },
                { id: 6, name: 'Side door', dest: 'Level D - Four', portal: false },
            ],
        ],
        ['Level B - Two', [{ id: 9, name: 'Back', dest: 'Level A - One', portal: false }]],
        [
            'Level C - Three',
            [
                { id: 2, name: 'Nowhere', dest: 'Level Z - Gone', portal: false },
                { id: 3, name: 'Portal', dest: 'portal', portal: false },
                { id: 4, name: 'Warp', dest: 'Level C - Three', portal: true },
            ],
        ],
        ['Level D - Four', [{ id: 7, name: 'Out', dest: 'Level A - One', portal: false }]],
    ]),
};
const disc = ['Level A - One', 'Level B - Two', 'Level C - Three', 'Level D - Four'];

function project(titles: string[], start: string | null = null): ModProject {
    let p = newProject('Map test', 'campaign', 1);
    for (const t of titles) p = addDiscLevel(p, t);
    return { ...p, start };
}

describe('campaign map graph', () => {
    it('has a card per level and an arrow per exit, without portals', () => {
        const p = project(['Level A - One', 'Level B - Two', 'Level C - Three']);
        const g = buildCampaignGraph(p, exits, validateProject(p, { discLevels: disc, exits }));
        expect(g.exitsKnown).toBe(true);
        expect(g.nodes.map((n) => n.title)).toEqual(['Level A - One', 'Level B - Two', 'Level C - Three']);
        const a = g.nodes[0]!;
        expect(a.start).toBe(true);
        expect(a.exits.map((e) => [e.id, e.to, e.row])).toEqual([
            [5, 'Level B - Two', 0],
            [6, null, 1], // Level D is on the disc but not in the project: a stub
        ]);
        const c = g.nodes[2]!;
        // the "portal" destination and the portal flag are both left out
        expect(c.exits.map((e) => e.id)).toEqual([2]);
        expect(c.exits[0]!.problem).toBeTruthy();
        expect(c.problems).toHaveLength(1);
        expect(c.unreachable).toBe(true);
        expect(g.nodes[1]!.unreachable).toBe(false);
    });
    it('matches destinations by title, so an exit into a copy names the copy', () => {
        let p = project(['Level A - One']);
        p = addLevelCopy(p, 'Level B - Two', 'Level 9X - Copy');
        const g = buildCampaignGraph(p, exits);
        // A leads to Level B on the disc, not to the copy of it
        expect(g.nodes[0]!.exits[0]!.to).toBeNull();
        // the copy keeps the exits of its source
        expect(g.nodes[1]!.exits.map((e) => e.to)).toEqual(['Level A - One']);
    });
    it('has cards without rows while the exits are unknown', () => {
        const g = buildCampaignGraph(project(['Level A - One', 'Level B - Two'], 'Level B - Two'), null);
        expect(g.exitsKnown).toBe(false);
        expect(g.nodes.every((n) => n.exits.length === 0 && !n.unreachable)).toBe(true);
        expect(g.nodes[1]!.start).toBe(true);
    });
    it('sizes cards by their exit rows', () => {
        const g = buildCampaignGraph(project(['Level A - One', 'Level B - Two']), exits);
        expect(mapCardHeight(g.nodes[0]!)).toBeGreaterThan(mapCardHeight(g.nodes[1]!));
        expect(mapExitPinY(1) - mapExitPinY(0)).toBe(MAP_ROW_H);
        expect(mapExitPinY(0)).toBeGreaterThan(MAP_HEAD_H);
    });
});

describe('campaign map layout', () => {
    it('puts levels in columns by route from the start level', () => {
        const p = project(['Level C - Three', 'Level D - Four', 'Level B - Two', 'Level A - One'], 'Level A - One');
        const pos = autoLayout(buildCampaignGraph(p, exits), p.start);
        const col = (t: string): number => pos[t]!.x / (MAP_CARD_W + MAP_GAP_X);
        expect(col('Level A - One')).toBe(0);
        expect(col('Level B - Two')).toBe(1);
        expect(col('Level D - Four')).toBe(1);
        // Level C is reached by no exit: it goes in a column of its own after the route
        expect(col('Level C - Three')).toBe(2);
        // B is listed before D among A's exits, so it sits above D
        expect(pos['Level B - Two']!.y).toBeLessThan(pos['Level D - Four']!.y);
    });
    it('never overlaps cards in a column', () => {
        const titles = Array.from({ length: 9 }, (_, i) => `Level ${i} - Loose`);
        const g = buildCampaignGraph(project(titles), exits);
        const pos = autoLayout(g);
        const cards = g.nodes.map((n) => ({ ...pos[n.title]!, h: mapCardHeight(n) }));
        for (const a of cards)
            for (const b of cards) {
                if (a === b || a.x !== b.x) continue;
                expect(a.y + a.h <= b.y || b.y + b.h <= a.y).toBe(true);
            }
        // the start level alone, then the eight others four to a column
        expect(new Set(cards.map((c) => c.x)).size).toBe(3);
    });
    it('keeps saved places and puts new levels under them', () => {
        const p = project(['Level A - One', 'Level B - Two', 'Level C - Three']);
        const g = buildCampaignGraph(p, exits);
        expect(mapPositions(g, undefined)).toEqual(autoLayout(g));
        const pos = mapPositions(g, { 'Level A - One': { x: 100, y: 50 }, 'Level B - Two': { x: 600, y: 0 } });
        expect(pos['Level A - One']).toEqual({ x: 100, y: 50 });
        expect(pos['Level C - Three']!.x).toBe(100);
        expect(pos['Level C - Three']!.y).toBeGreaterThan(50 + mapCardHeight(g.nodes[0]!));
    });
    it('measures the box around the cards and their stubs', () => {
        const g = buildCampaignGraph(project(['Level A - One']), exits);
        const b = mapBounds(g, { 'Level A - One': { x: 10, y: 20 } });
        expect(b).toEqual({ x: 10, y: 20, w: MAP_CARD_W + MAP_STUB_W, h: mapCardHeight(g.nodes[0]!) });
        expect(mapBounds(g, {})).toEqual({ x: 0, y: 0, w: 0, h: 0 });
    });
});

describe('campaign map in the project file', () => {
    it('round-trips card places and drops bad entries', () => {
        const p = { ...project(['Level A - One', 'Level B - Two']), mapLayout: { 'Level A - One': { x: 1, y: -2 } } };
        expect(parseProject(serializeProject(p))).toEqual(p);
        const raw = JSON.parse(serializeProject(p)) as Record<string, unknown>;
        raw['mapLayout'] = { a: { x: 1, y: 2 }, b: { x: 'no', y: 2 }, c: null, d: [1, 2] };
        expect(parseProject(JSON.stringify(raw)).mapLayout).toEqual({ a: { x: 1, y: 2 } });
        raw['mapLayout'] = 'nope';
        expect(parseProject(JSON.stringify(raw))).not.toHaveProperty('mapLayout');
    });
    it('leaves older projects without a map layout unchanged', () => {
        const p = project(['Level A - One']);
        expect(serializeProject(p)).not.toMatch(/mapLayout/);
        expect(parseProject(serializeProject(p))).toEqual(p);
    });
    it('forgets the place of a removed level', () => {
        const p = {
            ...project(['Level A - One', 'Level B - Two']),
            mapLayout: { 'Level A - One': { x: 0, y: 0 }, 'Level B - Two': { x: 5, y: 5 } },
        };
        expect(removeLevel(p, 'Level B - Two').mapLayout).toEqual({ 'Level A - One': { x: 0, y: 0 } });
        expect(removeLevel(removeLevel(p, 'Level B - Two'), 'Level A - One')).not.toHaveProperty('mapLayout');
    });
});
