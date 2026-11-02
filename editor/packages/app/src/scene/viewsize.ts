/**
 * Drawing-buffer size of a 3D viewport. A resize only ever changes the canvas and the camera's aspect: it never
 * touches the scene. Resizing the canvas clears it, so the view applies a new size in its own frame, right before it
 * draws (never from a ResizeObserver callback, which runs after the frame is drawn and would show an empty canvas).
 * While the size keeps changing (a splitter drag, a window being resized) the pixel ratio is capped at 1 so each step
 * is cheap; once it has been still for `RESIZE_SETTLE_MS` the full ratio comes back.
 */
export interface CanvasSize {
    /** CSS pixels. */
    w: number;
    h: number;
    /** Device pixels per CSS pixel. */
    ratio: number;
}

/** A resize this soon after the last one counts as part of the same drag. */
export const RESIZE_SETTLE_MS = 180;

/** Pixel ratio the view draws at when it is not being resized (2 at most: a 4K buffer costs a lot for little). */
export function fullRatio(devicePixelRatio: number): number {
    const r = Number.isFinite(devicePixelRatio) && devicePixelRatio > 0 ? devicePixelRatio : 1;
    return Math.min(r, 2);
}

/** Size for a host box of `cssW` x `cssH`; `busy` while a resize is in progress. A hidden host (0 x 0) gets 1 x 1. */
export function canvasSize(cssW: number, cssH: number, devicePixelRatio: number, busy: boolean): CanvasSize {
    const full = fullRatio(devicePixelRatio);
    return {
        w: Math.max(Math.floor(cssW), 1),
        h: Math.max(Math.floor(cssH), 1),
        ratio: busy ? Math.min(full, 1) : full,
    };
}

export function sameSize(a: CanvasSize | null, b: CanvasSize): boolean {
    return !!a && a.w === b.w && a.h === b.h && a.ratio === b.ratio;
}

/** True when the last resize came less than `RESIZE_SETTLE_MS` after the one before it (a drag is going on). */
export function inBurst(prevAt: number, at: number): boolean {
    return at - prevAt < RESIZE_SETTLE_MS;
}

/** The view drew at a lowered ratio and the resizing has stopped: time to draw at the full ratio again. */
export function settled(current: CanvasSize | null, lastAt: number, now: number, devicePixelRatio: number): boolean {
    return !!current && current.ratio < fullRatio(devicePixelRatio) && now - lastAt >= RESIZE_SETTLE_MS;
}
