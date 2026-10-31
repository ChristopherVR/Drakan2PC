import type { LevelDocument, LevelObject, Transform } from './document.ts';

/** Snapping settings. Distances are game units (2048 = one layer cell), angles degrees. */
export interface SnapSettings {
    grid: boolean;
    gridSize: number;
    angle: boolean;
    angleStep: number;
}

export const DEFAULT_SNAP: SnapSettings = { grid: true, gridSize: 256, angle: true, angleStep: 15 };
export const GRID_SIZES = [32, 64, 128, 256, 512, 1024, 2048];
export const ANGLE_STEPS = [1, 5, 15, 30, 45, 90];

/** Round `v` to the nearest multiple of `step` (unchanged when `step` is not positive). */
export function snapValue(v: number, step: number): number {
    if (!(step > 0) || !Number.isFinite(v)) return v;
    const r = Math.round(v / step) * step;
    return r === 0 ? 0 : r; // no negative zero
}

/** Snap x and z to the grid; y (height) is left alone unless `snapY`. */
export function snapPosition(
    p: readonly [number, number, number],
    s: SnapSettings,
    snapY = false,
): [number, number, number] {
    if (!s.grid) return [p[0], p[1], p[2]];
    return [snapValue(p[0], s.gridSize), snapY ? snapValue(p[1], s.gridSize) : p[1], snapValue(p[2], s.gridSize)];
}

/** Snap an angle in radians to `s.angleStep` degrees. */
export function snapAngle(rad: number, s: SnapSettings): number {
    if (!s.angle) return rad;
    const step = (s.angleStep * Math.PI) / 180;
    return snapValue(rad, step);
}

/** Height function: ground height under (x, z) in game units, or null when there is no ground there. */
export type GroundFn = (x: number, z: number) => number | null;

/** New transforms with every object's y set to the ground height (objects over no ground are left out). */
export function snapToGround(
    doc: LevelDocument,
    ids: readonly number[],
    ground: GroundFn,
    offset = 0,
): Map<number, Transform> {
    const out = new Map<number, Transform>();
    for (const id of ids) {
        const o = doc.get(id);
        if (!o) continue;
        const h = ground(o.transform.pos[0], o.transform.pos[2]);
        if (h === null || !Number.isFinite(h)) continue;
        const y = h + offset;
        if (y === o.transform.pos[1]) continue;
        out.set(id, { pos: [o.transform.pos[0], y, o.transform.pos[2]], rot: [...o.transform.rot] });
    }
    return out;
}

/**
 * Fallback ground for placement: the height of the object nearest to `near` in the xz plane, else the median object
 * height (0 for an empty level).
 */
export function nearestObjectPlaneY(objs: Iterable<LevelObject>, near?: readonly [number, number, number]): number {
    let best: number | null = null;
    let bd = Infinity;
    const ys: number[] = [];
    for (const o of objs) {
        ys.push(o.transform.pos[1]);
        if (near) {
            const d = Math.hypot(o.transform.pos[0] - near[0], o.transform.pos[2] - near[2]);
            if (d < bd) {
                bd = d;
                best = o.transform.pos[1];
            }
        }
    }
    if (best !== null) return best;
    if (!ys.length) return 0;
    ys.sort((a, b) => a - b);
    return ys[ys.length >> 1]!;
}
