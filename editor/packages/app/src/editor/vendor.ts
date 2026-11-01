import {
    ITEM_PRICE_FIELD,
    itemPrice,
    priceProblem,
    readVendor,
    salePrice,
    stockKey,
    vendorIssues,
    vendorSchemaOf,
    writeSetting,
    writeStock,
    type LevelObject,
    type StockEntry,
    type VendorView,
} from '@drakan/editor-core';
import type { Ctx, Panel } from './ctx.ts';
import { fill, h, icon } from './dom.ts';
import { registerCommand, registerPanel, runCommand } from './registry.ts';
import './vendor.css';
import { ITEM_DBS, VendorPrices, vendorPrices, type VendorHost } from './vendorprices.ts';

/**
 * Vendors: a dock tab to edit what a shopkeeper sells and for how much, and a "Vendor" section for the Details panel.
 * Stock and price modifiers are object fields of the level (exported with the level). An item's own price is a field of
 * the item class in the shared database, so a price edit ships as a class-record override of that database (the Story
 * pack carries it) and applies to every shop that sells the item. docs/reference/level-format.md section 16.
 */

// ---- item list --------------------------------------------------------------------------------------------------------

interface ItemInfo {
    stem: string;
    id: number;
    name: string;
    price: number;
}

function itemList(ctx: Ctx, names: ReadonlyMap<number, string>): ItemInfo[] {
    const p = ctx.schemas();
    if (!p) return [];
    return p
        .classesWithField(ITEM_PRICE_FIELD)
        .filter((c) => c.stem in ITEM_DBS && c.name)
        .map((c) => ({
            stem: c.stem,
            id: c.id,
            name: (c.stem === 'RESOURES' ? names.get(c.id) : undefined) || c.name,
            price: c.value,
        }))
        .sort((a, b) => a.name.localeCompare(b.name));
}

export const vendorsIn = (ctx: Ctx): LevelObject[] => {
    const p = ctx.schemas();
    if (!p) return [];
    return [...ctx.doc().objects()].filter((o) => vendorSchemaOf(o, p));
};

const fmt = (n: number): string => String(Number(n.toFixed(3)));

// ---- Details section --------------------------------------------------------------------------------------------------

/** Body of the Details panel's "Vendor" section for one vendor object: the price modifiers and a stock summary. */
export function vendorDetails(ctx: Ctx, o: LevelObject, locked: boolean): HTMLElement | null {
    const provider = ctx.schemas();
    const schema = provider ? vendorSchemaOf(o, provider) : null;
    if (!provider || !schema) return null;
    const v = readVendor(o, schema, provider);
    const num = (label: string, key: 'saleMod' | 'buyMod', title: string): HTMLElement => {
        const input = h('input', {
            type: 'number',
            class: 'num',
            step: 0.05,
            min: 0,
            value: fmt(v.settings[key]),
            title,
            'aria-label': label,
            disabled: locked,
        });
        input.addEventListener('change', () => {
            const n = Number(input.value);
            if (!Number.isFinite(n) || n < 0) {
                input.value = fmt(v.settings[key]);
                return;
            }
            ctx.edit(`Set ${label} of ${o.class}`, [o.id], (x) => writeSetting(x, schema, key, n));
        });
        return h('label', { class: 'nf' }, h('span', { class: 'lab' }, label), input);
    };
    const infinite = v.entries.filter((e) => e.mode === 'infinite').length;
    const limited = v.entries.length - infinite;
    return h(
        'div',
        { class: 'vendor-sec' },
        num('Sale price x', 'saleMod', 'What the player pays = item price x this (Item Sale Price Mod)'),
        num('Buy-back x', 'buyMod', 'What the shop pays for the items the player sells (Item Buy Price Mod)'),
        h(
            'div',
            { class: 'note dim' },
            `${v.entries.length} item(s) for sale: ${infinite} unlimited, ${limited} limited.`,
        ),
        h(
            'div',
            { class: 'btnrow' },
            h(
                'button',
                {
                    class: 'btn primary',
                    type: 'button',
                    onclick: () => runCommand('panel.vendors'),
                    title: 'Open the Vendors tab to change items, quantities and prices',
                },
                icon('item', 14),
                'Edit stock and prices',
            ),
        ),
    );
}

// ---- Vendors panel ----------------------------------------------------------------------------------------------------

export function createVendorPanel(ctx: Ctx, host: VendorHost | null, prices: VendorPrices = vendorPrices): Panel {
    const root = h('div', { class: 'panel vendors' });
    const body = h('div', { class: 'vendor-body' });
    let names: ReadonlyMap<number, string> = new Map();
    let namesLoaded = false;
    let chosen: number | null = null;
    let query = '';
    let items: ItemInfo[] | null = null;

    const loadNames = (): void => {
        if (namesLoaded || !host) return;
        namesLoaded = true;
        host.itemNames()
            .then((n) => {
                names = n;
                items = null;
                render();
            })
            .catch(() => undefined);
    };

    const current = (): LevelObject | null => {
        const list = vendorsIn(ctx);
        const sel = [...ctx.view().selection].map((id) => ctx.doc().get(id)).find((o) => o && list.includes(o));
        if (sel) chosen = sel.id;
        return list.find((o) => o.id === chosen) ?? list[0] ?? null;
    };

    function nameFor(stem: string, id: number): string {
        const it = (items ??= itemList(ctx, names)).find((x) => x.stem === stem && x.id === id);
        if (it) return it.name;
        const t = ctx.schemas()?.classNumber(stem, id, ITEM_PRICE_FIELD);
        return t ? `${stem} #${id}` : `${stem} #${id} (unknown)`;
    }
    const discPrice = (stem: string, id: number): number | null =>
        ctx.schemas()?.classNumber(stem, id, ITEM_PRICE_FIELD)?.value ?? null;

    function numberBox(
        value: number,
        opts: { min: number; step: number; label: string; disabled?: boolean },
        on: (n: number) => void,
    ): HTMLInputElement {
        const input = h('input', {
            type: 'number',
            class: 'num',
            min: opts.min,
            step: opts.step,
            value: fmt(value),
            'aria-label': opts.label,
            disabled: opts.disabled,
        });
        input.addEventListener('change', () => {
            const n = Number(input.value);
            if (!Number.isFinite(n) || n < opts.min) input.value = fmt(value);
            else on(n);
        });
        return input;
    }

    function editStock(o: LevelObject, view: VendorView, label: string, entries: StockEntry[]): void {
        const provider = ctx.schemas()!;
        let bad: StockEntry[] = [];
        ctx.edit(label, [o.id], (x) => {
            bad = writeStock(x, view.schema, provider, entries).unreachable;
        });
        if (bad.length)
            ctx.toast(
                `${nameFor(bad[0]!.stem, bad[0]!.id)} cannot be sold by this shop (its class cannot name that database).`,
                'warn',
            );
    }

    function render(): void {
        loadNames();
        const provider = ctx.schemas();
        const vendors = vendorsIn(ctx);
        const head = h(
            'div',
            { class: 'panel-head' },
            h('span', { class: 'panel-title' }, icon('item', 14), 'Vendors'),
        );
        if (!provider || !ctx.doc().size) {
            fill(
                root,
                head,
                h(
                    'div',
                    { class: 'empty-state' },
                    h('strong', {}, 'No level open'),
                    h(
                        'span',
                        {},
                        'Open a level that has shops (Surdana, Andrellian Isles, Ravenshold, Valley of the Fallen).',
                    ),
                ),
            );
            return;
        }
        if (!vendors.length) {
            fill(
                root,
                head,
                h(
                    'div',
                    { class: 'empty-state' },
                    h('strong', {}, 'No vendors in this level'),
                    h('span', {}, 'Shops are objects of the Blacksmith, Alchemist, Mage or Store classes.'),
                ),
            );
            return;
        }
        const o = current()!;
        const schema = vendorSchemaOf(o, provider)!;
        const view = readVendor(o, schema, provider);
        const locked = ctx.view().lockedIds.has(o.id);
        const picker = h('select', { class: 'sel', 'aria-label': 'Vendor' }) as HTMLSelectElement;
        for (const v of vendors) picker.append(h('option', { value: String(v.id) }, `${v.class || 'Shop'} #${v.id}`));
        picker.value = String(o.id);
        picker.addEventListener('change', () => {
            chosen = Number(picker.value);
            render();
        });
        const find = h(
            'button',
            {
                class: 'tool sm',
                type: 'button',
                title: 'Select and frame this vendor',
                'aria-label': 'Locate vendor',
                onclick: () => ctx.locate([o.id]),
            },
            icon('focus', 14),
        );
        const tools = h('div', { class: 'panel-tools' }, picker, find);

        const rows = view.entries.map((e, i) => {
            const base = discPrice(e.stem, e.id);
            const editable = e.stem in ITEM_DBS;
            const price = base === null ? null : itemPrice(prices.map, e.stem, e.id, base);
            const edited = base !== null && prices.map.has(stockKey(e));
            const kind = h('select', {
                class: 'sel',
                'aria-label': 'Stock kind',
                disabled: locked,
            }) as HTMLSelectElement;
            kind.append(h('option', { value: 'limited' }, 'Limited'), h('option', { value: 'infinite' }, 'Unlimited'));
            kind.value = e.mode;
            kind.addEventListener('change', () => {
                const next = view.entries.map((x, j) =>
                    j === i ? { ...x, mode: kind.value as StockEntry['mode'] } : x,
                );
                editStock(o, view, `Change stock kind of ${nameFor(e.stem, e.id)}`, next);
            });
            const qty = numberBox(
                e.mode === 'infinite' ? 1 : e.count,
                { min: 1, step: 1, label: 'Quantity', disabled: locked || e.mode === 'infinite' },
                (n) => {
                    const next = view.entries.map((x, j) => (j === i ? { ...x, count: Math.floor(n) } : x));
                    editStock(o, view, `Set quantity of ${nameFor(e.stem, e.id)}`, next);
                },
            );
            const priceBox =
                price === null
                    ? h('span', { class: 'dim' }, 'n/a')
                    : numberBox(price, { min: 0, step: 1, label: 'Item price', disabled: !editable }, (n) => {
                          const bad = priceProblem(n);
                          if (bad) {
                              ctx.toast(bad, 'warn');
                              render();
                              return;
                          }
                          prices.set(e.stem, e.id, n, base!);
                      });
            return h(
                'tr',
                { class: edited ? 'edited' : '' },
                h(
                    'td',
                    { class: 'v-name', title: `${e.stem} #${e.id}` },
                    nameFor(e.stem, e.id),
                    edited ? h('span', { class: 'dim' }, ` (was ${base})`) : null,
                ),
                h('td', {}, kind),
                h('td', {}, qty),
                h('td', {}, priceBox),
                h(
                    'td',
                    { class: 'v-pay mono', title: 'Item price x sale modifier, what the player pays' },
                    price === null ? '' : String(salePrice(price, view.settings.saleMod)),
                ),
                h(
                    'td',
                    {},
                    h(
                        'button',
                        {
                            class: 'tool sm',
                            type: 'button',
                            title: 'Remove from stock',
                            'aria-label': 'Remove item',
                            disabled: locked,
                            onclick: () =>
                                editStock(
                                    o,
                                    view,
                                    `Remove ${nameFor(e.stem, e.id)} from ${o.class}`,
                                    view.entries.filter((_, j) => j !== i),
                                ),
                        },
                        icon('trash', 14),
                    ),
                ),
            );
        });
        const table = h(
            'table',
            { class: 'vendor-table' },
            h('thead', {}, h('tr', {}, ...['Item', 'Stock', 'Qty', 'Price', 'Pays', ''].map((t) => h('th', {}, t)))),
            h('tbody', {}, ...rows),
        );

        const settings = h(
            'div',
            { class: 'vendor-settings' },
            h(
                'label',
                { class: 'nf' },
                h('span', { class: 'lab' }, 'Sale price x'),
                numberBox(
                    view.settings.saleMod,
                    { min: 0, step: 0.05, label: 'Sale price modifier', disabled: locked },
                    (n) =>
                        ctx.edit(`Set sale modifier of ${o.class}`, [o.id], (x) =>
                            writeSetting(x, schema, 'saleMod', n),
                        ),
                ),
            ),
            h(
                'label',
                { class: 'nf' },
                h('span', { class: 'lab' }, 'Buy-back x'),
                numberBox(
                    view.settings.buyMod,
                    { min: 0, step: 0.05, label: 'Buy-back price modifier', disabled: locked },
                    (n) =>
                        ctx.edit(`Set buy-back modifier of ${o.class}`, [o.id], (x) =>
                            writeSetting(x, schema, 'buyMod', n),
                        ),
                ),
            ),
        );

        // Item picker: search the item classes, click to add (limited, 1 unit).
        const list = (items ??= itemList(ctx, names));
        const q = query.trim().toLowerCase();
        const hits = q
            ? list.filter((it) => it.name.toLowerCase().includes(q) || String(it.id) === q).slice(0, 40)
            : [];
        const search = h('input', {
            type: 'search',
            class: 'fsearch',
            placeholder: `Add an item: search ${list.length} items by name`,
            'aria-label': 'Search items',
            value: query,
            disabled: locked,
        });
        search.addEventListener('input', () => {
            query = search.value;
            render();
            (root.querySelector('.vendor-add input') as HTMLInputElement | null)?.focus();
        });
        const results = h(
            'div',
            { class: 'vendor-hits' },
            ...hits.map((it) =>
                h(
                    'button',
                    {
                        class: 'btn sm',
                        type: 'button',
                        title: `${it.stem} #${it.id}`,
                        onclick: () => {
                            if (
                                view.entries.some((x) => x.stem === it.stem && x.id === it.id && x.mode === 'limited')
                            ) {
                                ctx.toast(`${it.name} is already in stock; raise its quantity instead.`, 'info');
                                return;
                            }
                            query = '';
                            editStock(o, view, `Add ${it.name} to ${o.class}`, [
                                ...view.entries,
                                { stem: it.stem, id: it.id, mode: 'limited', count: 1 },
                            ]);
                        },
                    },
                    `${it.name} - ${it.price} gold`,
                ),
            ),
        );
        const issues = vendorIssues(view, (e) => discPrice(e.stem, e.id));
        const notes = h(
            'div',
            { class: 'vendor-notes' },
            ...issues.map((i) => h('div', { class: `note ${i.level === 'error' ? 'bad' : 'warn'}` }, i.message)),
            h(
                'div',
                { class: 'note dim' },
                'An item price is a property of the item, so it changes in every shop that sells it. Use the sale modifier for one shop. The price is exported with the story pack (Story tab, Export).',
            ),
        );
        fill(
            root,
            head,
            tools,
            h(
                'div',
                { class: 'vendor-body' },
                settings,
                table,
                h('div', { class: 'vendor-add' }, search, results),
                notes,
            ),
        );
    }

    prices.subscribe(() => render());
    root.append(body);
    render();
    return {
        el: root,
        onSelection: () => render(),
        onChange: () => render(),
        onLoad: () => {
            chosen = null;
            items = null;
            render();
        },
    };
}

// ---- install ---------------------------------------------------------------------------------------------------------

export interface VendorInstallHost extends VendorHost {
    ctx(): Ctx;
}

/** Register the Vendors dock tab and its palette command. Called once from `main.ts`. */
export function installVendors(host: VendorInstallHost): { prices: VendorPrices } {
    vendorPrices.setHost(host);
    registerPanel({
        id: 'vendors',
        title: 'Vendors',
        icon: 'item',
        panel: createVendorPanel(host.ctx(), host),
        command: {
            title: 'Edit vendors and shop stock',
            keywords: 'shop merchant store blacksmith alchemist mage stock price gold buy sell',
            hint: 'Change what a shopkeeper sells and for how much',
        },
    });
    registerCommand({
        id: 'vendors.select',
        title: 'Select the vendors of this level',
        category: 'Selection',
        icon: 'item',
        keywords: 'shop merchant',
        enabled: () => vendorsIn(host.ctx()).length > 0,
        run: () => {
            host.ctx().locate(vendorsIn(host.ctx()).map((o) => o.id));
        },
    });
    return { prices: vendorPrices };
}
