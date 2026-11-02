import type { ExitIndex } from './logic.ts';
import { levelExits, type MapPoint, type ModProject, type ProjectIssue, type ProjectLevel } from './project.ts';

/**
 * The Campaign map of a project: one card per project level, one arrow per exit. This module is the pure part (the
 * graph from the levels and their exits, card sizes, the automatic layout); the app draws it (project/campaignmap.ts).
 * Portals (exits that stay inside the level) are left out; exits to a level outside the project end in a stub.
 */

/** Card geometry in map units, shared by the layout and the view so arrows meet the drawn pins. */
export const MAP_CARD_W = 248;
export const MAP_HEAD_H = 58;
export const MAP_ROW_H = 24;
export const MAP_FOOT_H = 6;
/** Height of the incoming pin on a card's left edge. */
export const MAP_IN_PIN_Y = 18;
/** Wide enough for a stub (arrow and label) between two columns. */
export const MAP_GAP_X = 260;
export const MAP_GAP_Y = 40;
/** Room a stub takes to the right of a card (its arrow and its label). */
export const MAP_STUB_W = 216;
/** Cards per column for levels that no exit reaches (auto layout). */
const LOOSE_PER_COLUMN = 4;

export interface MapExit {
    id: number;
    name: string;
    /** Destination as the exit names it (null when it could not be read). */
    dest: string | null;
    /** Title of the project level it leads to, null when the destination is not in the project. */
    to: string | null;
    problem: string | null;
    /** Row of the exit on its card (0 = first exit row). */
    row: number;
}

export interface MapNode {
    title: string;
    level: ProjectLevel;
    /** Position in the project's level list. */
    index: number;
    start: boolean;
    exits: MapExit[];
    /** Errors and warnings of the project checks about this level (exit problems included). */
    problems: string[];
    /** No exit of the project leads here (only checked when the exits are known). */
    unreachable: boolean;
}

export interface CampaignGraph {
    nodes: MapNode[];
    /** The exits were read; without them the cards have no rows and no arrows. */
    exitsKnown: boolean;
}

const norm = (s: string): string => s.toLowerCase().replace(/[^a-z0-9]/g, '');

/** The start card: the project's start level, else the first level. */
export const mapStart = (p: ModProject): string | null =>
    (p.levels.find((l) => l.title === p.start) ?? p.levels[0])?.title ?? null;

export function buildCampaignGraph(
    p: ModProject,
    index: ExitIndex | null,
    issues: readonly ProjectIssue[] = [],
): CampaignGraph {
    const start = mapStart(p);
    const nodes = p.levels.map((l, i): MapNode => {
        const exits = index
            ? levelExits(p, l, index)
                  .filter((e) => !e.portal)
                  .map((e, row): MapExit => {
                      const to = e.dest ? (p.levels.find((t) => norm(t.title) === norm(e.dest!))?.title ?? null) : null;
                      return { id: e.id, name: e.name, dest: e.dest, to, problem: e.problem, row };
                  })
            : [];
        const mine = issues.filter((x) => x.level === l.title);
        return {
            title: l.title,
            level: l,
            index: i,
            start: l.title === start,
            exits,
            problems: mine.filter((x) => x.severity !== 'info').map((x) => x.message),
            unreachable: !!index && mine.some((x) => x.code === 'unreachable'),
        };
    });
    return { nodes, exitsKnown: !!index };
}

export const mapCardHeight = (n: MapNode): number =>
    MAP_HEAD_H + n.exits.length * MAP_ROW_H + (n.exits.length ? MAP_FOOT_H : 0);

/** Height of an exit's outgoing pin, from the card's top. */
export const mapExitPinY = (row: number): number => MAP_HEAD_H + row * MAP_ROW_H + MAP_ROW_H / 2;

/**
 * Lay the cards out by route: the start level in the first column, the levels its exits lead to in the next, and so
 * on (breadth first, in the order the exits are listed). Levels no exit reaches come last, a few per column. Columns
 * are centred on each other.
 */
export function autoLayout(g: CampaignGraph, start: string | null = null): Record<string, MapPoint> {
    const out: Record<string, MapPoint> = {};
    if (!g.nodes.length) return out;
    const byTitle = new Map(g.nodes.map((n) => [n.title, n]));
    const first = byTitle.get(start ?? '') ?? g.nodes.find((n) => n.start) ?? g.nodes[0]!;
    const depth = new Map<string, number>([[first.title, 0]]);
    const queue = [first];
    for (let i = 0; i < queue.length; i++) {
        const n = queue[i]!;
        for (const e of n.exits) {
            if (!e.to || depth.has(e.to)) continue;
            depth.set(e.to, depth.get(n.title)! + 1);
            queue.push(byTitle.get(e.to)!);
        }
    }
    const columns: MapNode[][] = [];
    for (const n of queue) (columns[depth.get(n.title)!] ??= []).push(n);
    const loose = g.nodes.filter((n) => !depth.has(n.title));
    for (let i = 0; i < loose.length; i += LOOSE_PER_COLUMN) columns.push(loose.slice(i, i + LOOSE_PER_COLUMN));

    const colHeight = (c: MapNode[]): number =>
        c.reduce((s, n) => s + mapCardHeight(n), 0) + Math.max(0, c.length - 1) * MAP_GAP_Y;
    const tallest = Math.max(...columns.map(colHeight));
    columns.forEach((c, ci) => {
        let y = Math.round((tallest - colHeight(c)) / 2);
        for (const n of c) {
            out[n.title] = { x: ci * (MAP_CARD_W + MAP_GAP_X), y };
            y += mapCardHeight(n) + MAP_GAP_Y;
        }
    });
    return out;
}

/**
 * Where each card is drawn: its saved place, or the auto layout when nothing is saved. Cards without a saved place
 * (levels added since the map was arranged) go in a row under the arranged ones.
 */
export function mapPositions(
    g: CampaignGraph,
    saved: Readonly<Record<string, MapPoint>> | undefined,
    start: string | null = null,
): Record<string, MapPoint> {
    const placed = g.nodes.filter((n) => saved?.[n.title]);
    if (!placed.length) return autoLayout(g, start);
    const out: Record<string, MapPoint> = {};
    let minX = Infinity;
    let bottom = -Infinity;
    for (const n of placed) {
        const s = saved![n.title]!;
        out[n.title] = { x: s.x, y: s.y };
        minX = Math.min(minX, s.x);
        bottom = Math.max(bottom, s.y + mapCardHeight(n));
    }
    let k = 0;
    for (const n of g.nodes) {
        if (out[n.title]) continue;
        out[n.title] = { x: minX + k++ * (MAP_CARD_W + MAP_GAP_X), y: bottom + MAP_GAP_Y * 2 };
    }
    return out;
}

export interface MapBounds {
    x: number;
    y: number;
    w: number;
    h: number;
}

/** The box around every card (and the stubs to their right); zero size when there are no cards. */
export function mapBounds(g: CampaignGraph, pos: Readonly<Record<string, MapPoint>>): MapBounds {
    let x0 = Infinity;
    let y0 = Infinity;
    let x1 = -Infinity;
    let y1 = -Infinity;
    for (const n of g.nodes) {
        const p = pos[n.title];
        if (!p) continue;
        const stubs = n.exits.some((e) => !e.to);
        x0 = Math.min(x0, p.x);
        y0 = Math.min(y0, p.y);
        x1 = Math.max(x1, p.x + MAP_CARD_W + (stubs ? MAP_STUB_W : 0));
        y1 = Math.max(y1, p.y + mapCardHeight(n));
    }
    if (x0 === Infinity) return { x: 0, y: 0, w: 0, h: 0 };
    return { x: x0, y: y0, w: x1 - x0, h: y1 - y0 };
}
