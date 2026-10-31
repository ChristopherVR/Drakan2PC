import { groupCatalog, searchCatalog, type AssetClass, type AssetIcon } from '@drakan/editor-core';
import { KINDS, KIND_HINT } from '../level.ts';
import type { Ctx, Panel } from './ctx.ts';
import { attachAssetDrag, dragState } from './assetdrag.ts';
import { fill, h, hex, icon, raf, store } from './dom.ts';
import { emptyState, onContextMenu, searchField } from './ui.ts';

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
    /** The selected folder: 'all', 'fav', 'level' or `db:<ODB stem>`. */
    let folder = store.get('cb-folder') ?? 'all';
    const kindsOn = new Set<string>();
    let query = '';
    let selected: string | null = null;
    const collapsed = new Set<string>(store.json<string[]>('assets-collapsed', []));
    const showAll = new Set<string>();

    const searchBox = searchField('Search classes', 'Search classes by name, group or id');
    const search = searchBox.input;
    const info = h('span', { class: 'dim cb-info' });
    const size = h('input', {
        type: 'range',
        min: 64,
        max: 140,
        value: store.get('tile-size') ?? '88',
        'aria-label': 'Tile size',
        class: 'slider',
    });
    const kindChips = h('div', { class: 'chips' });
    /** Tiles (large thumbnails) or a compact list: the list is the old Place panel's row view. */
    let view: 'tiles' | 'list' = store.get('cb-view') === 'list' ? 'list' : 'tiles';
    const viewBtns = (['tiles', 'list'] as const).map((v) => {
        const b = h(
            'button',
            {
                class: `seg-btn${v === view ? ' active' : ''}`,
                type: 'button',
                'data-view': v,
                title: v === 'tiles' ? 'Thumbnail tiles' : 'Compact list',
                'aria-label': v === 'tiles' ? 'Thumbnail tiles' : 'Compact list',
            },
            icon(v === 'tiles' ? 'grid' : 'list', 13),
        );
        b.addEventListener('click', () => {
            view = v;
            store.set('cb-view', v);
            for (const x of viewBtns) x.classList.toggle('active', x.dataset.view === v);
            body.classList.toggle('as-list', v === 'list');
            build();
        });
        return b;
    });
    const tree = h('nav', { class: 'cb-tree', 'aria-label': 'Class folders' });
    const crumb = h('div', { class: 'cb-crumb' });
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
            searchBox.el,
            h('span', { class: 'spacer' }),
            info,
            h('div', { class: 'seg' }, ...viewBtns),
            h('label', { class: 'size' }, icon('grid', 13), size),
        ),
        h('div', { class: 'assets-kinds' }, kindChips),
        h('div', { class: 'assets-main' }, tree, h('div', { class: 'cb-content' }, crumb, body), detail),
    );
    body.style.setProperty('--tile', `${size.value}px`);
    body.classList.toggle('as-list', view === 'list');
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
        attachAssetDrag(
            t,
            cls,
            ctx,
            (res) => dragImages.get(res),
            (c) => loader?.(c),
        );
        t.addEventListener('click', () => {
            if (dragState.suppressClick) return;
            selected = cls.resource;
            body.querySelectorAll('.tile.sel').forEach((x) => x.classList.remove('sel'));
            t.classList.add('sel');
            showDetail(cls);
        });
        t.addEventListener('dblclick', () => ctx.placeClass(cls));
        onContextMenu(t, () => [
            { heading: cls.name },
            { label: 'Place at view centre', icon: 'plus', run: () => ctx.placeClass(cls) },
            {
                label: favs.has(cls.resource) ? 'Remove from favourites' : 'Add to favourites',
                icon: 'star',
                run: () => {
                    if (favs.has(cls.resource)) favs.delete(cls.resource);
                    else favs.add(cls.resource);
                    saveFavs();
                    build();
                },
            },
            { sep: true },
            {
                label: 'Copy resource id',
                icon: 'copy',
                run: () => void navigator.clipboard?.writeText(cls.resource).catch(() => undefined),
            },
        ]);
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

    function folderRow(key: string, label: string, ico: string, n: number, depth = 0): HTMLElement {
        const b = h(
            'button',
            { class: `ui-row cb-folder${folder === key ? ' sel' : ''}`, type: 'button', 'data-folder': key },
            h('span', { class: 'cb-indent', style: `width:${depth * 12}px` }),
            icon(folder === key ? 'folder-open' : ico, 14),
            h('span', { class: 'name' }, label),
            h('span', { class: 'dim n' }, String(n)),
        );
        b.addEventListener('click', () => {
            folder = key;
            store.set('cb-folder', key);
            showAll.clear();
            build();
        });
        return b;
    }

    /** `db:STEM` or `db:STEM/kind`: a container, or the classes of one kind inside it. */
    const parseDb = (f: string): { db: string; kind: string | null } | null => {
        if (!f.startsWith('db:')) return null;
        const [db, kind] = f.slice(3).split('/');
        return { db: db!, kind: kind ?? null };
    };
    const openDbs = new Set<string>();

    function buildTree(all: AssetClass[]): void {
        const dbs = new Map<string, number>();
        for (const c of all) dbs.set(c.db, (dbs.get(c.db) ?? 0) + 1);
        const rows: HTMLElement[] = [
            folderRow('all', 'All classes', 'box', all.length),
            folderRow('level', 'In this level', 'layers', all.filter((c) => c.instances > 0).length),
            folderRow('fav', 'Favourites', 'star', all.filter((c) => favs.has(c.resource)).length),
        ];
        if (dbs.size) {
            rows.push(h('div', { class: 'cb-tree-head' }, 'Containers (ODB)'));
            const cur = parseDb(folder);
            if (cur) openDbs.add(cur.db);
            for (const [db, n] of [...dbs].sort((a, b) => a[0].localeCompare(b[0]))) {
                const row = folderRow(`db:${db}`, db, 'folder', n, 0);
                const open = openDbs.has(db);
                const kinds = new Map<string, number>();
                for (const c of all) if (c.db === db) kinds.set(c.kind, (kinds.get(c.kind) ?? 0) + 1);
                if (kinds.size > 1) {
                    // A caret toggles the sub-folders (one per kind of class in the container) without changing the folder.
                    const caret = h(
                        'span',
                        {
                            class: `cb-caret${open ? ' open' : ''}`,
                            role: 'button',
                            'aria-label': open ? 'Collapse' : 'Expand',
                        },
                        icon('chevron', 11),
                    );
                    caret.addEventListener('click', (e) => {
                        e.stopPropagation();
                        if (openDbs.has(db)) openDbs.delete(db);
                        else openDbs.add(db);
                        build();
                    });
                    row.prepend(caret);
                }
                rows.push(row);
                if (open && kinds.size > 1) {
                    for (const k of KINDS) {
                        const kn = kinds.get(k.kind);
                        if (kn) rows.push(folderRow(`db:${db}/${k.kind}`, k.label, 'folder', kn, 2));
                    }
                }
            }
        }
        fill(tree, ...rows);
    }

    function buildKindChips(all: AssetClass[]): void {
        const present = new Set(all.map((c) => c.kind));
        fill(
            kindChips,
            ...KINDS.filter((k) => present.has(k.kind)).map((k) => {
                const on = kindsOn.has(k.kind);
                const c = h(
                    'button',
                    {
                        class: `chip${on ? ' active' : ''}`,
                        type: 'button',
                        title: `Show only ${k.label} classes. ${KIND_HINT[k.kind]}`,
                    },
                    h('i', { class: 'ui-dot', style: `background:#${k.color.toString(16).padStart(6, '0')}` }),
                    k.label,
                );
                c.addEventListener('click', () => {
                    if (kindsOn.has(k.kind)) kindsOn.delete(k.kind);
                    else kindsOn.add(k.kind);
                    build();
                });
                return c;
            }),
        );
    }

    function build(): void {
        const all = ctx.catalog();
        const pf = parseDb(folder);
        if (pf && !all.some((c) => c.db === pf.db)) folder = 'all';
        buildTree(all);
        buildKindChips(all);
        let list = searchCatalog(all, query);
        if (kindsOn.size) list = list.filter((c) => kindsOn.has(c.kind));
        if (folder === 'fav') list = list.filter((c) => favs.has(c.resource));
        else if (folder === 'level') list = list.filter((c) => c.instances > 0);
        else if (pf) list = list.filter((c) => c.db === pf.db && (!pf.kind || c.kind === pf.kind));
        const folderName =
            folder === 'all'
                ? 'All classes'
                : folder === 'fav'
                  ? 'Favourites'
                  : folder === 'level'
                    ? 'In this level'
                    : pf
                      ? `${pf.db}${pf.kind ? ` / ${KINDS.find((k) => k.kind === pf.kind)?.label ?? pf.kind}` : ''}`
                      : folder;
        fill(crumb, icon('folder-open', 14), h('span', { class: 'dim' }, 'Content'), icon('chevron', 11), folderName);
        info.textContent = all.length ? `${list.length} of ${all.length} classes` : '';
        if (!all.length) {
            fill(
                body,
                emptyState('box', 'No classes yet', 'Open a level from the disc to browse the classes it can use.'),
            );
            return;
        }
        if (!list.length) {
            fill(
                body,
                emptyState(
                    folder === 'fav' ? 'star' : 'search',
                    folder === 'fav' ? 'No favourites yet' : 'No classes match',
                    folder === 'fav'
                        ? 'Click the star on a tile to keep a class here.'
                        : 'Clear the search or the kind filters to see more.',
                ),
            );
            return;
        }
        // The "Favourites" and "In this level" groups only make sense when browsing everything.
        let groups = groupCatalog(list, favs);
        if (folder !== 'all' || query)
            groups = groups.filter((g) => g.title !== 'Favourites' && g.title !== 'In this level');
        if (folder === 'all' && !query)
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
