import { ReplaceItemCommand, SetFieldCommand } from '@drakan/editor-core';
import { INV_CELL_COUNT, type InventoryCell } from '@drakan/formats';
import { badge, emptyState, h } from './dom.ts';
import type { PanelCtx } from './panels.ts';

/** The Tab screen's grid is 8 cells wide and 3 high; a multi-cell item (the bow) spans down from its first cell. */
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

    const tiles: HTMLElement[] = [];
    for (let k = 0; k < INV_CELL_COUNT; k++) {
        const col = (k % INV_COLS) + 1;
        const row = Math.floor(k / INV_COLS) + 1;
        const c = byCell.get(k);
        if (!c || c.marker !== undefined) {
            if (covered.has(k)) continue; // drawn as part of the item above
            tiles.push(
                h('div', {
                    class: 'slot empty',
                    style: `grid-column:${col};grid-row:${row}`,
                    title: `Cell ${k}: empty`,
                }),
            );
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
            h(
                'button',
                {
                    class: `slot item${state.selected === k ? ' sel' : ''}${equipped ? ' eq' : ''}${replaced ? ' chg' : ''}`,
                    style: `grid-column:${col} / span ${sp.cols};grid-row:${row} / span ${sp.rows}`,
                    title: [`Cell ${k}: ${name}`, info?.detail].filter(Boolean).join('\n'),
                    'data-cell': k,
                    onclick: () => {
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
                `${real.length} items, ${equippedCount} equipped, ${hotCount} in quick slots`,
            ),
            h('span', { class: 'spacer' }),
            h(
                'button',
                {
                    disabled: true,
                    title: 'Not supported yet: use Replace with… on an existing item for now (a new item record crashes the game after load, docs/journal/2026-11-01/save-editor-round3.md).',
                },
                '+ Add item (not supported yet)',
            ),
        ),
        h('div', { class: 'invgrid' }, ...tiles),
        inspector,
        h(
            'div',
            { class: 'note-box' },
            h('b', null, 'Adding and removing items: not supported. '),
            'The game restores the carried items from a list in Rynn’s record and does not accept a brand-new item record yet ' +
                '(it crashes after load); use Replace with… to turn an existing item into another class of the same size. Durability, arrows, quantity, the equipped flag and the ' +
                'quick-slot flag of existing items are proven in game. ',
            h('b', null, 'Equipping by flag may not fill the Equipped panel: '),
            'the weapon actors held in Rynn’s hands are separate from the cell flag. Names and art come from the shared Resources ' +
                'database on your ISO (no level needed); without the ISO a short name and a placeholder are shown.',
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
            h(
                'div',
                { class: 'frow' },
                badge('proven in game', 'high', 'Edits of these fields were loaded and shown in game.'),
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
        ...templates.map((t) =>
            h(
                'option',
                { value: String(t.entry), selected: pending && t.resource === current },
                `${itemName(t.resource, infoOf(t.resource))} (${resourceKey(t.resource)})`,
            ),
        ),
    ) as HTMLSelectElement;
    if (!pending) select.value = '';
    select.onchange = () => {
        if (!select.value) return;
        ctx.run(new ReplaceItemCommand(c.cell, Number(select.value)));
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
        badge(
            'proven in game once',
            'medium',
            'Loaded in game: a potion became a second dagger in the same cell. Pick items of the same size.',
        ),
        h(
            'small',
            { class: 'sv-dim' },
            templates.length
                ? 'Classes come from item records in this save (pickups, shop stock, chests).'
                : 'This save has no other item records to copy from.',
        ),
    );
}
