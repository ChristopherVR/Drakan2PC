// Synthetic data only; no game bytes.
import { describe, expect, it } from 'vitest';
import { LevelDocument, TerrainStroke } from '@drakan/editor-core';
import { CELL_SIZE, type TerrainLayer } from '@drakan/formats';
import { averageColor, buildSwatches, colorName } from '../src/editor/landscape/palette.ts';
import { EditableTerrain, cellBlends } from '../src/scene/terrainlive.ts';

function flat(index: number, w: number, h: number, ox: number, refs: number[]): TerrainLayer {
    const nv = (w + 1) * (h + 1);
    const nc = w * h;
    const left = new Uint32Array(nc).map((_, i) => refs[i % refs.length]!);
    return {
        index,
        id: index,
        width: w,
        height: h,
        type: 0,
        originX: ox,
        originZ: 0,
        worldHeight: 0,
        name: `L${index}`,
        flags: 0,
        lightDirection: 0,
        lightAscension: 0,
        lightColor: 0,
        ambientColor: 0,
        lightDropoff: 0,
        visible: [],
        heights: new Float32Array(nv),
        vertexFlags: new Uint16Array(nv),
        vertexMask: new Uint8Array(nv),
        cellFlags: new Uint16Array(nc),
        cellTexLeft: left,
        cellTexRight: left.slice(),
        cellUv: new Uint16Array(nc * 8),
        source: 'ps2',
    };
}

describe('landscape palette', () => {
    it('names ground colours plainly', () => {
        expect(colorName([60, 120, 40])).toBe('Grass');
        expect(colorName([200, 180, 130])).toBe('Sand');
        expect(colorName([128, 128, 128])).toBe('Grey stone');
        expect(colorName([30, 30, 32])).toBe('Dark stone');
        expect(averageColor(new Uint8Array([10, 20, 30, 255, 30, 40, 50, 255]))).toEqual([20, 30, 40]);
    });

    it('keeps disc textures that were painted away and numbers duplicate names', () => {
        const green = new Uint8Array(16).fill(0).map((_, i) => [60, 120, 40, 255][i % 4]!);
        const tex = [1, 2, 3].map((ref) => ({ ref, width: 2, height: 2, rgba: green }));
        const sw = buildSwatches([flat(0, 4, 1, 0, [1, 1, 1, 2])], tex, [1, 2, 3]);
        expect(sw.map((s) => s.ref)).toEqual([1, 2, 3]);
        expect(sw.map((s) => s.name)).toEqual(['Grass', 'Grass 2', 'Grass 3']);
        expect(sw[2]!.cells).toBe(0);
        expect(sw[0]!.share).toBe(0.75);
    });
});

describe('live terrain mesh', () => {
    it('rebuilds only touched chunks and follows the document', () => {
        const doc = new LevelDocument();
        doc.attachTerrain([flat(0, 40, 40, 0, [7]), flat(1, 4, 4, 40, [8])]);
        const live = new EditableTerrain(
            () => doc.terrain!,
            () => null,
        );
        doc.onTerrain = (t) => live.touch(t);
        const before = live.group.children.length;
        expect(before).toBe(2);
        const s = new TerrainStroke(doc, [0], 'Raise');
        s.sculpt('raise', 5 * CELL_SIZE, 5 * CELL_SIZE, { radius: 2 * CELL_SIZE, strength: 1, falloff: 0 }, 0.1);
        expect(live.pending).toBe(true);
        live.flush();
        // Layer 0 now has its own 16-cell chunks (3 x 3); layer 1 keeps its shared mesh.
        expect(live.group.children.length).toBe(9 + 1);
        const keys = new Set(live.group.children.map((m) => m.userData.terrainKey as string));
        expect(keys.has('d:0:0,0')).toBe(true);
        expect(live.heightAt(5 * CELL_SIZE, 5 * CELL_SIZE)).toBeGreaterThan(0);
        // A dab touches one chunk: only it is rebuilt.
        const other = live.group.children.find((m) => m.userData.terrainKey === 'd:0:2,2');
        s.sculpt('raise', 5 * CELL_SIZE, 5 * CELL_SIZE, { radius: 2 * CELL_SIZE, strength: 1, falloff: 0 }, 0.1);
        live.flush();
        expect(live.group.children.includes(other!)).toBe(true);
        live.dispose();
    });
});

describe('ground texture blending (the writer weight rule)', () => {
    // 4 x 1 strip: grass grass sand sand.
    const L = flat(0, 4, 1, 0, [1, 1, 2, 2]);
    const rank = (r: number): number => r;

    it('blends a neighbour texture over the border cell by vertex counts', () => {
        // Cell 0 has only grass around it: nothing to blend.
        expect(cellBlends(L, 0, 0, rank)).toEqual([]);
        // Cell 1 (grass) borders sand at its east corners: there 1 grass + 1 sand cell share the vertex -> 1/2.
        expect(cellBlends(L, 1, 0, rank)).toEqual([{ ref: 2, alpha: [0, 0.5, 0, 0.5] }]);
        // Cell 2 (sand) gets grass at its west corners.
        expect(cellBlends(L, 2, 0, rank)).toEqual([{ ref: 1, alpha: [0.5, 0, 0.5, 0] }]);
    });

    it('composites several textures to their vertex weights', () => {
        // 2 x 2: grass, sand / rock, grass. The centre vertex has 2 grass, 1 sand, 1 rock.
        const Q = flat(0, 2, 2, 0, [1, 2, 3, 1]);
        const b = cellBlends(Q, 0, 0, rank);
        expect(b.map((x) => x.ref)).toEqual([2, 3]);
        // Over grass (2/4): sand 1/(2+1), then rock 1/(2+1+1); composited = grass 1/2, sand 1/4, rock 1/4.
        const a2 = b[0]!.alpha[3];
        const a3 = b[1]!.alpha[3];
        expect(a2).toBeCloseTo(1 / 3);
        expect(a3).toBeCloseTo(1 / 4);
        const sand = a2 * (1 - a3);
        const grass = (1 - a2) * (1 - a3);
        expect(grass).toBeCloseTo(0.5);
        expect(sand).toBeCloseTo(0.25);
    });

    it('adds overlay meshes at texture borders and rebuilds them after a repaint', () => {
        const doc = new LevelDocument();
        doc.attachTerrain([flat(0, 8, 8, 0, [7])]);
        const live = new EditableTerrain(
            () => doc.terrain!,
            () => null,
        );
        doc.onTerrain = (t) => live.touch(t);
        const overlays = (): number => live.group.children.filter((m) => m.userData.terrainBlend).length;
        expect(overlays()).toBe(0);
        const s = new TerrainStroke(doc, [0], 'Paint');
        s.paint(9, 4 * CELL_SIZE, 4 * CELL_SIZE, { radius: CELL_SIZE, strength: 1, falloff: 0 });
        live.flush();
        // Grass gets a sand overlay and sand a grass overlay.
        expect(overlays()).toBe(2);
        s.cancel();
        live.flush();
        expect(overlays()).toBe(0);
        live.blend = false;
        live.rebuildAll();
        expect(overlays()).toBe(0);
        live.dispose();
    });
});
