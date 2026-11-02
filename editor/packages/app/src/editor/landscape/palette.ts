// The paint palette ("biome"): the ground textures the level already uses, with a picture and a plain name each.
// Terrain textures carry no names on the disc, so the name describes the picture's colour ("Grass", "Sand", "Grey
// stone"); the texture id stays in the tooltip.

import { terrainPalette } from '@drakan/editor-core';
import { texDb, texId, type TerrainLayer } from '@drakan/formats';
import type { SceneTexture } from '../../scene/decode.ts';

export interface Swatch {
    ref: number;
    name: string;
    /** Cells that use it, and the share of all ground cells (0..1). */
    cells: number;
    share: number;
    /** PNG data URL ('' when the texture did not decode). */
    url: string;
    /** Texture file it comes from (tooltip). */
    file: string;
}

/** Average colour of an RGBA8 image (sampled). */
export function averageColor(rgba: Uint8Array): [number, number, number] {
    let r = 0;
    let g = 0;
    let b = 0;
    let n = 0;
    const step = Math.max(4, Math.floor(rgba.length / 4 / 2048) * 4);
    for (let i = 0; i + 3 < rgba.length; i += step) {
        r += rgba[i]!;
        g += rgba[i + 1]!;
        b += rgba[i + 2]!;
        n++;
    }
    return n ? [r / n, g / n, b / n] : [128, 128, 128];
}

/** A plain-language name for a ground texture from its average colour. */
export function colorName([r, g, b]: [number, number, number]): string {
    const max = Math.max(r, g, b) / 255;
    const min = Math.min(r, g, b) / 255;
    const l = (max + min) / 2;
    const d = max - min;
    const s = d === 0 ? 0 : d / (1 - Math.abs(2 * l - 1));
    let hue = 0;
    if (d > 0) {
        const R = r / 255;
        const G = g / 255;
        const B = b / 255;
        if (max === R) hue = 60 * (((G - B) / d) % 6);
        else if (max === G) hue = 60 * ((B - R) / d + 2);
        else hue = 60 * ((R - G) / d + 4);
        if (hue < 0) hue += 360;
    }
    if (s < 0.14 || d < 0.06) return l < 0.22 ? 'Dark stone' : l < 0.58 ? 'Grey stone' : 'Pale stone';
    if (hue < 18 || hue >= 330) return l < 0.35 ? 'Red earth' : 'Red rock';
    if (hue < 45) return l < 0.32 ? 'Dark earth' : l < 0.5 ? 'Earth' : 'Sand';
    if (hue < 70) return l < 0.35 ? 'Moss' : l < 0.55 ? 'Dry grass' : 'Pale sand';
    if (hue < 165) return l < 0.25 ? 'Dark grass' : 'Grass';
    if (hue < 260) return l < 0.35 ? 'Cold stone' : 'Ice';
    return 'Violet rock';
}

function swatchUrl(t: SceneTexture, size: number): string {
    if (typeof document === 'undefined') return '';
    const full = document.createElement('canvas');
    full.width = t.width;
    full.height = t.height;
    const fc = full.getContext('2d');
    if (!fc) return '';
    const img = new ImageData(new Uint8ClampedArray(t.rgba), t.width, t.height);
    fc.putImageData(img, 0, 0);
    const c = document.createElement('canvas');
    c.width = c.height = size;
    const cx = c.getContext('2d');
    if (!cx) return '';
    // Rows are stored bottom-up (GL): flip so the swatch looks like the ground.
    cx.translate(0, size);
    cx.scale(1, -1);
    cx.drawImage(full, 0, 0, size, size);
    return c.toDataURL('image/png');
}

const urlMemo = new Map<string, string>();

/**
 * Swatches for the level's terrain, most used first. `keep` are refs that stay in the palette even when no cell uses
 * them any more (the disc level's own set, so painting a texture away does not remove it). Names are made unique in
 * ref order ("Grass", "Grass 2"), so they do not change while painting.
 */
export function buildSwatches(
    layers: readonly TerrainLayer[],
    textures: readonly SceneTexture[],
    keep: Iterable<number> = [],
    size = 56,
): Swatch[] {
    const byRef = new Map(textures.map((t) => [t.ref, t]));
    const counts = new Map(terrainPalette(layers).map((p) => [p.ref, p.cells]));
    for (const r of keep) if (!counts.has(r)) counts.set(r, 0);
    const total = [...counts.values()].reduce((n, c) => n + c, 0) || 1;
    const seen = new Map<string, number>();
    const out = [...counts.keys()]
        .sort((a, b) => a - b)
        .map((ref): Swatch => {
            const t = byRef.get(ref);
            let name = t ? colorName(averageColor(t.rgba)) : 'Untextured';
            const n = (seen.get(name) ?? 0) + 1;
            seen.set(name, n);
            if (n > 1) name = `${name} ${n}`;
            const file =
                t?.txd
                    ?.split('/')
                    .pop()
                    ?.replace(/\.TXD$/i, '') ?? `database ${texDb(ref)}`;
            const key = `${file}:${ref}:${size}`;
            let url = urlMemo.get(key);
            if (url === undefined) {
                url = t ? swatchUrl(t, size) : '';
                urlMemo.set(key, url);
                if (urlMemo.size > 400) urlMemo.delete(urlMemo.keys().next().value!);
            }
            const cells = counts.get(ref) ?? 0;
            return { ref, name, cells, share: cells / total, url, file: `${file}, texture ${texId(ref)}` };
        });
    return out.sort((a, b) => b.cells - a.cells || a.ref - b.ref);
}
