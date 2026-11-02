// Synthetic terrain only; no game bytes.
import { describe, expect, it } from 'vitest';
import { parsePs2Terrain, ps2BlobBuilder, writePs2Terrain, type TerrainLayer } from '@drakan/formats';
import { LevelDocument, hasTerrainEdits, terrainPackEntry } from '../src/index.ts';

function layer(index: number, w: number, h: number, ox: number): TerrainLayer {
    const nv = (w + 1) * (h + 1);
    return {
        index,
        id: 10 + index,
        width: w,
        height: h,
        type: 0,
        originX: ox,
        originZ: 4,
        worldHeight: 500,
        name: `layer ${index}`,
        flags: 0,
        lightDirection: 0,
        lightAscension: 0,
        lightColor: 0xffffff,
        ambientColor: 0x202020,
        lightDropoff: 0,
        visible: [],
        heights: new Float32Array(nv).fill(500),
        vertexFlags: new Uint16Array(nv),
        vertexMask: new Uint8Array(nv).fill(15),
        cellFlags: new Uint16Array(w * h).fill(0xff00),
        cellTexLeft: new Uint32Array(w * h).fill(0x90001),
        cellTexRight: new Uint32Array(w * h).fill(0x90001),
        cellUv: new Uint16Array(w * h * 8),
        source: 'ps2',
    };
}

const disc = (): Uint8Array => writePs2Terrain([layer(0, 4, 4, 0), layer(1, 3, 2, 4)], ps2BlobBuilder(0x18));
const ENTRY = { type: 1, id: 0, sub: 0 };

describe('terrainPackEntry', () => {
    it('is null without terrain edits', () => {
        const doc = new LevelDocument();
        doc.attachTerrain(parsePs2Terrain(disc()));
        expect(hasTerrainEdits(doc)).toBe(false);
        expect(terrainPackEntry('LEVEL.LVL', ENTRY, disc(), doc)).toBeNull();
    });

    it('rebuilds the 0x0001 entry with the edited heights', () => {
        const original = disc();
        const doc = new LevelDocument();
        doc.attachTerrain(parsePs2Terrain(original));
        doc.writableLayer(1).heights[3] = 900;
        const t = terrainPackEntry('LEVEL.LVL', ENTRY, original, doc)!;
        expect(t.change.file).toBe('LEVEL.LVL');
        expect(t.change.entry).toEqual(ENTRY);
        expect(parsePs2Terrain(t.change.data)[1]!.heights[3]).toBe(900);
        expect(t.line).toBe('1 terrain layer(s) edited');
    });

    it('attaches the disc terrain to a document read from JSON before rebuilding', () => {
        const original = disc();
        const a = new LevelDocument();
        a.attachTerrain(parsePs2Terrain(original));
        a.writableLayer(0).heights[0] = 700;
        const b = LevelDocument.fromJSON(JSON.stringify(a.toJSON()));
        expect(b.terrain).toBeNull();
        expect(hasTerrainEdits(b)).toBe(true);
        const t = terrainPackEntry('LEVEL.LVL', ENTRY, original, b)!;
        expect(parsePs2Terrain(t.change.data)[0]!.heights[0]).toBe(700);
    });
});
