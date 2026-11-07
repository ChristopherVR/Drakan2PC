/**
 * Shared UI components (behaviour half of `components.css`): context menu, search field, empty state, styled
 * tooltips. Depends only on `dom.ts`. Class names are the `ui-*` ones documented in components.css.
 */
import { h, icon } from './dom.ts';
import { installDropdowns } from './dropdown.ts';

// ---- control family ----------------------------------------------------------------------------------------------

/**
 * One call at start-up: every `<select>` becomes the shared dropdown (dropdown.ts) and every range slider paints its
 * filled part (`--fill`, read by components.css). Checkboxes, radios and number fields are styled by CSS alone.
 */
export function installControls(): void {
    installDropdowns();
    const paint = (r: HTMLInputElement): void => {
        const min = Number(r.min || 0);
        const max = Number(r.max || 100);
        const v = Number(r.value);
        const pct = max > min ? ((v - min) / (max - min)) * 100 : 0;
        r.style.setProperty('--fill', `${Math.max(0, Math.min(100, pct))}%`);
    };
    const isRange = (t: EventTarget | null): t is HTMLInputElement =>
        t instanceof HTMLInputElement && t.type === 'range';
    document.addEventListener('input', (e) => isRange(e.target) && paint(e.target), true);
    document.addEventListener('change', (e) => isRange(e.target) && paint(e.target), true);
    // values set from script do not fire events: repaint when the pointer or focus reaches a slider, and on insert
    document.addEventListener('pointerover', (e) => isRange(e.target) && paint(e.target), true);
    const scan = (n: Node): void => {
        if (n instanceof HTMLInputElement) {
            if (n.type === 'range') queueMicrotask(() => paint(n));
        } else if (n instanceof Element)
            for (const r of n.querySelectorAll<HTMLInputElement>('input[type="range"]')) queueMicrotask(() => paint(r));
    };
    scan(document.body);
    new MutationObserver((muts) => {
        for (const m of muts) for (const n of m.addedNodes) scan(n);
    }).observe(document.body, { childList: true, subtree: true });
}

// ---- search field ------------------------------------------------------------------------------------------------

/** A rounded search input with a magnifier inside: `{ el, input }`. */
export function searchField(placeholder: string, label = placeholder): { el: HTMLElement; input: HTMLInputElement } {
    const input = h('input', { type: 'search', class: 'ui-input', placeholder, 'aria-label': label });
    const el = h('div', { class: 'ui-search' }, icon('search', 13), input);
    return { el, input };
}

// ---- empty state ---------------------------------------------------------------------------------------------------

/** Icon, one-line title, one sentence of guidance and an optional primary action. */
/** Placeholder rows for a list that is being filled (`aria-busy`, announced as loading, a quiet shimmer). */
export function skeletonRows(count = 8, label = 'Loading'): HTMLElement {
    const rows = Array.from({ length: count }, (_, i) =>
        h('div', { class: 'ui-skel-row', style: `--w:${48 + ((i * 17) % 40)}%` }, h('i', {}), h('b', {})),
    );
    return h('div', { class: 'ui-skeleton', role: 'status', 'aria-busy': 'true', 'aria-label': label }, ...rows);
}

export function emptyState(
    ico: string,
    title: string,
    text: string,
    action?: { label: string; run: () => void },
): HTMLElement {
    return h(
        'div',
        { class: 'ui-empty empty-state' },
        icon(ico, 28),
        h('strong', {}, title),
        h('span', {}, text),
        action ? h('button', { class: 'ui-btn primary', type: 'button', onclick: action.run }, action.label) : null,
    );
}

// ---- context menu --------------------------------------------------------------------------------------------------

export type MenuItem =
    | { label: string; run: () => void; icon?: string; hint?: string; disabled?: boolean; danger?: boolean }
    | { sep: true }
    | { heading: string };

let openMenu: HTMLElement | null = null;
/** Where the keyboard was before the menu opened; it goes back there when the menu closes without moving focus. */
let menuOpener: HTMLElement | SVGElement | null = null;

export function closeContextMenu(): void {
    const menu = openMenu;
    const hadFocus = !!menu && (menu.contains(document.activeElement) || document.activeElement === document.body);
    menu?.remove();
    openMenu = null;
    if (menu && hadFocus && menuOpener?.isConnected) menuOpener.focus({ preventScroll: true });
    menuOpener = null;
}

/** Show a menu at viewport coordinates; closes on outside click, Escape, scroll or blur. */
export function contextMenu(x: number, y: number, items: MenuItem[]): void {
    closeContextMenu();
    const was = document.activeElement;
    menuOpener = was instanceof HTMLElement || was instanceof SVGElement ? was : null;
    const menu = h('div', { class: 'ui-menu', role: 'menu' });
    for (const it of items) {
        if ('sep' in it) menu.append(h('div', { class: 'ui-menu-sep', role: 'separator' }));
        else if ('heading' in it) menu.append(h('div', { class: 'ui-menu-label' }, it.heading));
        else {
            menu.append(
                h(
                    'button',
                    {
                        class: `ui-menu-item${it.danger ? ' danger' : ''}`,
                        type: 'button',
                        role: 'menuitem',
                        disabled: it.disabled,
                        onclick: () => {
                            closeContextMenu();
                            it.run();
                        },
                    },
                    it.icon ? icon(it.icon, 14) : h('span', { class: 'ph' }),
                    it.label,
                    it.hint ? h('span', { class: 'hint' }, it.hint) : null,
                ),
            );
        }
    }
    document.body.append(menu);
    const r = menu.getBoundingClientRect();
    menu.style.left = `${Math.max(4, Math.min(x, window.innerWidth - r.width - 4))}px`;
    menu.style.top = `${Math.max(4, Math.min(y, window.innerHeight - r.height - 4))}px`;
    openMenu = menu;
    menu.querySelector<HTMLElement>('button:not(:disabled)')?.focus({ preventScroll: true });
    const away = (e: Event): void => {
        if (e instanceof KeyboardEvent) {
            if (e.key !== 'Escape') return;
        } else if (menu.contains(e.target as Node)) return;
        closeContextMenu();
        cleanup();
    };
    const cleanup = (): void => {
        window.removeEventListener('pointerdown', away, true);
        window.removeEventListener('keydown', away, true);
        window.removeEventListener('blur', away, true);
        window.removeEventListener('wheel', away, true);
    };
    window.addEventListener('pointerdown', away, true);
    window.addEventListener('keydown', away, true);
    window.addEventListener('blur', away, true);
    window.addEventListener('wheel', away, true);
    menu.addEventListener('keydown', (e) => {
        if (!['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(e.key)) return;
        e.preventDefault();
        const btns = [...menu.querySelectorAll<HTMLButtonElement>('button:not(:disabled)')];
        const i = btns.indexOf(document.activeElement as HTMLButtonElement);
        const next = e.key === 'Home' ? 0 : e.key === 'End' ? btns.length - 1 : i + (e.key === 'ArrowDown' ? 1 : -1);
        btns[(next + btns.length) % btns.length]?.focus();
    });
}

/** Attach a context menu to an element; `build` returns the items for the event (empty = no menu). */
export function onContextMenu(el: HTMLElement, build: (e: MouseEvent) => MenuItem[]): void {
    el.addEventListener('contextmenu', (e) => {
        const items = build(e);
        if (!items.length) return;
        e.preventDefault();
        e.stopPropagation();
        contextMenu(e.clientX, e.clientY, items);
    });
}

// ---- tooltips ------------------------------------------------------------------------------------------------------

/**
 * Replace native `title` bubbles on `.ui-tool` / `.tool` / `[data-tip]` elements with the styled tooltip. A trailing
 * "(Ctrl+Z)" in the title is shown as a key cap. The `title` stays as `aria-label` fallback and is restored on leave.
 */
export function installTooltips(): void {
    const tip = h('div', { class: 'ui-tooltip', role: 'tooltip', hidden: true });
    document.body.append(tip);
    let timer = 0;
    let target: HTMLElement | null = null;
    const hide = (): void => {
        window.clearTimeout(timer);
        tip.hidden = true;
        if (target?.dataset.tipTitle !== undefined) {
            target.title = target.dataset.tipTitle;
            delete target.dataset.tipTitle;
        }
        target = null;
    };
    document.addEventListener('pointerover', (e) => {
        const el = (e.target as HTMLElement | null)?.closest?.<HTMLElement>('.ui-tool, .tool, .vp-gizmo, [data-tip]');
        if (!el || el === target) return;
        hide();
        const text = el.dataset.tip ?? el.title;
        if (!text) return;
        target = el;
        if (el.title) {
            el.dataset.tipTitle = el.title;
            if (!el.getAttribute('aria-label')) el.setAttribute('aria-label', el.title);
            el.removeAttribute('title');
        }
        timer = window.setTimeout(() => {
            const m = /^(.*?)\s*\(([^()]+)\)$/.exec(text);
            tip.replaceChildren(m ? m[1]! : text);
            if (m) tip.append(h('span', { class: 'ui-kbd' }, m[2]));
            tip.hidden = false;
            const r = el.getBoundingClientRect();
            const t = tip.getBoundingClientRect();
            tip.style.left = `${Math.max(4, Math.min(r.left + r.width / 2 - t.width / 2, window.innerWidth - t.width - 4))}px`;
            const below = r.bottom + 6 + t.height < window.innerHeight;
            tip.style.top = `${below ? r.bottom + 6 : r.top - t.height - 6}px`;
        }, 450);
    });
    document.addEventListener('pointerout', (e) => {
        if (target && !target.contains(e.relatedTarget as Node | null)) hide();
    });
    document.addEventListener('pointerdown', hide, true);
}
