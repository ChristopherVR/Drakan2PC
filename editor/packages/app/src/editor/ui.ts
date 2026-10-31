/**
 * Shared UI components (behaviour half of `components.css`): context menu, search field, empty state, styled
 * tooltips. Depends only on `dom.ts`. Class names are the `ui-*` ones documented in components.css.
 */
import { h, icon } from './dom.ts';

// ---- search field ------------------------------------------------------------------------------------------------

/** A rounded search input with a magnifier inside: `{ el, input }`. */
export function searchField(placeholder: string, label = placeholder): { el: HTMLElement; input: HTMLInputElement } {
    const input = h('input', { type: 'search', class: 'ui-input', placeholder, 'aria-label': label });
    const el = h('div', { class: 'ui-search' }, icon('search', 13), input);
    return { el, input };
}

// ---- empty state ---------------------------------------------------------------------------------------------------

/** Icon, one-line title, one sentence of guidance and an optional primary action. */
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

export function closeContextMenu(): void {
    openMenu?.remove();
    openMenu = null;
}

/** Show a menu at viewport coordinates; closes on outside click, Escape, scroll or blur. */
export function contextMenu(x: number, y: number, items: MenuItem[]): void {
    closeContextMenu();
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
        if (e.key !== 'ArrowDown' && e.key !== 'ArrowUp') return;
        e.preventDefault();
        const btns = [...menu.querySelectorAll<HTMLButtonElement>('button:not(:disabled)')];
        const i = btns.indexOf(document.activeElement as HTMLButtonElement);
        btns[(i + (e.key === 'ArrowDown' ? 1 : -1) + btns.length) % btns.length]?.focus();
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
