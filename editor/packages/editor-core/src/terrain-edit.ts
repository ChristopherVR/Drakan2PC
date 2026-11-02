import {
    CELL_FLAG_BACKSLASH,
    CELL_SIZE,
    TEX_HOLE,
    TEX_INVISIBLE,
    layerHeightAt,
    type TerrainLayer,
} from '@drakan/formats';
import type { Command } from './commands.ts';
import type { LevelDocument, TerrainTouch } from './document.ts';
import { cloneTerrainLayer, heightOfRaw, rawOfHeight } from './terrain-json.ts';

/**
 * Landscape editing on terrain layers (docs/reference/level-format.md section 10): sculpt brushes on vertex heights,
 * texture painting on cells, expanding a layer and adding a new one. Pure (no DOM, no three.js).
 *
 * Units: world (game) units, a cell is `CELL_SIZE` (2048). Heights are kept on the LVL's raw u16 grid
 * (`worldHeight + (raw - 0x8000) * 2`, clamped to 0..0xffff), so what the editor shows is what the file can hold.
 *
 * A brush stroke (`TerrainStroke`) changes the document live, dab by dab, and `finish()` returns one undoable command
 * for the whole stroke. Structural edits (`expandLayerCommand`, `addLayerCommand`) are commands right away.
 */

export type SculptTool = 'raise' | 'smooth' | 'flatten' | 'noise';
export type LayerEdge = 'west' | 'east' | 'north' | 'south';

export interface BrushSettings {
    /** World units. */
    radius: number;
    /** 0..1. */
    strength: number;
    /** 0..1: the outer part of the radius where the effect fades out (0 = hard edge). */
    falloff: number;
}

/**
 * Height change per second at strength 1 (Raise / Lower), as a fraction of the brush radius: a big brush builds a big
 * hill as fast as a small brush builds a small one (how Unreal's sculpt tool feels).
 */
export const RAISE_RATE = 0.5;
/** Noise amplitude per second at strength 1, as a fraction of the brush radius. */
export const NOISE_RATE = 0.3;

/** World units per second for a rate and brush radius (radius at least one cell). */
const perSecond = (rate: number, radius: number): number => rate * Math.max(CELL_SIZE, radius);
/** Blend speed of Smooth / Flatten at strength 1 (per second, exponential). */
export const BLEND_RATE = 10;

const clamp01 = (v: number): number => (v < 0 ? 0 : v > 1 ? 1 : v);

/** Brush weight 1 inside `radius * (1 - falloff)`, easing (smoothstep) to 0 at `radius`. */
export function brushWeight(dist: number, b: Pick<BrushSettings, 'radius' | 'falloff'>): number {
    if (!(dist < b.radius)) return 0;
    const inner = b.radius * (1 - clamp01(b.falloff));
    if (dist <= inner) return 1;
    const t = (dist - inner) / (b.radius - inner);
    return 1 - t * t * (3 - 2 * t);
}

/** Nearest height the layer can store (raw u16 grid, clamped). */
export function quantizeHeight(L: Pick<TerrainLayer, 'worldHeight'>, h: number): number {
    return heightOfRaw(L.worldHeight, rawOfHeight(L.worldHeight, h));
}

/** Lowest and highest height a layer can store. */
export function layerHeightRange(L: Pick<TerrainLayer, 'worldHeight'>): [number, number] {
    return [heightOfRaw(L.worldHeight, 0), heightOfRaw(L.worldHeight, 0xffff)];
}

/** Deterministic 0..1 hash of integer coordinates and a seed. */
function hash2(x: number, z: number, seed: number): number {
    let h = Math.imul(x | 0, 374761393) ^ Math.imul(z | 0, 668265263) ^ Math.imul(seed | 0, 1274126177);
    h = Math.imul(h ^ (h >>> 13), 1103515245);
    h ^= h >>> 16;
    return (h >>> 0) / 4294967296;
}

/** Smooth value noise in -1..1 over world (x, z) with `period` world units between lattice points. */
export function valueNoise(x: number, z: number, period: number, seed: number): number {
    const fx = x / period;
    const fz = z / period;
    const ix = Math.floor(fx);
    const iz = Math.floor(fz);
    const tx = fx - ix;
    const tz = fz - iz;
    const sx = tx * tx * (3 - 2 * tx);
    const sz = tz * tz * (3 - 2 * tz);
    const a = hash2(ix, iz, seed);
    const b = hash2(ix + 1, iz, seed);
    const c = hash2(ix, iz + 1, seed);
    const d = hash2(ix + 1, iz + 1, seed);
    const top = a + (b - a) * sx;
    const bot = c + (d - c) * sx;
    return (top + (bot - top) * sz) * 2 - 1;
}

// ---------------------------------------------------------------- layer queries

const isSurface = (ref: number): boolean => ref !== TEX_HOLE && ref !== TEX_INVISIBLE;

/**
 * The layer whose surface at (x, z) is closest to `y` (a ray hit on the terrain), or -1 when no layer covers the point
 * or the nearest surface is further than `tolerance`.
 */
export function layerAt(layers: readonly TerrainLayer[], x: number, z: number, y: number, tolerance = 400): number {
    let best = -1;
    let bestD = tolerance;
    for (let i = 0; i < layers.length; i++) {
        const h = layerHeightAt(layers[i]!, x, z);
        if (Number.isNaN(h)) continue;
        const d = Math.abs(h - y);
        if (d <= bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

/**
 * Layers joined to `start` along shared grid points at the same height (within `tolerance`), `start` first. Layers
 * stitched together like this form one ground surface: a brush edits all of them so the seams stay closed.
 */
export function connectedLayers(layers: readonly TerrainLayer[], start: number, tolerance = 4): number[] {
    if (!layers[start]) return [];
    const seen = new Set<number>([start]);
    const out = [start];
    for (let q = 0; q < out.length; q++) {
        const A = layers[out[q]!]!;
        for (let j = 0; j < layers.length; j++) {
            if (seen.has(j)) continue;
            if (sharesSeam(A, layers[j]!, tolerance)) {
                seen.add(j);
                out.push(j);
            }
        }
    }
    return out;
}

function sharesSeam(A: TerrainLayer, B: TerrainLayer, tol: number): boolean {
    const x0 = Math.max(A.originX, B.originX);
    const x1 = Math.min(A.originX + A.width, B.originX + B.width);
    const z0 = Math.max(A.originZ, B.originZ);
    const z1 = Math.min(A.originZ + A.height, B.originZ + B.height);
    if (x0 > x1 || z0 > z1) return false;
    // Sample at most ~4096 shared points.
    const step = Math.max(1, Math.ceil(Math.sqrt(((x1 - x0 + 1) * (z1 - z0 + 1)) / 4096)));
    const sa = A.width + 1;
    const sb = B.width + 1;
    for (let gz = z0; gz <= z1; gz += step) {
        for (let gx = x0; gx <= x1; gx += step) {
            const ha = A.heights[(gz - A.originZ) * sa + (gx - A.originX)]!;
            const hb = B.heights[(gz - B.originZ) * sb + (gx - B.originX)]!;
            if (Math.abs(ha - hb) <= tol) return true;
        }
    }
    return false;
}

/** Texture refs used by the level's cells (holes and invisible cells left out), most used first: the paint palette. */
export function terrainPalette(layers: readonly TerrainLayer[]): { ref: number; cells: number }[] {
    const n = new Map<number, number>();
    for (const L of layers) {
        for (let c = 0; c < L.cellTexLeft.length; c++) {
            const r = L.cellTexLeft[c]!;
            if (isSurface(r)) n.set(r, (n.get(r) ?? 0) + 1);
            const rr = L.cellTexRight[c]!;
            if (rr !== r && isSurface(rr)) n.set(rr, (n.get(rr) ?? 0) + 1);
        }
    }
    return [...n].map(([ref, cells]) => ({ ref, cells })).sort((a, b) => b.cells - a.cells || a.ref - b.ref);
}

// ---------------------------------------------------------------- commands

interface HeightEdit {
    layer: number;
    idx: Uint32Array;
    before: Float32Array;
    after: Float32Array;
}

interface CellEdit {
    layer: number;
    idx: Uint32Array;
    beforeL: Uint32Array;
    beforeR: Uint32Array;
    afterL: Uint32Array;
    afterR: Uint32Array;
}

type Rect = { x0: number; z0: number; x1: number; z1: number };

function vertexRect(L: TerrainLayer, idx: ArrayLike<number>): Rect | undefined {
    if (!idx.length) return undefined;
    const s = L.width + 1;
    let x0 = Infinity;
    let z0 = Infinity;
    let x1 = -Infinity;
    let z1 = -Infinity;
    for (let i = 0; i < idx.length; i++) {
        const v = idx[i]!;
        const vx = v % s;
        const vz = (v - vx) / s;
        if (vx < x0) x0 = vx;
        if (vx > x1) x1 = vx;
        if (vz < z0) z0 = vz;
        if (vz > z1) z1 = vz;
    }
    // A vertex touches the cells on both sides of it.
    return {
        x0: Math.max(0, x0 - 1),
        z0: Math.max(0, z0 - 1),
        x1: Math.min(L.width, x1 + 1),
        z1: Math.min(L.height, z1 + 1),
    };
}

function cellRect(L: TerrainLayer, idx: ArrayLike<number>): Rect | undefined {
    if (!idx.length) return undefined;
    let x0 = Infinity;
    let z0 = Infinity;
    let x1 = -Infinity;
    let z1 = -Infinity;
    for (let i = 0; i < idx.length; i++) {
        const c = idx[i]!;
        const cx = c % L.width;
        const cz = (c - cx) / L.width;
        if (cx < x0) x0 = cx;
        if (cx > x1) x1 = cx;
        if (cz < z0) z0 = cz;
        if (cz > z1) z1 = cz;
    }
    return { x0, z0, x1: x1 + 1, z1: z1 + 1 };
}

const union = (a: Rect | undefined, b: Rect | undefined): Rect | undefined =>
    !a
        ? b
        : !b
          ? a
          : { x0: Math.min(a.x0, b.x0), z0: Math.min(a.z0, b.z0), x1: Math.max(a.x1, b.x1), z1: Math.max(a.z1, b.z1) };

/** One brush stroke (heights and/or cell textures on any number of layers) as one undo step. */
export class TerrainEditCommand implements Command {
    constructor(
        readonly label: string,
        private readonly heights: HeightEdit[],
        private readonly cells: CellEdit[],
    ) {}
    /** Layers the command changes. */
    get layers(): number[] {
        return [...new Set([...this.heights.map((e) => e.layer), ...this.cells.map((e) => e.layer)])];
    }
    private put(doc: LevelDocument, after: boolean): void {
        const touched = new Map<number, Rect | undefined>();
        for (const e of this.heights) {
            const L = doc.writableLayer(e.layer);
            const src = after ? e.after : e.before;
            for (let i = 0; i < e.idx.length; i++) L.heights[e.idx[i]!] = src[i]!;
            touched.set(e.layer, union(touched.get(e.layer), vertexRect(L, e.idx)));
        }
        for (const e of this.cells) {
            const L = doc.writableLayer(e.layer);
            const sl = after ? e.afterL : e.beforeL;
            const sr = after ? e.afterR : e.beforeR;
            for (let i = 0; i < e.idx.length; i++) {
                L.cellTexLeft[e.idx[i]!] = sl[i]!;
                L.cellTexRight[e.idx[i]!] = sr[i]!;
            }
            touched.set(e.layer, union(touched.get(e.layer), cellRect(L, e.idx)));
        }
        for (const [layer, rect] of touched) {
            doc.markTerrainEdit(layer, after ? 1 : -1);
            doc.touchTerrain(rect ? { layer, rect } : { layer });
        }
    }
    apply(doc: LevelDocument): void {
        this.put(doc, true);
    }
    revert(doc: LevelDocument): void {
        this.put(doc, false);
    }
}

/** Replace a whole layer (expand) or append / remove one (add layer). `before` null = the layer is new. */
export class TerrainLayerCommand implements Command {
    private readonly before: TerrainLayer | null;
    constructor(
        readonly label: string,
        readonly index: number,
        before: TerrainLayer | null,
        private readonly after: TerrainLayer | null,
    ) {
        // A copy: the layer in the document may still be the disc read, which must never become editable.
        this.before = before && cloneTerrainLayer(before);
    }
    apply(doc: LevelDocument): void {
        doc.setTerrainLayer(this.index, this.after);
        if (this.before && this.after) doc.markTerrainEdit(this.index, 1);
    }
    revert(doc: LevelDocument): void {
        doc.setTerrainLayer(this.index, this.before);
        if (this.before && this.after) doc.markTerrainEdit(this.index, -1);
    }
}

// ---------------------------------------------------------------- strokes

export interface SculptOptions {
    /** Lower instead of raise; for Smooth/Flatten/Noise: no effect. */
    invert?: boolean;
    /** Flatten target height (world units). */
    target?: number;
}

export interface PaintOptions {
    /** Also paint over holes and invisible cells (fill them in). Painting `TEX_HOLE` itself cuts holes. */
    fillHoles?: boolean;
}

/**
 * A brush stroke over a set of layers (`connectedLayers` of the one under the cursor, or a single layer). Each dab
 * changes the document at once and returns what it touched (for a live mesh update); `finish` turns the whole stroke
 * into one `TerrainEditCommand`.
 */
export class TerrainStroke {
    private readonly before = new Map<number, Map<number, number>>();
    /** Unquantised heights, so slow brushes add up instead of rounding away. */
    private readonly acc = new Map<number, Map<number, number>>();
    private readonly cellBefore = new Map<number, Map<number, [number, number]>>();
    readonly layers: readonly number[];

    constructor(
        private readonly doc: LevelDocument,
        layers: readonly number[],
        readonly label: string,
        private readonly seed = 1,
    ) {
        this.layers = [...new Set(layers)].filter((i) => !!doc.terrain?.[i]);
    }

    get empty(): boolean {
        return !this.before.size && !this.cellBefore.size;
    }

    private layersNear(x0: number, z0: number, x1: number, z1: number): number[] {
        const t = this.doc.terrain!;
        const out: number[] = [];
        for (const i of this.layers) {
            const L = t[i]!;
            const lx0 = L.originX * CELL_SIZE;
            const lz0 = L.originZ * CELL_SIZE;
            if (x1 < lx0 || z1 < lz0 || x0 > lx0 + L.width * CELL_SIZE || z0 > lz0 + L.height * CELL_SIZE) continue;
            out.push(i);
        }
        return out;
    }

    private cur(i: number, L: TerrainLayer, v: number): number {
        return this.acc.get(i)?.get(v) ?? L.heights[v]!;
    }

    private setHeight(i: number, v: number, h: number): void {
        let L = this.doc.terrain![i]!;
        let b = this.before.get(i);
        if (!b) {
            L = this.doc.writableLayer(i);
            this.before.set(i, (b = new Map()));
            this.acc.set(i, new Map());
        }
        const [lo, hi] = layerHeightRange(L);
        const c = h < lo ? lo : h > hi ? hi : h;
        if (!b.has(v)) b.set(v, L.heights[v]!);
        this.acc.get(i)!.set(v, c);
        L.heights[v] = quantizeHeight(L, c);
    }

    /** Vertices of layer `i` inside a circle: calls `fn(v, gx, gz, dist)` with the global grid coordinate. */
    private eachVertex(
        L: TerrainLayer,
        x: number,
        z: number,
        r: number,
        fn: (v: number, gx: number, gz: number, d: number) => void,
    ): void {
        const s = L.width + 1;
        const vx0 = Math.max(0, Math.floor((x - r) / CELL_SIZE) - L.originX);
        const vx1 = Math.min(L.width, Math.ceil((x + r) / CELL_SIZE) - L.originX);
        const vz0 = Math.max(0, Math.floor((z - r) / CELL_SIZE) - L.originZ);
        const vz1 = Math.min(L.height, Math.ceil((z + r) / CELL_SIZE) - L.originZ);
        for (let vz = vz0; vz <= vz1; vz++) {
            for (let vx = vx0; vx <= vx1; vx++) {
                const gx = L.originX + vx;
                const gz = L.originZ + vz;
                const d = Math.hypot(gx * CELL_SIZE - x, gz * CELL_SIZE - z);
                if (d < r) fn(vz * s + vx, gx, gz, d);
            }
        }
    }

    /** Height at a global grid point among `near` layers, the one closest to `ref` (null when none has the point). */
    private gridHeight(near: readonly number[], gx: number, gz: number, ref: number): number | null {
        const t = this.doc.terrain!;
        let best: number | null = null;
        for (const i of near) {
            const L = t[i]!;
            const vx = gx - L.originX;
            const vz = gz - L.originZ;
            if (vx < 0 || vz < 0 || vx > L.width || vz > L.height) continue;
            const h = this.cur(i, L, vz * (L.width + 1) + vx);
            if (best === null || Math.abs(h - ref) < Math.abs(best - ref)) best = h;
        }
        return best;
    }

    /** One sculpt dab at (x, z), `dt` seconds of brush time. */
    sculpt(
        tool: SculptTool,
        x: number,
        z: number,
        b: BrushSettings,
        dt: number,
        o: SculptOptions = {},
    ): TerrainTouch[] {
        const t = this.doc.terrain;
        if (!t) return [];
        const r = b.radius;
        const near = this.layersNear(x - r - CELL_SIZE, z - r - CELL_SIZE, x + r + CELL_SIZE, z + r + CELL_SIZE);
        const k = clamp01(b.strength);
        const blend = 1 - Math.exp(-BLEND_RATE * k * dt);
        const writes: [number, number, number][] = [];
        for (const i of near) {
            const L = t[i]!;
            const s = L.width + 1;
            this.eachVertex(L, x, z, r, (v, gx, gz, d) => {
                const w = brushWeight(d, b);
                if (w <= 0) return;
                const h = this.cur(i, L, v);
                let nh = h;
                if (tool === 'raise') nh = h + (o.invert ? -1 : 1) * perSecond(RAISE_RATE, r) * k * w * dt;
                else if (tool === 'noise') {
                    const period = Math.max(CELL_SIZE, r / 2);
                    nh =
                        h +
                        valueNoise(gx * CELL_SIZE, gz * CELL_SIZE, period, this.seed) *
                            perSecond(NOISE_RATE, r) *
                            k *
                            w *
                            dt;
                } else if (tool === 'flatten') {
                    if (o.target === undefined) return;
                    nh = h + (o.target - h) * blend * w;
                } else {
                    // Smooth: 3x3 average; neighbours past the layer edge come from the stitched layers.
                    const vx = v % s;
                    const vz = (v - vx) / s;
                    let sum = 0;
                    let n = 0;
                    for (let dz = -1; dz <= 1; dz++) {
                        for (let dx = -1; dx <= 1; dx++) {
                            const nx = vx + dx;
                            const nz = vz + dz;
                            let nhv: number | null;
                            if (nx >= 0 && nz >= 0 && nx <= L.width && nz <= L.height)
                                nhv = this.cur(i, L, nz * s + nx);
                            else nhv = this.gridHeight(near, gx + dx, gz + dz, h);
                            if (nhv === null) continue;
                            sum += nhv;
                            n++;
                        }
                    }
                    nh = h + (sum / n - h) * blend * w;
                }
                if (nh !== h) writes.push([i, v, nh]);
            });
        }
        // Smooth reads its neighbours before anything is written, so the result does not depend on the visit order.
        for (const [i, v, h] of writes) this.setHeight(i, v, h);
        return this.touches(writes);
    }

    /**
     * Ramp: blend the vertices within `width / 2` of the segment a-b towards the straight slope between the two
     * points (heights included), measured from where they were before the stroke.
     */
    ramp(
        a: [number, number, number],
        b: [number, number, number],
        width: number,
        falloff: number,
        strength = 1,
    ): TerrainTouch[] {
        const t = this.doc.terrain;
        if (!t) return [];
        const r = width / 2;
        const dx = b[0] - a[0];
        const dz = b[2] - a[2];
        const len2 = dx * dx + dz * dz;
        const near = this.layersNear(
            Math.min(a[0], b[0]) - r,
            Math.min(a[2], b[2]) - r,
            Math.max(a[0], b[0]) + r,
            Math.max(a[2], b[2]) + r,
        );
        const writes: [number, number, number][] = [];
        // Every call starts again from the heights before the stroke (the ramp is dragged out live).
        for (const [i, m] of this.before) for (const [v, h] of m) writes.push([i, v, h]);
        const cx = (a[0] + b[0]) / 2;
        const cz = (a[2] + b[2]) / 2;
        const reach = Math.sqrt(len2) / 2 + r;
        for (const i of near) {
            const L = t[i]!;
            this.eachVertex(L, cx, cz, reach, (v, gx, gz) => {
                const px = gx * CELL_SIZE;
                const pz = gz * CELL_SIZE;
                const u = len2 > 0 ? clamp01(((px - a[0]) * dx + (pz - a[2]) * dz) / len2) : 0;
                const d = Math.hypot(px - (a[0] + dx * u), pz - (a[2] + dz * u));
                const w = brushWeight(d, { radius: r, falloff }) * clamp01(strength);
                if (w <= 0) return;
                const h0 = this.before.get(i)?.get(v) ?? L.heights[v]!;
                const target = a[1] + (b[1] - a[1]) * u;
                writes.push([i, v, h0 + (target - h0) * w]);
            });
        }
        for (const [i, v, h] of writes) this.setHeight(i, v, h);
        return this.touches(writes);
    }

    private touches(writes: [number, number, number][]): TerrainTouch[] {
        const t = this.doc.terrain!;
        const by = new Map<number, number[]>();
        for (const [i, v] of writes) {
            let a = by.get(i);
            if (!a) by.set(i, (a = []));
            a.push(v);
        }
        const out = [...by].map(([layer, idx]) => ({ layer, rect: vertexRect(t[layer]!, idx) }));
        for (const tt of out) this.doc.touchTerrain(tt);
        return out;
    }

    /**
     * Paint cells whose centre is inside the brush with `ref` (an existing level texture, or `TEX_HOLE` to cut holes).
     * Strength below 1 scatters: each cell is painted with that probability (stable for the stroke), which breaks up a
     * hard edge between two ground types.
     */
    paint(ref: number, x: number, z: number, b: BrushSettings, o: PaintOptions = {}): TerrainTouch[] {
        const t = this.doc.terrain;
        if (!t) return [];
        const r = b.radius;
        const out: TerrainTouch[] = [];
        const k = clamp01(b.strength);
        for (const i of this.layersNear(x - r, z - r, x + r, z + r)) {
            let L = t[i]!;
            const cx0 = Math.max(0, Math.floor((x - r) / CELL_SIZE) - L.originX);
            const cx1 = Math.min(L.width - 1, Math.floor((x + r) / CELL_SIZE) - L.originX);
            const cz0 = Math.max(0, Math.floor((z - r) / CELL_SIZE) - L.originZ);
            const cz1 = Math.min(L.height - 1, Math.floor((z + r) / CELL_SIZE) - L.originZ);
            const changed: number[] = [];
            for (let cz = cz0; cz <= cz1; cz++) {
                for (let cx = cx0; cx <= cx1; cx++) {
                    const gx = L.originX + cx;
                    const gz = L.originZ + cz;
                    const d = Math.hypot((gx + 0.5) * CELL_SIZE - x, (gz + 0.5) * CELL_SIZE - z);
                    // The cell under the brush centre is always painted, however small the brush.
                    const inside = d < CELL_SIZE * 0.71 || brushWeight(d, b) >= 0.5;
                    if (!inside) continue;
                    if (k < 1 && hash2(gx, gz, this.seed * 7919 + i) >= k) continue;
                    const c = cz * L.width + cx;
                    const curL = L.cellTexLeft[c]!;
                    const curR = L.cellTexRight[c]!;
                    if (curL === ref && curR === ref) continue;
                    if (ref !== TEX_HOLE && !o.fillHoles && (!isSurface(curL) || !isSurface(curR))) continue;
                    let cb = this.cellBefore.get(i);
                    if (!cb) {
                        L = this.doc.writableLayer(i);
                        this.cellBefore.set(i, (cb = new Map()));
                    }
                    if (!cb.has(c)) cb.set(c, [curL, curR]);
                    L.cellTexLeft[c] = ref;
                    L.cellTexRight[c] = ref;
                    changed.push(c);
                }
            }
            if (changed.length) out.push({ layer: i, rect: cellRect(L, changed) });
        }
        for (const tt of out) this.doc.touchTerrain(tt);
        return out;
    }

    /** The stroke as one undo step (already applied), or null when it changed nothing. */
    finish(): TerrainEditCommand | null {
        const t = this.doc.terrain;
        if (!t) return null;
        const heights: HeightEdit[] = [];
        for (const [layer, m] of this.before) {
            const L = t[layer]!;
            const keep = [...m].filter(([v, h]) => L.heights[v] !== h);
            if (!keep.length) continue;
            heights.push({
                layer,
                idx: Uint32Array.from(keep, ([v]) => v),
                before: Float32Array.from(keep, ([, h]) => h),
                after: Float32Array.from(keep, ([v]) => L.heights[v]!),
            });
        }
        const cells: CellEdit[] = [];
        for (const [layer, m] of this.cellBefore) {
            const L = t[layer]!;
            const keep = [...m].filter(([c, [l, r]]) => L.cellTexLeft[c] !== l || L.cellTexRight[c] !== r);
            if (!keep.length) continue;
            cells.push({
                layer,
                idx: Uint32Array.from(keep, ([c]) => c),
                beforeL: Uint32Array.from(keep, ([, [l]]) => l),
                beforeR: Uint32Array.from(keep, ([, [, r]]) => r),
                afterL: Uint32Array.from(keep, ([c]) => L.cellTexLeft[c]!),
                afterR: Uint32Array.from(keep, ([c]) => L.cellTexRight[c]!),
            });
        }
        // Layers that were cloned for the stroke but end up unchanged: let undo-free bookkeeping drop them again.
        for (const layer of new Set([...this.before.keys(), ...this.cellBefore.keys()])) {
            if (!heights.some((e) => e.layer === layer) && !cells.some((e) => e.layer === layer))
                this.doc.markTerrainEdit(layer, 0);
        }
        this.before.clear();
        this.acc.clear();
        this.cellBefore.clear();
        if (!heights.length && !cells.length) return null;
        return new TerrainEditCommand(this.label, heights, cells);
    }

    /** Put everything the stroke changed back (a cancelled stroke). */
    cancel(): void {
        const cmd = this.finish();
        if (!cmd) return;
        // revert counts -1 per layer for an edit that was never counted.
        for (const l of cmd.layers) this.doc.markTerrainEdit(l, 1);
        cmd.revert(this.doc);
    }
}

// ---------------------------------------------------------------- expand and add

/** Bit (of the vertex mask's low 4) for the cell to the top-left / top-right / bottom-left / bottom-right of a vertex. */
interface MaskBits {
    tl: number;
    tr: number;
    bl: number;
    br: number;
}

const single = (v: number): boolean => v === 1 || v === 2 || v === 4 || v === 8;

/** Which mask bit means which neighbouring cell, read off the layer's own corner vertices (each sees one cell). */
export function vertexMaskBits(L: TerrainLayer): MaskBits {
    const s = L.width + 1;
    const m = (vx: number, vz: number): number => L.vertexMask[vz * s + vx]! & 15;
    const bits = { br: m(0, 0), bl: m(L.width, 0), tr: m(0, L.height), tl: m(L.width, L.height) };
    const vals = [bits.tl, bits.tr, bits.bl, bits.br];
    if (vals.every(single) && new Set(vals).size === 4) return bits;
    return { tl: 1, tr: 2, bl: 4, br: 8 };
}

function boundsMask(bits: MaskBits, w: number, hgt: number, vx: number, vz: number): number {
    let m = 0;
    if (vx > 0 && vz > 0) m |= bits.tl;
    if (vx < w && vz > 0) m |= bits.tr;
    if (vx > 0 && vz < hgt) m |= bits.bl;
    if (vx < w && vz < hgt) m |= bits.br;
    return m;
}

/**
 * A copy of `L` grown by `cells` on one edge. New vertices take the height of the edge they extend (a flat shelf to
 * sculpt from), new cells copy the texture, flags and UVs of the edge cell beside them. `west` / `north` move the
 * origin (cell units, must stay >= 0).
 */
export function expandLayer(L: TerrainLayer, edge: LayerEdge, cells: number): TerrainLayer {
    const n = Math.floor(cells);
    if (n < 1) throw new Error('expand by at least one cell');
    const ox = edge === 'west' ? n : 0;
    const oz = edge === 'north' ? n : 0;
    if (L.originX - ox < 0 || L.originZ - oz < 0) throw new Error('the layer is at the edge of the level map');
    const W = L.width + (edge === 'west' || edge === 'east' ? n : 0);
    const H = L.height + (edge === 'north' || edge === 'south' ? n : 0);
    const bits = vertexMaskBits(L);
    const s0 = L.width + 1;
    const s1 = W + 1;
    const nv = (W + 1) * (H + 1);
    const nc = W * H;
    const out: TerrainLayer = {
        ...L,
        visible: [...L.visible],
        width: W,
        height: H,
        originX: L.originX - ox,
        originZ: L.originZ - oz,
        heights: new Float32Array(nv),
        vertexFlags: new Uint16Array(nv),
        vertexMask: new Uint8Array(nv),
        cellFlags: new Uint16Array(nc),
        cellTexLeft: new Uint32Array(nc),
        cellTexRight: new Uint32Array(nc),
        cellUv: new Uint16Array(nc * 8),
    };
    const ci = (v: number, max: number): number => (v < 0 ? 0 : v > max ? max : v);
    for (let vz = 0; vz <= H; vz++) {
        for (let vx = 0; vx <= W; vx++) {
            const sx = vx - ox;
            const sz = vz - oz;
            const inOld = sx >= 0 && sz >= 0 && sx <= L.width && sz <= L.height;
            const src = ci(sz, L.height) * s0 + ci(sx, L.width);
            const v = vz * s1 + vx;
            out.heights[v] = L.heights[src]!;
            out.vertexFlags[v] = L.vertexFlags[src]!;
            const bm = boundsMask(bits, W, H, vx, vz);
            out.vertexMask[v] = inOld ? L.vertexMask[src]! | bm : (L.vertexMask[src]! & ~15) | bm;
        }
    }
    for (let cz = 0; cz < H; cz++) {
        for (let cx = 0; cx < W; cx++) {
            const src = ci(cz - oz, L.height - 1) * L.width + ci(cx - ox, L.width - 1);
            const c = cz * W + cx;
            out.cellFlags[c] = L.cellFlags[src]!;
            out.cellTexLeft[c] = L.cellTexLeft[src]!;
            out.cellTexRight[c] = L.cellTexRight[src]!;
            out.cellUv.set(L.cellUv.subarray(src * 8, src * 8 + 8), c * 8);
        }
    }
    return out;
}

const EDGE_NAMES: Record<LayerEdge, string> = { west: 'west', east: 'east', north: 'north', south: 'south' };

/** Undoable "Expand <layer> <edge>" (null when there is no such layer). Throws when the edge cannot grow. */
export function expandLayerCommand(
    doc: LevelDocument,
    index: number,
    edge: LayerEdge,
    cells: number,
): TerrainLayerCommand | null {
    const L = doc.terrain?.[index];
    if (!L) return null;
    const next = expandLayer(L, edge, cells);
    return new TerrainLayerCommand(
        `Expand ${L.name || `layer ${index}`} ${EDGE_NAMES[edge]} by ${cells} cell${cells === 1 ? '' : 's'}`,
        index,
        L,
        next,
    );
}

/** The four corner UVs of a plain cell (`Uc Ud Ub Ua Vc Vd Vb Va`, a = top left, v up). */
export const DEFAULT_CELL_UV: readonly number[] = [0, 0xffff, 0xffff, 0, 0, 0, 0xffff, 0xffff];

export interface NewLayerSpec {
    /** Cell units. */
    originX: number;
    originZ: number;
    width: number;
    height: number;
    /** World units. */
    worldHeight: number;
    /** Texture ref for every cell (one of the level's terrain textures). */
    ref: number;
    name?: string;
    /** 0 floor (default), 1 ceiling, 2 in between. */
    type?: number;
}

/**
 * A new flat layer. Light settings, flags and vertex/cell flag words come from `template` (an existing layer, so the
 * new one is lit like its neighbours); UVs from a cell of `template` that uses the same texture, else the plain square.
 */
export function makeLayer(spec: NewLayerSpec, index: number, id: number, template?: TerrainLayer): TerrainLayer {
    const W = Math.max(1, Math.floor(spec.width));
    const H = Math.max(1, Math.floor(spec.height));
    if (spec.originX < 0 || spec.originZ < 0) throw new Error('a layer cannot start west or north of the level map');
    const nv = (W + 1) * (H + 1);
    const nc = W * H;
    const wh = Math.fround(spec.worldHeight);
    let uv: readonly number[] = DEFAULT_CELL_UV;
    let cellFlag = CELL_FLAG_BACKSLASH;
    let vflag = 0;
    if (template) {
        const c = template.cellTexLeft.findIndex((r) => r === spec.ref);
        if (c >= 0) uv = [...template.cellUv.subarray(c * 8, c * 8 + 8)];
        cellFlag = template.cellFlags[c >= 0 ? c : 0] ?? cellFlag;
        vflag = template.vertexFlags[0] ?? 0;
    }
    const L: TerrainLayer = {
        index,
        id,
        width: W,
        height: H,
        type: spec.type ?? 0,
        originX: Math.floor(spec.originX),
        originZ: Math.floor(spec.originZ),
        worldHeight: wh,
        name: spec.name ?? `New layer ${index}`,
        // Flag 0x04 marks a flat layer (no normals stored); a new layer is meant to be sculpted.
        flags: (template?.flags ?? 0) & ~0x04,
        lightDirection: template?.lightDirection ?? 0,
        lightAscension: template?.lightAscension ?? 0,
        lightColor: template?.lightColor ?? 0xffffff,
        ambientColor: template?.ambientColor ?? 0x808080,
        lightDropoff: template?.lightDropoff ?? 0,
        visible: template ? [...template.visible] : [],
        heights: new Float32Array(nv).fill(wh),
        vertexFlags: new Uint16Array(nv).fill(vflag),
        vertexMask: new Uint8Array(nv),
        cellFlags: new Uint16Array(nc).fill(cellFlag),
        cellTexLeft: new Uint32Array(nc).fill(spec.ref),
        cellTexRight: new Uint32Array(nc).fill(spec.ref),
        cellUv: new Uint16Array(nc * 8),
        source: template?.source ?? 'ps2',
    };
    const bits = template ? vertexMaskBits(template) : { tl: 1, tr: 2, bl: 4, br: 8 };
    for (let vz = 0; vz <= H; vz++)
        for (let vx = 0; vx <= W; vx++) L.vertexMask[vz * (W + 1) + vx] = boundsMask(bits, W, H, vx, vz);
    for (let c = 0; c < nc; c++) L.cellUv.set(uv, c * 8);
    return L;
}

/** Undoable "Add layer": a new flat layer appended after the existing ones (null without terrain). */
export function addLayerCommand(doc: LevelDocument, spec: NewLayerSpec, template?: number): TerrainLayerCommand | null {
    const t = doc.terrain;
    if (!t) return null;
    const index = t.length;
    const id = t.reduce((m, L) => Math.max(m, L.id), 0) + 1;
    const tpl = template !== undefined ? t[template] : t[0];
    const L = makeLayer(spec, index, id, tpl);
    return new TerrainLayerCommand(`Add layer ${L.name}`, index, null, L);
}
