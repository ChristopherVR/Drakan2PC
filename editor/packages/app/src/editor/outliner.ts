import {
    OBJECT_CATEGORIES,
    categoryIndex,
    type AssetClass,
    type LevelObject,
    type ObjectCategory,
} from '@drakan/editor-core';
import type { Ctx, Panel } from './ctx.ts';
import { fill, h, icon, raf } from './dom.ts';
import { buildObjectMenu } from './objectmenu.ts';
import { emptyState, onContextMenu, searchField, skeletonRows, type MenuItem } from './ui.ts';
import { isLoading, onLoading } from './loadstate.ts';

interface ClassGroup {
    key: string;
    name: string;
    ids: number[];
}
interface CategoryGroup {
    cat: ObjectCategory;
    label: string;
    hint: string;
    color: string;
    classes: ClassGroup[];
    count: number;
}

const hexColor = (c: number): string => `#${c.toString(16).padStart(6, '0')}`;
const LABEL = new Map(OBJECT_CATEGORIES.map((c) => [c.id, c.label.toLowerCase()]));

/**
 * Outliner: objects grouped by display category (`categories.ts` in editor-core) and class, with search, visibility and lock toggles, multi-select (Ctrl toggles,
 * Shift selects a range) and double-click to frame. The tree is rebuilt only when the object set, search or toggles
 * change; selection highlights are patched in place so moving objects stays cheap.
 */
export function createOutliner(ctx: Ctx): Panel {
    const searchBox = searchField('Search objects', 'Search objects by class, id or category');
    const search = searchBox.input;
    const count = h('span', { class: 'dim count' });
    const tree = h('div', {
        class: 'tree',
        role: 'tree',
        tabindex: '0',
        'aria-label': 'Objects of the level',
        title: 'Arrow keys move, Right and Left open and close a group, Enter selects (Ctrl+Enter adds to the selection)',
    });
    // Keyboard position in the tree, kept by name because the tree is rebuilt when a group opens or closes.
    let kbKey = '';
    const rowKey = (r: HTMLElement): string =>
        r.dataset.id ? `o${r.dataset.id}` : `g${/d\d+/.exec(r.className)?.[0] ?? ''}:${r.querySelector('.name')?.textContent ?? ''}`;
    const treeRows = (): HTMLElement[] => [...tree.querySelectorAll<HTMLElement>('.row')];
    function paintKb(scroll = false): void {
        for (const r of treeRows()) {
            const on = !!kbKey && rowKey(r) === kbKey;
            r.classList.toggle('kb', on);
            if (on && scroll) r.scrollIntoView({ block: 'nearest' });
        }
    }
    tree.addEventListener('focus', () => {
        const rs = treeRows();
        if (!rs.length) return;
        if (!kbKey || !rs.some((r) => rowKey(r) === kbKey)) kbKey = rowKey(rs.find((r) => r.classList.contains('sel')) ?? rs[0]!);
        paintKb(true);
    });
    tree.addEventListener('keydown', (e) => {
        if (e.altKey || e.target !== tree) return;
        const rs = treeRows();
        const i = rs.findIndex((r) => rowKey(r) === kbKey);
        const cur = i >= 0 ? rs[i]! : null;
        const go = (n: number): void => {
            const r = rs[Math.max(0, Math.min(rs.length - 1, n))];
            if (!r) return;
            kbKey = rowKey(r);
            paintKb(true);
        };
        switch (e.key) {
            case 'ArrowDown': go(i + 1); break;
            case 'ArrowUp': go(i - 1); break;
            case 'Home': go(0); break;
            case 'End': go(rs.length - 1); break;
            case 'ArrowRight':
            case 'ArrowLeft': {
                const grp = cur?.classList.contains('grp') ? cur : null;
                const open = grp?.getAttribute('aria-expanded') === 'true';
                if (grp && (e.key === 'ArrowRight') !== open) grp.querySelector<HTMLElement>('.chev')?.click();
                else if (e.key === 'ArrowRight') go(i + 1);
                else go(i - 1);
                break;
            }
            case 'Enter':
            case ' ':
                cur?.dispatchEvent(new MouseEvent('click', { bubbles: true, ctrlKey: e.ctrlKey, metaKey: e.metaKey, shiftKey: e.shiftKey }));
                break;
            default:
                return;
        }
        e.preventDefault();
        queueMicrotask(() => paintKb(true));
    });
    const root = h(
        'div',
        { class: 'panel outliner' },
        h(
            'div',
            { class: 'panel-head' },
            h('span', { class: 'panel-title' }, icon('outliner', 14), 'Outliner'),
            count,
            h('span', { class: 'spacer' }),
            h(
                'button',
                { class: 'tool sm', title: 'Collapse all', onclick: () => (expanded.clear(), build()) },
                icon('chevron-down', 14),
            ),
        ),
        h('div', { class: 'panel-tools' }, searchBox.el),
        tree,
    );

    const expanded = new Set<string>();
    let groups: CategoryGroup[] = [];
    let catFor: AssetClass[] | null = null;
    let catFn = categoryIndex([]);
    const categoryOf = (o: LevelObject): ObjectCategory => {
        const cat = ctx.catalog();
        if (cat !== catFor) {
            catFor = cat;
            catFn = categoryIndex(cat);
        }
        return catFn(o);
    };
    let builtKey = '';
    const rows = new Map<number, HTMLElement>();
    const groupRows = new Map<string, { el: HTMLElement; ids: number[] }>();
    let lastClicked: { group: string; id: number } | null = null;
    let query = '';

    const matches = (o: LevelObject): boolean => {
        if (!query) return true;
        return `${o.id} ${o.class} ${LABEL.get(categoryOf(o)) ?? ''} ${o.resource}`.toLowerCase().includes(query);
    };

    function collect(): CategoryGroup[] {
        const byCat = new Map<ObjectCategory, Map<string, ClassGroup>>();
        for (const o of ctx.doc().objects()) {
            if (!matches(o)) continue;
            const cat = categoryOf(o);
            let m = byCat.get(cat);
            if (!m) byCat.set(cat, (m = new Map()));
            const name = o.class || o.resource || 'Unnamed';
            let g = m.get(name);
            if (!g) m.set(name, (g = { key: name, name, ids: [] }));
            g.ids.push(o.id);
        }
        const out: CategoryGroup[] = [];
        for (const k of OBJECT_CATEGORIES) {
            const m = byCat.get(k.id);
            if (!m) continue;
            const classes = [...m.values()].sort((a, b) => a.name.localeCompare(b.name));
            out.push({
                cat: k.id,
                label: k.label,
                hint: k.hint,
                color: hexColor(k.color),
                classes,
                count: classes.reduce((n, c) => n + c.ids.length, 0),
            });
        }
        return out;
    }

    function structureKey(): string {
        const v = ctx.view();
        let sum = 0;
        for (const o of ctx.doc().objects()) sum = (sum + o.id * 31 + o.class.length) >>> 0;
        return `${ctx.doc().size}:${sum}:${query}:${ctx.catalog().length}`;
    }

    const eyeBtn = (hidden: boolean, onclick: () => void, title: string): HTMLElement => {
        const b = h('button', {
            class: `vis${hidden ? ' off' : ''}`,
            title,
            'aria-label': title,
            onclick: ((e: Event) => {
                e.stopPropagation();
                onclick();
            }) as EventListener,
        });
        b.append(icon(hidden ? 'eye-off' : 'eye', 14));
        return b;
    };
    const lockBtn = (locked: boolean, onclick: () => void, title: string): HTMLElement => {
        const b = h('button', {
            class: `vis lock${locked ? ' on' : ''}`,
            title,
            'aria-label': title,
            onclick: ((e: Event) => {
                e.stopPropagation();
                onclick();
            }) as EventListener,
        });
        b.append(icon(locked ? 'lock' : 'unlock', 14));
        return b;
    };

    /** Right-click menu: acts on the clicked ids (selecting them first unless they are already selected). */
    const rowMenu = (ids: number[]): MenuItem[] => buildObjectMenu(ctx, ids);

    function objectRow(o: LevelObject, groupKey: string, siblings: number[]): HTMLElement {
        const v = ctx.view();
        const hidden = v.hiddenIds.has(o.id);
        const locked = v.lockedIds.has(o.id);
        const row = h(
            'div',
            { class: 'row obj', role: 'treeitem', 'data-id': o.id, draggable: false, 'aria-label': `${o.class}, number ${o.id}` },
            h('span', { class: 'indent' }),
            h('span', { class: 'name' }, `#${o.id}`, h('span', { class: 'dim' }, ` ${o.class}`)),
            eyeBtn(hidden, () => v.setHidden([o.id], !v.hiddenIds.has(o.id)), hidden ? 'Show' : 'Hide'),
            lockBtn(locked, () => v.setLocked([o.id], !v.lockedIds.has(o.id)), locked ? 'Unlock' : 'Lock'),
        );
        row.classList.toggle('hidden', hidden);
        row.classList.toggle('locked', locked);
        row.classList.toggle('sel', v.selection.has(o.id));
        row.addEventListener('click', (e) => {
            if (v.picking) {
                v.completePick(o.id);
                return;
            }
            if (e.shiftKey && lastClicked && lastClicked.group === groupKey) {
                const a = siblings.indexOf(lastClicked.id);
                const b = siblings.indexOf(o.id);
                if (a >= 0 && b >= 0) {
                    const [lo, hi] = [Math.min(a, b), Math.max(a, b)];
                    const ids = siblings.slice(lo, hi + 1);
                    v.select(e.ctrlKey || e.metaKey ? [...v.selection, ...ids] : ids);
                    return;
                }
            }
            lastClicked = { group: groupKey, id: o.id };
            v.select([o.id], e.ctrlKey || e.metaKey);
        });
        row.addEventListener('dblclick', () => ctx.locate([o.id]));
        onContextMenu(row, () => rowMenu([o.id]));
        rows.set(o.id, row);
        return row;
    }

    function groupRow(
        key: string,
        depth: number,
        label: string,
        count: number,
        ids: () => number[],
        color: string | null,
        body: () => HTMLElement[],
        hint = '',
    ): HTMLElement {
        const open = expanded.has(key);
        const wrap = h('div', { class: 'group' });
        const v = ctx.view();
        const allIds = ids();
        const row = h(
            'div',
            {
                class: `row grp d${depth}`,
                role: 'treeitem',
                'aria-expanded': String(open),
                'aria-label': `${label}, ${count} object${count === 1 ? '' : 's'}`,
                ...(hint ? { title: hint } : {}),
            },
            h(
                'button',
                {
                    class: `chev${open ? ' open' : ''}`,
                    'aria-label': open ? 'Collapse' : 'Expand',
                    onclick: ((e: Event) => {
                        e.stopPropagation();
                        toggle();
                    }) as EventListener,
                },
                icon('chevron', 12),
            ),
            color ? h('i', { class: 'ui-dot', style: `background:${color}` }) : null,
            h('span', { class: 'name' }, label),
            h('span', { class: 'dim n' }, String(count)),
        );
        const allHidden = allIds.length > 0 && allIds.every((i) => v.hiddenIds.has(i));
        const allLocked = allIds.length > 0 && allIds.every((i) => v.lockedIds.has(i));
        row.append(
            eyeBtn(
                allHidden,
                () => v.setHidden(ids(), !ids().every((i) => v.hiddenIds.has(i))),
                allHidden ? 'Show group' : 'Hide group',
            ),
            lockBtn(
                allLocked,
                () => v.setLocked(ids(), !ids().every((i) => v.lockedIds.has(i))),
                allLocked ? 'Unlock group' : 'Lock group',
            ),
        );
        row.classList.toggle('hidden', allHidden);
        row.addEventListener('click', (e) => {
            v.select(ids(), e.ctrlKey || e.metaKey);
            // A plain click on a group name also opens it (the chevron alone is easy to miss).
            if (!e.ctrlKey && !e.metaKey && !e.shiftKey && !expanded.has(key)) toggle();
        });
        row.addEventListener('dblclick', () => ctx.locate(ids()));
        onContextMenu(row, () => rowMenu(ids()));
        function toggle(): void {
            if (expanded.has(key)) expanded.delete(key);
            else expanded.add(key);
            build();
        }
        groupRows.set(key, { el: row, ids: allIds });
        wrap.append(row);
        if (open) {
            const kids = h('div', { class: 'kids', role: 'group' });
            kids.append(...body());
            wrap.append(kids);
        }
        return wrap;
    }

    function build(): void {
        rows.clear();
        groupRows.clear();
        groups = collect();
        builtKey = structureKey();
        const searching = query.length > 0;
        const total = ctx.doc().size;
        count.textContent = total ? `${groups.reduce((n, g) => n + g.count, 0)} / ${total}` : '';
        if (!groups.length && isLoading()) {
            count.textContent = '';
            fill(tree, skeletonRows(9, 'Loading the level objects'));
            return;
        }
        if (!groups.length) {
            fill(
                tree,
                total
                    ? emptyState('search', 'No objects match', 'Clear the search to see every object.')
                    : emptyState('outliner', 'No level open', 'Pick a level to list its objects.'),
            );
            return;
        }
        const nodes: HTMLElement[] = [];
        for (const g of groups) {
            const kk = `k:${g.cat}`;
            if (searching && g.classes.length <= 3) expanded.add(kk);
            nodes.push(
                groupRow(
                    kk,
                    0,
                    g.label,
                    g.count,
                    () => g.classes.flatMap((c) => c.ids),
                    g.color,
                    () =>
                        g.classes.map((c) => {
                            const ck = `c:${g.cat}:${c.key}`;
                            if (searching && c.ids.length <= 40) expanded.add(ck);
                            return groupRow(
                                ck,
                                1,
                                c.name,
                                c.ids.length,
                                () => c.ids,
                                null,
                                () => {
                                    const doc = ctx.doc();
                                    const list = c.ids
                                        .slice(0, 600)
                                        .map((id) => doc.get(id))
                                        .filter((o): o is LevelObject => !!o);
                                    const els = list.map((o) => objectRow(o, ck, c.ids));
                                    if (c.ids.length > list.length) {
                                        els.push(
                                            h(
                                                'div',
                                                { class: 'more' },
                                                `${c.ids.length - list.length} more: narrow the search`,
                                            ),
                                        );
                                    }
                                    return els;
                                },
                            );
                        }),
                    g.hint,
                ),
            );
        }
        fill(tree, ...nodes);
        paintKb();
    }

    const syncSelection = raf(() => {
        const v = ctx.view();
        for (const [id, row] of rows) row.classList.toggle('sel', v.selection.has(id));
        // Reveal a single selected object that has no row yet (picked in the viewport).
        if (v.selection.size === 1) {
            const id = [...v.selection][0]!;
            let row = rows.get(id);
            const o = ctx.doc().get(id);
            if (!row && o && matches(o)) {
                const cat = categoryOf(o);
                expanded.add(`k:${cat}`);
                expanded.add(`c:${cat}:${o.class || o.resource || 'Unnamed'}`);
                build();
                row = rows.get(id);
            }
            row?.scrollIntoView({ block: 'nearest' });
        }
        for (const [, g] of groupRows) {
            g.el.classList.toggle('sel', g.ids.length > 0 && g.ids.every((i) => v.selection.has(i)));
        }
    });

    const syncVisibility = raf(() => {
        const v = ctx.view();
        // Group toggles depend on every member, so rebuild (cheap: rows are lazy).
        void v;
        build();
    });

    let timer = 0;
    search.addEventListener('input', () => {
        window.clearTimeout(timer);
        timer = window.setTimeout(() => {
            query = search.value.trim().toLowerCase();
            build();
        }, 120);
    });

    // the placeholder rows come and go with a load
    onLoading(() => {
        if (!ctx.doc().size) build();
    });

    return {
        el: root,
        onSelection: syncSelection,
        onChange() {
            if (structureKey() !== builtKey) build();
        },
        onVisibility: syncVisibility,
        onLoad() {
            expanded.clear();
            build();
        },
    };
}
