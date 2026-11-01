import { GUIDES, guideById, type Guide } from '@drakan/editor-core';
import { h, icon } from '../dom.ts';
import { registerCommand } from '../registry.ts';
import './project.css';

/**
 * The Help drawer: short task guides (text in `editor-core/src/guides.ts`, mirrored in docs/reference/editor-guide.md).
 * Opened from the top bar, the palette, and the small "?" on each panel.
 */
export interface Help {
    open(guideId?: string): void;
    close(): void;
    toggle(): void;
    /** Put a "?" button in the top right of `host` that opens `guideId`. */
    attach(host: Element | null, guideId: string, label?: string): void;
    isOpen(): boolean;
}

/** The path through a task as small connected steps: the illustration of a guide. */
export function flowDiagram(labels: readonly string[]): HTMLElement {
    const el = h('ol', { class: 'hp-flow', 'aria-label': 'Path through the task' });
    labels.forEach((t, i) => {
        el.append(h('li', {}, h('span', { class: 'n' }, String(i + 1)), h('span', { class: 't' }, t)));
    });
    return el;
}

export function createHelp(): Help {
    const bottomGuides = new Map<Element, string>();
    const body = h('div', { class: 'hp-body' });
    const title = h('h2', {}, 'Help');
    const back = h(
        'button',
        { class: 'ui-btn sm hp-back', type: 'button', hidden: true, onclick: () => show(null) },
        icon('chevron', 12),
        'All guides',
    );
    const closeBtn = h(
        'button',
        { class: 'ui-btn sm hp-x', type: 'button', 'aria-label': 'Close help', onclick: () => close() },
        icon('x', 14),
    );
    const el = h(
        'aside',
        { class: 'hp-drawer', id: 'help-drawer', role: 'complementary', 'aria-label': 'Help', hidden: true },
        h('header', {}, back, title, h('span', { class: 'spacer' }), closeBtn),
        body,
    );
    document.body.append(el);
    let current: string | null = null;

    function list(): void {
        title.textContent = 'Help';
        back.hidden = true;
        body.replaceChildren(
            h('p', { class: 'hp-lead' }, 'Short guides for the things people build with the editor. Pick one.'),
            ...GUIDES.map((g) =>
                h(
                    'button',
                    { class: 'hp-item', type: 'button', onclick: () => show(g.id), 'data-guide': g.id },
                    h('strong', {}, g.title),
                    h('span', {}, g.summary),
                ),
            ),
            h(
                'p',
                { class: 'hp-foot' },
                'Press ',
                h('kbd', {}, 'Ctrl+P'),
                ' to search every command, ',
                h('kbd', {}, '?'),
                ' for the keyboard shortcuts.',
            ),
        );
    }

    function guide(g: Guide): void {
        title.textContent = g.title;
        back.hidden = false;
        body.replaceChildren(
            h('p', { class: 'hp-lead' }, g.summary),
            flowDiagram(g.flow),
            h('h3', {}, 'Steps'),
            h('ol', { class: 'hp-steps' }, ...g.steps.map((s) => h('li', {}, s))),
            ...(g.notes.length
                ? [h('h3', {}, 'Good to know'), h('ul', { class: 'hp-notes' }, ...g.notes.map((s) => h('li', {}, s)))]
                : []),
        );
        body.scrollTop = 0;
    }

    function show(id: string | null): void {
        current = id;
        const g = id ? guideById(id) : undefined;
        if (g) guide(g);
        else list();
    }

    function open(id?: string): void {
        show(id ?? current);
        el.hidden = false;
        document.body.classList.add('help-open');
    }
    function close(): void {
        el.hidden = true;
        document.body.classList.remove('help-open');
    }

    window.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' && !el.hidden && !document.querySelector('.pal-overlay:not([hidden])')) close();
    });

    registerCommand({
        id: 'help.guides',
        title: 'Open the help guides',
        category: 'Help',
        icon: 'help',
        keywords: 'how to tutorial guide docs',
        hint: 'Short guides for placing objects, maps, story, packaging and saves',
        run: () => (el.hidden ? open() : close()),
    });
    for (const g of GUIDES) {
        registerCommand({
            id: `help.guide.${g.id}`,
            title: `Help: ${g.title}`,
            category: 'Help',
            keywords: `how to guide ${g.summary}`,
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
                onclick: () => (el.hidden ? open() : close()),
            },
            icon('help', 14),
            'Help',
        );
        bar.querySelector('.spacer')?.after(b);
    }
    return {
        open,
        close,
        toggle: () => (el.hidden ? open() : close()),
        isOpen: () => !el.hidden,
        attach(host, guideId, label = 'Open the guide for this panel') {
            if (!host) return;
            const strip = host.closest('#ed-bottom')?.querySelector<HTMLElement>('.dock-tabs');
            if (strip) {
                // Bottom dock: one "?" at the end of the tab strip that opens the guide of the tab being shown, so it
                // never sits on top of a panel's own buttons.
                bottomGuides.set(host, guideId);
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
                                    const shown = [...bottomGuides.keys()].find((el) => el.getClientRects().length > 0);
                                    open(shown ? bottomGuides.get(shown) : undefined);
                                },
                            },
                            '?',
                        ),
                    );
                }
                return;
            }
            if (host.querySelector(':scope > .hp-q')) return;
            if (getComputedStyle(host).position === 'static') (host as HTMLElement).style.position = 'relative';
            host.append(
                h(
                    'button',
                    {
                        class: 'hp-q',
                        type: 'button',
                        title: label,
                        'aria-label': label,
                        onclick: (e: Event) => {
                            e.stopPropagation();
                            open(guideId);
                        },
                    },
                    '?',
                ),
            );
        },
    };
}
