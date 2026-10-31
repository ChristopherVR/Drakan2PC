/** Top-down "where is it" pictures of a save's positioned records (x right, z down; y is height and is ignored). */

export interface MapPoint {
    x: number;
    z: number;
    /** Save entry index. */
    entry: number;
    /** Group key; the colour derives from it. */
    group: string;
}

export interface MapBounds {
    minX: number;
    maxX: number;
    minZ: number;
    maxZ: number;
}

export function mapBounds(points: readonly MapPoint[]): MapBounds {
    if (!points.length) return { minX: 0, maxX: 1, minZ: 0, maxZ: 1 };
    let minX = Infinity;
    let maxX = -Infinity;
    let minZ = Infinity;
    let maxZ = -Infinity;
    for (const p of points) {
        minX = Math.min(minX, p.x);
        maxX = Math.max(maxX, p.x);
        minZ = Math.min(minZ, p.z);
        maxZ = Math.max(maxZ, p.z);
    }
    // Keep a square-ish aspect and some padding.
    const w = Math.max(maxX - minX, 1);
    const d = Math.max(maxZ - minZ, 1);
    const pad = Math.max(w, d) * 0.04;
    return { minX: minX - pad, maxX: minX + w + pad, minZ: minZ - pad, maxZ: minZ + d + pad };
}

export function groupColor(key: string): string {
    let hash = 2166136261;
    for (let i = 0; i < key.length; i++) hash = Math.imul(hash ^ key.charCodeAt(i), 16777619);
    return `hsl(${(hash >>> 0) % 360} 62% 56%)`;
}

function project(b: MapBounds, w: number, h: number, x: number, z: number): [number, number] {
    const sx = w / (b.maxX - b.minX);
    const sz = h / (b.maxZ - b.minZ);
    const s = Math.min(sx, sz);
    const ox = (w - (b.maxX - b.minX) * s) / 2;
    const oz = (h - (b.maxZ - b.minZ) * s) / 2;
    return [ox + (x - b.minX) * s, oz + (z - b.minZ) * s];
}

export interface MapStyle {
    /** Draw these on top, larger. */
    highlight?: readonly MapPoint[];
    selected?: number | null;
    player?: MapPoint | null;
    arokh?: MapPoint | null;
    /** One colour for every dot (a group thumbnail); otherwise coloured by group. */
    mono?: string;
    dot?: number;
}

export function drawMap(canvas: HTMLCanvasElement, all: readonly MapPoint[], b: MapBounds, style: MapStyle = {}): void {
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    const w = canvas.width;
    const h = canvas.height;
    ctx.clearRect(0, 0, w, h);
    const dot = style.dot ?? 1.6;
    for (const p of all) {
        const [px, pz] = project(b, w, h, p.x, p.z);
        ctx.fillStyle = style.mono ?? groupColor(p.group);
        ctx.globalAlpha = style.highlight ? 0.28 : 0.9;
        ctx.fillRect(px - dot / 2, pz - dot / 2, dot, dot);
    }
    ctx.globalAlpha = 1;
    for (const p of style.highlight ?? []) {
        const [px, pz] = project(b, w, h, p.x, p.z);
        ctx.fillStyle = groupColor(p.group);
        ctx.fillRect(px - dot, pz - dot, dot * 2, dot * 2);
    }
    const ring = (p: MapPoint, color: string, r: number): void => {
        const [px, pz] = project(b, w, h, p.x, p.z);
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.arc(px, pz, r, 0, Math.PI * 2);
        ctx.stroke();
    };
    if (style.player) ring(style.player, '#3fb950', 6);
    if (style.arokh) ring(style.arokh, '#d29922', 6);
    const sel = style.selected == null ? undefined : all.find((p) => p.entry === style.selected);
    if (sel) ring(sel, '#ffffff', 5);
}

/** The record nearest to a click on a map canvas (within a few pixels), or null. */
export function pickMap(
    canvas: HTMLCanvasElement,
    all: readonly MapPoint[],
    b: MapBounds,
    ev: { clientX: number; clientY: number },
): number | null {
    const r = canvas.getBoundingClientRect();
    const mx = ((ev.clientX - r.left) / r.width) * canvas.width;
    const mz = ((ev.clientY - r.top) / r.height) * canvas.height;
    let best: number | null = null;
    let bestD = 8 * 8;
    for (const p of all) {
        const [px, pz] = project(b, canvas.width, canvas.height, p.x, p.z);
        const d = (px - mx) ** 2 + (pz - mz) ** 2;
        if (d < bestD) {
            bestD = d;
            best = p.entry;
        }
    }
    return best;
}
