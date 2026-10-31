import { h } from './dom.ts';

/**
 * Virtualised single-column list: only the rows near the viewport exist in the DOM, so a container with thousands of
 * entries scrolls as smoothly as a short one. One active row (keyboard and hover) and a "pick" action per row.
 */
export interface VListOptions<T> {
    rowHeight: number;
    /** Stable identity of an item (keeps the active row across `setItems`). */
    key: (item: T) => string;
    /** Build the row's content; the list adds the wrapper, position, state classes and ARIA. */
    row: (item: T, index: number) => HTMLElement;
    onPick: (item: T) => void;
    /** The active item changed (null when the list is empty). */
    onActive?: (item: T | null) => void;
    /** Rows now in the DOM (for lazy thumbnails). */
    onVisible?: (items: readonly T[]) => void;
    /** Shown instead of rows when there are none. */
    empty?: () => Node;
    label: string;
    /** Grid mode: cells of this width (rows are `rowHeight` tall); arrow up/down move by a whole row of cells. */
    cellWidth?: number;
}

export interface VList<T> {
    el: HTMLElement;
    setItems(items: readonly T[], activeKey?: string): void;
    /** Move the active row by `delta` rows (clamped); in grid mode a row is a line of cells. */
    move(delta: number): void;
    /** Move the active item by `delta` items (grid mode: left and right). */
    step(delta: number): void;
    moveTo(index: number): void;
    active(): T | null;
    /** Rebuild the rows in view (item content changed, e.g. a name arrived). */
    refresh(): void;
    /** True for items whose row is in the DOM now. */
    isVisible(key: string): boolean;
}

const OVERSCAN = 5;

export function virtualList<T>(opts: VListOptions<T>): VList<T> {
    const rh = opts.rowHeight;
    const spacer = h('div', { class: 'vl-spacer' });
    const el = h('div', { class: 'vlist', role: 'listbox', 'aria-label': opts.label, tabindex: '-1' }, spacer);
    const id = `vl${Math.random().toString(36).slice(2, 8)}`;
    let items: readonly T[] = [];
    let activeIndex = -1;
    let shown = new Set<string>();
    let queued = false;
    const cols = (): number => (opts.cellWidth ? Math.max(1, Math.floor((el.clientWidth - 4) / opts.cellWidth)) : 1);

    const render = (): void => {
        queued = false;
        const vh = el.clientHeight || 300;
        const nc = cols();
        const first = Math.max(0, (Math.floor(el.scrollTop / rh) - OVERSCAN) * nc);
        const last = Math.min(items.length, (Math.ceil((el.scrollTop + vh) / rh) + OVERSCAN) * nc);
        spacer.style.height = `${Math.ceil(items.length / nc) * rh}px`;
        const rows: HTMLElement[] = [];
        const keys = new Set<string>();
        for (let i = first; i < last; i++) {
            const item = items[i]!;
            const k = opts.key(item);
            keys.add(k);
            const content = opts.row(item, i);
            const row = h('div', {
                class: `vl-row${i === activeIndex ? ' active' : ''}`,
                role: 'option',
                id: `${id}-${i}`,
                'aria-selected': String(i === activeIndex),
                style: opts.cellWidth
                    ? `top:${Math.floor(i / nc) * rh}px;height:${rh}px;left:${(i % nc) * opts.cellWidth}px;width:${opts.cellWidth}px;right:auto`
                    : `top:${i * rh}px;height:${rh}px`,
                onclick: () => {
                    activeIndex = i;
                    opts.onPick(item);
                },
                onpointermove: () => {
                    if (activeIndex !== i) setActive(i, false);
                },
            });
            row.append(content);
            rows.push(row);
        }
        const empty = items.length === 0 && opts.empty ? opts.empty() : null;
        spacer.replaceChildren(...rows, ...(empty ? [empty] : []));
        shown = keys;
        opts.onVisible?.(items.slice(first, last));
    };

    const schedule = (): void => {
        if (queued) return;
        queued = true;
        requestAnimationFrame(render);
    };

    function scrollIntoView(i: number): void {
        const top = Math.floor(i / cols()) * rh;
        if (top < el.scrollTop) el.scrollTop = top;
        else if (top + rh > el.scrollTop + el.clientHeight) el.scrollTop = top + rh - el.clientHeight;
    }

    function setActive(i: number, scroll: boolean): void {
        const was = activeIndex;
        activeIndex = items.length ? Math.max(0, Math.min(items.length - 1, i)) : -1;
        if (scroll && activeIndex >= 0) scrollIntoView(activeIndex);
        for (const r of spacer.querySelectorAll<HTMLElement>('.vl-row.active')) {
            r.classList.remove('active');
            r.setAttribute('aria-selected', 'false');
        }
        const row = spacer.querySelector<HTMLElement>(`[id="${id}-${activeIndex}"]`);
        if (row) {
            row.classList.add('active');
            row.setAttribute('aria-selected', 'true');
            el.setAttribute('aria-activedescendant', row.id);
        } else el.removeAttribute('aria-activedescendant');
        if (was !== activeIndex) opts.onActive?.(activeIndex >= 0 ? items[activeIndex]! : null);
        if (scroll) schedule();
    }

    el.addEventListener('scroll', schedule, { passive: true });
    new ResizeObserver(schedule).observe(el);

    return {
        el,
        setItems(next, activeKey) {
            items = next;
            spacer.style.height = `${Math.ceil(next.length / cols()) * rh}px`;
            let at = activeKey === undefined ? -1 : next.findIndex((x) => opts.key(x) === activeKey);
            if (at < 0) at = next.length ? 0 : -1;
            el.scrollTop = 0;
            activeIndex = at;
            if (at >= 0) scrollIntoView(at);
            render();
            opts.onActive?.(at >= 0 ? next[at]! : null);
            const row = spacer.querySelector<HTMLElement>(`[id="${id}-${at}"]`);
            if (row) el.setAttribute('aria-activedescendant', row.id);
        },
        move: (d) => setActive((activeIndex < 0 ? 0 : activeIndex) + d * cols(), true),
        step: (d) => setActive((activeIndex < 0 ? 0 : activeIndex) + d, true),
        moveTo: (i) => setActive(i, true),
        active: () => (activeIndex >= 0 ? (items[activeIndex] ?? null) : null),
        refresh: render,
        isVisible: (k) => shown.has(k),
    };
}
