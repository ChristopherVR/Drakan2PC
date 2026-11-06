import { h, icon, store } from './dom.ts';
import {
    dragTo,
    grownSize,
    keyResize,
    readLayout,
    roomFor,
    shownSize,
    writeLayout,
    type Axis,
    type PaneLimits,
    type PaneState,
} from './splitmath.ts';

/**
 * Resizable Level Editor layout, like Unreal's: the Place sidebar on the left, the Outliner and Details column on the
 * right (split between the two), and the bottom drawer under the viewport. Every boundary is a thin splitter: drag it,
 * focus it and use the arrow keys (Shift for bigger steps, Home / End for smallest / largest, Enter to hide or show
 * the panel), double-click it to go back to the starting size. Dragging a side panel or the drawer almost shut hides
 * it; a tab on the viewport's edge (or the Window menu) brings it back. Sizes and hidden panels are remembered.
 * The size rules themselves are in splitmath.ts.
 */
export type DockKey = 'left' | 'right' | 'bottom';

interface Pane {
    key: DockKey | 'outliner';
    /** Accessible name of the splitter. */
    label: string;
    panel: HTMLElement;
    splitter: HTMLElement;
    lim: PaneLimits;
    axis: Axis;
    /** The pane sits after its splitter: moving the splitter toward it shrinks it. */
    invert: boolean;
    collapsible: boolean;
    /** Pixels the rest of the axis keeps (the viewport and the splitters, or Details under the Outliner). */
    keep(total: number): number;
    /** Length of the axis the pane shares. */
    total(): number;
    /** Open neighbours on the same axis. */
    others(): number;
}

export interface Layout {
    toggle(key: DockKey, open?: boolean): void;
    isOpen(key: DockKey): boolean;
    reset(): void;
    /**
     * Show the bottom dock (used when something wants a tab in view). With `share`, also grow it to at least that share
     * of the Level Editor's height (an editor that needs room, like Story, docks there).
     */
    openBottom(share?: number): void;
}

const STORE_KEY = 'layout';
/** The viewport keeps at least this much of the window: a fixed floor, or a share of a small window. */
const viewportW = (total: number): number => Math.max(360, Math.round(total * 0.4)) + 8;
const viewportH = (total: number): number => Math.max(160, Math.round(total * 0.3)) + 4;
const detailsH = (): number => 164;
const TIP = 'Drag to resize. Double-click to go back to the starting size.';

export function initLayout(root: HTMLElement, onResize: () => void): Layout {
    const q = (s: string): HTMLElement => {
        const el = root.querySelector<HTMLElement>(s);
        if (!el) throw new Error(`layout: missing ${s}`);
        return el;
    };
    const body = q('#ed-body');
    const mid = q('#ed-mid');
    const right = q('#ed-right');
    const viewport = q('#lv-viewport');
    const short = window.innerHeight < 800;
    const LIMITS: Record<Pane['key'], PaneLimits> = {
        left: { min: 200, max: 560, def: 320 },
        right: { min: 240, max: 720, def: 340 },
        // A short window gets a shorter drawer so the viewport keeps most of the height.
        bottom: { min: 120, max: 720, def: short ? 224 : 280 },
        outliner: { min: 110, max: 2000, def: 300 },
    };
    const stored = store.json<Record<string, unknown>>(STORE_KEY, {});
    // Older versions kept the Outliner height on its own.
    if (!stored.outliner) {
        const old = store.json<{ size?: unknown }>('outliner-h', {});
        if (typeof old.size === 'number') stored.outliner = { size: old.size, open: true };
    }
    const state = readLayout(stored, LIMITS);
    const st = (k: Pane['key']): PaneState => state[k]!;
    // A narrow window starts with the Outliner and Details folded away (one click brings them back): three columns left the
    // viewport a few hundred pixels.
    if (!stored.right && window.innerWidth < 900) st('right').open = false;

    const panes: Pane[] = [
        {
            key: 'left',
            label: 'Resize the Place panel',
            panel: q('#ed-left'),
            splitter: q('#ed-split-left'),
            lim: LIMITS.left,
            axis: 'x',
            invert: false,
            collapsible: true,
            keep: viewportW,
            total: () => body.clientWidth,
            others: () => shownOf('right'),
        },
        {
            key: 'right',
            label: 'Resize the Outliner and Details',
            panel: right,
            splitter: q('#ed-split-right'),
            lim: LIMITS.right,
            axis: 'x',
            invert: true,
            collapsible: true,
            keep: viewportW,
            total: () => body.clientWidth,
            others: () => shownOf('left'),
        },
        {
            key: 'bottom',
            label: 'Resize the bottom panel',
            panel: q('#ed-bottom'),
            splitter: q('#ed-split-bottom'),
            lim: LIMITS.bottom,
            axis: 'y',
            invert: true,
            collapsible: true,
            keep: viewportH,
            total: () => mid.clientHeight,
            others: () => 0,
        },
        {
            key: 'outliner',
            label: 'Resize the Outliner',
            panel: q('#ed-outliner'),
            splitter: q('#ed-split-outliner'),
            lim: LIMITS.outliner,
            axis: 'y',
            invert: false,
            collapsible: false,
            keep: detailsH,
            total: () => right.clientHeight,
            others: () => 0,
        },
    ];
    const pane = (k: Pane['key']): Pane => panes.find((p) => p.key === k)!;
    // Before the first layout pass sizes are their stored values (the room check needs measured boxes).
    const shown = new Map<Pane['key'], number>();
    const shownOf = (k: Pane['key']): number => shown.get(k) ?? (st(k).open ? st(k).size : 0);
    const maxOf = (p: Pane): number => {
        const total = p.total();
        return roomFor(total, p.others(), p.lim, p.keep(total));
    };

    // Tabs on the viewport edge that bring a hidden side panel back (Unreal shows a collapsed panel's tab the same way).
    const edge = (k: 'left' | 'right', text: string, ico: string): HTMLElement => {
        const b = h(
            'button',
            {
                type: 'button',
                class: `ed-edge-tab ${k}`,
                title: `Show the ${text} panel`,
                'aria-label': `Show the ${text} panel`,
                hidden: true,
                onclick: () => layout.toggle(k, true),
            },
            icon(ico, 14),
            h('span', {}, text),
        );
        viewport.append(b);
        return b;
    };
    const edges: Partial<Record<DockKey, HTMLElement>> = {
        left: edge('left', 'Place', 'place'),
        right: edge('right', 'Outliner', 'outliner'),
    };

    const apply = (p: Pane): void => {
        const s = st(p.key);
        const max = maxOf(p);
        const px = shownSize(s, p.lim, max);
        shown.set(p.key, px);
        root.style.setProperty(`--ed-${p.key}`, `${px}px`);
        p.panel.classList.toggle('collapsed', !s.open);
        p.splitter.classList.toggle('collapsed', !s.open);
        p.splitter.setAttribute('aria-valuemin', '0');
        p.splitter.setAttribute('aria-valuemax', String(max));
        p.splitter.setAttribute('aria-valuenow', String(px));
        p.splitter.setAttribute('aria-valuetext', s.open ? `${px} pixels` : 'hidden');
        const e = edges[p.key as DockKey];
        if (e) e.hidden = s.open;
    };
    const applyAll = (): void => {
        // Side panels first: the room of each depends on the other's shown size.
        for (const p of panes) apply(p);
        apply(pane('left'));
    };
    const save = (): void => store.set(STORE_KEY, writeLayout(state));
    // Tell the rest of the page once per frame at most, and not on every step of a drag: the viewport and the lists
    // watch their own boxes (ResizeObserver), the broadcast is for window-level listeners (menus, cards).
    let notifyRaf = 0;
    const notify = (): void => {
        if (notifyRaf) return;
        notifyRaf = requestAnimationFrame(() => {
            notifyRaf = 0;
            onResize();
        });
    };
    /** `final` is false for the steps of a drag: the panels move, nothing is stored or broadcast yet. */
    const changed = (final = true): void => {
        applyAll();
        if (!final) return;
        save();
        notify();
    };

    for (const p of panes) {
        const sp = p.splitter;
        sp.setAttribute('role', 'separator');
        sp.setAttribute('tabindex', '0');
        // A vertical bar (left / right column) separates panes side by side.
        sp.setAttribute('aria-orientation', p.axis === 'x' ? 'vertical' : 'horizontal');
        sp.setAttribute('aria-label', p.label);
        if (p.panel.id) sp.setAttribute('aria-controls', p.panel.id);
        sp.title = p.collapsible ? `${TIP} Drag it almost shut to hide the panel.` : TIP;
        sp.classList.add('ui-splitter', p.axis === 'x' ? 'v' : 'h');

        let start = 0;
        let startSize = 0;
        let prev: PaneState = st(p.key);
        sp.addEventListener('pointerdown', (e) => {
            if (e.button !== 0) return;
            e.preventDefault();
            sp.setPointerCapture(e.pointerId);
            start = p.axis === 'x' ? e.clientX : e.clientY;
            startSize = shownOf(p.key);
            prev = { ...st(p.key) };
            sp.classList.add('dragging');
            document.body.classList.add(p.axis === 'x' ? 'ed-resizing' : 'ed-resizing-y');
        });
        sp.addEventListener('pointermove', (e) => {
            if (!sp.hasPointerCapture(e.pointerId)) return;
            const delta = (p.axis === 'x' ? e.clientX : e.clientY) - start;
            state[p.key] = dragTo(startSize, delta, p.invert, p.lim, maxOf(p), p.collapsible, prev);
            changed(false);
        });
        const end = (e: PointerEvent): void => {
            if (!sp.hasPointerCapture(e.pointerId)) return;
            sp.releasePointerCapture(e.pointerId);
            sp.classList.remove('dragging');
            document.body.classList.remove('ed-resizing', 'ed-resizing-y');
            changed();
        };
        sp.addEventListener('pointerup', end);
        sp.addEventListener('pointercancel', end);
        sp.addEventListener('lostpointercapture', () => {
            sp.classList.remove('dragging');
            document.body.classList.remove('ed-resizing', 'ed-resizing-y');
        });
        sp.addEventListener('dblclick', () => {
            state[p.key] = { size: p.lim.def, open: true };
            changed();
        });
        sp.addEventListener('keydown', (e) => {
            const next = keyResize(st(p.key), e.key, e.shiftKey, p.axis, p.invert, p.lim, maxOf(p), p.collapsible);
            if (!next) return;
            e.preventDefault();
            state[p.key] = next;
            changed();
        });
    }
    applyAll();
    // The window got smaller or larger: fit the panels again (the remembered sizes stay as they are).
    window.addEventListener('resize', applyAll);
    // The right column is measured once it has a height (it has none while an editor tab covers the Level Editor).
    if (typeof ResizeObserver !== 'undefined') new ResizeObserver(() => apply(pane('outliner'))).observe(right);

    const layout: Layout = {
        toggle(key, open) {
            const s = st(key);
            state[key] = { size: s.size, open: open ?? !s.open };
            changed();
        },
        isOpen: (key) => st(key).open,
        reset() {
            for (const p of panes) state[p.key] = { size: p.lim.def, open: true };
            changed();
        },
        openBottom(share) {
            const s = st('bottom');
            const p = pane('bottom');
            const size = share ? grownSize(s.size, p.total(), share, p.lim, maxOf(p)) : s.size;
            if (s.open && size === s.size) return;
            state.bottom = { size, open: true };
            changed();
        },
    };
    return layout;
}
