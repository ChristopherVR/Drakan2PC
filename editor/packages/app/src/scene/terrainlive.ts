import * as THREE from 'three';
import {
    CELL_FLAG_BACKSLASH,
    CELL_SIZE,
    TEX_HOLE,
    TEX_INVISIBLE,
    terrainHeightAt,
    type TerrainLayer,
} from '@drakan/formats';
import { SCENE_SCALE, type TerrainScene } from './terrain.ts';
import type { TextureLookup } from './textures.ts';

/** Cells per chunk edge for untouched layers (merged per origin chunk and texture, like `buildTerrain`). */
const SHARED_CHUNK = 32;
/** Cells per chunk edge of a layer being edited: a brush dab rebuilds only the few chunks it touched. */
const EDIT_CHUNK = 16;

interface Bucket {
    pos: number[];
    uv: number[];
}

/** Triangles of `L`'s cells in [x0,x1) x [z0,z1), per texture ref (same split, winding and UVs as `terrainBuckets`). */
function addCells(out: Map<number, Bucket>, L: TerrainLayer, x0: number, z0: number, x1: number, z1: number): void {
    const S = SCENE_SCALE;
    const stride = L.width + 1;
    const uv = L.cellUv;
    const H = L.heights;
    for (let cz = z0; cz < z1; cz++) {
        for (let cx = x0; cx < x1; cx++) {
            const c = cz * L.width + cx;
            const tl = L.cellTexLeft[c]!;
            const tr = L.cellTexRight[c]!;
            const skipL = tl === TEX_HOLE || tl === TEX_INVISIBLE;
            const skipR = tr === TEX_HOLE || tr === TEX_INVISIBLE;
            if (skipL && skipR) continue;
            const ax = (L.originX + cx) * CELL_SIZE * S;
            const az = (L.originZ + cz) * CELL_SIZE * S;
            const bx = ax + CELL_SIZE * S;
            const cz1 = az + CELL_SIZE * S;
            const o = c * 8;
            const v = cz * stride + cx;
            // a b / c d corners: x, y, z, u, v
            const A = [ax, H[v]! * S, az, uv[o + 3]! / 65535, uv[o + 7]! / 65535] as const;
            const B = [bx, H[v + 1]! * S, az, uv[o + 2]! / 65535, uv[o + 6]! / 65535] as const;
            const C = [ax, H[v + stride]! * S, cz1, uv[o]! / 65535, uv[o + 4]! / 65535] as const;
            const D = [bx, H[v + stride + 1]! * S, cz1, uv[o + 1]! / 65535, uv[o + 5]! / 65535] as const;
            const back = (L.cellFlags[c]! & CELL_FLAG_BACKSLASH) !== 0;
            if (!skipL) push(out, tl, A, C, back ? D : B);
            if (!skipR) push(out, tr, back ? A : B, back ? D : C, back ? B : D);
        }
    }
}

type Corner = readonly [number, number, number, number, number];

function push(out: Map<number, Bucket>, ref: number, p: Corner, q: Corner, r: Corner): void {
    let b = out.get(ref);
    if (!b) out.set(ref, (b = { pos: [], uv: [] }));
    b.pos.push(p[0], p[1], p[2], q[0], q[1], q[2], r[0], r[1], r[2]);
    b.uv.push(p[3], p[4], q[3], q[4], r[3], r[4]);
}

/** Touched part of the terrain: one layer's cell rectangle (`x1`/`z1` exclusive) or the whole layer. */
export interface TerrainDirty {
    layer: number;
    rect?: { x0: number; z0: number; x1: number; z1: number };
}

/**
 * Terrain that can be edited while it is shown. Untouched layers are merged per (origin chunk, texture) like
 * `buildTerrain`; the first edit of a layer moves it into its own 16x16-cell chunks, so a brush dab rebuilds only the
 * chunks it touched (`touch` marks, `flush` rebuilds). `layers()` is read on every rebuild and height query, so the
 * scene follows the document's layer array (copy-on-write layers and appended ones included).
 */
export class EditableTerrain implements TerrainScene {
    readonly group = new THREE.Group();
    private readonly meshes = new Map<string, THREE.Mesh[]>();
    private readonly materials = new Map<number, THREE.Material>();
    private readonly detached = new Set<number>();
    /** Chunk keys each detached layer currently has (its size can change). */
    private readonly subKeys = new Map<number, Set<string>>();
    private readonly dirty = new Set<string>();
    /** Shared origin-chunk key of every layer at the last full build. */
    private sharedKey: string[] = [];

    constructor(
        private readonly source: () => readonly TerrainLayer[],
        private readonly textures: TextureLookup,
    ) {
        this.group.name = 'terrain';
        this.rebuildAll();
    }

    get layers(): readonly TerrainLayer[] {
        return this.source();
    }

    heightAt(x: number, z: number, nearY?: number): number {
        return terrainHeightAt(this.source(), x, z, nearY);
    }

    /** Throw away every mesh and build again from `layers()` (a different document was attached). */
    rebuildAll(): void {
        for (const k of [...this.meshes.keys()]) this.dropKey(k);
        this.detached.clear();
        this.subKeys.clear();
        this.dirty.clear();
        const t = this.source();
        this.sharedKey = t.map(
            (L) => `s:${Math.floor(L.originX / SHARED_CHUNK)},${Math.floor(L.originZ / SHARED_CHUNK)}`,
        );
        const groups = new Map<string, number[]>();
        t.forEach((_, i) => {
            const k = this.sharedKey[i]!;
            let g = groups.get(k);
            if (!g) groups.set(k, (g = []));
            g.push(i);
        });
        for (const [k, ids] of groups) this.buildKey(k, ids);
    }

    /** Mark a changed part for the next `flush`. */
    touch(d: TerrainDirty): void {
        const t = this.source();
        const L = t[d.layer];
        if (!this.detached.has(d.layer)) {
            this.detached.add(d.layer);
            const sk = this.sharedKey[d.layer];
            if (sk) this.dirty.add(sk);
            this.subKeys.set(d.layer, new Set());
            d = { layer: d.layer };
        }
        const subs = this.subKeys.get(d.layer)!;
        if (!d.rect || !L) {
            // Whole layer (new, expanded, removed): its chunk grid may have changed.
            for (const k of subs) this.dirty.add(k);
            subs.clear();
            if (!L) return;
            for (let cz = 0; cz < L.height; cz += EDIT_CHUNK)
                for (let cx = 0; cx < L.width; cx += EDIT_CHUNK) {
                    const k = `d:${d.layer}:${cx / EDIT_CHUNK},${cz / EDIT_CHUNK}`;
                    subs.add(k);
                    this.dirty.add(k);
                }
            return;
        }
        const r = d.rect;
        for (let cz = Math.floor(r.z0 / EDIT_CHUNK); cz * EDIT_CHUNK < r.z1; cz++)
            for (let cx = Math.floor(r.x0 / EDIT_CHUNK); cx * EDIT_CHUNK < r.x1; cx++) {
                const k = `d:${d.layer}:${cx},${cz}`;
                subs.add(k);
                this.dirty.add(k);
            }
    }

    get pending(): boolean {
        return this.dirty.size > 0;
    }

    /** Rebuild the chunks marked since the last flush. */
    flush(): void {
        if (!this.dirty.size) return;
        const t = this.source();
        const keys = [...this.dirty];
        this.dirty.clear();
        for (const k of keys) {
            this.dropKey(k);
            if (k.startsWith('s:')) {
                const ids: number[] = [];
                this.sharedKey.forEach((sk, i) => {
                    if (sk === k && !this.detached.has(i) && t[i]) ids.push(i);
                });
                this.buildKey(k, ids);
            } else {
                const [, li, xz] = k.split(':');
                const i = Number(li);
                const L = t[i];
                if (!L || !this.subKeys.get(i)?.has(k)) continue;
                const [cx, cz] = xz!.split(',').map(Number) as [number, number];
                const x0 = cx * EDIT_CHUNK;
                const z0 = cz * EDIT_CHUNK;
                if (x0 >= L.width || z0 >= L.height) continue;
                const b = new Map<number, Bucket>();
                addCells(b, L, x0, z0, Math.min(L.width, x0 + EDIT_CHUNK), Math.min(L.height, z0 + EDIT_CHUNK));
                this.addMeshes(k, b);
            }
        }
    }

    private buildKey(k: string, ids: number[]): void {
        const t = this.source();
        const b = new Map<number, Bucket>();
        for (const i of ids) {
            const L = t[i]!;
            addCells(b, L, 0, 0, L.width, L.height);
        }
        this.addMeshes(k, b);
    }

    private addMeshes(k: string, buckets: Map<number, Bucket>): void {
        const list: THREE.Mesh[] = [];
        for (const [ref, bk] of buckets) {
            if (!bk.pos.length) continue;
            const g = new THREE.BufferGeometry();
            g.setAttribute('position', new THREE.Float32BufferAttribute(bk.pos, 3));
            g.setAttribute('uv', new THREE.Float32BufferAttribute(bk.uv, 2));
            g.computeVertexNormals();
            g.computeBoundingSphere();
            g.computeBoundingBox();
            const mesh = new THREE.Mesh(g, this.material(ref));
            mesh.frustumCulled = true;
            mesh.userData.terrainKey = k;
            this.group.add(mesh);
            list.push(mesh);
        }
        if (list.length) this.meshes.set(k, list);
    }

    private dropKey(k: string): void {
        const list = this.meshes.get(k);
        if (!list) return;
        for (const m of list) {
            this.group.remove(m);
            m.geometry.dispose();
        }
        this.meshes.delete(k);
    }

    private material(ref: number): THREE.Material {
        let m = this.materials.get(ref);
        if (m) return m;
        const t = this.textures(ref);
        const wire = (this.materials.values().next().value as THREE.MeshLambertMaterial | undefined)?.wireframe;
        m = t
            ? new THREE.MeshLambertMaterial({ map: t, side: THREE.DoubleSide, wireframe: !!wire })
            : new THREE.MeshLambertMaterial({ color: 0x6b7a58, side: THREE.DoubleSide, wireframe: !!wire });
        this.materials.set(ref, m);
        return m;
    }

    dispose(): void {
        for (const k of [...this.meshes.keys()]) this.dropKey(k);
        for (const m of this.materials.values()) m.dispose();
        this.materials.clear();
    }
}
