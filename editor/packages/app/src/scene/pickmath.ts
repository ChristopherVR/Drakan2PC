// Pure picking math shared by the viewport (no three.js, no DOM): pointer to NDC, projected points to client pixels,
// nearest-point and rectangle tests. Kept separate so it is unit tested without a renderer.

export interface RectLike {
    left: number;
    top: number;
    width: number;
    height: number;
}

/** Client pixel to normalised device coordinates (-1..1, y up) inside `rect`. */
export function clientToNdc(rect: RectLike, x: number, y: number): [number, number] {
    return [((x - rect.left) / rect.width) * 2 - 1, -((y - rect.top) / rect.height) * 2 + 1];
}

/** NDC to client pixel (inverse of `clientToNdc`). */
export function ndcToClient(rect: RectLike, nx: number, ny: number): [number, number] {
    return [((nx + 1) / 2) * rect.width + rect.left, ((1 - ny) / 2) * rect.height + rect.top];
}

export interface ScreenPoint {
    id: number;
    /** Client pixels. */
    x: number;
    y: number;
}

/** The point nearest to (x, y) within `maxPx` pixels, or null. Ties keep the first. */
export function nearestScreenPoint(points: Iterable<ScreenPoint>, x: number, y: number, maxPx: number): number | null {
    let best = maxPx * maxPx;
    let id: number | null = null;
    for (const p of points) {
        const d = (p.x - x) ** 2 + (p.y - y) ** 2;
        if (d < best) {
            best = d;
            id = p.id;
        }
    }
    return id;
}

/** Ids of the points inside the rectangle spanned by two corners (any order, edges included). */
export function pointsInRect(points: Iterable<ScreenPoint>, x0: number, y0: number, x1: number, y1: number): number[] {
    const lx = Math.min(x0, x1);
    const hx = Math.max(x0, x1);
    const ly = Math.min(y0, y1);
    const hy = Math.max(y0, y1);
    const out: number[] = [];
    for (const p of points) if (p.x >= lx && p.x <= hx && p.y >= ly && p.y <= hy) out.push(p.id);
    return out;
}

/** The nearest of several per-mesh hits (each already the closest of its mesh). */
export function nearestHit<T extends { distance: number }>(hits: Iterable<T | null | undefined>): T | null {
    let best: T | null = null;
    for (const h of hits) if (h && (!best || h.distance < best.distance)) best = h;
    return best;
}
