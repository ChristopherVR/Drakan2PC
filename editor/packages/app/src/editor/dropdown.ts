/**
 * The one dropdown of the app. Every `<select>` in the document is upgraded to it (see `installDropdowns`), so panels
 * keep using the native element as their data model (`value`, `options`, `change` events, `disabled`) and get the
 * styled, keyboard-driven listbox for free. New code can build one with `selectField()`.
 *
 *   trigger   <button class="ui-dd" role="combobox">  value label + chevron; same height and padding as inputs
 *   popup     <div class="ui-dd-pop" role="listbox">  fixed to the viewport, flips above near the bottom edge,
 *             follows the trigger when a panel scrolls, filters with a search box when the list is long
 *   keyboard  closed: Enter / Space / Alt+Down / arrows open, typing opens on the first match
 *             open: arrows, Home / End, PageUp / PageDown, type-ahead, Enter picks, Esc / Tab close
 *
 * Opt-outs: `data-native` keeps a select native; `multiple` or `size > 1` selects are never upgraded. `data-search`
 * = "on" / "off" forces the search box; `data-placeholder` is shown when nothing is selected. `aria-invalid="true"`
 * marks the trigger invalid. Depends only on `dom.ts`.
 */
import { h, icon } from './dom.ts';

/** Lists longer than this get a search box. */
const SEARCH_AFTER = 12;
const TYPEAHEAD_MS = 700;

interface Enhanced {
    trigger: HTMLButtonElement;
    refresh(): void;
    open(): void;
}

const enhanced = new WeakMap<HTMLSelectElement, Enhanced>();
let current: { sel: HTMLSelectElement; close: (focus: boolean) => void } | null = null;
let seq = 0;

// Native accessors, looked up on first use so the module also loads where there is no DOM (unit tests).
let valueDesc: PropertyDescriptor;
let indexDesc: PropertyDescriptor;

function optLabel(o: HTMLOptionElement): string {
    return (o.label || o.text).trim();
}

/** Width of the longest option label in the trigger's font, so the trigger does not jump as the value changes. */
let measureCtx: CanvasRenderingContext2D | null | undefined;
function measure(sel: HTMLSelectElement, trigger: HTMLElement): number {
    if (measureCtx === undefined) measureCtx = document.createElement('canvas').getContext('2d');
    if (!measureCtx) return 0;
    const cs = getComputedStyle(trigger);
    measureCtx.font = `${cs.fontWeight} ${cs.fontSize} ${cs.fontFamily}`;
    let w = 0;
    for (const o of sel.options) w = Math.max(w, measureCtx.measureText(optLabel(o)).width);
    return Math.ceil(w);
}

export function closeDropdown(): void {
    current?.close(false);
}

/** Upgrade one select. Safe to call twice; returns the trigger, or null when the select stays native. */
export function enhanceSelect(sel: HTMLSelectElement): HTMLButtonElement | null {
    const have = enhanced.get(sel);
    if (have) {
        if (sel.nextSibling !== have.trigger && sel.parentNode) sel.after(have.trigger);
        return have.trigger;
    }
    if (sel.multiple || sel.size > 1 || sel.hasAttribute('data-native')) return null;
    valueDesc ??= Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype, 'value')!;
    indexDesc ??= Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype, 'selectedIndex')!;

    const id = `ui-dd-${++seq}`;
    const valueEl = h('span', { class: 'ui-dd-value' });
    const trigger = h(
        'button',
        {
            type: 'button',
            role: 'combobox',
            'aria-haspopup': 'listbox',
            'aria-expanded': 'false',
            'aria-controls': `${id}-list`,
        },
        valueEl,
        icon('chevron-down', 12),
    ) as HTMLButtonElement;
    trigger.classList.add('ui-dd');

    const syncAttrs = (): void => {
        const extra = sel.className.split(/\s+/).filter((c) => c && c !== 'ui-select' && c !== 'ui-dd-native');
        trigger.className = ['ui-dd', ...extra].join(' ');
        trigger.disabled = sel.disabled;
        trigger.hidden = sel.hidden || sel.style.display === 'none';
        const label =
            sel.getAttribute('aria-label') ??
            (sel.id ? document.querySelector(`label[for="${CSS.escape(sel.id)}"]`)?.textContent?.trim() : null) ??
            sel.title;
        if (label) trigger.setAttribute('aria-label', label);
        else trigger.removeAttribute('aria-label');
        if (sel.title) trigger.title = sel.title;
        else trigger.removeAttribute('title');
        const invalid = sel.getAttribute('aria-invalid') === 'true' || sel.classList.contains('invalid');
        trigger.toggleAttribute('aria-invalid', invalid);
        if (invalid) trigger.setAttribute('aria-invalid', 'true');
        if (sel.style.width) trigger.style.width = sel.style.width;
        if (sel.style.flex) trigger.style.flex = sel.style.flex;
        if (sel.style.minWidth) trigger.style.minWidth = sel.style.minWidth;
        if (sel.style.maxWidth) trigger.style.maxWidth = sel.style.maxWidth;
    };

    let widthDirty = true;
    const refresh = (): void => {
        const o = sel.selectedIndex >= 0 ? sel.options[sel.selectedIndex] : undefined;
        // nothing selected: the placeholder, or the control's name while it has no options yet
        const text = o
            ? optLabel(o)
            : (sel.dataset.placeholder ?? (sel.options.length ? '' : (sel.getAttribute('aria-label') ?? '')));
        valueEl.textContent = text;
        valueEl.classList.toggle('placeholder', !o || (!o.value && !!sel.dataset.placeholder));
        if (widthDirty && trigger.isConnected) {
            widthDirty = false;
            const w = measure(sel, trigger);
            if (w) trigger.style.setProperty('--dd-w', `${w}px`);
        }
    };

    // Programmatic `select.value = x` / `selectedIndex = n` repaint the trigger.
    Object.defineProperty(sel, 'value', {
        configurable: true,
        get: () => valueDesc.get!.call(sel),
        set: (v: string) => {
            valueDesc.set!.call(sel, v);
            refresh();
        },
    });
    Object.defineProperty(sel, 'selectedIndex', {
        configurable: true,
        get: () => indexDesc.get!.call(sel),
        set: (v: number) => {
            indexDesc.set!.call(sel, v);
            refresh();
        },
    });

    new MutationObserver((muts) => {
        for (const m of muts) {
            if (m.target === sel && m.type === 'attributes') syncAttrs();
            if (m.type === 'childList' || (m.target !== sel && m.attributeName === 'label')) widthDirty = true;
        }
        refresh();
    }).observe(sel, {
        attributes: true,
        childList: true,
        subtree: true,
        characterData: true,
        attributeFilter: [
            'class',
            'disabled',
            'hidden',
            'style',
            'title',
            'aria-label',
            'aria-invalid',
            'selected',
            'label',
        ],
    });

    sel.classList.add('ui-dd-native');
    sel.tabIndex = -1;
    sel.setAttribute('aria-hidden', 'true');
    sel.addEventListener('change', refresh);
    sel.addEventListener('focus', () => trigger.focus());

    // ---- popup -------------------------------------------------------------------------------------------------
    let typed = '';
    let typedAt = 0;

    const open = (initialQuery = ''): void => {
        if (trigger.disabled || current?.sel === sel) return;
        closeDropdown();
        const opts = [...sel.options];
        const useSearch = sel.dataset.search === 'on' || (sel.dataset.search !== 'off' && opts.length > SEARCH_AFTER);
        const list = h('div', { class: 'ui-dd-list', role: 'listbox', id: `${id}-list`, tabindex: '-1' });
        if (trigger.getAttribute('aria-label')) list.setAttribute('aria-label', trigger.getAttribute('aria-label')!);
        const pop = h('div', { class: 'ui-dd-pop' });
        const search = useSearch
            ? (h('input', {
                  type: 'search',
                  class: 'ui-dd-search',
                  placeholder: 'Filter',
                  'aria-label': 'Filter options',
                  autocomplete: 'off',
                  spellcheck: 'false',
              }) as HTMLInputElement)
            : null;
        if (search) pop.append(h('div', { class: 'ui-dd-searchrow' }, icon('search', 12), search));
        pop.append(list);
        const empty = h('div', { class: 'ui-dd-empty', hidden: true }, 'No matches');
        pop.append(empty);

        // rows (option groups become headings)
        const rows: { o: HTMLOptionElement; el: HTMLElement; text: string }[] = [];
        let lastGroup: HTMLOptGroupElement | null = null;
        const heads: { el: HTMLElement; members: HTMLElement[] }[] = [];
        opts.forEach((o, i) => {
            const grp = o.parentElement instanceof HTMLOptGroupElement ? o.parentElement : null;
            if (grp && grp !== lastGroup) {
                const el = h('div', { class: 'ui-dd-group', role: 'presentation' }, grp.label);
                list.append(el);
                heads.push({ el, members: [] });
            }
            lastGroup = grp;
            const disabled = o.disabled || !!grp?.disabled;
            const el = h(
                'div',
                {
                    class: 'ui-dd-opt',
                    role: 'option',
                    id: `${id}-o${i}`,
                    'aria-selected': o.selected ? 'true' : 'false',
                    'aria-disabled': disabled ? 'true' : undefined,
                    'data-i': i,
                },
                h('span', { class: 'ui-dd-check' }, o.selected ? icon('check', 12) : null),
                h('span', { class: 'ui-dd-label' }, optLabel(o) || ' '),
            );
            if (o.title) el.title = o.title;
            list.append(el);
            rows.push({ o, el, text: optLabel(o).toLowerCase() });
            if (grp) heads[heads.length - 1]?.members.push(el);
        });

        let active = -1;
        const visible = (): number[] =>
            rows.flatMap((r, i) => (!r.el.hidden && r.el.getAttribute('aria-disabled') !== 'true' ? [i] : []));
        const setActive = (i: number, scroll = true): void => {
            rows[active]?.el.classList.remove('active');
            active = i;
            const r = rows[i];
            if (!r) {
                (search ?? list).removeAttribute('aria-activedescendant');
                return;
            }
            r.el.classList.add('active');
            (search ?? list).setAttribute('aria-activedescendant', r.el.id);
            if (scroll) r.el.scrollIntoView({ block: 'nearest' });
        };
        const move = (delta: number): void => {
            const vis = visible();
            if (!vis.length) return;
            const at = vis.indexOf(active);
            const next = at < 0 ? (delta > 0 ? 0 : vis.length - 1) : Math.max(0, Math.min(vis.length - 1, at + delta));
            setActive(vis[next]!);
        };
        const filter = (q: string): void => {
            const needle = q.trim().toLowerCase();
            for (const r of rows) r.el.hidden = !!needle && !r.text.includes(needle);
            for (const g of heads) g.el.hidden = g.members.every((m) => m.hidden);
            const vis = visible();
            empty.hidden = vis.length > 0 || rows.every((r) => !r.el.hidden);
            if (!vis.includes(active)) setActive(vis[0] ?? -1);
            position();
        };
        const commit = (i: number): void => {
            const r = rows[i];
            if (!r || r.el.getAttribute('aria-disabled') === 'true') return;
            const changed = !r.o.selected;
            close(true);
            if (!changed) return;
            indexDesc.set!.call(sel, i);
            refresh();
            sel.dispatchEvent(new Event('input', { bubbles: true }));
            sel.dispatchEvent(new Event('change', { bubbles: true }));
        };
        const typeahead = (ch: string): void => {
            const now = performance.now();
            typed = now - typedAt > TYPEAHEAD_MS ? ch : typed + ch;
            typedAt = now;
            const q = typed.toLowerCase();
            const vis = visible();
            const start = Math.max(0, vis.indexOf(active));
            const order = [...vis.slice(start + (typed.length === 1 ? 1 : 0)), ...vis.slice(0, start + 1)];
            const hit =
                order.find((i) => rows[i]!.text.startsWith(q)) ??
                (/^(.)\1+$/.test(q) ? order.find((i) => rows[i]!.text.startsWith(q[0]!)) : undefined);
            if (hit !== undefined) setActive(hit);
        };

        // position: below the trigger, or above when there is more room there; follows scrolling panels
        const position = (): void => {
            const r = trigger.getBoundingClientRect();
            if (r.bottom < 0 || r.top > window.innerHeight || (r.width === 0 && r.height === 0)) {
                close(false);
                return;
            }
            const vw = window.innerWidth;
            const vh = window.innerHeight;
            pop.style.minWidth = `${Math.round(r.width)}px`;
            pop.style.maxWidth = `${Math.max(Math.round(r.width), Math.min(480, vw - 16))}px`;
            pop.style.maxHeight = '';
            const natural = Math.min(pop.scrollHeight + 2, 360);
            const below = vh - r.bottom - 8;
            const above = r.top - 8;
            const up = below < Math.min(natural, 180) && above > below;
            const room = Math.max(96, up ? above : below);
            const hgt = Math.min(natural, room);
            pop.style.maxHeight = `${hgt}px`;
            pop.style.top = `${Math.round(up ? r.top - 2 - hgt : r.bottom + 2)}px`;
            const w = pop.offsetWidth;
            pop.style.left = `${Math.round(Math.max(8, Math.min(r.left, vw - w - 8)))}px`;
            pop.classList.toggle('up', up);
        };

        const onDown = (e: PointerEvent): void => {
            const t = e.target as Node;
            if (!pop.contains(t) && !trigger.contains(t)) close(false);
        };
        const onScroll = (e: Event): void => {
            if (pop.contains(e.target as Node)) return;
            position();
        };
        const onResize = (): void => close(false);
        const close = (focus: boolean): void => {
            if (current?.sel !== sel) return;
            current = null;
            pop.remove();
            trigger.setAttribute('aria-expanded', 'false');
            trigger.classList.remove('open');
            document.removeEventListener('pointerdown', onDown, true);
            document.removeEventListener('scroll', onScroll, true);
            window.removeEventListener('resize', onResize);
            window.removeEventListener('blur', onResize);
            if (focus) trigger.focus({ preventScroll: true });
        };

        const onKey = (e: KeyboardEvent): void => {
            const k = e.key;
            if (k === 'ArrowDown' || k === 'ArrowUp') move(k === 'ArrowDown' ? 1 : -1);
            else if (k === 'PageDown' || k === 'PageUp') move(k === 'PageDown' ? 10 : -10);
            else if ((k === 'Home' || k === 'End') && !(search && search.value)) {
                const vis = visible();
                setActive(k === 'Home' ? vis[0]! : vis[vis.length - 1]!);
            } else if (k === 'Enter') commit(active);
            else if (k === 'Escape') close(true);
            else if (k === 'Tab') {
                close(false);
                return;
            } else if (!search && k.length === 1 && !e.ctrlKey && !e.metaKey && !e.altKey) typeahead(k);
            else return;
            e.preventDefault();
            e.stopPropagation();
        };
        (search ?? list).addEventListener('keydown', onKey);
        search?.addEventListener('input', () => filter(search.value));
        list.addEventListener('pointermove', (e) => {
            const el = (e.target as HTMLElement).closest<HTMLElement>('.ui-dd-opt');
            if (el && el.getAttribute('aria-disabled') !== 'true') setActive(Number(el.dataset.i), false);
        });
        list.addEventListener('pointerdown', (e) => e.preventDefault()); // keep focus in the search box / list
        list.addEventListener('click', (e) => {
            const el = (e.target as HTMLElement).closest<HTMLElement>('.ui-dd-opt');
            if (el) commit(Number(el.dataset.i));
        });

        // a host (e.g. Save mode) can re-map the colour tokens for its own popups
        (trigger.closest('[data-dd-host]') ?? document.body).append(pop);
        trigger.setAttribute('aria-expanded', 'true');
        trigger.classList.add('open');
        current = { sel, close };
        position();
        setActive(
            sel.selectedIndex >= 0 && !rows[sel.selectedIndex]?.el.hasAttribute('aria-disabled')
                ? sel.selectedIndex
                : (visible()[0] ?? -1),
        );
        rows[active]?.el.scrollIntoView({ block: 'center' });
        if (search) {
            search.value = initialQuery;
            if (initialQuery) filter(initialQuery);
            search.focus({ preventScroll: true });
        } else {
            list.focus({ preventScroll: true });
            if (initialQuery) typeahead(initialQuery);
        }
        document.addEventListener('pointerdown', onDown, true);
        document.addEventListener('scroll', onScroll, true);
        window.addEventListener('resize', onResize);
        window.addEventListener('blur', onResize);
    };

    trigger.addEventListener('click', () => (current?.sel === sel ? closeDropdown() : open()));
    trigger.addEventListener('keydown', (e) => {
        const k = e.key;
        if (k === 'ArrowDown' || k === 'ArrowUp' || k === 'Enter' || k === ' ' || k === 'F4') {
            e.preventDefault();
            open();
        } else if (k.length === 1 && !e.ctrlKey && !e.metaKey && !e.altKey) {
            e.preventDefault();
            typed = '';
            open(k);
        }
    });

    enhanced.set(sel, { trigger, refresh, open: () => open() });
    syncAttrs();
    if (sel.parentNode) sel.after(trigger);
    refresh();
    return trigger;
}

/** Re-sync a select's trigger after changing `option.selected` directly (assigning `value` does this already). */
export function refreshSelect(sel: HTMLSelectElement): void {
    enhanced.get(sel)?.refresh();
}

/** The trigger that stands in for `sel`, if it was upgraded. */
export function dropdownTrigger(sel: HTMLSelectElement): HTMLButtonElement | null {
    return enhanced.get(sel)?.trigger ?? null;
}

let installed = false;

/**
 * Upgrade every select now and whenever one is added later (panels build their DOM after start-up). Moves or
 * removes the trigger together with its select.
 */
export function installDropdowns(root: HTMLElement = document.body): void {
    if (installed) return;
    installed = true;
    // `option.selected = true` is a property, not an attribute: repaint the owning select's trigger.
    const selDesc = Object.getOwnPropertyDescriptor(HTMLOptionElement.prototype, 'selected');
    if (selDesc?.set) {
        Object.defineProperty(HTMLOptionElement.prototype, 'selected', {
            configurable: true,
            enumerable: selDesc.enumerable ?? true,
            get(this: HTMLOptionElement) {
                return selDesc.get!.call(this);
            },
            set(this: HTMLOptionElement, v: boolean) {
                selDesc.set!.call(this, v);
                const s = this.closest('select');
                if (s) enhanced.get(s)?.refresh();
            },
        });
    }
    const scan = (n: Node): void => {
        if (n instanceof HTMLSelectElement) enhanceSelect(n);
        else if (n instanceof Element) for (const s of n.querySelectorAll('select')) enhanceSelect(s);
    };
    scan(root);
    new MutationObserver((muts) => {
        for (const m of muts) {
            for (const n of m.addedNodes) scan(n);
            for (const n of m.removedNodes) {
                if (!(n instanceof Element)) continue;
                const sels = n instanceof HTMLSelectElement ? [n] : [...n.querySelectorAll('select')];
                for (const s of sels) {
                    const e = enhanced.get(s);
                    if (!e || s.isConnected) continue;
                    if (current?.sel === s) closeDropdown();
                    if (e.trigger.parentNode !== n && !n.contains(e.trigger)) e.trigger.remove();
                }
            }
        }
    }).observe(root, { childList: true, subtree: true });
}

/** Build a select (upgraded once it is in the document). `options` are `[value, label]` pairs or plain strings. */
export function selectField(
    options: (string | [string, string])[],
    value: string,
    onChange: (v: string) => void,
    attrs: Record<string, string> = {},
): HTMLSelectElement {
    const sel = h('select', attrs) as HTMLSelectElement;
    for (const o of options) {
        const [v, l] = typeof o === 'string' ? [o, o] : o;
        sel.append(h('option', { value: v }, l));
    }
    sel.value = value;
    sel.addEventListener('change', () => onChange(sel.value));
    return sel;
}
