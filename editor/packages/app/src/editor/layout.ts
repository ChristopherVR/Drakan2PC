import { clamp, store } from './dom.ts';

/**
 * Resizable dock layout: right panel and bottom dock separated from the viewport by splitters. Sizes and
 * collapsed state persist (localStorage, optional). Double-click a splitter to collapse or restore its panel.
 */
interface Dock {
    key: 'right' | 'bottom';
    panel: HTMLElement;
    splitter: HTMLElement;
    min: number;
    max: number;
    def: number;
    /** Axis the size is measured on. */
    axis: 'x' | 'y';
    /** Dragging toward the viewport grows the panel (right and bottom docks). */
    invert: boolean;
}

export interface Layout {
    toggle(key: Dock['key'], open?: boolean): void;
    isOpen(key: Dock['key']): boolean;
    reset(): void;
    /** Show the bottom dock (used when something wants a tab in view). */
    openBottom(): void;
}

export function initLayout(root: HTMLElement, onResize: () => void): Layout {
    const q = (s: string): HTMLElement => {
        const el = root.querySelector<HTMLElement>(s);
        if (!el) throw new Error(`layout: missing ${s}`);
        return el;
    };
    const docks: Dock[] = [
        {
            key: 'right',
            panel: q('#ed-right'),
            splitter: q('#ed-split-right'),
            min: 240,
            max: 640,
            def: 320,
            axis: 'x',
            invert: true,
        },
        {
            key: 'bottom',
            panel: q('#ed-bottom'),
            splitter: q('#ed-split-bottom'),
            min: 120,
            max: 640,
            // A short window gets a shorter dock so the viewport keeps most of the height.
            def: window.innerHeight < 800 ? 224 : 280,
            axis: 'y',
            invert: true,
        },
    ];
    const state: Record<string, { size: number; open: boolean }> = store.json('layout', {});

    const apply = (d: Dock): void => {
        const s = (state[d.key] ??= { size: d.def, open: true });
        s.size = clamp(s.size, d.min, d.max);
        root.style.setProperty(`--ed-${d.key}`, s.open ? `${s.size}px` : '0px');
        d.panel.classList.toggle('collapsed', !s.open);
        d.splitter.classList.toggle('collapsed', !s.open);
    };
    const save = (): void => store.set('layout', JSON.stringify(state));
    const maxFor = (d: Dock): number => {
        // Keep at least 240 px of viewport.
        const total = d.axis === 'x' ? root.clientWidth : root.clientHeight - 80;
        const others = docks
            .filter((o) => o !== d && o.axis === d.axis)
            .reduce((n, o) => n + (state[o.key]?.open ? (state[o.key]?.size ?? 0) : 0), 0);
        return Math.max(d.min, Math.min(d.max, total - others - 240));
    };

    for (const d of docks) {
        apply(d);
        let start = 0;
        let startSize = 0;
        d.splitter.addEventListener('pointerdown', (e) => {
            e.preventDefault();
            d.splitter.setPointerCapture(e.pointerId);
            start = d.axis === 'x' ? e.clientX : e.clientY;
            startSize = state[d.key]!.open ? state[d.key]!.size : 0;
            d.splitter.classList.add('dragging');
            document.body.classList.add('ed-resizing');
        });
        d.splitter.addEventListener('pointermove', (e) => {
            if (!d.splitter.hasPointerCapture(e.pointerId)) return;
            const delta = (d.axis === 'x' ? e.clientX : e.clientY) - start;
            const size = startSize + (d.invert ? -delta : delta);
            const s = state[d.key]!;
            if (size < d.min * 0.5) s.open = false;
            else {
                s.open = true;
                s.size = clamp(size, d.min, maxFor(d));
            }
            apply(d);
            onResize();
        });
        const end = (e: PointerEvent): void => {
            if (d.splitter.hasPointerCapture(e.pointerId)) d.splitter.releasePointerCapture(e.pointerId);
            d.splitter.classList.remove('dragging');
            document.body.classList.remove('ed-resizing');
            save();
            onResize();
        };
        d.splitter.addEventListener('pointerup', end);
        d.splitter.addEventListener('pointercancel', end);
        d.splitter.addEventListener('dblclick', () => layout.toggle(d.key));
        d.splitter.addEventListener('keydown', (e) => {
            const step = e.shiftKey ? 60 : 20;
            const dir =
                e.key === 'ArrowLeft' || e.key === 'ArrowUp'
                    ? -1
                    : e.key === 'ArrowRight' || e.key === 'ArrowDown'
                      ? 1
                      : 0;
            if (!dir) return;
            e.preventDefault();
            const s = state[d.key]!;
            s.open = true;
            s.size = clamp(s.size + (d.invert ? -dir : dir) * step, d.min, maxFor(d));
            apply(d);
            save();
            onResize();
        });
    }

    // Outliner / Details split inside the right dock (height of the Outliner in px).
    const osplit = root.querySelector<HTMLElement>('#ed-split-outliner');
    const oDef = 300;
    const oState: { size: number } = store.json('outliner-h', { size: oDef });
    const applyO = (): void => {
        // The CSS caps the height at (dock - 160px); this only keeps the stored value sane while the tab is hidden.
        oState.size = clamp(oState.size, 110, 2000);
        root.style.setProperty('--ed-outliner', `${oState.size}px`);
    };
    applyO();
    if (osplit) {
        let start = 0;
        let startSize = 0;
        osplit.addEventListener('pointerdown', (e) => {
            e.preventDefault();
            osplit.setPointerCapture(e.pointerId);
            start = e.clientY;
            startSize = oState.size;
            osplit.classList.add('dragging');
            document.body.classList.add('ed-resizing-y');
        });
        osplit.addEventListener('pointermove', (e) => {
            if (!osplit.hasPointerCapture(e.pointerId)) return;
            oState.size = startSize + (e.clientY - start);
            applyO();
        });
        const end = (e: PointerEvent): void => {
            if (osplit.hasPointerCapture(e.pointerId)) osplit.releasePointerCapture(e.pointerId);
            osplit.classList.remove('dragging');
            document.body.classList.remove('ed-resizing-y');
            store.set('outliner-h', JSON.stringify(oState));
        };
        osplit.addEventListener('pointerup', end);
        osplit.addEventListener('pointercancel', end);
        osplit.addEventListener('dblclick', () => {
            oState.size = oDef;
            applyO();
            store.set('outliner-h', JSON.stringify(oState));
        });
        osplit.addEventListener('keydown', (e) => {
            const dir = e.key === 'ArrowUp' ? -1 : e.key === 'ArrowDown' ? 1 : 0;
            if (!dir) return;
            e.preventDefault();
            oState.size += dir * (e.shiftKey ? 60 : 20);
            applyO();
            store.set('outliner-h', JSON.stringify(oState));
        });
        window.addEventListener('resize', applyO);
    }

    const find = (k: Dock['key']): Dock => docks.find((d) => d.key === k)!;
    const layout: Layout = {
        toggle(key, open) {
            const s = state[key]!;
            s.open = open ?? !s.open;
            apply(find(key));
            save();
            onResize();
        },
        isOpen: (key) => state[key]!.open,
        reset() {
            for (const d of docks) state[d.key] = { size: d.def, open: true };
            for (const d of docks) apply(d);
            oState.size = oDef;
            applyO();
            store.set('outliner-h', JSON.stringify(oState));
            save();
            onResize();
        },
        openBottom() {
            if (!state.bottom!.open) layout.toggle('bottom', true);
        },
    };
    return layout;
}
