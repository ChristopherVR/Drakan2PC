import {
    AddItemCommand,
    type ItemTemplate,
    MAX_ITEM_CELLS,
    RemoveAddedItemCommand,
    ReplaceItemCommand,
    SetFieldCommand,
} from '@drakan/editor-core';
import { INV_CELL_COUNT, type InventoryCell } from '@drakan/formats';
import { gridTile, itemGrid } from '../shared/components.ts';
import { badge, emptyState, h } from './dom.ts';
import type { PanelCtx } from './panels.ts';

/** The Tab screen's grid: 8 cells wide, 3 high; a tall item (bow 2, long sword 3) spans down from its first cell. */
export const INV_COLS = 8;
const INDESTRUCTIBLE = 0xffffffff;

/** Names of the items proven in game (RAM resource id). Used only when the open level's class table has no name. */
const KNOWN_ITEMS: Record<number, string> = {
    0x31bc9: 'Bow',
    0x3217b: 'Wood Club',
    0x33752: 'Health Elixir',
    0x33cbc: 'Dagger',
};

export interface ClassInfo {
    name: string;
    /** Extra tooltip lines (class name, shop price), newline separated. */
    detail?: string;
    /** PNG data URL of the model render, or null while it is still being drawn / when there is no model. */
    thumb: string | null;
    /** Height in inventory cells (1..3) from the game disc's item class, when known. */
    cells?: number;
    /** A pickup the game uses up at once (mana power-up), not an item that stays in the pack. */
    powerUp?: boolean;
}

export const resourceKey = (resource: number): string => `0x${resource.toString(16)}`;

/** Display name: the ODB class name when a level with this class is open, else the table above, else the id. */
export function itemName(resource: number | undefined, info: ClassInfo | null | undefined): string {
    if (resource === undefined) return 'Unknown item';
    if (info?.name) return info.name.replace(/_/g, ' ');
    return KNOWN_ITEMS[resource] ?? `Item ${resourceKey(resource)}`;
}

/** The size of each item in cells (columns, rows), from the continuation markers (dx * 10 + dy) of its extra cells. */
export function spans(cells: InventoryCell[]): Map<number, { cols: number; rows: number }> {
    const out = new Map<number, { cols: number; rows: number }>();
    for (const c of cells) if (c.marker === undefined) out.set(c.cell, { cols: 1, rows: 1 });
    for (const c of cells) {
        if (c.marker === undefined) continue;
        const dx = Math.floor(c.marker / 10);
        const dy = c.marker % 10;
        const origin = out.get(c.cell - (dy * INV_COLS + dx));
        if (origin) {
            origin.cols = Math.max(origin.cols, dx + 1);
            origin.rows = Math.max(origin.rows, dy + 1);
        }
    }
    return out;
}

/** Preview replacement heights immediately, including releasing the cells of a shorter replacement. */
export function previewCells(cells: InventoryCell[], heights: ReadonlyMap<number, number>): InventoryCell[] {
    const sizes = spans(cells);
    const result: InventoryCell[] = [];
    for (const cell of cells) {
        if (cell.marker !== undefined) continue;
        result.push(cell);
        const rows = heights.get(cell.cell) ?? sizes.get(cell.cell)?.rows ?? 1;
        for (let row = 1; row < rows; row++) result.push({ cell: cell.cell + row * INV_COLS, marker: row });
    }
    return result;
}

/**
 * Height in cells of an item class for Add item: the game disc's class field first, else the height of a carried
 * item of the same class in this save, else undefined (not known).
 */
export function itemHeight(
    resource: number,
    info: ClassInfo | null | undefined,
    cells: InventoryCell[],
): number | undefined {
    if (info?.cells !== undefined) return info.cells;
    const span = spans(cells);
    for (const c of cells) if (c.marker === undefined && c.resource === resource) return span.get(c.cell)?.rows;
    return undefined;
}

const hueOf = (n: number): number => (Math.imul(n >>> 0, 2654435761) >>> 0) % 360;

const state: {
    selected: number | null;
    added: number | null;
    catalog: boolean;
    replace: number | null;
    query: string;
    template: number | null;
    limit: number;
} = {
    selected: null,
    added: null,
    catalog: false,
    replace: null,
    query: '',
    template: null,
    limit: 12,
};
let shownDocument: PanelCtx['doc'] | null = null;

/** The Tab-screen style 24-cell inventory grid with an inspector for the selected item. */
export function renderInventory(ctx: PanelCtx): HTMLElement {
    const { doc } = ctx;
    if (shownDocument !== doc) {
        shownDocument = doc;
        state.selected = null;
        state.added = null;
        state.catalog = false;
        state.template = null;
        state.replace = null;
    }
    const cells = previewCells(doc.inventory, doc.itemReplacementRows);
    if (!doc.hasInventory) {
        return emptyState(
            'bag',
            'No inventory found in this save',
            'The 24-cell controller image was not recognised in Rynn’s record, so there is nothing to show. Try another slot, or open Repair to see what this save contains.',
        );
    }
    const span = spans(cells);
    const byCell = new Map(cells.map((c) => [c.cell, c]));
    const covered = new Set(cells.filter((c) => c.marker !== undefined).map((c) => c.cell));
    const val = (cell: number, key: string): number => Number(doc.value(`inv.${cell}.${key}`));
    const thumbs: { el: HTMLImageElement; resource: number }[] = [];
    const infoOf = (resource: number | undefined, thumbnail = false): ClassInfo | null =>
        resource === undefined ? null : (ctx.classInfo?.(resourceKey(resource), thumbnail) ?? null);
    // Pending additions use exactly the same disc-derived art as carried items.
    const itemArt = (resource: number | undefined, name: string): HTMLElement => {
        const art = h('div', { class: 'art' });
        const info = infoOf(resource, true);
        if (info?.thumb) art.append(h('img', { src: info.thumb, alt: name }));
        else {
            const img = h('img', { alt: name, hidden: true });
            if (resource !== undefined) thumbs.push({ el: img, resource });
            art.append(
                img,
                h(
                    'span',
                    { class: 'ph', style: `--hue:${hueOf(resource ?? 0)}` },
                    name
                        .replace(/^Item /, '')
                        .slice(0, 2)
                        .toUpperCase(),
                ),
            );
        }
        return art;
    };

    const durText = (c: InventoryCell): string => {
        const cur = val(c.cell, 'durability');
        const max = val(c.cell, 'max');
        return cur === INDESTRUCTIBLE ? '∞' : `${cur} / ${max === INDESTRUCTIBLE ? '∞' : max}`;
    };
    const durFrac = (c: InventoryCell): number => {
        const cur = val(c.cell, 'durability');
        const max = val(c.cell, 'max');
        if (cur === INDESTRUCTIBLE || max === INDESTRUCTIBLE) return 1;
        return Math.max(0, Math.min(1, cur / Math.max(max, 1)));
    };

    const added = doc.addedPlaces();
    const addedCovered = new Set(added.flatMap((p) => p.cells.slice(1)));
    const tiles: HTMLElement[] = [];
    for (let k = 0; k < INV_CELL_COUNT; k++) {
        const c = byCell.get(k);
        if (!c || c.marker !== undefined) {
            if (covered.has(k) || addedCovered.has(k)) continue; // drawn as part of the item above
            const addIdx = added.findIndex((p) => p.cell === k);
            if (addIdx >= 0) {
                const tplRec = doc.itemTemplates().find((t) => t.entry === doc.itemAdds[addIdx]!.template);
                const rows = added[addIdx]!.cells.length;
                const nm = itemName(tplRec?.resource, infoOf(tplRec?.resource));
                tiles.push(
                    gridTile(
                        {
                            tag: 'button',
                            cls: `slot item chg${state.added === addIdx ? ' sel' : ''}`,
                            cell: k,
                            cols: INV_COLS,
                            span: { rows },
                            title: `Cell ${k}: new ${nm} (added when you save). Select for details.`,
                            data: { added: addIdx },
                            onClick: () => {
                                state.added = addIdx;
                                state.selected = null;
                                ctx.rerender();
                            },
                        },
                        itemArt(tplRec?.resource, nm),
                        h('span', { class: 'nm' }, `New: ${nm}`),
                    ),
                );
                continue;
            }
            tiles.push(gridTile({ cls: 'slot empty', cell: k, cols: INV_COLS, title: `Cell ${k}: empty` }));
            continue;
        }
        const sp = span.get(k)!;
        const resource = doc.itemResourceOf(k) ?? c.resource;
        const replaced = doc.itemReplacements.has(k);
        const info = infoOf(resource);
        const name = itemName(resource, info);
        const equipped = val(k, 'equipped') === 1;
        const hot = val(k, 'hot') === 1;
        const art = itemArt(resource, name);
        tiles.push(
            gridTile(
                {
                    tag: 'button',
                    cls: `slot item${state.selected === k ? ' sel' : ''}${equipped ? ' eq' : ''}${replaced ? ' chg' : ''}`,
                    cell: k,
                    cols: INV_COLS,
                    span: sp,
                    title: [`Cell ${k}: ${name}`, info?.detail].filter(Boolean).join('\n'),
                    data: { cell: k },
                    onClick: () => {
                        state.selected = k;
                        state.added = null;
                        ctx.rerender();
                    },
                },
                art,
                h(
                    'span',
                    { class: 'marks' },
                    equipped ? h('b', { class: 'mk e', title: 'Equipped flag' }, 'E') : null,
                    hot ? h('b', { class: 'mk q', title: 'In the Quick Inventory' }, 'Q') : null,
                ),
                h('span', { class: 'nm' }, name),
                c.durability !== undefined && !replaced
                    ? h(
                          'span',
                          { class: 'dbar' },
                          h('i', { style: `width:${(durFrac(c) * 100).toFixed(1)}%` }),
                          h('em', null, durText(c)),
                      )
                    : null,
            ),
        );
    }

    // Item art arrives after the level's models are decoded; poll a few times and fill the images in place.
    if (thumbs.length) {
        let tries = 0;
        const timer = window.setInterval(() => {
            tries++;
            let pending = 0;
            for (const t of thumbs) {
                if (!t.el.isConnected) continue;
                const i = infoOf(t.resource, true);
                if (i?.thumb) {
                    t.el.src = i.thumb;
                    t.el.hidden = false;
                    (t.el.nextElementSibling as HTMLElement | null)?.remove();
                } else pending++;
            }
            if (!pending || tries > 10 || !thumbs.some((t) => t.el.isConnected)) window.clearInterval(timer);
        }, 800);
    }

    const sel = state.selected !== null ? byCell.get(state.selected) : undefined;
    let inspector =
        sel && sel.marker === undefined
            ? inspect(
                  ctx,
                  sel,
                  itemName(
                      doc.itemResourceOf(sel.cell) ?? sel.resource,
                      infoOf(doc.itemResourceOf(sel.cell) ?? sel.resource),
                  ),
                  infoOf,
              )
            : h('div', { class: 'inspector sv-dim' }, 'Select an item to edit its durability, quantity and flags.');
    if (state.added !== null && doc.itemAdds[state.added]) {
        const index = state.added;
        const template = doc.itemTemplates().find((t) => t.entry === doc.itemAdds[index]!.template);
        const info = infoOf(template?.resource);
        const name = itemName(template?.resource, info);
        inspector = h(
            'div',
            { class: 'inspector sv-box inv-detail' },
            h('div', { class: 'inv-portrait' }, itemArt(template?.resource, name)),
            h(
                'div',
                { class: 'sv-meta' },
                h('small', { class: 'sv-dim' }, 'New item · pending until saved'),
                h('h3', null, name),
                h('p', null, 'This item uses its class defaults. Save and reopen to edit its quantity or durability.'),
                info?.detail ? h('p', { class: 'catalog-facts' }, info.detail) : null,
                h(
                    'button',
                    {
                        class: 'sv-btn',
                        onclick: () => {
                            state.added = null;
                            ctx.run(new RemoveAddedItemCommand(index));
                        },
                    },
                    'Remove added item',
                ),
            ),
        );
    }

    const real = cells.filter((c) => c.marker === undefined);
    const equippedCount = real.filter((c) => val(c.cell, 'equipped') === 1).length;
    const hotCount = real.filter((c) => val(c.cell, 'hot') === 1).length;
    const newCount = added.length;
    return h(
        'div',
        { class: 'invwrap' },
        h(
            'div',
            { class: 'invhead' },
            h('b', null, 'Inventory'),
            h(
                'small',
                { class: 'sv-dim' },
                `${real.length} item${real.length === 1 ? '' : 's'}${newCount ? `, ${newCount} new (not saved yet)` : ''}, ${equippedCount} equipped, ${hotCount} in quick slots`,
            ),
            h('span', { class: 'spacer' }),
            addControl(ctx, infoOf),
        ),
        itemGrid('invgrid', tiles),
        !doc.canAddItems
            ? h(
                  'p',
                  { class: 'note-box' },
                  'Your pack is empty or has no recognised item records. Pick up an item in the game and save first to enable adding items here.',
              )
            : null,
        inspector,
        state.catalog ? renderCatalog(ctx, infoOf, itemArt) : null,
        h(
            'div',
            { class: 'note-box' },
            'Add item copies an item class this save has a record for into the first free place as a new carried item. ' +
                'Tall items need that many empty cells in one column; without the game disc the size of a new item is not known, so it is given a whole free column. ' +
                'It also drops one untouched level object (the first entry of the static table) to keep the game’s slot numbering valid. ' +
                'Replace with… turns an existing item into another class (a taller class needs the cells under it).',
        ),
    );
}

function inspect(
    ctx: PanelCtx,
    c: InventoryCell,
    name: string,
    infoOf: (resource: number | undefined) => ClassInfo | null,
): HTMLElement {
    const { doc } = ctx;
    const number = (key: 'durability' | 'max', label: string): HTMLElement => {
        const field = `inv.${c.cell}.${key}`;
        const input = h('input', {
            type: 'number',
            min: 0,
            value: String(doc.value(field) ?? ''),
            disabled: !doc.editable(field),
            'data-field': field,
            onchange: () => {
                try {
                    const n = Number(input.value);
                    if (!Number.isInteger(n) || n < 0) throw new Error('expected a whole number');
                    const problem = doc.checkEdit(field, n);
                    if (problem) throw new Error(problem);
                    ctx.run(new SetFieldCommand(field, n));
                } catch (e) {
                    ctx.err(e);
                    input.value = String(doc.value(field) ?? '');
                }
            },
        });
        const orig = doc.originalValue(field);
        const was =
            orig !== undefined && orig !== doc.value(field) ? h('small', { class: 'sv-dim' }, ` was ${orig}`) : null;
        return h('label', { class: 'f' }, h('small', null, label), input, was);
    };
    const flag = (key: 'equipped' | 'hot', label: string, hint: string): HTMLElement => {
        const field = `inv.${c.cell}.${key}`;
        const cb = h('input', {
            type: 'checkbox',
            checked: doc.value(field) === 1,
            disabled: !doc.editable(field),
            'data-field': field,
            onchange: () => ctx.run(new SetFieldCommand(field, cb.checked ? 1 : 0)),
        });
        return h('label', { class: 'sv-check', title: hint }, cb, ` ${label}`);
    };
    const indestructible = doc.value(`inv.${c.cell}.durability`) === INDESTRUCTIBLE;
    const replacing = doc.itemReplacements.has(c.cell);
    const resource = doc.itemResourceOf(c.cell) ?? c.resource;
    const info = infoOf(resource);
    const replace = replaceControl(ctx, c, infoOf);
    return h(
        'div',
        { class: 'inspector sv-box inv-detail' },
        h(
            'div',
            { class: 'inv-portrait', 'aria-label': name },
            info?.thumb
                ? h('img', { src: info.thumb, alt: name })
                : h('span', { class: 'sv-dim' }, info ? 'Item preview unavailable' : 'Open your disc for item artwork'),
        ),
        h(
            'div',
            { class: 'sv-meta' },
            h('small', { class: 'sv-dim' }, `Cell ${c.cell}`),
            h('h3', null, name),
            info?.detail
                ? h(
                      'ul',
                      { class: 'inv-facts' },
                      ...info.detail
                          .split('\n')
                          .filter((line) => !line.startsWith('Class: '))
                          .map((line) => h('li', null, line)),
                  )
                : null,
            h(
                'small',
                { class: 'sv-dim' },
                `resource ${resource === undefined ? '?' : resourceKey(resource)} · handle 0x${(c.handle ?? 0).toString(16)}`,
            ),
            replace,
            c.durability === undefined || replacing
                ? h(
                      'small',
                      { class: 'sv-dim' },
                      replacing
                          ? 'Counts of the new item are its own defaults; undo the replacement to edit the old item.'
                          : 'The item record was not found, so its count cannot be edited.',
                  )
                : h(
                      'div',
                      { class: 'frow' },
                      number('durability', 'Durability / arrows / quantity'),
                      number('max', 'Max'),
                      indestructible ? badge('indestructible', 'medium', '0xffffffff means it never wears out.') : null,
                  ),
            h(
                'div',
                { class: 'frow' },
                flag('equipped', 'Equipped flag (E)', 'Sets the cell flag only; the Equipped panel may stay empty.'),
                flag('hot', 'Quick slot (Q)', 'Shows the item in the Quick Inventory.'),
            ),
        ),
    );
}

/** "Replace with..." : the item classes this save has a record for, in the app's dropdown (type to filter). */
function replaceControl(
    ctx: PanelCtx,
    c: InventoryCell,
    infoOf: (resource: number | undefined) => ClassInfo | null,
): HTMLElement {
    const { doc } = ctx;
    const pending = doc.itemReplacements.has(c.cell);
    return h(
        'div',
        { class: 'frow' },
        h(
            'button',
            {
                class: 'sv-btn',
                onclick: () => {
                    state.catalog = true;
                    state.replace = c.cell;
                    state.template = null;
                    ctx.rerender();
                    document.querySelector('.item-catalog')?.scrollIntoView({ block: 'start' });
                },
            },
            'Browse replacement…',
        ),
        pending
            ? h(
                  'button',
                  { class: 'sv-btn ghost', onclick: () => ctx.run(new ReplaceItemCommand(c.cell, undefined)) },
                  'Undo replacement',
              )
            : null,
    );
}

/** Open the shared visual catalog in Add mode. */
function addControl(ctx: PanelCtx, _infoOf: (resource: number | undefined) => ClassInfo | null): HTMLElement {
    return h(
        'button',
        {
            class: 'sv-btn',
            disabled: !ctx.doc.canAddItems,
            onclick: () => {
                state.catalog = true;
                state.replace = null;
                state.template = null;
                ctx.rerender();
                document.querySelector('.item-catalog')?.scrollIntoView({ block: 'start' });
            },
        },
        '+ Add item…',
    );
}

/** Search the classes present in this save, inspect their artwork, then choose an explicit action. */
function renderCatalog(
    ctx: PanelCtx,
    infoOf: (resource: number | undefined) => ClassInfo | null,
    art: (resource: number | undefined, name: string) => HTMLElement,
): HTMLElement {
    const { doc } = ctx;
    const templates = doc.itemTemplates();
    const replacement = state.replace;
    const grid = h('div', { class: 'catalog-grid' });
    const count = h('small', { class: 'sv-dim', 'aria-live': 'polite' });
    const detail = h('div', { class: 'catalog-detail' });

    const more = h(
        'button',
        {
            class: 'sv-btn ghost',
            onclick: () => {
                state.limit += 12;
                fill();
            },
        },
        'Show more items',
    );
    const fill = (): void => {
        detail.replaceChildren();
        const q = state.query.trim().toLowerCase();
        const filtered = templates
            .filter((t) =>
                `${itemName(t.resource, infoOf(t.resource))} ${resourceKey(t.resource)}`.toLowerCase().includes(q),
            )
            .sort((a, b) =>
                itemName(a.resource, infoOf(a.resource)).localeCompare(itemName(b.resource, infoOf(b.resource))),
            );
        count.textContent = `${filtered.length} item classes in this save`;
        more.hidden = filtered.length <= state.limit;
        grid.replaceChildren(
            ...filtered.slice(0, state.limit).map((t) => {
                const info = infoOf(t.resource);
                const name = itemName(t.resource, info);
                const rows = itemHeight(t.resource, info, doc.inventory);
                const height = rows ?? MAX_ITEM_CELLS;
                const fits =
                    replacement === null
                        ? doc.placeFor(height) !== null
                        : rows === undefined || doc.replaceFits(replacement, rows);
                const choose = (): void => {
                    state.template = t.entry;
                    const preview = art(t.resource, name);
                    detail.replaceChildren(
                        h('div', { class: 'inv-portrait' }, preview),
                        h(
                            'div',
                            { class: 'sv-meta' },
                            h('h3', null, name),
                            h(
                                'p',
                                { class: 'catalog-facts' },
                                info?.detail ||
                                    `Resource ${resourceKey(t.resource)}. Open your disc for gameplay facts and artwork.`,
                            ),
                            h(
                                'p',
                                { class: 'sv-dim' },
                                rows === undefined
                                    ? 'Size unknown: adding reserves a whole column.'
                                    : `${rows} inventory cell${rows === 1 ? '' : 's'} tall`,
                            ),
                            !fits
                                ? h(
                                      'p',
                                      { class: 'catalog-warning' },
                                      replacement === null
                                          ? noRoomMessage(name, height, rows)
                                          : 'There is not enough free space below this item for the replacement.',
                                  )
                                : null,
                            h(
                                'button',
                                {
                                    class: 'sv-btn primary',
                                    disabled: !fits,
                                    onclick: () => {
                                        if (replacement === null) {
                                            state.added = doc.itemAdds.length;
                                            state.selected = null;
                                            addChosen(ctx, t, info, height, rows);
                                        } else
                                            try {
                                                ctx.run(new ReplaceItemCommand(replacement, t.entry, rows));
                                                ctx.status(`Replaced with ${name}; pending until you save.`);
                                            } catch (e) {
                                                ctx.err(e);
                                            }
                                    },
                                },
                                replacement === null ? `Add ${name}` : `Replace with ${name}`,
                            ),
                        ),
                    );
                };
                if (state.template === t.entry) choose();
                return h(
                    'button',
                    {
                        class: `slot catalog-item${fits ? '' : ' no-room'}`,
                        'aria-pressed': state.template === t.entry ? 'true' : 'false',
                        onclick: () => {
                            choose();
                            for (const tile of grid.querySelectorAll('button')) {
                                tile.setAttribute(
                                    'aria-pressed',
                                    String(tile.getAttribute('data-template') === String(t.entry)),
                                );
                            }
                        },
                        'data-template': String(t.entry),
                        'aria-label': `Inspect ${name}`,
                    },
                    art(t.resource, name),
                    h('strong', null, name),
                    h('small', { class: 'sv-dim' }, resourceKey(t.resource)),
                    h(
                        'small',
                        { class: 'sv-dim' },
                        `${info?.powerUp ? 'Power-up · ' : ''}${rows ?? '?'} cells${fits ? '' : ' · no room'}`,
                    ),
                );
            }),
        );
        if (!filtered.length)
            grid.append(
                h(
                    'p',
                    { class: 'sv-dim' },
                    templates.length
                        ? 'No items match your search.'
                        : 'This save has no item records available to copy.',
                ),
            );
    };
    const search = h('input', {
        id: 'sv-catalog-search',
        type: 'search',
        value: state.query,
        placeholder: 'Search items by name or resource…',
        'aria-label': 'Search item catalog',
        oninput: (e) => {
            state.query = (e.target as HTMLInputElement).value;
            state.limit = 12;
            fill();
        },
    });
    fill();
    return h(
        'section',
        { class: 'sv-box item-catalog', 'aria-label': 'Item catalog' },
        h(
            'div',
            { class: 'invhead' },
            h('h3', null, replacement === null ? 'Add an item' : `Replace item in cell ${replacement}`),
            h('span', { class: 'spacer' }),
            h(
                'button',
                {
                    class: 'sv-btn ghost',
                    onclick: () => {
                        state.catalog = false;
                        ctx.rerender();
                    },
                },
                'Close catalog',
            ),
        ),
        h(
            'p',
            { class: 'sv-dim' },
            'Choose an item to inspect it before adding or replacing. Only classes with a usable record in this save are available.',
        ),
        search,
        count,
        grid,
        more,
        detail,
    );
}

/** Add the chosen item, or say why it does not fit. Either way the status line shows the latest outcome. */
export function addChosen(
    ctx: Pick<PanelCtx, 'doc' | 'run' | 'status' | 'err'>,
    t: ItemTemplate,
    info: ClassInfo | null,
    rows: number,
    known: number | undefined,
): void {
    const name = itemName(t.resource, info);
    if (ctx.doc.placeFor(rows) === null) {
        ctx.status(noRoomMessage(name, rows, known), true);
        return;
    }
    try {
        ctx.run(new AddItemCommand(t.entry, rows));
        ctx.status(addedMessage(name, !!info?.powerUp));
    } catch (e) {
        ctx.err(e);
    }
}

/** Status text after an item was added (replaces an earlier "does not fit" message). */
export function addedMessage(name: string, powerUp: boolean): string {
    return powerUp
        ? `Added ${name}; the game uses it up as soon as it is picked up. Pending until you save.`
        : `Added ${name}; pending until you save.`;
}

function addOptions(e: { items: AddEntry[]; powerUps: AddEntry[] }): HTMLElement[] {
    const opt = (x: AddEntry): HTMLElement => h('option', { value: x.value }, x.text);
    return [
        ...e.items.map(opt),
        ...(e.powerUps.length ? [h('optgroup', { label: POWER_UP_GROUP }, ...e.powerUps.map(opt))] : []),
    ];
}

/** One line of the Add item list. */
export interface AddEntry {
    value: string;
    text: string;
}

/**
 * The lines of the Add item list: the items first, then the pickups the game uses at once (shown under their own
 * heading). Pickups are only told apart when the disc names them; without it they stay among the items.
 */
export function addEntries(
    templates: ItemTemplate[],
    infoOf: (resource: number | undefined) => ClassInfo | null,
    fits: (t: ItemTemplate) => boolean,
): { items: AddEntry[]; powerUps: AddEntry[] } {
    const entry = (t: ItemTemplate): AddEntry => ({
        value: String(t.entry),
        text: `${itemName(t.resource, infoOf(t.resource))} (${resourceKey(t.resource)})${fits(t) ? '' : ' - no room'}`,
    });
    return {
        items: templates.filter((t) => !infoOf(t.resource)?.powerUp).map(entry),
        powerUps: templates.filter((t) => infoOf(t.resource)?.powerUp).map(entry),
    };
}

export const POWER_UP_GROUP = 'Power-ups (used at once)';

/** The plain-language reason a new item does not fit; `known` is the item's height when the disc or save says it. */
export function noRoomMessage(name: string, rows: number, known: number | undefined): string {
    if (rows <= 1) return `${name} does not fit: the inventory is full.`;
    if (known === undefined) {
        return `${name} does not fit: its size is not known without the game disc, so it needs a column with ${rows} empty cells, and there is none.`;
    }
    return `${name} does not fit: it is ${rows} cells tall and no column has ${rows} empty cells one above the other.`;
}
