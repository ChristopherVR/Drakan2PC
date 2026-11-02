/**
 * Size rules of the Level Editor's resizable areas (Place sidebar, Outliner and Details column, bottom drawer, and the
 * Outliner / Details split). Pure functions: layout.ts wires them to splitters, the tests check them alone.
 */

/** Smallest, largest and starting size of a pane in pixels. */
export interface PaneLimits {
    min: number;
    max: number;
    def: number;
}

export interface PaneState {
    size: number;
    open: boolean;
}

export const clampSize = (v: number, lo: number, hi: number): number => Math.min(Math.max(lo, hi), Math.max(lo, v));

/**
 * Largest size a pane may take: its own maximum, but never so much that the rest of the axis (`total` minus the open
 * neighbours on the same axis) drops below `keep` pixels. Never less than the pane's minimum.
 */
export function roomFor(total: number, others: number, lim: PaneLimits, keep: number): number {
    if (!(total > 0)) return lim.max;
    return Math.max(lim.min, Math.min(lim.max, Math.floor(total - others - keep)));
}

/** The size the pane is drawn at: 0 when collapsed, otherwise its stored size fitted into the room it has now. */
export const shownSize = (s: PaneState, lim: PaneLimits, max: number): number =>
    s.open ? clampSize(s.size, lim.min, max) : 0;

/**
 * A drag of the pane's splitter by `delta` pixels from where it started (`startSize`; 0 if it was collapsed).
 * `invert`: the pane sits after the splitter (right column, bottom drawer), so moving toward it shrinks it. Dragging
 * below half the minimum collapses a collapsible pane; a pane that cannot collapse stops at its minimum.
 */
export function dragTo(
    startSize: number,
    delta: number,
    invert: boolean,
    lim: PaneLimits,
    max: number,
    collapsible: boolean,
    prev: PaneState,
): PaneState {
    const size = startSize + (invert ? -delta : delta);
    if (collapsible && size < lim.min * 0.5) return { size: prev.size, open: false };
    return { size: clampSize(Math.round(size), lim.min, max), open: true };
}

export type Axis = 'x' | 'y';

/**
 * Keyboard on a focused splitter. Arrow keys along the splitter's axis move it 16 px (64 px with Shift), Home and End
 * jump to the smallest and largest size, Enter collapses or restores a collapsible pane. Returns null for other keys.
 */
export function keyResize(
    s: PaneState,
    key: string,
    shift: boolean,
    axis: Axis,
    invert: boolean,
    lim: PaneLimits,
    max: number,
    collapsible: boolean,
): PaneState | null {
    const step = shift ? 64 : 16;
    const back = axis === 'x' ? 'ArrowLeft' : 'ArrowUp';
    const fwd = axis === 'x' ? 'ArrowRight' : 'ArrowDown';
    const cur = s.open ? clampSize(s.size, lim.min, max) : lim.min;
    if (key === back || key === fwd) {
        const dir = (key === fwd ? 1 : -1) * (invert ? -1 : 1);
        // A collapsed pane opens at its minimum on the first key that grows it.
        if (!s.open) return dir > 0 ? { size: lim.min, open: true } : null;
        return { size: clampSize(cur + dir * step, lim.min, max), open: true };
    }
    if (key === 'Home') return { size: lim.min, open: true };
    if (key === 'End') return { size: max, open: true };
    if (key === 'Enter' && collapsible) return { size: s.size, open: !s.open };
    return null;
}

/**
 * Stored sizes read back: anything missing, non-numeric or out of range falls back to the default (open). The stored
 * value is user data from an older version or another tab, so it is never trusted.
 */
export function readLayout(raw: unknown, limits: Record<string, PaneLimits>): Record<string, PaneState> {
    const src = raw && typeof raw === 'object' ? (raw as Record<string, unknown>) : {};
    const out: Record<string, PaneState> = {};
    for (const [key, lim] of Object.entries(limits)) {
        const v = src[key];
        const o = v && typeof v === 'object' ? (v as Record<string, unknown>) : {};
        const size = typeof o.size === 'number' && Number.isFinite(o.size) ? o.size : lim.def;
        out[key] = {
            size: clampSize(Math.round(size), lim.min, lim.max),
            open: typeof o.open === 'boolean' ? o.open : true,
        };
    }
    return out;
}

/** JSON text of a layout for storage. */
export const writeLayout = (state: Record<string, PaneState>): string =>
    JSON.stringify(
        Object.fromEntries(Object.entries(state).map(([k, s]) => [k, { size: Math.round(s.size), open: s.open }])),
    );

/**
 * Size of a pane asked to take at least `share` of `total` (an editor that needs room docks into the bottom drawer):
 * the larger of its size and that share, never past `max` (the room it has) and never below its minimum. A pane already
 * that large keeps its size, so a drawer the user made bigger is not shrunk.
 */
export function grownSize(size: number, total: number, share: number, lim: PaneLimits, max: number): number {
    if (!(total > 0) || !(share > 0)) return size;
    const want = Math.round(total * Math.min(share, 1));
    return want > size ? clampSize(want, lim.min, Math.max(size, max)) : size;
}
