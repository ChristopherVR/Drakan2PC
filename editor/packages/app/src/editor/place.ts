import { searchCatalog, type AssetClass } from '@drakan/editor-core';
import { KINDS, KIND_HINT } from '../level.ts';
import { attachAssetDrag, dragState } from './assetdrag.ts';
import type { Ctx, Panel } from './ctx.ts';
import { fill, h, icon, raf, store } from './dom.ts';
import { emptyState, searchField } from './ui.ts';

export interface PlacePanel extends Panel {
    /** Put a finished model thumbnail (PNG data URL) into every row of a class. */
    applyThumbnail(resource: string, url: string): void;
}

const PER_GROUP = 80;
const hexColor = (n: number): string => `#${n.toString(16).padStart(6, '0')}`;
const colorOf = (cls: AssetClass): string => hexColor(KINDS.find((k) => k.kind === cls.kind)?.color ?? 0x888888);

/**
 * Place panel: a compact palette for putting new objects into the level. Classes are grouped by kind; the default
 * scope is the classes the level already uses (the safe ones: new instances copy an existing object). Drag a row into
 * the viewport, or double-click / press Enter to place it at the view centre.
 */
export function createPlace(ctx: Ctx): PlacePanel {
    const searchBox = searchField('Search classes', 'Search classes to place');
    let scope: 'level' | 'all' = store.get('place-scope') === 'all' ? 'all' : 'level';
    let kind: string | null = null;
    let query = '';
    const collapsed = new Set<string>();
    const showAll = new Set<string>();
    const dragImages = new Map<string, HTMLImageElement>();

    const scopeBtns = (['level', 'all'] as const).map((sc) => {
        const b = h(
            'button',
            { class: `seg-btn${sc === scope ? ' active' : ''}`, type: 'button', 'data-scope': sc },
            sc === 'level' ? 'In this level' : 'All classes',
        );
        b.addEventListener('click', () => {
            scope = sc;
            store.set('place-scope', sc);
            for (const x of scopeBtns) x.classList.toggle('active', x.dataset.scope === sc);
            build();
        });
        return b;
    });
    const kindBar = h('div', { class: 'chips place-kinds' });
    const list = h('div', { class: 'place-list', role: 'listbox', 'aria-label': 'Classes to place' });
    const count = h('span', { class: 'dim count' });
    const root = h(
        'div',
        { class: 'panel place' },
        h('div', { class: 'panel-head' }, h('span', { class: 'panel-title' }, icon('place', 14), 'Place'), count),
        h('div', { class: 'panel-tools' }, searchBox.el),
        h('div', { class: 'panel-tools seg-row' }, h('div', { class: 'seg' }, ...scopeBtns)),
        kindBar,
        list,
    );

    function remember(resource: string, url: string): void {
        if (dragImages.has(resource)) return;
        const pre = new Image();
        pre.src = url;
        dragImages.set(resource, pre);
    }

    function thumb(cls: AssetClass): HTMLElement {
        const box = h('div', { class: 'thumb sm', style: `--k:${colorOf(cls)}`, 'data-res': cls.resource });
        const url = ctx.classThumb(cls);
        if (url) {
            box.append(h('img', { src: url, alt: '', draggable: false }));
            remember(cls.resource, url);
        } else box.append(icon(cls.icon, 18));
        return box;
    }

    function rowFor(cls: AssetClass): HTMLElement {
        const r = h(
            'div',
            {
                class: 'ui-row place-row',
                tabindex: 0,
                role: 'option',
                'data-res': cls.resource,
                title: `${cls.name}\n${cls.db} class ${cls.classId}\nDrag into the viewport, or double-click to place at the view centre`,
            },
            thumb(cls),
            h('span', { class: 'name' }, cls.name),
            cls.instances ? h('span', { class: 'dim n' }, String(cls.instances)) : null,
        );
        attachAssetDrag(
            r,
            cls,
            ctx,
            (res) => dragImages.get(res),
            (c) => void ctx.classThumb(c),
            '',
        );
        r.addEventListener('dblclick', () => {
            if (!dragState.suppressClick) ctx.placeClass(cls);
        });
        r.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') ctx.placeClass(cls);
        });
        return r;
    }

    function build(): void {
        const all = ctx.catalog();
        const present = new Set(all.map((c) => c.kind));
        fill(
            kindBar,
            ...[null, ...KINDS.filter((k) => present.has(k.kind))].map((k) => {
                const b = h(
                    'button',
                    {
                        class: `chip${(k?.kind ?? null) === kind ? ' active' : ''}`,
                        type: 'button',
                        title: k ? `${k.label}: ${KIND_HINT[k.kind]}` : 'All kinds',
                    },
                    k ? h('i', { class: 'ui-dot', style: `background:${hexColor(k.color)}` }) : null,
                    k ? k.label : 'All',
                );
                b.addEventListener('click', () => {
                    kind = k?.kind ?? null;
                    build();
                });
                return b;
            }),
        );
        if (!all.length) {
            count.textContent = '';
            fill(
                list,
                emptyState('place', 'Nothing to place yet', 'Open a level to fill this palette with its classes.'),
            );
            return;
        }
        let items = searchCatalog(all, query);
        if (scope === 'level') items = items.filter((c) => c.instances > 0);
        if (kind) items = items.filter((c) => c.kind === kind);
        count.textContent = `${items.length}`;
        if (!items.length) {
            fill(
                list,
                emptyState(
                    'search',
                    'No classes match',
                    scope === 'level' ? 'Try All classes to see everything the disc offers.' : 'Clear the search.',
                ),
            );
            return;
        }
        const byKind = new Map<string, AssetClass[]>();
        for (const c of items) {
            const l = byKind.get(c.kind) ?? [];
            byKind.set(c.kind, l);
            l.push(c);
        }
        const nodes: HTMLElement[] = [];
        for (const k of KINDS) {
            const g = byKind.get(k.kind);
            if (!g) continue;
            g.sort((a, b) => b.instances - a.instances || a.name.localeCompare(b.name));
            const closed = collapsed.has(k.kind) && !query;
            const head = h(
                'button',
                { class: `sec-head${closed ? '' : ' open'}`, type: 'button', 'aria-expanded': String(!closed) },
                icon('chevron', 12),
                h('i', { class: 'ui-dot', style: `background:${hexColor(k.color)}` }),
                k.label,
                h('span', { class: 'dim n' }, String(g.length)),
            );
            head.addEventListener('click', () => {
                if (collapsed.has(k.kind)) collapsed.delete(k.kind);
                else collapsed.add(k.kind);
                build();
            });
            const sec = h('section', { class: 'place-group' }, head);
            if (!closed) {
                const lim = showAll.has(k.kind) ? g.length : PER_GROUP;
                for (const c of g.slice(0, lim)) sec.append(rowFor(c));
                if (g.length > lim) {
                    const more = h(
                        'button',
                        { class: 'ui-btn sm more', type: 'button' },
                        `Show ${g.length - lim} more`,
                    );
                    more.addEventListener('click', () => {
                        showAll.add(k.kind);
                        build();
                    });
                    sec.append(more);
                }
            }
            nodes.push(sec);
        }
        const top = list.scrollTop;
        fill(list, ...nodes);
        list.scrollTop = top;
    }

    let timer = 0;
    searchBox.input.addEventListener('input', () => {
        window.clearTimeout(timer);
        timer = window.setTimeout(() => {
            query = searchBox.input.value.trim();
            build();
        }, 120);
    });
    const rebuildSoon = raf(build);
    let lastSum = 0;
    return {
        el: root,
        applyThumbnail(resource, url) {
            remember(resource, url);
            for (const box of root.querySelectorAll<HTMLElement>('.thumb'))
                if (box.dataset.res === resource)
                    box.replaceChildren(h('img', { src: url, alt: '', draggable: false }));
        },
        onLoad() {
            showAll.clear();
            dragImages.clear();
            build();
        },
        onChange() {
            // Instance counts are updated by the Content Browser; only rebuild when they changed.
            let sum = 0;
            for (const c of ctx.catalog()) sum = (sum * 31 + c.instances) | 0;
            if (sum === lastSum) return;
            lastSum = sum;
            rebuildSoon();
        },
    };
}
