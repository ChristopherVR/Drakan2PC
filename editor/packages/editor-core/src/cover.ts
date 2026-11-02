import { zlibSync } from 'fflate';

/**
 * Campaign cover pictures made from nothing but numbers: a sky gradient, a sun or moon, layered mountain ridges in
 * haze and, optionally, a dragon in flight. No game asset is read, no canvas is needed (works headless and in a
 * worker), and the same palette + seed always gives the same picture. The game's title card shows it as the
 * campaign's picture (`previews/campaign-<id>.png`, docs/reference/modding.md "Campaign packs").
 */

export const COVER_WIDTH = 960;
export const COVER_HEIGHT = 540;

type Rgb = [number, number, number];

export interface CoverPalette {
    id: string;
    label: string;
    /** Sky from the top to the horizon. */
    sky: [Rgb, Rgb, Rgb];
    sun: Rgb;
    /** The nearest ridge colour; farther ridges blend toward the haze. */
    ridge: Rgb;
    haze: Rgb;
    stars: boolean;
}

export const COVER_PALETTES: readonly CoverPalette[] = [
    {
        id: 'dusk',
        label: 'Dusk',
        sky: [
            [38, 24, 62],
            [168, 78, 92],
            [246, 170, 96],
        ],
        sun: [255, 222, 160],
        ridge: [22, 14, 30],
        haze: [196, 110, 104],
        stars: false,
    },
    {
        id: 'ember',
        label: 'Ember',
        sky: [
            [30, 8, 6],
            [122, 30, 14],
            [236, 120, 40],
        ],
        sun: [255, 200, 110],
        ridge: [16, 6, 4],
        haze: [160, 58, 26],
        stars: false,
    },
    {
        id: 'frost',
        label: 'Frost',
        sky: [
            [28, 52, 92],
            [110, 150, 196],
            [214, 230, 240],
        ],
        sun: [255, 255, 246],
        ridge: [24, 36, 58],
        haze: [150, 178, 206],
        stars: false,
    },
    {
        id: 'forest',
        label: 'Forest',
        sky: [
            [34, 58, 60],
            [118, 150, 110],
            [226, 214, 150],
        ],
        sun: [255, 244, 196],
        ridge: [14, 28, 18],
        haze: [126, 150, 112],
        stars: false,
    },
    {
        id: 'night',
        label: 'Night',
        sky: [
            [6, 8, 22],
            [20, 30, 66],
            [62, 76, 120],
        ],
        sun: [232, 236, 255],
        ridge: [6, 8, 16],
        haze: [44, 54, 92],
        stars: true,
    },
];

export interface CoverOptions {
    palette: string;
    seed: number;
    /** Draw a dragon in flight (default true). */
    dragon?: boolean;
    width?: number;
    height?: number;
}

export const coverPalette = (id: string): CoverPalette => COVER_PALETTES.find((p) => p.id === id) ?? COVER_PALETTES[0]!;

/** Small seeded random numbers (mulberry32). */
function rng(seed: number): () => number {
    let a = seed >>> 0 || 0x9e3779b9;
    return () => {
        a = (a + 0x6d2b79f5) >>> 0;
        let t = a;
        t = Math.imul(t ^ (t >>> 15), t | 1);
        t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}

const mix = (a: Rgb, b: Rgb, t: number): Rgb => [
    a[0] + (b[0] - a[0]) * t,
    a[1] + (b[1] - a[1]) * t,
    a[2] + (b[2] - a[2]) * t,
];
const clamp01 = (v: number): number => (v < 0 ? 0 : v > 1 ? 1 : v);

/** A dragon seen from below, wings spread, in unit coordinates (x right, y down, about -1..1). */
const DRAGON: readonly [number, number][] = [
    [0, -0.42], // snout
    [0.06, -0.3],
    [0.07, -0.16], // neck
    [0.14, -0.1],
    [0.42, -0.26], // right wing, leading edge
    [0.78, -0.3],
    [1.0, -0.18], // wing tip
    [0.8, -0.1],
    [0.7, 0.02],
    [0.56, -0.02], // scalloped trailing edge
    [0.46, 0.1],
    [0.32, 0.04],
    [0.2, 0.14],
    [0.1, 0.16],
    [0.06, 0.34], // tail
    [0.03, 0.6],
    [0.08, 0.78],
    [0, 0.72],
    [-0.08, 0.78],
    [-0.03, 0.6],
    [-0.06, 0.34],
    [-0.1, 0.16],
    [-0.2, 0.14],
    [-0.32, 0.04],
    [-0.46, 0.1],
    [-0.56, -0.02],
    [-0.7, 0.02],
    [-0.8, -0.1],
    [-1.0, -0.18],
    [-0.78, -0.3],
    [-0.42, -0.26],
    [-0.14, -0.1],
    [-0.07, -0.16],
    [-0.06, -0.3],
];

function inPolygon(x: number, y: number, poly: readonly [number, number][]): boolean {
    let inside = false;
    for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
        const [xi, yi] = poly[i]!;
        const [xj, yj] = poly[j]!;
        if (yi > y !== yj > y && x < ((xj - xi) * (y - yi)) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
}

/** RGB pixels (row major, 3 bytes each) of a cover. */
export function renderCoverPixels(o: CoverOptions): { width: number; height: number; rgb: Uint8Array } {
    const w = o.width ?? COVER_WIDTH;
    const h = o.height ?? COVER_HEIGHT;
    const pal = coverPalette(o.palette);
    const r = rng(o.seed);
    const px = new Float32Array(w * h * 3);
    const put = (i: number, c: Rgb, a = 1): void => {
        px[i] = px[i]! + (c[0] - px[i]!) * a;
        px[i + 1] = px[i + 1]! + (c[1] - px[i + 1]!) * a;
        px[i + 2] = px[i + 2]! + (c[2] - px[i + 2]!) * a;
    };
    const horizon = h * (0.55 + r() * 0.1);
    // sky
    for (let y = 0; y < h; y++) {
        const t = clamp01(y / horizon);
        const c = t < 0.6 ? mix(pal.sky[0], pal.sky[1], t / 0.6) : mix(pal.sky[1], pal.sky[2], (t - 0.6) / 0.4);
        for (let x = 0; x < w; x++) put((y * w + x) * 3, c);
    }
    if (pal.stars) {
        const n = Math.round((w * h) / 2600);
        for (let i = 0; i < n; i++) {
            const x = Math.floor(r() * w);
            const y = Math.floor(r() * horizon * 0.85);
            put((y * w + x) * 3, [255, 255, 255], 0.35 + r() * 0.6);
        }
    }
    // sun or moon with a glow
    const sx = w * (0.2 + r() * 0.6);
    const sy = horizon * (0.35 + r() * 0.35);
    const sr = h * (0.06 + r() * 0.04);
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            const d = Math.hypot(x - sx, y - sy);
            const i = (y * w + x) * 3;
            if (d < sr) put(i, pal.sun, clamp01(sr - d));
            else put(i, pal.sun, 0.45 * Math.exp(-(d - sr) / (h * 0.12)));
        }
    }
    // mountain ridges, far to near
    const layers = 4;
    for (let l = 0; l < layers; l++) {
        const near = l / (layers - 1); // 0 far .. 1 near
        const base = horizon + (h - horizon) * (0.05 + near * 0.55) - h * 0.18 * (1 - near);
        const amp = h * (0.07 + 0.1 * (1 - near * 0.5));
        const waves = Array.from({ length: 5 }, (_, k) => ({
            f: ((k + 1) * (1.4 + r() * 1.6) * Math.PI * 2) / w,
            p: r() * Math.PI * 2,
            a: 1 / (k + 1) ** (1.2 + near * 0.3),
        }));
        const peaks = Array.from({ length: 2 + Math.floor(r() * 2) }, () => ({
            x: r() * w,
            s: w * (0.05 + r() * 0.1),
            a: amp * (0.6 + r() * 0.9),
        }));
        const col = mix(pal.haze, pal.ridge, 0.25 + near * 0.75);
        for (let x = 0; x < w; x++) {
            let v = 0;
            for (const wv of waves) v += Math.sin(x * wv.f + wv.p) * wv.a;
            let top = base - v * amp * 0.5;
            for (const pk of peaks) top -= pk.a * Math.max(0, 1 - Math.abs(x - pk.x) / pk.s);
            const y0 = Math.max(0, Math.floor(top));
            for (let y = y0; y < h; y++) {
                const edge = y === y0 ? clamp01(1 - (top - y0)) : 1;
                // a little haze at each ridge's foot
                const fog = clamp01((y - top) / (h * 0.5)) * 0.25 * (1 - near);
                put((y * w + x) * 3, mix(col, pal.haze, fog), edge);
            }
        }
    }
    // a dragon in flight
    if (o.dragon !== false) {
        const scale = h * (0.12 + r() * 0.08);
        const cx = w * (0.25 + r() * 0.5);
        const cy = horizon * (0.25 + r() * 0.3);
        const tilt = (r() - 0.5) * 0.6;
        const cos = Math.cos(tilt);
        const sin = Math.sin(tilt);
        const col = mix(pal.ridge, pal.sky[0], 0.2);
        const x0 = Math.max(0, Math.floor(cx - scale * 1.1));
        const x1 = Math.min(w - 1, Math.ceil(cx + scale * 1.1));
        const y0 = Math.max(0, Math.floor(cy - scale * 1.1));
        const y1 = Math.min(h - 1, Math.ceil(cy + scale * 1.1));
        for (let y = y0; y <= y1; y++) {
            for (let x = x0; x <= x1; x++) {
                // 2x2 samples: smooth edges
                let hit = 0;
                for (const [ox, oy] of [
                    [0.25, 0.25],
                    [0.75, 0.25],
                    [0.25, 0.75],
                    [0.75, 0.75],
                ] as const) {
                    const dx = (x + ox - cx) / scale;
                    const dy = (y + oy - cy) / scale;
                    if (inPolygon(dx * cos + dy * sin, -dx * sin + dy * cos, DRAGON)) hit++;
                }
                if (hit) put((y * w + x) * 3, col, hit / 4);
            }
        }
    }
    // soft vignette
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            const dx = x / w - 0.5;
            const dy = y / h - 0.5;
            const v = clamp01((Math.hypot(dx, dy) - 0.35) / 0.4) * 0.45;
            put((y * w + x) * 3, [0, 0, 0], v);
        }
    }
    const rgb = new Uint8Array(w * h * 3);
    for (let i = 0; i < rgb.length; i++) rgb[i] = Math.max(0, Math.min(255, Math.round(px[i]!)));
    return { width: w, height: h, rgb };
}

const CRC_TABLE = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        t[n] = c >>> 0;
    }
    return t;
})();
function crc32(parts: Uint8Array[]): number {
    let c = 0xffffffff;
    for (const p of parts) for (let i = 0; i < p.length; i++) c = CRC_TABLE[(c ^ p[i]!) & 0xff]! ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}

/** An RGB (3 bytes) or RGBA (4 bytes) image as PNG bytes. */
export function encodePng(width: number, height: number, pixels: Uint8Array, channels: 3 | 4 = 3): Uint8Array {
    const stride = width * channels;
    const raw = new Uint8Array((stride + 1) * height);
    for (let y = 0; y < height; y++) {
        raw[y * (stride + 1)] = 0; // filter: none
        raw.set(pixels.subarray(y * stride, (y + 1) * stride), y * (stride + 1) + 1);
    }
    const chunks: Uint8Array[] = [];
    const chunk = (type: string, data: Uint8Array): void => {
        const head = new Uint8Array(8);
        const dv = new DataView(head.buffer);
        dv.setUint32(0, data.length);
        for (let i = 0; i < 4; i++) head[4 + i] = type.charCodeAt(i);
        const tail = new Uint8Array(4);
        new DataView(tail.buffer).setUint32(0, crc32([head.subarray(4), data]));
        chunks.push(head, data, tail);
    };
    const ihdr = new Uint8Array(13);
    const dv = new DataView(ihdr.buffer);
    dv.setUint32(0, width);
    dv.setUint32(4, height);
    ihdr[8] = 8;
    ihdr[9] = channels === 4 ? 6 : 2;
    chunk('IHDR', ihdr);
    chunk('IDAT', zlibSync(raw, { level: 6 }));
    chunk('IEND', new Uint8Array(0));
    const sig = new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10]);
    const out = new Uint8Array(sig.length + chunks.reduce((a, c) => a + c.length, 0));
    out.set(sig, 0);
    let o = sig.length;
    for (const c of chunks) {
        out.set(c, o);
        o += c.length;
    }
    return out;
}

/** A cover picture as PNG bytes (960 x 540 unless sized). */
export function renderCoverPng(o: CoverOptions): Uint8Array {
    const { width, height, rgb } = renderCoverPixels(o);
    return encodePng(width, height, rgb, 3);
}

/** Width and height of a PNG, or null when the bytes are not one. */
export function pngSize(png: Uint8Array): { width: number; height: number } | null {
    if (png.length < 24 || png[0] !== 137 || png[1] !== 80 || png[2] !== 78 || png[3] !== 71) return null;
    const dv = new DataView(png.buffer, png.byteOffset, png.byteLength);
    return { width: dv.getUint32(16), height: dv.getUint32(20) };
}
