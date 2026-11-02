import type { TerrainLayer, TerrainLayerDef } from '@drakan/formats';

/**
 * Compact JSON form of edited terrain layers (autosave and project files). Only layers the document changed or appended
 * are stored; the rest come from the disc again when the level opens (`LevelDocument.attachTerrain`). Arrays are base64
 * of their little-endian bytes; heights are stored as the raw u16 the LVL keeps (`height = worldHeight + (raw - 0x8000) * 2`).
 */
export interface TerrainLayerJson {
    def: TerrainLayerDef;
    source: 'ps2' | 'pc';
    /** u16 raw heights. */
    heights: string;
    vertexFlags: string;
    vertexMask: string;
    cellFlags: string;
    cellTexLeft: string;
    /** Absent when every cell's right triangle uses its left ref (always on PS2). */
    cellTexRight?: string;
    cellUv: string;
}

export interface TerrainJson {
    /** Layers the disc level had when the edits were made (indices at or past it were appended). */
    baseCount: number;
    layers: TerrainLayerJson[];
}

function toB64(bytes: Uint8Array): string {
    let s = '';
    for (let i = 0; i < bytes.length; i += 0x8000) {
        s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
    }
    return btoa(s);
}

function fromB64(s: string): Uint8Array {
    const bin = atob(s);
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
}

const bytesOf = (a: ArrayBufferView): Uint8Array => new Uint8Array(a.buffer, a.byteOffset, a.byteLength);
/** Copy into a fresh aligned buffer (base64 output is not aligned for 16/32-bit views). */
const aligned = (b: Uint8Array): ArrayBuffer => b.slice().buffer as ArrayBuffer;

/** Raw u16 of an absolute height (clamped, rounded). */
export function rawOfHeight(worldHeight: number, h: number): number {
    const r = Math.round((h - worldHeight) / 2) + 0x8000;
    return r < 0 ? 0 : r > 0xffff ? 0xffff : r;
}

export function heightOfRaw(worldHeight: number, raw: number): number {
    return worldHeight + (raw - 0x8000) * 2;
}

export function defOf(L: TerrainLayer): TerrainLayerDef {
    return {
        index: L.index,
        id: L.id,
        width: L.width,
        height: L.height,
        type: L.type,
        originX: L.originX,
        originZ: L.originZ,
        worldHeight: L.worldHeight,
        name: L.name,
        flags: L.flags,
        lightDirection: L.lightDirection,
        lightAscension: L.lightAscension,
        lightColor: L.lightColor,
        ambientColor: L.ambientColor,
        lightDropoff: L.lightDropoff,
        visible: [...L.visible],
    };
}

/** Deep copy (fresh typed arrays). */
export function cloneTerrainLayer(L: TerrainLayer): TerrainLayer {
    return {
        ...defOf(L),
        heights: L.heights.slice(),
        vertexFlags: L.vertexFlags.slice(),
        vertexMask: L.vertexMask.slice(),
        cellFlags: L.cellFlags.slice(),
        cellTexLeft: L.cellTexLeft.slice(),
        cellTexRight: L.cellTexRight.slice(),
        cellUv: L.cellUv.slice(),
        source: L.source,
    };
}

export function terrainLayerToJson(L: TerrainLayer): TerrainLayerJson {
    const raw = new Uint16Array(L.heights.length);
    for (let i = 0; i < raw.length; i++) raw[i] = rawOfHeight(L.worldHeight, L.heights[i]!);
    let sameRight = true;
    for (let i = 0; i < L.cellTexLeft.length && sameRight; i++) sameRight = L.cellTexLeft[i] === L.cellTexRight[i];
    return {
        def: defOf(L),
        source: L.source,
        heights: toB64(bytesOf(raw)),
        vertexFlags: toB64(bytesOf(L.vertexFlags)),
        vertexMask: toB64(bytesOf(L.vertexMask)),
        cellFlags: toB64(bytesOf(L.cellFlags)),
        cellTexLeft: toB64(bytesOf(L.cellTexLeft)),
        ...(sameRight ? {} : { cellTexRight: toB64(bytesOf(L.cellTexRight)) }),
        cellUv: toB64(bytesOf(L.cellUv)),
    };
}

export function terrainLayerFromJson(j: TerrainLayerJson): TerrainLayer {
    const d = j.def;
    const nv = (d.width + 1) * (d.height + 1);
    const nc = d.width * d.height;
    const sized = <T extends { length: number }>(a: T, n: number, what: string): T => {
        if (a.length !== n) throw new Error(`terrain layer ${d.index}: ${what} has ${a.length} entries, expected ${n}`);
        return a;
    };
    const raw = sized(new Uint16Array(aligned(fromB64(j.heights))), nv, 'heights');
    const heights = new Float32Array(nv);
    for (let i = 0; i < nv; i++) heights[i] = heightOfRaw(d.worldHeight, raw[i]!);
    const left = sized(new Uint32Array(aligned(fromB64(j.cellTexLeft))), nc, 'cell textures');
    return {
        ...d,
        visible: [...d.visible],
        heights,
        vertexFlags: sized(new Uint16Array(aligned(fromB64(j.vertexFlags))), nv, 'vertex flags'),
        vertexMask: sized(new Uint8Array(aligned(fromB64(j.vertexMask))), nv, 'vertex masks'),
        cellFlags: sized(new Uint16Array(aligned(fromB64(j.cellFlags))), nc, 'cell flags'),
        cellTexLeft: left,
        cellTexRight: j.cellTexRight
            ? sized(new Uint32Array(aligned(fromB64(j.cellTexRight))), nc, 'cells')
            : left.slice(),
        cellUv: sized(new Uint16Array(aligned(fromB64(j.cellUv))), nc * 8, 'cell UVs'),
        source: j.source,
    };
}
