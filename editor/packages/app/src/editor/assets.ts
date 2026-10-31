import { groupCatalog, searchCatalog, type AssetClass, type AssetIcon } from '@drakan/editor-core';
import { KINDS } from '../level.ts';
import type { Ctx, Panel } from './ctx.ts';
import { fill, h, hex, icon, raf, store } from './dom.ts';

/** Supplies a thumbnail for a class (the model renderer plugs in here); null means "use the icon". */
export type ThumbnailProvider = (cls: AssetClass) => HTMLCanvasElement | HTMLImageElement | string | null;

export interface AssetsPanel extends Panel {
    favourites: Set<string>;
    setThumbnailProvider(p: ThumbnailProvider | null): void;
    /** Re-render tiles (after thumbnails became available). */
    refreshTiles(): void;
    /**
     * Lazy thumbnails: called once per tile when it scrolls into view and the provider has nothing yet. The loader
     * produces the image in the background and then calls `applyThumbnail`.
     */
    setThumbnailLoader(fn: ((cls: AssetClass) => void) | null): void;
    /** Put a finished thumbnail (PNG data URL) into every tile and the detail view of a class, without a rebuild. */
    applyThumbnail(resource: string, url: string): void;
}

const PER_GROUP = 160;

const kindColor = (cls: AssetClass): string =>
    `#${(KINDS.find((k) => k.kind === cls.kind)?.color ?? 0x888888).toString(16).padStart(6, '0')}`;

/**
 * Asset Browser: every class the level can use, grouped by category, with search, favourites and a size slider. Drag a
 * tile into the viewport to place a new instance where the cursor meets the ground; double-click places it at the
 * centre of the view. Thumbnails come from an optional provider; without one a coloured icon stands in.
 */
export function createAssets(ctx: Ctx): AssetsPanel {
    const favs = new Set<string>(store.json<string[]>('favourites', []));
    const saveFavs = (): void => store.set('favourites', JSON.stringify([...favs]));
    let provider: ThumbnailProvider | null = null;
    let loader: ((cls: AssetClass) => void) | null = null;
    /** Pre-decoded images for the drag preview (setDragImage needs a loaded image). */
    const dragImages = new Map<string, HTMLImageElement>();
    let suppressClick = false;
    const classOf = new Map<string, AssetClass>();
    const lazy =
        typeof IntersectionObserver === 'undefined'
            ? null
            : new IntersectionObserver(
                  (entries) => {
                      for (const en of entries) {
                          if (!en.isIntersecting) continue;
                          lazy?.unobserve(en.target);
                          const cls = classOf.get((en.target as HTMLElement).dataset.res ?? '');
                          if (cls) loader?.(cls);
                      }
                  },
                  { rootMargin: '120px' },
              );
    let filter: 'all' | 'fav' | 'level' = 'all';
    let query = '';
    let selected: string | null = null;
    const collapsed = new Set<string>(store.json<string[]>('assets-collapsed', []));
    const showAll = new Set<string>();

    const search = h('input', {
        type: 'search',
        placeholder: 'Search classes by name, group or id',
        'aria-label': 'Search classes',
    });
    const info = h('span', { class: 'dim' });
    const size = h('input', {
        type: 'range',
        min: 64,
        max: 140,
        value: store.get('tile-size') ?? '88',
        'aria-label': 'Tile size',
        class: 'slider',
    });
    const chips = (['all', 'fav', 'level'] as const).map((f) =>
        h(
            'button',
            {
                class: `chip${f === filter ? ' active' : ''}`,
                type: 'button',
                'data-f': f,
                onclick: () => {
                    filter = f;
                    for (const c of chips) c.classList.toggle('active', c.dataset.f === f);
                    build();
                },
            },
            f === 'all' ? 'All' : f === 'fav' ? 'Favourites' : 'In this level',
        ),
    );
    const body = h('div', { class: 'assets-body' });
    const detail = h(
        'div',
        { class: 'assets-detail' },
        h('span', { class: 'dim' }, 'Select a class to see its details. Drag a tile into the viewport to place it.'),
    );
    const root = h(
        'div',
        { class: 'assets' },
        h(
            'div',
            { class: 'assets-bar' },
            search,
            h('div', { class: 'chips' }, ...chips),
            info,
            h('span', { class: 'spacer' }),
            h('label', { class: 'size' }, 'Size', size),
        ),
        h('div', { class: 'assets-main' }, body, detail),
    );
    body.style.setProperty('--tile', `${size.value}px`);
    size.addEventListener('input', () => {
        body.style.setProperty('--tile', `${size.value}px`);
        store.set('tile-size', size.value);
    });

    function thumbImg(resource: string, url: string): HTMLImageElement {
        const img = h('img', { src: url, alt: '', draggable: false });
        if (!dragImages.has(resource)) {
            const pre = new Image();
            pre.src = url;
            dragImages.set(resource, pre);
        }
        return img;
    }

    function thumb(cls: AssetClass): HTMLElement {
        const box = h('div', { class: 'thumb', style: `--k:${kindColor(cls)}`, 'data-res': cls.resource });
        classOf.set(cls.resource, cls);
        const t = provider?.(cls) ?? null;
        if (t instanceof HTMLElement) box.append(t);
        else if (typeof t === 'string' && t) box.append(thumbImg(cls.resource, t));
        else {
            box.append(icon(cls.icon satisfies AssetIcon, 28));
            if (loader) {
                if (lazy) lazy.observe(box);
                else loader(cls);
            }
        }
        return box;
    }

    function tile(cls: AssetClass): HTMLElement {
        const star = h('button', {
            class: `star${favs.has(cls.resource) ? ' on' : ''}`,
            type: 'button',
            title: favs.has(cls.resource) ? 'Remove from favourites' : 'Add to favourites',
            'aria-label': 'Toggle favourite',
        });
        star.append(icon('star', 13));
        star.addEventListener('click', (e) => {
            e.stopPropagation();
            if (favs.has(cls.resource)) favs.delete(cls.resource);
            else favs.add(cls.resource);
            saveFavs();
            build();
        });
        const t = h(
            'div',
            {
                class: `tile${selected === cls.resource ? ' sel' : ''}`,
                'data-res': cls.resource,
                title: `${cls.name}\n${cls.db} class ${cls.classId} (${cls.resource})`,
            },
            thumb(cls),
            h('span', { class: 'label' }, cls.name),
            cls.instances ? h('span', { class: 'badge' }, String(cls.instances)) : null,
            star,
        );
        // Pointer-based drag: works with a real mouse, touch and pen, and shows the model thumbnail as the ghost.
        // (The native HTML5 drag is not used: browsers differ and automation cannot start it.)
        t.addEventListener('pointerdown', (e) => {
            if (e.button !== 0 || (e.target as HTMLElement).closest('.star')) return;
            const sx = e.clientX;
            const sy = e.clientY;
            const view = ctx.view();
            let ghost: HTMLElement | null = null;
            const pre = dragImages.get(cls.resource);
            const move = (m: PointerEvent): void => {
                if (!ghost) {
                    if (Math.hypot(m.clientX - sx, m.clientY - sy) < 5) return;
                    view.dragKey = cls.resource;
                    loader?.(cls);
                    const img = dragImages.get(cls.resource) ?? pre;
                    ghost = h('div', { class: 'drag-ghost live' }, img?.complete ? img.cloneNode() : null, cls.name);
                    document.body.append(ghost);
                    document.body.classList.add('dragging-asset');
                }
                ghost.style.left = `${m.clientX + 12}px`;
                ghost.style.top = `${m.clientY + 12}px`;
                if (view.isOverViewport(m.clientX, m.clientY)) {
                    view.dragHover(m);
                    ghost.classList.add('over');
                } else {
                    view.placeGhostHide();
                    ghost.classList.remove('over');
                }
            };
            const end = (u: PointerEvent, cancelled: boolean): void => {
                window.removeEventListener('pointermove', move);
                window.removeEventListener('pointerup', up);
                window.removeEventListener('pointercancel', cancel);
                document.body.classList.remove('dragging-asset');
                if (!ghost) return;
                ghost.remove();
                if (!cancelled && view.isOverViewport(u.clientX, u.clientY)) view.dragDrop(u);
                else view.dragCancel();
                suppressClick = true;
                setTimeout(() => (suppressClick = false), 0);
            };
            const up = (u: PointerEvent): void => end(u, false);
            const cancel = (u: PointerEvent): void => end(u, true);
            window.addEventListener('pointermove', move);
            window.addEventListener('pointerup', up);
            window.addEventListener('pointercancel', cancel);
        });
        t.addEventListener('click', () => {
            if (suppressClick) return;
            selected = cls.resource;
            body.querySelectorAll('.tile.sel').forEach((x) => x.classList.remove('sel'));
            t.classList.add('sel');
            showDetail(cls);
        });
        t.addEventListener('dblclick', () => ctx.placeClass(cls));
        return t;
    }

    function showDetail(cls: AssetClass): void {
        const row = (k: string, v: string): HTMLElement =>
            h('div', { class: 'kv' }, h('span', { class: 'dim' }, k), h('span', {}, v));
        fill(
            detail,
            h(
                'div',
                { class: 'detail-head' },
                thumb(cls),
                h('div', {}, h('strong', {}, cls.name), h('div', { class: 'dim' }, cls.category)),
            ),
            row('Resource', cls.resource),
            row('ODB', `${cls.db} (index ${cls.dbIndex})`),
            row('Class id', `${cls.classId} (0x${hex(cls.classId)})`),
            row('Engine class', cls.runtimeClass === null ? 'n/a' : `0x${hex(cls.runtimeClass)}`),
            row('OotF class id', cls.rflClass === null ? 'n/a' : String(cls.rflClass)),
            row('In this level', String(cls.instances)),
            h(
                'button',
                { class: 'btn primary', type: 'button', onclick: () => ctx.placeClass(cls) },
                icon('plus', 14),
                'Place at view centre',
            ),
            h(
                'p',
                { class: 'dim small' },
                cls.instances
                    ? 'New instances copy the fields of an existing one.'
                    : 'No instance in this level: the new object gets the class defaults (unverified in game).',
            ),
        );
    }

    function build(): void {
        const all = ctx.catalog();
        let list = searchCatalog(all, query);
        if (filter === 'fav') list = list.filter((c) => favs.has(c.resource));
        if (filter === 'level') list = list.filter((c) => c.instances > 0);
        info.textContent = all.length ? `${list.length} of ${all.length} classes` : '';
        if (!all.length) {
            fill(body, h('div', { class: 'empty-note' }, 'Open a level from the disc to browse its classes.'));
            return;
        }
        if (!list.length) {
            fill(
                body,
                h(
                    'div',
                    { class: 'empty-note' },
                    filter === 'fav' ? 'No favourites yet: click the star on a tile.' : 'No classes match.',
                ),
            );
            return;
        }
        // The "Favourites" and "In this level" groups only make sense on the All tab.
        let groups = groupCatalog(list, favs);
        if (filter !== 'all' || query)
            groups = groups.filter((g) => g.title !== 'Favourites' && g.title !== 'In this level');
        if (filter === 'all' && !query)
            groups = groups.filter((g) => g.title !== 'In this level' || g.items.length < all.length);
        const nodes: HTMLElement[] = [];
        for (const g of groups) {
            const key = g.title;
            const closed = collapsed.has(key) && !query;
            const head = h(
                'button',
                { class: `ghead${closed ? '' : ' open'}`, type: 'button', 'aria-expanded': String(!closed) },
                icon('chevron', 12),
                h('span', {}, g.title),
                h('span', { class: 'dim' }, String(g.items.length)),
            );
            head.addEventListener('click', () => {
                if (collapsed.has(key)) collapsed.delete(key);
                else collapsed.add(key);
                store.set('assets-collapsed', JSON.stringify([...collapsed]));
                build();
            });
            const sec = h('section', { class: 'agroup' }, head);
            if (!closed) {
                const grid = h('div', { class: 'grid' });
                const lim = showAll.has(key) ? g.items.length : PER_GROUP;
                for (const c of g.items.slice(0, lim)) grid.append(tile(c));
                sec.append(grid);
                if (g.items.length > lim) {
                    sec.append(
                        h(
                            'button',
                            {
                                class: 'btn more',
                                type: 'button',
                                onclick: () => {
                                    showAll.add(key);
                                    build();
                                },
                            },
                            `Show ${g.items.length - lim} more`,
                        ),
                    );
                }
            }
            nodes.push(sec);
        }
        const top = body.scrollTop;
        fill(body, ...nodes);
        body.scrollTop = top;
    }

    let timer = 0;
    search.addEventListener('input', () => {
        window.clearTimeout(timer);
        timer = window.setTimeout(() => {
            query = search.value.trim();
            build();
        }, 120);
    });

    const rebuildSoon = raf(build);
    return {
        el: root,
        favourites: favs,
        setThumbnailProvider(p) {
            provider = p;
            rebuildSoon();
        },
        refreshTiles: rebuildSoon,
        setThumbnailLoader(fn) {
            loader = fn;
            rebuildSoon();
        },
        applyThumbnail(resource, url) {
            for (const box of root.querySelectorAll<HTMLElement>('.thumb')) {
                if (box.dataset.res !== resource) continue;
                box.replaceChildren(thumbImg(resource, url));
            }
        },
        onLoad() {
            showAll.clear();
            dragImages.clear();
            selected = null;
            build();
            fill(
                detail,
                h(
                    'span',
                    { class: 'dim' },
                    'Select a class to see its details. Drag a tile into the viewport to place it.',
                ),
            );
        },
        onChange() {
            // Instance counts change when objects are added or removed; keep it cheap by only recounting.
            const counts = new Map<string, number>();
            for (const o of ctx.doc().objects()) counts.set(o.resource, (counts.get(o.resource) ?? 0) + 1);
            let changed = false;
            for (const c of ctx.catalog()) {
                const n = counts.get(c.resource) ?? 0;
                if (c.instances !== n) {
                    c.instances = n;
                    changed = true;
                }
            }
            if (changed) rebuildSoon();
        },
    };
}
