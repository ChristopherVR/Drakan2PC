import { CELL_SIZE, TEX_HOLE, layerHeightAt, type TerrainLayer } from '@drakan/formats';
import { describe, expect, it } from 'vitest';
import {
    CommandStack,
    LevelDocument,
    TerrainStroke,
    addLayerCommand,
    brushWeight,
    connectedLayers,
    expandLayer,
    expandLayerCommand,
    groundedObjects,
    layerAt,
    layerHeightRange,
    makeLayer,
    quantizeHeight,
    terrainLayerFromJson,
    terrainLayerToJson,
    terrainPalette,
    vertexMaskBits,
    type LevelObject,
    type TerrainTouch,
} from '../src/index.ts';

const GRASS = 0x00090001;
const ROCK = 0x00090002;

/** A flat synthetic layer: `w x h` cells at origin (ox, oz) cells, every vertex at `y`. */
function layer(index: number, w: number, h: number, ox = 0, oz = 0, y = 1000, ref = GRASS): TerrainLayer {
    const nv = (w + 1) * (h + 1);
    const nc = w * h;
    const L: TerrainLayer = {
        index,
        id: 100 + index,
        width: w,
        height: h,
        type: 0,
        originX: ox,
        originZ: oz,
        worldHeight: 1000,
        name: `L${index}`,
        flags: 0x04,
        lightDirection: 1,
        lightAscension: 2,
        lightColor: 0xffeedd,
        ambientColor: 0x334455,
        lightDropoff: 0,
        visible: [index],
        heights: new Float32Array(nv).fill(y),
        vertexFlags: new Uint16Array(nv).fill(0x200),
        vertexMask: new Uint8Array(nv),
        cellFlags: new Uint16Array(nc).fill(1),
        cellTexLeft: new Uint32Array(nc).fill(ref),
        cellTexRight: new Uint32Array(nc).fill(ref),
        cellUv: new Uint16Array(nc * 8),
        source: 'ps2',
    };
    // Corner masks: tl=1 tr=2 bl=4 br=8 (what vertexMaskBits reads back).
    for (let vz = 0; vz <= h; vz++)
        for (let vx = 0; vx <= w; vx++) {
            let m = 0;
            if (vx > 0 && vz > 0) m |= 1;
            if (vx < w && vz > 0) m |= 2;
            if (vx > 0 && vz < h) m |= 4;
            if (vx < w && vz < h) m |= 8;
            L.vertexMask[vz * (w + 1) + vx] = m;
        }
    return L;
}

function docWith(...layers: TerrainLayer[]): LevelDocument {
    const d = new LevelDocument();
    d.level = 'test';
    d.attachTerrain(layers);
    return d;
}

const brush = { radius: 3 * CELL_SIZE, strength: 1, falloff: 0.5 };

describe('brush basics', () => {
    it('weights 1 inside, 0 outside, eases between', () => {
        expect(brushWeight(0, brush)).toBe(1);
        expect(brushWeight(brush.radius, brush)).toBe(0);
        const mid = brushWeight(brush.radius * 0.75, brush);
        expect(mid).toBeGreaterThan(0);
        expect(mid).toBeLessThan(1);
        expect(brushWeight(brush.radius * 0.99, { radius: brush.radius, falloff: 0 })).toBe(1);
    });

    it('quantises to the raw u16 grid and clamps', () => {
        const L = layer(0, 1, 1);
        expect(quantizeHeight(L, 1003)).toBe(1004);
        const [lo, hi] = layerHeightRange(L);
        expect(lo).toBe(1000 - 65536);
        expect(hi).toBe(1000 + 65534);
        expect(quantizeHeight(L, 1e9)).toBe(hi);
        expect(quantizeHeight(L, -1e9)).toBe(lo);
    });
});

describe('sculpt strokes', () => {
    it('raises a hill, one undo step, copy-on-write', () => {
        const base = layer(0, 8, 8);
        const disc = base.heights.slice();
        const d = docWith(base);
        const st = new CommandStack(d);
        const stroke = new TerrainStroke(d, [0], 'Raise');
        const c = 4 * CELL_SIZE;
        for (let i = 0; i < 10; i++) stroke.sculpt('raise', c, c, brush, 0.05);
        const cmd = stroke.finish()!;
        st.run(cmd);
        const L = d.terrain![0]!;
        expect(L).not.toBe(base);
        expect(base.heights).toEqual(disc);
        const top = layerHeightAt(L, c, c);
        // 0.5 s at 0.5 * radius per second.
        expect(top).toBeGreaterThan(1000 + 0.9 * 0.5 * brush.radius * 0.5);
        // Falloff: the edge of the brush rose less than the centre, outside did not move.
        expect(layerHeightAt(L, c + 2.5 * CELL_SIZE, c)).toBeLessThan(top);
        expect(L.heights[0]).toBe(1000);
        expect(d.terrainChanged.has(0)).toBe(true);
        st.undo();
        expect([...d.terrain![0]!.heights]).toEqual([...disc]);
        expect(d.terrainChanged.has(0)).toBe(false);
        st.redo();
        expect(layerHeightAt(d.terrain![0]!, c, c)).toBe(top);
    });

    it('lowers with invert and never leaves the raw range', () => {
        const d = docWith(layer(0, 4, 4));
        const s = new TerrainStroke(d, [0], 'Lower');
        for (let i = 0; i < 200; i++) s.sculpt('raise', 2 * CELL_SIZE, 2 * CELL_SIZE, brush, 1, { invert: true });
        s.finish();
        const L = d.terrain![0]!;
        expect(Math.min(...L.heights)).toBe(layerHeightRange(L)[0]);
    });

    it('slow brushes accumulate instead of rounding away', () => {
        const d = docWith(layer(0, 4, 4));
        const s = new TerrainStroke(d, [0], 'Raise');
        const weak = { ...brush, strength: 0.0004 };
        for (let i = 0; i < 100; i++) s.sculpt('raise', 2 * CELL_SIZE, 2 * CELL_SIZE, weak, 0.016);
        // 0.5 * 6144 * 0.0004 * 0.016 = 0.0197 per dab, 1.97 after 100 dabs: rounds to the 2-unit grid.
        expect(d.terrain![0]!.heights[2 * 5 + 2]).toBe(1002);
    });

    it('flattens towards a target and smooths a spike', () => {
        const L0 = layer(0, 6, 6);
        L0.heights[3 * 7 + 3] = 5000;
        const d = docWith(L0);
        const s = new TerrainStroke(d, [0], 'Smooth');
        for (let i = 0; i < 20; i++) s.sculpt('smooth', 3 * CELL_SIZE, 3 * CELL_SIZE, brush, 0.05);
        s.finish();
        const sm = d.terrain![0]!.heights[3 * 7 + 3]!;
        expect(sm).toBeLessThan(2000);
        const f = new TerrainStroke(d, [0], 'Flatten');
        for (let i = 0; i < 60; i++) f.sculpt('flatten', 3 * CELL_SIZE, 3 * CELL_SIZE, brush, 0.05, { target: 3000 });
        f.finish();
        expect(Math.abs(d.terrain![0]!.heights[3 * 7 + 3]! - 3000)).toBeLessThanOrEqual(2);
    });

    it('noise is deterministic and moves both ways', () => {
        const run = (): Float32Array => {
            const d = docWith(layer(0, 10, 10));
            const s = new TerrainStroke(d, [0], 'Noise', 7);
            for (let i = 0; i < 10; i++)
                s.sculpt('noise', 5 * CELL_SIZE, 5 * CELL_SIZE, { ...brush, radius: 5 * CELL_SIZE }, 0.1);
            s.finish();
            return d.terrain![0]!.heights;
        };
        const a = run();
        expect([...a]).toEqual([...run()]);
        expect(Math.max(...a)).toBeGreaterThan(1000);
        expect(Math.min(...a)).toBeLessThan(1000);
    });

    it('keeps seams closed across stitched layers', () => {
        // Two 4x4 layers side by side sharing the column x = 4.
        const d = docWith(layer(0, 4, 4, 0, 0), layer(1, 4, 4, 4, 0), layer(2, 4, 4, 0, 0, 9000));
        const set = connectedLayers(d.terrain!, 0);
        expect(set.sort()).toEqual([0, 1]);
        const s = new TerrainStroke(d, set, 'Raise');
        for (let i = 0; i < 5; i++) s.sculpt('raise', 4 * CELL_SIZE, 2 * CELL_SIZE, brush, 0.1);
        for (let i = 0; i < 5; i++) s.sculpt('smooth', 4 * CELL_SIZE, 2 * CELL_SIZE, brush, 0.1);
        s.finish();
        const [A, B, C] = d.terrain!;
        for (let vz = 0; vz <= 4; vz++) expect(A!.heights[vz * 5 + 4]).toBe(B!.heights[vz * 5 + 0]);
        expect(A!.heights[2 * 5 + 4]).toBeGreaterThan(1000);
        // The layer stacked above was not part of the surface.
        expect(C!.heights.every((h) => h === 9000)).toBe(true);
        expect(d.terrainChanged.has(2)).toBe(false);
    });

    it('ramps between two points and redraws live from the start state', () => {
        const d = docWith(layer(0, 10, 2));
        const s = new TerrainStroke(d, [0], 'Ramp');
        const a: [number, number, number] = [0, 1000, CELL_SIZE];
        s.ramp(a, [10 * CELL_SIZE, 9000, CELL_SIZE], 2 * CELL_SIZE, 0);
        s.ramp(a, [10 * CELL_SIZE, 5000, CELL_SIZE], 2 * CELL_SIZE, 0);
        s.finish();
        const L = d.terrain![0]!;
        expect(layerHeightAt(L, 5 * CELL_SIZE, CELL_SIZE)).toBe(3000);
        expect(layerHeightAt(L, 10 * CELL_SIZE, CELL_SIZE)).toBe(5000);
    });

    it('reports touched cell rectangles', () => {
        const d = docWith(layer(0, 20, 20));
        const seen: TerrainTouch[] = [];
        d.onTerrain = (t) => seen.push(t);
        const s = new TerrainStroke(d, [0], 'Raise');
        s.sculpt('raise', 10 * CELL_SIZE, 10 * CELL_SIZE, { radius: 1.5 * CELL_SIZE, strength: 1, falloff: 0 }, 0.1);
        expect(seen).toHaveLength(1);
        expect(seen[0]!.rect).toEqual({ x0: 8, z0: 8, x1: 12, z1: 12 });
        s.cancel();
        expect(d.terrain![0]!.heights.every((h) => h === 1000)).toBe(true);
        expect(d.terrainChanged.size).toBe(0);
    });
});

describe('paint', () => {
    it('paints existing textures, skips holes, undoes', () => {
        const L0 = layer(0, 6, 6);
        L0.cellTexLeft[0] = L0.cellTexRight[0] = TEX_HOLE;
        L0.cellTexLeft[35] = L0.cellTexRight[35] = ROCK;
        const d = docWith(L0);
        expect(terrainPalette(d.terrain!).map((p) => p.ref)).toEqual([GRASS, ROCK]);
        const st = new CommandStack(d);
        const s = new TerrainStroke(d, [0], 'Paint');
        s.paint(ROCK, 1 * CELL_SIZE, 1 * CELL_SIZE, { radius: 1.6 * CELL_SIZE, strength: 1, falloff: 0 });
        st.run(s.finish()!);
        const L = d.terrain![0]!;
        expect(L.cellTexLeft[0]).toBe(TEX_HOLE);
        expect(L.cellTexLeft[1 * 6 + 1]).toBe(ROCK);
        expect(L.cellTexRight[1 * 6 + 1]).toBe(ROCK);
        st.undo();
        expect(d.terrain![0]!.cellTexLeft[1 * 6 + 1]).toBe(GRASS);
    });

    it('scatters at low strength and cuts holes', () => {
        const d = docWith(layer(0, 20, 20));
        const s = new TerrainStroke(d, [0], 'Paint', 3);
        s.paint(ROCK, 10 * CELL_SIZE, 10 * CELL_SIZE, { radius: 8 * CELL_SIZE, strength: 0.5, falloff: 0 });
        s.finish();
        const n = [...d.terrain![0]!.cellTexLeft].filter((r) => r === ROCK).length;
        expect(n).toBeGreaterThan(40);
        expect(n).toBeLessThan(170);
        const h = new TerrainStroke(d, [0], 'Hole');
        h.paint(TEX_HOLE, 0.5 * CELL_SIZE, 0.5 * CELL_SIZE, { radius: 100, strength: 1, falloff: 0 });
        h.finish();
        expect(d.terrain![0]!.cellTexLeft[0]).toBe(TEX_HOLE);
        expect(Number.isNaN(layerHeightAt(d.terrain![0]!, 0.5 * CELL_SIZE, 0.5 * CELL_SIZE))).toBe(true);
    });
});

describe('expand and add layer', () => {
    it('grows each edge with edge heights and textures', () => {
        const L = layer(0, 3, 2, 5, 5);
        L.heights[0] = 2000; // top-left vertex
        L.cellTexLeft[0] = L.cellTexRight[0] = ROCK;
        expect(vertexMaskBits(L)).toEqual({ tl: 1, tr: 2, bl: 4, br: 8 });
        const w = expandLayer(L, 'west', 2);
        expect([w.width, w.height, w.originX, w.originZ]).toEqual([5, 2, 3, 5]);
        expect(w.heights[0]).toBe(2000);
        expect(w.heights[1]).toBe(2000);
        expect(w.heights[2]).toBe(2000);
        expect(w.cellTexLeft[0]).toBe(ROCK);
        expect(w.cellTexLeft[1]).toBe(ROCK);
        expect(w.cellTexLeft[2]).toBe(ROCK);
        // The old west edge vertices are interior now.
        expect(w.vertexMask[1 * 6 + 2]).toBe(15);
        expect(w.vertexMask[0]).toBe(8);
        const s = expandLayer(L, 'south', 1);
        expect([s.width, s.height, s.originZ]).toEqual([3, 3, 5]);
        expect(s.heights.length).toBe(16);
        expect(() => expandLayer(layer(1, 2, 2, 0, 0), 'north', 1)).toThrow();
    });

    it('expand and add layer are undoable and survive JSON', () => {
        const d = docWith(layer(0, 4, 4), layer(1, 2, 2, 10, 10, 1000, ROCK));
        const st = new CommandStack(d);
        st.run(expandLayerCommand(d, 0, 'east', 3)!);
        expect(d.terrain![0]!.width).toBe(7);
        st.run(addLayerCommand(d, { originX: 20, originZ: 20, width: 3, height: 2, worldHeight: 500, ref: ROCK })!);
        expect(d.terrain!.length).toBe(3);
        const added = d.terrain![2]!;
        expect(added.index).toBe(2);
        expect(added.heights.every((h) => h === 500)).toBe(true);
        expect(added.lightColor).toBe(0xffeedd);
        expect(added.flags & 0x04).toBe(0);
        expect([...d.terrainChanged].sort()).toEqual([0, 2]);

        const json = JSON.parse(JSON.stringify(d.toJSON()));
        expect(json.terrain.layers).toHaveLength(2);
        const back = LevelDocument.fromJSON(json);
        expect(back.terrain).toBeNull();
        expect(back.hasPendingTerrain).toBe(true);
        back.attachTerrain([layer(0, 4, 4), layer(1, 2, 2, 10, 10, 1000, ROCK)]);
        expect(back.terrain!.map((L) => L.width)).toEqual([7, 2, 3]);
        expect([...back.terrainChanged].sort()).toEqual([0, 2]);

        st.undo();
        expect(d.terrain!.length).toBe(2);
        expect(d.terrainChanged.has(2)).toBe(false);
        st.undo();
        expect(d.terrain![0]!.width).toBe(4);
        expect(d.terrainChanged.size).toBe(0);
        expect(d.toJSON().terrain).toBeUndefined();
    });

    it('layer JSON round-trips exactly', () => {
        const L = layer(0, 3, 3);
        L.heights[5] = 1000 + 2 * 1234;
        L.cellTexRight[2] = ROCK;
        L.cellUv.set([1, 2, 3, 4, 5, 6, 7, 8], 8);
        const back = terrainLayerFromJson(JSON.parse(JSON.stringify(terrainLayerToJson(L))));
        expect(back).toEqual(L);
    });

    it('finds the layer under a point by height', () => {
        const t = [layer(0, 4, 4), layer(1, 4, 4, 0, 0, 6000)];
        expect(layerAt(t, CELL_SIZE, CELL_SIZE, 5900)).toBe(1);
        expect(layerAt(t, CELL_SIZE, CELL_SIZE, 1100)).toBe(0);
        expect(layerAt(t, 99 * CELL_SIZE, CELL_SIZE, 1000)).toBe(-1);
    });
});

function obj(id: number, x: number, y: number, z: number): LevelObject {
    return {
        id,
        class: `Obj${id}`,
        type: 0,
        kind: 'static',
        resource: '',
        model: '',
        transform: { pos: [x, y, z], rot: [0, 0, 0] },
        props: {},
    };
}

describe('objects ride the sculpted ground', () => {
    const c = 4 * CELL_SIZE;
    it('moves only objects standing on the edited layers, in the same undo step', () => {
        // Layer 1 is a roof 5000 above the ground at the same place.
        const d = docWith(layer(0, 8, 8), layer(1, 8, 8, 0, 0, 6000));
        d.insert(obj(1, c, 1000, c)); // on the ground
        d.insert(obj(2, c + 100, 1040, c)); // 40 above: still standing (tolerance 64)
        d.insert(obj(3, c, 1500, c + 100)); // flying / stacked on something
        d.insert(obj(4, c, 6000, c)); // on the roof layer
        d.insert(obj(5, 7.9 * CELL_SIZE, 1000, 7.9 * CELL_SIZE)); // on the ground, outside the brush
        d.insert(obj(6, c - 100, 1000, c)); // locked
        d.insert(obj(7, c, 780, c - 100)); // a tree sunk 220 into the ground
        d.insert(obj(8, c + 100, 300, c - 100)); // 700 under the ground (a logic object)
        expect(groundedObjects(d, [0]).map((g) => g.id)).toEqual([1, 2, 5, 6, 7]);
        expect(groundedObjects(d, [1]).map((g) => g.id)).toEqual([4]);

        const st = new CommandStack(d);
        const seen: number[] = [];
        const stroke = new TerrainStroke(d, [0], 'Raise', 1, {
            carryObjects: true,
            skipObject: (o) => o.id === 6,
            onObjects: (ids) => seen.push(...ids),
        });
        for (let i = 0; i < 10; i++) stroke.sculpt('raise', c, c, brush, 0.05);
        const L = d.terrain![0]!;
        const top = layerHeightAt(L, c, c);
        expect(top).toBeGreaterThan(1500);
        // Live: the object follows the ground during the stroke.
        expect(d.get(1)!.transform.pos[1]).toBe(top);
        expect(seen).toContain(1);
        const cmd = stroke.finish()!;
        expect(cmd.lifts.map((m) => m.id).sort()).toEqual([1, 2, 7]);
        st.run(cmd);
        expect(d.get(1)!.transform.pos[1]).toBe(top);
        expect(d.get(2)!.transform.pos[1]).toBeCloseTo(layerHeightAt(L, c + 100, c) + 40, 3);
        expect(d.get(3)!.transform.pos[1]).toBe(1500);
        expect(d.get(4)!.transform.pos[1]).toBe(6000);
        expect(d.get(5)!.transform.pos[1]).toBe(1000);
        expect(d.get(6)!.transform.pos[1]).toBe(1000);
        expect(d.get(7)!.transform.pos[1]).toBeCloseTo(layerHeightAt(L, c, c - 100) - 220, 3);
        expect(d.get(8)!.transform.pos[1]).toBe(300);
        // One undo puts the ground and the objects back; redo moves both again.
        st.undo();
        expect(layerHeightAt(d.terrain![0]!, c, c)).toBe(1000);
        expect(d.get(1)!.transform.pos[1]).toBe(1000);
        expect(d.get(2)!.transform.pos[1]).toBe(1040);
        st.redo();
        expect(d.get(1)!.transform.pos[1]).toBe(top);
    });

    it('leaves objects alone when carrying is off, and cancel puts them back', () => {
        const d = docWith(layer(0, 8, 8));
        d.insert(obj(1, c, 1000, c));
        const off = new TerrainStroke(d, [0], 'Raise');
        off.sculpt('raise', c, c, brush, 0.2);
        expect(off.finish()!.lifts).toEqual([]);
        expect(d.get(1)!.transform.pos[1]).toBe(1000);
        // The object is now buried under the raised ground: no longer standing on it, so it is not carried either.
        expect(groundedObjects(d, [0])).toEqual([]);

        d.setTerrainLayer(0, layer(0, 8, 8));
        const on = new TerrainStroke(d, [0], 'Lower', 2, { carryObjects: true });
        const before = layerHeightAt(d.terrain![0]!, c, c);
        on.sculpt('raise', c, c, brush, 0.2, { invert: true });
        expect(d.get(1)!.transform.pos[1]).toBeLessThan(1000);
        on.cancel();
        expect(layerHeightAt(d.terrain![0]!, c, c)).toBe(before);
        expect(d.get(1)!.transform.pos[1]).toBe(1000);
    });

    it('ramp and smooth carry objects too', () => {
        const d = docWith(layer(0, 8, 8));
        d.insert(obj(1, c, 1000, c));
        const s = new TerrainStroke(d, [0], 'Ramp', 1, { carryObjects: true });
        s.ramp([CELL_SIZE, 1000, c], [7 * CELL_SIZE, 4000, c], 2 * CELL_SIZE, 0);
        const ground = layerHeightAt(d.terrain![0]!, c, c);
        expect(ground).toBeGreaterThan(2000);
        expect(d.get(1)!.transform.pos[1]).toBeCloseTo(ground, 3);
        expect(s.finish()!.lifts).toHaveLength(1);
    });
});

describe('new layer visible list', () => {
    it('lists its own index first, then the template links without duplicates', () => {
        const tpl = layer(3, 2, 2);
        tpl.visible = [3, 7, 3, 9, 12];
        const L = makeLayer({ originX: 0, originZ: 0, width: 2, height: 2, worldHeight: 0, ref: GRASS }, 12, 99, tpl);
        expect(L.visible).toEqual([12, 3, 7, 9]);
        expect(
            makeLayer({ originX: 0, originZ: 0, width: 1, height: 1, worldHeight: 0, ref: GRASS }, 5, 1).visible,
        ).toEqual([5]);
    });
});
