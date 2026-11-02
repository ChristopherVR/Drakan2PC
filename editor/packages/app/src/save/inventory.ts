import {
    AddItemCommand,
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

const state: { selected: number | null } = { selected: null };

/** The Tab-screen style 24-cell inventory grid with an inspector for the selected item. */
export function renderInventory(ctx: PanelCtx): HTMLElement {
    const { doc } = ctx;
    const cells = doc.inventory;
    if (!cells.length) {
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
    const infoOf = (resource: number | undefined): ClassInfo | null =>
        resource === undefined ? null : (ctx.classInfo?.(resourceKey(resource)) ?? null);

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
                            cls: 'slot item chg',
                            cell: k,
                            cols: INV_COLS,
                            span: { rows },
                            title: `Cell ${k}: new ${nm} (added when you save). Click to remove.`,
                            data: { added: addIdx },
                            onClick: () => ctx.run(new RemoveAddedItemCommand(addIdx)),
                        },
                        h(
                            'div',
                            { class: 'art' },
                            h('span', { class: 'ph', style: `--hue:${hueOf(tplRec?.resource ?? 0)}` }, '+'),
                        ),
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
        const art = h('div', { class: 'art' });
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
                const i = infoOf(t.resource);
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
    const inspector =
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
        inspector,
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
    const replace = replaceControl(ctx, c, infoOf);
    return h(
        'div',
        { class: 'inspector sv-box' },
        h(
            'div',
            { class: 'sv-meta' },
            h('small', { class: 'sv-dim' }, `Cell ${c.cell}`),
            h('h3', null, name),
            h(
                'small',
                { class: 'sv-dim' },
                `resource ${c.resource === undefined ? '?' : resourceKey(c.resource)} · handle 0x${(c.handle ?? 0).toString(16)}`,
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
    const templates = doc.itemTemplates();
    const current = doc.itemResourceOf(c.cell);
    const pending = doc.itemReplacements.has(c.cell);
    const select = h(
        'select',
        { 'data-field': `item.${c.cell}`, 'data-placeholder': 'Replace with…', 'aria-label': 'Replace with' },
        h('option', { value: '' }, 'Replace with…'),
        ...templates.map((t) => {
            const rows = itemHeight(t.resource, infoOf(t.resource), doc.inventory);
            const fits = rows === undefined || doc.replaceFits(c.cell, rows);
            return h(
                'option',
                { value: String(t.entry), selected: pending && t.resource === current },
                `${itemName(t.resource, infoOf(t.resource))} (${resourceKey(t.resource)})${fits ? '' : ' - no room'}`,
            );
        }),
    ) as HTMLSelectElement;
    if (!pending) select.value = '';
    select.onchange = () => {
        if (!select.value) return;
        const t = templates.find((x) => String(x.entry) === select.value);
        if (!t) return;
        // The class's height when the disc or a carried item says it: the item then takes that many cells.
        const rows = itemHeight(t.resource, infoOf(t.resource), doc.inventory);
        try {
            ctx.run(new ReplaceItemCommand(c.cell, t.entry, rows));
        } catch (e) {
            select.value = '';
            ctx.err(e);
        }
    };
    return h(
        'div',
        { class: 'frow' },
        h('label', { class: 'f' }, h('small', null, 'Replace with…'), select),
        pending
            ? h(
                  'button',
                  { class: 'sv-btn ghost sm', onclick: () => ctx.run(new ReplaceItemCommand(c.cell, undefined)) },
                  'Undo replacement',
              )
            : null,
        h(
            'small',
            { class: 'sv-dim' },
            templates.length
                ? 'Classes come from item records in this save (pickups, shop stock, chests).'
                : 'This save has no other item records to copy from.',
        ),
    );
}

/** "+ Add item": a dropdown of the item classes this save has a record for (type to filter). */
function addControl(ctx: PanelCtx, infoOf: (resource: number | undefined) => ClassInfo | null): HTMLElement {
    const { doc } = ctx;
    const templates = doc.itemTemplates();
    const rowsOf = (resource: number): number =>
        itemHeight(resource, infoOf(resource), doc.inventory) ?? MAX_ITEM_CELLS; // not known: a whole column
    const allPlaced = doc.addedCells().every((c) => c >= 0);
    const free = allPlaced && doc.placeFor(1) !== null;
    const select = h(
        'select',
        {
            'data-field': 'item.add',
            'data-placeholder': '+ Add item…',
            'aria-label': 'Add item',
            disabled: !templates.length || !free,
        },
        h('option', { value: '' }, '+ Add item…'),
        ...templates.map((t) => {
            const fits = doc.placeFor(rowsOf(t.resource)) !== null;
            return h(
                'option',
                { value: String(t.entry) },
                `${itemName(t.resource, infoOf(t.resource))} (${resourceKey(t.resource)})${fits ? '' : ' - no room'}`,
            );
        }),
    ) as HTMLSelectElement;
    select.value = '';
    select.onchange = () => {
        if (!select.value) return;
        const t = templates.find((x) => String(x.entry) === select.value);
        select.value = '';
        if (!t) return;
        const rows = rowsOf(t.resource);
        const name = itemName(t.resource, infoOf(t.resource));
        if (doc.placeFor(rows) === null) {
            ctx.status(noRoomMessage(name, rows, itemHeight(t.resource, infoOf(t.resource), doc.inventory)), true);
            return;
        }
        try {
            ctx.run(new AddItemCommand(t.entry, rows));
        } catch (e) {
            ctx.err(e);
        }
    };
    return h('label', { class: 'f', title: free ? 'Add a new carried item' : 'The inventory is full' }, select);
}

/** The plain-language reason a new item does not fit; `known` is the item's height when the disc or save says it. */
export function noRoomMessage(name: string, rows: number, known: number | undefined): string {
    if (rows <= 1) return `${name} does not fit: the inventory is full.`;
    if (known === undefined) {
        return `${name} does not fit: its size is not known without the game disc, so it needs a column with ${rows} empty cells, and there is none.`;
    }
    return `${name} does not fit: it is ${rows} cells tall and no column has ${rows} empty cells one above the other.`;
}
