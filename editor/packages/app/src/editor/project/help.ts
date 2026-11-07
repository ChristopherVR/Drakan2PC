import { GUIDES, guideById, guideWebUrl, searchGuides, type Guide } from '@drakan/editor-core';
import { isDesktop } from '../../platform.ts';
import { h, icon, store } from '../dom.ts';
import { registerCommand } from '../registry.ts';
import { hasTour, onTourRegistry, startTour } from '../tour/registry.ts';
import './project.css';

/**
 * The Help drawer: a docked side panel (like Unreal's documentation panel) with the task guides of
 * `editor-core/src/guides.ts`, which are also published on the documentation site (docs/reference/editor-guide.md).
 *
 *   list   a search box over every guide (title, summary, steps, notes) and the matching guides, keyboard navigable
 *   guide  summary, "Show me" (when the tour system has a tour for it), "Open on the web" (the guide's anchor on the
 *          site), the path diagram, numbered steps, notes and the previous / next guide
 *
 * Opened from the Help button in the top bar, Help > Open the help guides, the palette, and the small "?" that
 * `attach` puts on a panel. The drawer can be widened by dragging its left edge.
 */
export interface Help {
    open(guideId?: string): void;
    close(): void;
    toggle(): void;
    /** Put a "?" button in the top right of `host` that opens `guideId`. */
    attach(host: Element | null, guideId: string, label?: string): void;
    isOpen(): boolean;
}

/** Icon of each guide in the list (display only). */
export const GUIDE_ICONS: Record<string, string> = {
    start: 'rocket',
    objects: 'place',
    triggers: 'trigger',
    vendors: 'shop',
    campaign: 'layers',
    multiplayer: 'world',
    story: 'chat',
    menus: 'list',
    package: 'download',
    save: 'save',
    files: 'folder',
    tutorials: 'play',
    landscape: 'ground',
    'new-shop': 'shop',
    quests: 'check-circle',
    conversations: 'chat',
    cutscenes: 'camera',
    ootf: 'download',
    'campaign-pack': 'layers',
    'game-modes': 'world',
    playtest: 'play',
};

const WIDTH_KEY = 'help-width';
const MIN_W = 320;
const MAX_W = 760;

/** The tour a guide's "Show me" starts: its own `tour` id, else the guide id. */
export const tourIdOf = (g: Guide): string => g.tour ?? g.id;

/** The path through a task as small connected steps: the illustration of a guide. */
export function flowDiagram(labels: readonly string[]): HTMLElement {
    const el = h('ol', { class: 'hp-flow', 'aria-label': 'Path through the task' });
    labels.forEach((t, i) => {
        el.append(h('li', {}, h('span', { class: 'n' }, String(i + 1)), h('span', { class: 't' }, t)));
    });
    return el;
}

/** `text` with every search word wrapped in <mark>. */
function marked(text: string, words: readonly string[]): (Node | string)[] {
    if (!words.length) return [text];
    const re = new RegExp(`(${words.map((w) => w.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('|')})`, 'gi');
    return text.split(re).map((part, i) => (i % 2 ? h('mark', {}, part) : part));
}

/** A short piece of the guide around the first search word that is not in its title or summary. */
function snippet(g: Guide, words: readonly string[]): string | null {
    if (!words.length) return null;
    const head = `${g.title} ${g.summary}`.toLowerCase();
    const w = words.find((x) => !head.includes(x));
    if (!w) return null;
    const text = [...g.steps, ...g.notes].find((s) => s.toLowerCase().includes(w));
    if (!text) return null;
    const at = text.toLowerCase().indexOf(w);
    const from = Math.max(0, text.lastIndexOf(' ', Math.max(0, at - 40)) + 1);
    const cut = text.slice(from, from + 110);
    return `${from > 0 ? '...' : ''}${cut}${from + 110 < text.length ? '...' : ''}`;
}

export function createHelp(): Help {
    const bottomGuides = new Map<Element, string>();
    let current: string | null = null;
    let query = '';
    let returnFocus: HTMLElement | null = null;

    const words = (): string[] => query.toLowerCase().split(/\s+/).filter(Boolean);

    // ---- chrome -----------------------------------------------------------------------------------------------------
    const title = h('h2', { id: 'hp-title' }, 'Help');
    const back = h(
        'button',
        {
            class: 'ui-tool hp-back',
            type: 'button',
            title: 'All guides',
            'aria-label': 'Back to all guides',
            onclick: () => show(null, true),
        },
        icon('chevron', 14),
    );
    const webAll = h(
        'a',
        {
            class: 'ui-tool hp-webtop',
            href: guideWebUrl(),
            target: '_blank',
            rel: 'noopener',
            title: 'Open the guides on the web',
            'aria-label': 'Open the guides on the web',
        },
        icon('external', 14),
    );
    const closeBtn = h(
        'button',
        {
            class: 'ui-tool hp-x',
            type: 'button',
            title: 'Close (Esc)',
            'aria-label': 'Close help',
            onclick: () => close(),
        },
        icon('x', 14),
    );
    const input = h('input', {
        type: 'search',
        class: 'ui-input',
        placeholder: 'Search the guides',
        'aria-label': 'Search the guides',
        'aria-controls': 'hp-list',
        spellcheck: 'false',
        autocomplete: 'off',
    });
    const count = h('span', { class: 'hp-count', 'aria-live': 'polite' });
    const search = h('div', { class: 'hp-search' }, h('div', { class: 'ui-search' }, icon('search', 13), input), count);
    const body = h('div', { class: 'hp-body' });
    const grip = h('div', {
        class: 'hp-grip',
        role: 'separator',
        'aria-orientation': 'vertical',
        'aria-label': 'Resize help',
        title: 'Drag to resize, double-click for the default width',
    });
    const el = h(
        'aside',
        {
            class: 'hp-drawer',
            id: 'help-drawer',
            role: 'complementary',
            'aria-labelledby': 'hp-title',
            hidden: true,
        },
        grip,
        h('header', { class: 'hp-head' }, back, title, h('span', { class: 'spacer' }), webAll, closeBtn),
        search,
        body,
    );
    document.body.append(el);

    const savedW = Number(store.get(WIDTH_KEY));
    if (savedW >= MIN_W && savedW <= MAX_W) el.style.width = `${savedW}px`;
    grip.addEventListener('pointerdown', (e) => {
        e.preventDefault();
        grip.setPointerCapture(e.pointerId);
        const move = (ev: PointerEvent): void => {
            const w = Math.round(Math.min(MAX_W, Math.max(MIN_W, window.innerWidth - ev.clientX)));
            el.style.width = `${w}px`;
        };
        const up = (): void => {
            grip.removeEventListener('pointermove', move);
            grip.removeEventListener('pointerup', up);
            store.set(WIDTH_KEY, String(parseInt(el.style.width, 10) || ''));
        };
        grip.addEventListener('pointermove', move);
        grip.addEventListener('pointerup', up);
    });
    grip.addEventListener('dblclick', () => {
        el.style.width = '';
        store.set(WIDTH_KEY, '');
    });

    /** "Open on the web": a normal link in a browser; the desktop app has no browser to hand it to, so it copies it. */
    function webLink(a: HTMLAnchorElement, note?: HTMLElement): void {
        if (!isDesktop) return;
        a.addEventListener('click', (e) => {
            e.preventDefault();
            const url = a.href;
            void navigator.clipboard?.writeText(url).then(
                () => {
                    if (note) note.textContent = 'Link copied. Paste it into your web browser.';
                },
                () => {
                    if (note) note.textContent = url;
                },
            );
        });
    }
    webLink(webAll);

    // ---- list -------------------------------------------------------------------------------------------------------
    function list(): void {
        title.textContent = 'Help';
        back.hidden = true;
        search.hidden = false;
        el.dataset['view'] = 'list';
        const w = words();
        const found = searchGuides(query);
        count.textContent = w.length ? `${found.length} of ${GUIDES.length}` : '';
        const rows = found.map((g) => {
            const snip = snippet(g, w);
            return h(
                'li',
                {},
                h(
                    'button',
                    { class: 'hp-item', type: 'button', 'data-guide': g.id, onclick: () => show(g.id, true) },
                    h('span', { class: 'hp-ico' }, icon(GUIDE_ICONS[g.id] ?? 'help', 16)),
                    h(
                        'span',
                        { class: 'hp-txt' },
                        h('strong', {}, ...marked(g.title, w)),
                        h('span', { class: 'hp-sum' }, ...marked(g.summary, w)),
                        snip ? h('span', { class: 'hp-snip' }, ...marked(snip, w)) : null,
                    ),
                    h('span', { class: 'hp-meta' }, `${g.steps.length} steps`),
                ),
            );
        });
        body.replaceChildren(
            rows.length
                ? h('ul', { class: 'hp-list', id: 'hp-list', 'aria-label': 'Guides' }, ...rows)
                : h(
                      'div',
                      { class: 'hp-none' },
                      h('strong', {}, 'No guide matches'),
                      h('span', {}, 'Try another word, or press Ctrl+P to search every command.'),
                  ),
            h(
                'p',
                { class: 'hp-foot' },
                h('kbd', {}, 'Ctrl+P'),
                ' searches every command, object and level. ',
                h('kbd', {}, 'F1'),
                ' lists the keyboard shortcuts. The "?" on a panel opens its guide.',
            ),
        );
    }

    // ---- guide ------------------------------------------------------------------------------------------------------
    function guide(g: Guide): void {
        title.textContent = g.title;
        back.hidden = false;
        search.hidden = true;
        el.dataset['view'] = 'guide';
        const w = words();
        const i = GUIDES.indexOf(g);
        const prev = GUIDES[i - 1];
        const next = GUIDES[i + 1];
        const note = h('span', { class: 'hp-note', 'aria-live': 'polite' });
        const web = h(
            'a',
            {
                class: 'ui-btn sm hp-web',
                href: guideWebUrl(g.id),
                target: '_blank',
                rel: 'noopener',
                title: guideWebUrl(g.id),
            },
            icon('external', 13),
            'Open on the web',
        );
        webLink(web, note);
        const tour = tourIdOf(g);
        const showMe = hasTour(tour)
            ? h(
                  'button',
                  {
                      class: 'ui-btn sm primary hp-tour',
                      type: 'button',
                      title: 'Walk through this task in the editor',
                      onclick: () => {
                          close(false);
                          if (!startTour(tour)) {
                              open(g.id);
                              note.textContent = 'That walkthrough cannot start here yet.';
                          }
                      },
                  },
                  icon('play', 13),
                  'Show me',
              )
            : null;
        const heading = h('p', { class: 'hp-lead', tabindex: '-1' }, ...marked(g.summary, w));
        body.replaceChildren(
            h(
                'div',
                { class: 'hp-hero' },
                h('span', { class: 'hp-ico lg' }, icon(GUIDE_ICONS[g.id] ?? 'help', 20)),
                heading,
            ),
            h('div', { class: 'hp-actions' }, showMe, web, note),
            flowDiagram(g.flow),
            h('h3', {}, 'Steps'),
            h(
                'ol',
                { class: 'hp-steps' },
                ...g.steps.map((s, n) =>
                    h(
                        'li',
                        {},
                        h('span', { class: 'n', 'aria-hidden': 'true' }, String(n + 1)),
                        h('p', {}, ...marked(s, w)),
                    ),
                ),
            ),
            ...(g.notes.length
                ? [
                      h('h3', {}, 'Good to know'),
                      h('ul', { class: 'hp-notes' }, ...g.notes.map((s) => h('li', {}, ...marked(s, w)))),
                  ]
                : []),
            h(
                'nav',
                { class: 'hp-pager', 'aria-label': 'More guides' },
                prev
                    ? h(
                          'button',
                          { class: 'hp-page prev', type: 'button', onclick: () => show(prev.id, true) },
                          h('small', {}, 'Previous'),
                          prev.title,
                      )
                    : h('span', {}),
                next
                    ? h(
                          'button',
                          { class: 'hp-page next', type: 'button', onclick: () => show(next.id, true) },
                          h('small', {}, 'Next'),
                          next.title,
                      )
                    : h('span', {}),
            ),
        );
        body.scrollTop = 0;
    }

    /** Show a guide (or the list). `focus` moves the keyboard focus into the drawer. */
    function show(id: string | null, focus = false): void {
        const from = current;
        current = id;
        const g = id ? guideById(id) : undefined;
        if (g) guide(g);
        else {
            current = null;
            list();
        }
        if (!focus) return;
        if (g) body.querySelector<HTMLElement>('.hp-lead')?.focus({ preventScroll: true });
        else {
            const row = from ? body.querySelector<HTMLElement>(`[data-guide="${from}"]`) : null;
            (row ?? input).focus({ preventScroll: true });
            row?.scrollIntoView({ block: 'nearest' });
        }
    }

    function open(id?: string): void {
        const wasOpen = !el.hidden;
        if (!wasOpen) {
            const a = document.activeElement;
            returnFocus = a instanceof HTMLElement && a !== document.body ? a : null;
        }
        // A "?" opens its guide fresh, without the last search highlighted.
        if (id) query = input.value = '';
        // Visible first: a hidden element cannot take the focus.
        el.hidden = false;
        document.body.classList.add('help-open');
        show(id ?? current, true);
    }
    function close(restore = true): void {
        if (el.hidden) return;
        el.hidden = true;
        document.body.classList.remove('help-open');
        if (restore && returnFocus?.isConnected) returnFocus.focus({ preventScroll: true });
        returnFocus = null;
    }

    // Re-draw when the tour system registers, so "Show me" appears on the open guide.
    onTourRegistry(() => {
        if (!el.hidden && current) show(current);
    });

    // ---- keyboard -----------------------------------------------------------------------------------------------------
    input.addEventListener('input', () => {
        query = input.value;
        list();
    });
    el.addEventListener('keydown', (e) => {
        const t = e.target as HTMLElement;
        if (e.key === 'Escape') {
            e.stopPropagation();
            e.preventDefault();
            if (t === input && input.value) {
                query = input.value = '';
                list();
            } else close();
            return;
        }
        if (el.dataset['view'] === 'guide') {
            if (e.key === 'Backspace' && t.tagName !== 'INPUT' && t.tagName !== 'TEXTAREA') {
                e.preventDefault();
                show(null, true);
            }
            return;
        }
        const items = [...body.querySelectorAll<HTMLElement>('.hp-item')];
        const at = items.indexOf(t);
        const go = (i: number): void => {
            const it = items[Math.max(0, Math.min(items.length - 1, i))];
            if (it) {
                e.preventDefault();
                it.focus();
                it.scrollIntoView({ block: 'nearest' });
            }
        };
        if (t === input) {
            if (e.key === 'ArrowDown') go(0);
            else if (e.key === 'Enter' && items[0]) {
                e.preventDefault();
                items[0].click();
            }
        } else if (at >= 0) {
            if (e.key === 'ArrowDown') go(at + 1);
            else if (e.key === 'ArrowUp') {
                if (at === 0) {
                    e.preventDefault();
                    input.focus();
                } else go(at - 1);
            } else if (e.key === 'Home') go(0);
            else if (e.key === 'End') go(items.length - 1);
            else if (e.key.length === 1 && !e.ctrlKey && !e.metaKey && !e.altKey) {
                // Typing on a row continues the search.
                input.focus();
            }
        }
    });
    // Esc with the focus outside the drawer still closes it (not while the palette or a dialog is up).
    window.addEventListener('keydown', (e) => {
        if (
            e.key === 'Escape' &&
            !el.hidden &&
            !el.contains(document.activeElement) &&
            !document.querySelector('.pal-overlay:not([hidden]), dialog[open], .help-overlay:not([hidden])')
        )
            close(false);
    });

    // ---- commands -----------------------------------------------------------------------------------------------------
    registerCommand({
        id: 'help.guides',
        title: 'Open the help guides',
        category: 'Help',
        icon: 'help',
        keywords: 'how to tutorial guide docs documentation manual',
        hint: 'Short guides for placing objects, campaigns, maps, story, menus, packaging and saves',
        run: () => (el.hidden ? open() : close()),
    });
    registerCommand({
        id: 'help.web',
        title: 'Open the guides on the web',
        category: 'Help',
        icon: 'external',
        keywords: 'documentation site website online manual pages',
        run: () => {
            if (isDesktop) open();
            else window.open(guideWebUrl(), '_blank', 'noopener');
        },
    });
    for (const g of GUIDES) {
        registerCommand({
            id: `help.guide.${g.id}`,
            title: `Help: ${g.title}`,
            category: 'Help',
            icon: GUIDE_ICONS[g.id] ?? 'help',
            keywords: `how to guide ${g.summary} ${g.flow.join(' ')}`,
            run: () => open(g.id),
        });
    }

    // Top bar button, once.
    const bar = document.getElementById('appbar');
    if (bar && !document.getElementById('help-btn')) {
        const b = h(
            'button',
            {
                class: 'ui-btn sm hp-top',
                id: 'help-btn',
                type: 'button',
                title: 'Guides for building with the editor',
                'aria-controls': 'help-drawer',
                onclick: () => (el.hidden ? open() : close()),
            },
            icon('help', 14),
            'Help',
        );
        bar.querySelector('.spacer')?.after(b);
    }

    /** Show only the innermost "?" of nested panels (Details and the World Settings tab inside it, say). */
    function tidy(): void {
        const qs = [...document.querySelectorAll<HTMLElement>('.hp-q:not(.hp-strip)')];
        for (const q of qs) {
            const host = q.parentElement;
            const inner =
                host && qs.some((o) => o !== q && host.contains(o) && o.parentElement?.getClientRects().length);
            q.classList.toggle('hp-shadowed', !!inner);
        }
    }

    return {
        open,
        close: () => close(),
        toggle: () => (el.hidden ? open() : close()),
        isOpen: () => !el.hidden,
        attach(host, guideId, label = 'Open the guide for this panel') {
            if (!host) return;
            const name = guideById(guideId)?.title;
            const tip = name ? `Help: ${name}` : label;
            const strip = host.closest('#ed-bottom')?.querySelector<HTMLElement>('.dock-tabs');
            if (strip) {
                // Bottom dock: one "?" at the end of the tab strip that opens the guide of the tab being shown, so it
                // never sits on top of a panel's own buttons.
                bottomGuides.set(host, guideId);
                // A panel moved into the dock from a tab of its own drops the "?" it had there.
                host.querySelector(':scope > .hp-q')?.remove();
                if (!strip.querySelector(':scope > .hp-q')) {
                    strip.append(
                        h(
                            'button',
                            {
                                class: 'hp-q hp-strip',
                                type: 'button',
                                title: 'Open the guide for this panel',
                                'aria-label': 'Open the guide for this panel',
                                onclick: () => {
                                    const shown = [...bottomGuides.keys()].find((x) => x.getClientRects().length > 0);
                                    open(shown ? bottomGuides.get(shown) : undefined);
                                },
                            },
                            '?',
                        ),
                    );
                }
                return;
            }
            bottomGuides.delete(host);
            const have = host.querySelector<HTMLElement>(':scope > .hp-q');
            if (have) {
                tidy();
                return;
            }
            if (getComputedStyle(host).position === 'static') (host as HTMLElement).style.position = 'relative';
            host.append(
                h(
                    'button',
                    {
                        class: 'hp-q',
                        type: 'button',
                        title: tip,
                        'aria-label': tip,
                        'data-help': guideId,
                        onclick: (e: Event) => {
                            e.stopPropagation();
                            open(guideId);
                        },
                    },
                    '?',
                ),
            );
            tidy();
        },
    };
}
