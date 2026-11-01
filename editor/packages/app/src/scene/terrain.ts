import * as THREE from 'three';
import {
    CELL_FLAG_BACKSLASH,
    CELL_SIZE,
    TEX_HOLE,
    TEX_INVISIBLE,
    layerHeightAt,
    terrainHeightAt,
    type TerrainLayer,
} from '@drakan/formats';
import type { TextureLookup } from './textures.ts';

/** Scene units per game (world) unit; matches `LevelView`'s `S`. */
export const SCENE_SCALE = 0.001;
/** Cells per chunk edge: terrain is merged per (chunk, texture) so frustum culling stays effective. */
const CHUNK_CELLS = 32;

export interface TerrainScene {
    /** Add to a scene. One mesh per (chunk, texture), frustum culled by its bounding sphere. */
    group: THREE.Group;
    layers: readonly TerrainLayer[];
    /** Terrain height (game units) at (x, z) game units; NaN if no layer covers it. `nearY` picks the closest surface. */
    heightAt(x: number, z: number, nearY?: number): number;
    dispose(): void;
}

type Corner = [number, number, number, number, number];

export interface TerrainBucket {
    /** Stored texture reference (`id | db << 16`) of the cells in the bucket. */
    ref: number;
    /** Chunk key (`cx,cz` in chunk units). */
    chunk: string;
    /** Scene units, 3 floats per vertex, non-indexed triangles. */
    pos: number[];
    /** 2 floats per vertex, stored V (GL order, row 0 = bottom). */
    uv: number[];
}

/**
 * Terrain triangles per (chunk, texture). Holes and invisible cells are skipped. Triangles follow the engine's cell
 * split (cell flag bit 0: a-d diagonal, else b-c) and the stored UV corners (`Uc Ud Ub Ua Vc Vd Vb Va`), wound counter
 * clockwise seen from +y. Coordinates: scene = game units * SCENE_SCALE, y up. `perCell` chunks by cell position (the
 * default keys a whole layer by its origin, which suits culling but cannot split a large layer; the exporter splits).
 */
export function terrainBuckets(
    layers: readonly TerrainLayer[],
    opts: { chunkCells?: number; perCell?: boolean } = {},
): TerrainBucket[] {
    const chunk = opts.chunkCells ?? CHUNK_CELLS;
    const buckets = new Map<string, TerrainBucket>();
    const S = SCENE_SCALE;
    for (const L of layers) {
        const stride = L.width + 1;
        const layerKey = `${Math.floor(L.originX / chunk)},${Math.floor(L.originZ / chunk)}`;
        for (let cz = 0; cz < L.height; cz++) {
            for (let cx = 0; cx < L.width; cx++) {
                const c = cz * L.width + cx;
                const tl = L.cellTexLeft[c]!;
                const tr = L.cellTexRight[c]!;
                const skipL = tl === TEX_HOLE || tl === TEX_INVISIBLE;
                const skipR = tr === TEX_HOLE || tr === TEX_INVISIBLE;
                if (skipL && skipR) continue;
                const ck = opts.perCell
                    ? `${Math.floor((L.originX + cx) / chunk)},${Math.floor((L.originZ + cz) / chunk)}`
                    : layerKey;
                const x0 = (L.originX + cx) * CELL_SIZE * S;
                const z0 = (L.originZ + cz) * CELL_SIZE * S;
                const x1 = x0 + CELL_SIZE * S;
                const z1 = z0 + CELL_SIZE * S;
                const uv = L.cellUv;
                const o = c * 8;
                const A: Corner = [x0, L.heights[cz * stride + cx]! * S, z0, uv[o + 3]! / 65535, uv[o + 7]! / 65535];
                const B: Corner = [
                    x1,
                    L.heights[cz * stride + cx + 1]! * S,
                    z0,
                    uv[o + 2]! / 65535,
                    uv[o + 6]! / 65535,
                ];
                const C: Corner = [x0, L.heights[(cz + 1) * stride + cx]! * S, z1, uv[o]! / 65535, uv[o + 4]! / 65535];
                const D: Corner = [
                    x1,
                    L.heights[(cz + 1) * stride + cx + 1]! * S,
                    z1,
                    uv[o + 1]! / 65535,
                    uv[o + 5]! / 65535,
                ];
                const back = (L.cellFlags[c]! & CELL_FLAG_BACKSLASH) !== 0;
                const tris: [boolean, number, Corner[]][] = [
                    [skipL, tl, back ? [A, C, D] : [A, C, B]],
                    [skipR, tr, back ? [A, D, B] : [B, C, D]],
                ];
                for (const [skip, ref, tri] of tris) {
                    if (skip) continue;
                    const k = `${ck}:${ref}`;
                    let b = buckets.get(k);
                    if (!b) buckets.set(k, (b = { ref, chunk: ck, pos: [], uv: [] }));
                    for (const v of tri) {
                        b.pos.push(v[0], v[1], v[2]);
                        b.uv.push(v[3], v[4]);
                    }
                }
            }
        }
    }
    return [...buckets.values()];
}

/** Build the terrain for decoded layers (see `terrainBuckets`). */
export function buildTerrain(
    layers: readonly TerrainLayer[],
    textures: TextureLookup,
    opts: { chunkCells?: number } = {},
): TerrainScene {
    const buckets = terrainBuckets(layers, opts);
    const group = new THREE.Group();
    group.name = 'terrain';
    const geometries: THREE.BufferGeometry[] = [];
    const materials = new Map<number, THREE.Material>();
    for (const b of buckets) {
        const g = new THREE.BufferGeometry();
        g.setAttribute('position', new THREE.Float32BufferAttribute(b.pos, 3));
        g.setAttribute('uv', new THREE.Float32BufferAttribute(b.uv, 2));
        g.computeVertexNormals();
        g.computeBoundingSphere();
        g.computeBoundingBox();
        geometries.push(g);
        let m = materials.get(b.ref);
        if (!m) {
            const t = textures(b.ref);
            m = t
                ? new THREE.MeshLambertMaterial({ map: t, side: THREE.DoubleSide })
                : new THREE.MeshLambertMaterial({ color: 0x6b7a58, side: THREE.DoubleSide });
            materials.set(b.ref, m);
        }
        const mesh = new THREE.Mesh(g, m);
        mesh.frustumCulled = true;
        group.add(mesh);
    }
    return {
        group,
        layers,
        heightAt: (x, z, nearY) => terrainHeightAt(layers, x, z, nearY),
        dispose() {
            for (const g of geometries) g.dispose();
            for (const m of materials.values()) m.dispose();
        },
    };
}

/** Height of one layer (game units); re-exported for tools that work per layer. */
export { layerHeightAt };
