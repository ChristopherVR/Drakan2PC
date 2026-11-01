// A small glTF 2.0 / GLB writer and a PNG encoder. No dependencies and no browser-only APIs (CompressionStream exists in
// browsers and Node 18+, with a stored-block fallback).

export const COMPONENT = { BYTE: 5120, UBYTE: 5121, SHORT: 5122, USHORT: 5123, UINT: 5125, FLOAT: 5126 } as const;
export const TARGET = { ARRAY_BUFFER: 34962, ELEMENT_ARRAY_BUFFER: 34963 } as const;

export type AccessorType = 'SCALAR' | 'VEC2' | 'VEC3' | 'VEC4' | 'MAT4';
const COMPONENTS: Record<AccessorType, number> = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 };

type TypedArray = Float32Array | Uint16Array | Uint32Array | Uint8Array;

/** glTF JSON is open-ended; the builder keeps it loosely typed. */
// eslint-disable-next-line @typescript-eslint/no-explicit-any
export type Json = any;

export interface GltfDoc {
    asset: { version: string; generator: string; extras?: Json };
    scene: number;
    scenes: { name?: string; nodes: number[] }[];
    nodes: Json[];
    meshes: Json[];
    materials: Json[];
    textures: Json[];
    images: Json[];
    samplers: Json[];
    skins: Json[];
    animations: Json[];
    cameras: Json[];
    accessors: Json[];
    bufferViews: Json[];
    buffers: Json[];
    extensionsUsed?: string[];
    extensions?: Json;
    extras?: Json;
}

const EMPTY_DROPPED = [
    'meshes',
    'materials',
    'textures',
    'images',
    'samplers',
    'skins',
    'animations',
    'cameras',
    'accessors',
    'bufferViews',
    'buffers',
    'nodes',
];

/** Collects binary data and glTF objects; `toGlb` writes the container. */
export class GltfBuilder {
    readonly doc: GltfDoc = {
        asset: { version: '2.0', generator: 'Drakan editor (Unreal export)' },
        scene: 0,
        scenes: [{ name: 'Level', nodes: [] }],
        nodes: [],
        meshes: [],
        materials: [],
        textures: [],
        images: [],
        samplers: [],
        skins: [],
        animations: [],
        cameras: [],
        accessors: [],
        bufferViews: [],
        buffers: [],
    };
    private readonly chunks: Uint8Array[] = [];
    private size = 0;
    private readonly used = new Set<string>();

    useExtension(name: string): void {
        this.used.add(name);
    }

    /** Append bytes (4-byte aligned) as a buffer view; returns its index. */
    addBufferView(bytes: Uint8Array, target?: number): number {
        const pad = (4 - (this.size % 4)) % 4;
        if (pad) {
            this.chunks.push(new Uint8Array(pad));
            this.size += pad;
        }
        const view: Json = { buffer: 0, byteOffset: this.size, byteLength: bytes.byteLength };
        if (target !== undefined) view.target = target;
        this.chunks.push(bytes);
        this.size += bytes.byteLength;
        this.doc.bufferViews.push(view);
        return this.doc.bufferViews.length - 1;
    }

    /** Add typed data as a buffer view plus accessor; `bounds` writes min/max (required for positions and times). */
    addAccessor(
        data: TypedArray,
        type: AccessorType,
        opts: { target?: number; normalized?: boolean; bounds?: boolean } = {},
    ): number {
        const comps = COMPONENTS[type];
        const componentType =
            data instanceof Float32Array
                ? COMPONENT.FLOAT
                : data instanceof Uint32Array
                  ? COMPONENT.UINT
                  : data instanceof Uint16Array
                    ? COMPONENT.USHORT
                    : COMPONENT.UBYTE;
        const bytes = new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
        const view = this.addBufferView(bytes, opts.target);
        const count = data.length / comps;
        const acc: Json = { bufferView: view, componentType, count, type };
        if (opts.normalized) acc.normalized = true;
        if (opts.bounds && count > 0) {
            const min = new Array<number>(comps).fill(Infinity);
            const max = new Array<number>(comps).fill(-Infinity);
            for (let i = 0; i < data.length; i++) {
                const c = i % comps;
                const v = data[i]!;
                if (v < min[c]!) min[c] = v;
                if (v > max[c]!) max[c] = v;
            }
            acc.min = min;
            acc.max = max;
        }
        this.doc.accessors.push(acc);
        return this.doc.accessors.length - 1;
    }

    addNode(node: Json): number {
        this.doc.nodes.push(node);
        return this.doc.nodes.length - 1;
    }

    /** The JSON as it will be written (empty arrays dropped, extensions listed). */
    json(): Json {
        const d: Json = { ...this.doc };
        for (const k of EMPTY_DROPPED) if (!d[k].length) delete d[k];
        if (this.size) d.buffers = [{ byteLength: this.size + ((4 - (this.size % 4)) % 4) }];
        if (this.used.size) d.extensionsUsed = [...this.used].sort();
        return d;
    }

    /** Pack the JSON and binary chunks into a .glb. */
    toGlb(): Uint8Array {
        const d = this.json();
        const bin = new Uint8Array(this.size + ((4 - (this.size % 4)) % 4));
        let at = 0;
        for (const c of this.chunks) {
            bin.set(c, at);
            at += c.byteLength;
        }
        const json = new TextEncoder().encode(JSON.stringify(d));
        const jsonLen = json.length + ((4 - (json.length % 4)) % 4);
        const total = 12 + 8 + jsonLen + (bin.length ? 8 + bin.length : 0);
        const out = new Uint8Array(total);
        const dv = new DataView(out.buffer);
        dv.setUint32(0, 0x46546c67, true); // glTF
        dv.setUint32(4, 2, true);
        dv.setUint32(8, total, true);
        dv.setUint32(12, jsonLen, true);
        dv.setUint32(16, 0x4e4f534a, true); // JSON
        out.set(json, 20);
        out.fill(0x20, 20 + json.length, 20 + jsonLen);
        if (bin.length) {
            const o = 20 + jsonLen;
            dv.setUint32(o, bin.length, true);
            dv.setUint32(o + 4, 0x004e4942, true); // BIN
            out.set(bin, o + 8);
        }
        return out;
    }
}

// ---- PNG ----------------------------------------------------------------------------------------------------------

const crcTable = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        t[n] = c >>> 0;
    }
    return t;
})();

export function crc32(b: Uint8Array, start = 0, end = b.length): number {
    let c = 0xffffffff;
    for (let i = start; i < end; i++) c = crcTable[(c ^ b[i]!) & 0xff]! ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}

function adler32(b: Uint8Array): number {
    let a = 1;
    let s = 0;
    for (let i = 0; i < b.length; i++) {
        a = (a + b[i]!) % 65521;
        s = (s + a) % 65521;
    }
    return ((s << 16) | a) >>> 0;
}

/** zlib stream of stored (uncompressed) blocks: the fallback when CompressionStream is missing. */
function zlibStored(raw: Uint8Array): Uint8Array {
    const blocks = Math.max(1, Math.ceil(raw.length / 65535));
    const out = new Uint8Array(2 + raw.length + blocks * 5 + 4);
    out[0] = 0x78;
    out[1] = 0x01;
    let o = 2;
    for (let i = 0; i < blocks; i++) {
        const n = Math.min(65535, raw.length - i * 65535);
        out[o++] = i === blocks - 1 ? 1 : 0;
        out[o++] = n & 0xff;
        out[o++] = n >> 8;
        out[o++] = ~n & 0xff;
        out[o++] = (~n >> 8) & 0xff;
        out.set(raw.subarray(i * 65535, i * 65535 + n), o);
        o += n;
    }
    new DataView(out.buffer).setUint32(o, adler32(raw), false);
    return out;
}

async function zlibDeflate(raw: Uint8Array): Promise<Uint8Array> {
    const CS = (globalThis as Json).CompressionStream;
    if (!CS) return zlibStored(raw);
    const stream = new Blob([raw as BlobPart]).stream().pipeThrough(new CS('deflate'));
    return new Uint8Array(await new Response(stream).arrayBuffer());
}

/**
 * Encode RGBA8 pixels as a PNG. `bottomUp` = row 0 is the bottom row (the editor's decoded textures, GL order): rows are
 * written reversed so the PNG's first row is the top, which is what glTF UVs (origin top left) expect.
 */
export async function encodePng(rgba: Uint8Array, width: number, height: number, bottomUp = true): Promise<Uint8Array> {
    const stride = width * 4;
    const raw = new Uint8Array((stride + 1) * height);
    for (let y = 0; y < height; y++) {
        const src = bottomUp ? height - 1 - y : y;
        raw.set(rgba.subarray(src * stride, src * stride + stride), y * (stride + 1) + 1);
    }
    const z = await zlibDeflate(raw);
    const out = new Uint8Array(8 + 25 + 12 + z.length + 12);
    out.set([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
    const dv = new DataView(out.buffer);
    let o = 8;
    const chunk = (type: string, data: Uint8Array): void => {
        dv.setUint32(o, data.length, false);
        for (let i = 0; i < 4; i++) out[o + 4 + i] = type.charCodeAt(i);
        out.set(data, o + 8);
        dv.setUint32(o + 8 + data.length, crc32(out, o + 4, o + 8 + data.length), false);
        o += 12 + data.length;
    };
    const ihdr = new Uint8Array(13);
    const hv = new DataView(ihdr.buffer);
    hv.setUint32(0, width, false);
    hv.setUint32(4, height, false);
    ihdr[8] = 8;
    ihdr[9] = 6; // RGBA
    chunk('IHDR', ihdr);
    chunk('IDAT', z);
    chunk('IEND', new Uint8Array(0));
    return out;
}
