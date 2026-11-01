// Material resolver backed by the PBR pack of analysis/scripts/pbr_pack.py (docs/reference/hd-textures.md section 10):
// `Materials/M_<Txd>_<id>.json` (schema 1) plus `Textures/T_<Txd>_<id>_{BC,N,ORM}.png`. Textures are found by the
// JSON's `sources` (`<TXD path>#<id>`, forward slashes, no language folder).

import type { MaterialResolver, ResolvedMaterial, TextureSource } from './levelglb.ts';

interface PackJson {
    schema: number;
    name: string;
    sources?: string[];
    blendMode?: ResolvedMaterial['blendMode'];
    twoSided?: boolean;
    opacityMaskClip?: number | null;
    emissive?: unknown;
    textures?: { baseColor?: string; normal?: string; orm?: string };
}

/** `ENGLISH\LEVELS\LEVEL1NA\LEVEL1NA.TXD` + id -> `LEVELS/LEVEL1NA/LEVEL1NA.TXD#id` (the pack's source key). */
export function pbrSourceKey(txd: string, ref: number): string {
    let p = txd.replace(/\\/g, '/').toUpperCase();
    p = p.replace(/^(ENGLISH|FRENCH|GERMAN|ITALIAN|SPANISH)\//, '');
    return `${p}#${ref & 0xffff}`;
}

const base = (p: string): string => p.replace(/^.*\//, '').replace(/\.png$/i, '');

export interface PbrPackOptions {
    /** The pack's normals are DirectX (green down); glTF wants OpenGL, so the green channel is flipped. */
    flipGreen?: (png: Uint8Array) => Promise<Uint8Array>;
}

export class PbrPack {
    private readonly bySource = new Map<string, PackJson>();
    readonly materials: number;
    readonly missing = new Set<string>();

    /** `files`: pack-relative path (`Materials/M_x.json`) to bytes. */
    constructor(
        private readonly files: ReadonlyMap<string, Uint8Array>,
        private readonly opt: PbrPackOptions = {},
    ) {
        let n = 0;
        for (const [path, bytes] of files) {
            if (!/^Materials\/M_.*\.json$/i.test(path)) continue;
            try {
                const j = JSON.parse(new TextDecoder().decode(bytes)) as PackJson;
                if (j.schema !== 1) continue;
                n++;
                for (const s of j.sources ?? []) this.bySource.set(s.toUpperCase(), j);
            } catch {
                /* skip a broken file */
            }
        }
        this.materials = n;
    }

    /** Async lookups are not possible in a `MaterialResolver`, so images are prepared first with `prepare`. */
    private readonly prepared = new Map<string, ResolvedMaterial>();

    /** Resolve and load (flip normals) every material the sources need; call before `buildLevelGltf`. */
    async prepare(sources: Iterable<TextureSource>): Promise<void> {
        for (const s of sources) {
            if (!s.txd) continue;
            const key = pbrSourceKey(s.txd, s.ref);
            if (this.prepared.has(key)) continue;
            const j = this.bySource.get(key);
            if (!j) {
                this.missing.add(key);
                continue;
            }
            const get = (p?: string): Uint8Array | null => (p ? (this.files.get(p) ?? null) : null);
            const t = j.textures ?? {};
            const bc = get(t.baseColor);
            const n = get(t.normal);
            const orm = get(t.orm);
            const out: ResolvedMaterial = {
                name: j.name,
                blendMode: j.blendMode,
                opacityMaskClip: j.opacityMaskClip ?? null,
                twoSided: j.twoSided,
                emissive: !!j.emissive,
            };
            if (bc && t.baseColor) out.baseColor = { name: base(t.baseColor), png: bc };
            if (n && t.normal) {
                const png = this.opt.flipGreen ? await this.opt.flipGreen(n) : n;
                out.normal = { name: base(t.normal), png };
            }
            if (orm && t.orm) out.orm = { name: base(t.orm), png: orm };
            this.prepared.set(key, out);
        }
    }

    resolver(): MaterialResolver {
        return (s) => (s.txd ? (this.prepared.get(pbrSourceKey(s.txd, s.ref)) ?? null) : null);
    }
}

/** Browser: flip the green channel of a PNG through a canvas (DirectX to OpenGL normals). */
export async function flipGreenInBrowser(png: Uint8Array): Promise<Uint8Array> {
    const bmp = await createImageBitmap(new Blob([png as BlobPart], { type: 'image/png' }));
    const c = new OffscreenCanvas(bmp.width, bmp.height);
    const g = c.getContext('2d')!;
    g.drawImage(bmp, 0, 0);
    const d = g.getImageData(0, 0, bmp.width, bmp.height);
    for (let i = 1; i < d.data.length; i += 4) d.data[i] = 255 - d.data[i]!;
    g.putImageData(d, 0, 0);
    return new Uint8Array(await (await c.convertToBlob({ type: 'image/png' })).arrayBuffer());
}
