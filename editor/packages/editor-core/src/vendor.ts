import { ODB_CLASS_TYPE, fieldIndex, patchClassData, type ClassFieldDef } from '@drakan/formats';
import type { LevelObject } from './document.ts';
import { ModError } from './mod.ts';
import {
    effectiveValue,
    setObjectField,
    typeCode,
    type ClassSchema,
    type SchemaField,
    type SchemaProvider,
} from './schema.ts';
import type { StoryEntryChange } from './story.ts';

/**
 * Vendors (shopkeepers). A shop is an object of the engine class `0x100ac` (RFL id 172): one class per shop in the level
 * ODB ("BlackSmith Store", "Alchemist", "Mage"). Its stock is two class-reference arrays, `Objects For Infinite Sale` (never
 * runs out) and `Objects For Sale` (one unit per element; a repeated item is several units); each element is an item class
 * `(db << 16) | id` of the shop's `.DB` list (RESOURES for weapons, armour and potions, MAGIC for spell unlockers). The price
 * is the item class's `Cost to Buy (in Gold)`; the shop scales it with `Item Sale Price Mod (0-1)` (what the player pays) and
 * `Item Buy Price Mod (0-1)` (what the shop pays for the player's items). docs/reference/level-format.md section 16.
 */
export const VENDOR_RFL_CLASS = 172;

export const VENDOR_FIELD = {
    infinite: 'Objects For Infinite Sale',
    limited: 'Objects For Sale',
    purchased: 'Items Purchased',
    saleMod: 'Item Sale Price Mod (0-1)',
    buyMod: 'Item Buy Price Mod (0-1)',
    durabilityMod: 'Durability Price Mod (0-1)',
    chargesMod: 'Charges Price Mod (0-1)',
    repairQuality: 'Repair Quality (0-1)',
    repairOrRecharge: 'Repair or recharge?',
    rows: 'Display items in Rows?',
    defaultOn: 'Default On Status',
} as const;

export const ITEM_PRICE_FIELD = 'Cost to Buy (in Gold)';
export const ITEM_QUANTITY_FIELD = 'Quantity';

/** The `Items Purchased` enum, as far as the shipped shops show (0 = nothing is sold back). Values 1 to 4 are used on the disc. */
export const ITEMS_PURCHASED_LABELS: Record<number, string> = {
    0: 'Nothing',
    1: 'Mode 1',
    2: 'Mode 2',
    3: 'Mode 3',
    4: 'Mode 4',
};

export const isVendorSchema = (schema: Pick<ClassSchema, 'fields'> | null | undefined): boolean =>
    !!schema &&
    schema.fields.some((f) => f.name === VENDOR_FIELD.limited && (f.type & 0x1000) !== 0) &&
    schema.fields.some((f) => f.name === VENDOR_FIELD.saleMod);

/** Does the object stock goods? Needs the class schema the document's resource names. */
export function vendorSchemaOf(o: LevelObject, provider: SchemaProvider | null): ClassSchema | null {
    const s = provider?.schemaFor(o.resource);
    return s && isVendorSchema(s) ? s : null;
}

/** One line of the stock list. */
export interface StockEntry {
    /** Container stem and class id of the item (`RESOURES`, 7869). */
    stem: string;
    id: number;
    /** `infinite` never runs out; `limited` sells `count` units. */
    mode: 'infinite' | 'limited';
    count: number;
}

export const stockKey = (e: Pick<StockEntry, 'stem' | 'id'>): string => `${e.stem}:${e.id}`;

/** Group the two arrays into entries: repeated limited elements become one entry with a count, in first-seen order. */
export function stockEntries(
    infinite: readonly { stem: string; id: number }[],
    limited: readonly { stem: string; id: number }[],
): StockEntry[] {
    const out: StockEntry[] = [];
    const seen = new Map<string, StockEntry>();
    for (const i of infinite) {
        const k = `i:${stockKey(i)}`;
        if (seen.has(k)) continue; // a repeated infinite item adds nothing
        const e: StockEntry = { stem: i.stem, id: i.id, mode: 'infinite', count: 1 };
        seen.set(k, e);
        out.push(e);
    }
    for (const i of limited) {
        const k = `l:${stockKey(i)}`;
        const e = seen.get(k);
        if (e) e.count++;
        else {
            const n: StockEntry = { stem: i.stem, id: i.id, mode: 'limited', count: 1 };
            seen.set(k, n);
            out.push(n);
        }
    }
    return out;
}

/** The item lists the entries write: infinite ones once each, limited ones repeated `count` times. */
export function stockLists(entries: readonly StockEntry[]): {
    infinite: { stem: string; id: number }[];
    limited: { stem: string; id: number }[];
} {
    const infinite: { stem: string; id: number }[] = [];
    const limited: { stem: string; id: number }[] = [];
    for (const e of entries) {
        if (e.mode === 'infinite') infinite.push({ stem: e.stem, id: e.id });
        else for (let i = 0; i < Math.max(1, Math.floor(e.count)); i++) limited.push({ stem: e.stem, id: e.id });
    }
    return { infinite, limited };
}

/** What the player pays: the item's price scaled by the shop's sale mod, rounded to whole gold (rounding is inferred). */
export const salePrice = (base: number, saleMod: number): number => Math.round(base * saleMod);
/** What the shop pays for an item the player sells. */
export const buyBackPrice = (base: number, buyMod: number): number => Math.round(base * buyMod);

export interface VendorSettings {
    saleMod: number;
    buyMod: number;
    durabilityMod: number;
    chargesMod: number;
    repairQuality: number;
    purchased: number;
    repairOrRecharge: number;
    rows: number;
    defaultOn: number;
}

export interface VendorView {
    schema: ClassSchema;
    entries: StockEntry[];
    settings: VendorSettings;
    /** Elements that did not resolve to an item class of a loaded database (kept when the stock is rewritten). */
    unresolved: number[];
}

const fieldByName = (schema: Pick<ClassSchema, 'fields'>, name: string): SchemaField | undefined =>
    schema.fields.find((f) => f.name === name);

function numberOf(o: LevelObject, schema: ClassSchema, name: string, fallback: number): number {
    const f = fieldByName(schema, name);
    if (!f) return fallback;
    const v = effectiveValue(o, f).value;
    return typeof v === 'number' ? v : fallback;
}

function elementsOf(o: LevelObject, schema: ClassSchema, name: string): number[] {
    const f = fieldByName(schema, name);
    if (!f) return [];
    const v = effectiveValue(o, f).value;
    return Array.isArray(v) ? [...v] : [];
}

/** Read a vendor object's stock and settings (overrides win over the class defaults). */
export function readVendor(o: LevelObject, schema: ClassSchema, provider: SchemaProvider): VendorView {
    const unresolved: number[] = [];
    const resolve = (name: string): { stem: string; id: number }[] => {
        const f = fieldByName(schema, name);
        const out: { stem: string; id: number }[] = [];
        if (!f) return out;
        for (const v of elementsOf(o, schema, name)) {
            if (v === 0) continue;
            const t = provider.resolveRef(schema, f.type, v);
            if (t.stem) out.push({ stem: t.stem, id: t.id });
            else unresolved.push(v);
        }
        return out;
    };
    const infinite = resolve(VENDOR_FIELD.infinite);
    const limited = resolve(VENDOR_FIELD.limited);
    return {
        schema,
        entries: stockEntries(infinite, limited),
        unresolved,
        settings: {
            saleMod: numberOf(o, schema, VENDOR_FIELD.saleMod, 1),
            buyMod: numberOf(o, schema, VENDOR_FIELD.buyMod, 1),
            durabilityMod: numberOf(o, schema, VENDOR_FIELD.durabilityMod, 0),
            chargesMod: numberOf(o, schema, VENDOR_FIELD.chargesMod, 0),
            repairQuality: numberOf(o, schema, VENDOR_FIELD.repairQuality, 1),
            purchased: numberOf(o, schema, VENDOR_FIELD.purchased, 0),
            repairOrRecharge: numberOf(o, schema, VENDOR_FIELD.repairOrRecharge, 0),
            rows: numberOf(o, schema, VENDOR_FIELD.rows, 0),
            defaultOn: numberOf(o, schema, VENDOR_FIELD.defaultOn, 1),
        },
    };
}

/**
 * Write the stock into `o` (call inside `ctx.edit`). Items the shop's database list cannot name are reported in the
 * result and left out of the arrays; an entry list the schema cannot hold throws.
 */
export function writeStock(
    o: LevelObject,
    schema: ClassSchema,
    provider: SchemaProvider,
    entries: readonly StockEntry[],
): { unreachable: StockEntry[] } {
    const lists = stockLists(entries);
    const unreachable: StockEntry[] = [];
    const encode = (name: string, list: readonly { stem: string; id: number }[]): void => {
        const f = fieldByName(schema, name);
        if (!f) throw new ModError(`${schema.name} has no field "${name}"`);
        if (typeCode(f.type) !== 3) throw new ModError(`"${name}" is not a class list`);
        const words: number[] = [];
        for (const i of list) {
            const v = provider.refValueFor(schema, i.stem, i.id);
            if (v === null) {
                const e = entries.find((x) => x.stem === i.stem && x.id === i.id);
                if (e && !unreachable.includes(e)) unreachable.push(e);
            } else words.push(v);
        }
        setObjectField(o, f, words);
    };
    encode(VENDOR_FIELD.infinite, lists.infinite);
    encode(VENDOR_FIELD.limited, lists.limited);
    return { unreachable };
}

/** Set one numeric vendor setting (price mods, repair quality, enums) on `o`. */
export function writeSetting(o: LevelObject, schema: ClassSchema, key: keyof typeof VENDOR_FIELD, value: number): void {
    const f = fieldByName(schema, VENDOR_FIELD[key]);
    if (!f) throw new ModError(`${schema.name} has no field "${VENDOR_FIELD[key]}"`);
    setObjectField(o, f, value);
}

// ---- item prices ----------------------------------------------------------------------------------------------------

/** One item class a price edit can change: where it lives and what the disc says it costs. */
export interface ItemClassRef {
    stem: string;
    id: number;
}

/** Price edits keyed `STEM:id` -> gold. */
export type PriceEdits = ReadonlyMap<string, number>;

export const MAX_PRICE = 9_999_999;

export function priceProblem(v: number): string | null {
    if (!Number.isFinite(v) || !Number.isInteger(v)) return 'A price is a whole number of gold.';
    if (v < 0) return 'A price cannot be negative.';
    if (v > MAX_PRICE) return `A price above ${MAX_PRICE} is not supported.`;
    return null;
}

export interface ItemRecordSource {
    /** Container file name the override goes under (`RESOURES.ODB`). */
    file: string;
    group: number;
    /** The 0x0102 record as stored. */
    record: Uint8Array;
    defs: readonly ClassFieldDef[];
}

/**
 * Class-record overrides that set the given prices. `source` returns the stored record of an item class (null when the
 * class is not editable, which is an error: a price the pack cannot carry must not be silently dropped). A price equal to
 * the stored one produces no entry.
 */
export function priceChanges(
    edits: PriceEdits,
    source: (stem: string, id: number) => ItemRecordSource | null,
): StoryEntryChange[] {
    const out: StoryEntryChange[] = [];
    for (const [key, price] of [...edits].sort((a, b) => a[0].localeCompare(b[0]))) {
        const bad = priceProblem(price);
        if (bad) throw new ModError(`${key}: ${bad}`);
        const at = key.lastIndexOf(':');
        const stem = key.slice(0, at);
        const id = Number(key.slice(at + 1));
        const src = source(stem, id);
        if (!src) throw new ModError(`${key}: the item class record could not be read, its price cannot be exported.`);
        const idx = fieldIndex(src.defs, ITEM_PRICE_FIELD);
        if (idx < 0) throw new ModError(`${key}: the class has no "${ITEM_PRICE_FIELD}" field.`);
        const data = patchClassData(src.record, new Map([[idx, price]]));
        if (data.length === src.record.length && data.every((b, i) => b === src.record[i])) continue;
        out.push({ file: src.file, type: ODB_CLASS_TYPE, id, sub: src.group, data, isNew: false });
    }
    return out;
}

/** The price an item has with the pending edits applied (`disc` when it was not edited). */
export const itemPrice = (edits: PriceEdits, stem: string, id: number, disc: number): number =>
    edits.get(`${stem}:${id}`) ?? disc;

// ---- checks ---------------------------------------------------------------------------------------------------------

export interface VendorIssue {
    level: 'error' | 'warning';
    message: string;
}

/** Plain-language problems with a vendor's stock: empty shop, gifts (price 0), duplicates of infinite items, unnamed items. */
export function vendorIssues(
    view: VendorView,
    priceOf: (e: Pick<StockEntry, 'stem' | 'id'>) => number | null,
): VendorIssue[] {
    const out: VendorIssue[] = [];
    if (!view.entries.length) out.push({ level: 'warning', message: 'This shop has nothing for sale.' });
    if (view.unresolved.length)
        out.push({
            level: 'warning',
            message: `${view.unresolved.length} stock element(s) point at a database this shop cannot name; they are kept as they are.`,
        });
    for (const e of view.entries) {
        const p = priceOf(e);
        if (p === null)
            out.push({ level: 'warning', message: `${e.stem} #${e.id} has no price field (not sellable).` });
        else if (p === 0)
            out.push({ level: 'warning', message: `${e.stem} #${e.id} costs 0 gold, so the shop gives it away.` });
    }
    const both = new Set(view.entries.filter((e) => e.mode === 'infinite').map(stockKey));
    for (const e of view.entries)
        if (e.mode === 'limited' && both.has(stockKey(e)))
            out.push({ level: 'warning', message: `${e.stem} #${e.id} is listed as limited and as infinite stock.` });
    if (view.settings.saleMod < 0) out.push({ level: 'error', message: 'The sale price modifier cannot be negative.' });
    return out;
}
