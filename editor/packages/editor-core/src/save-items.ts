import { zlibSync } from 'fflate';
import {
    CLASS_OFFSET,
    ITEM_PTR_GAP,
    isRynnClass,
    SaveFieldError,
    TYPE_RECORD,
    INV_CELL_COUNT,
    cellTable,
    itemRecord,
    listInventory,
    sameBytes,
    type SaveEntry,
    type SaveFile,
} from '@drakan/formats';

/**
 * Replace a carried item by another item class (the "Replace with..." edit of the Inventory tab).
 *
 * Proven in game (docs/journal/2026-11-01/save-editor-round3.md): the game restores the carried items from a list in
 * Rynn's record, so a record that is already carried keeps its place if it keeps its identity. The record body of
 * another item class (taken from any item record of the same save: a pickup lying around, shop stock, a chest) is
 * written over it, except for the fields that make it "this carried item": serial, handle pointers, +0x52..0x5e
 * (state), +0x96 and the position. Brand-new carried records are not accepted by the game yet.
 */
/**
 * Offset of the self handle in most item records (the potions, the dagger, the Wood Club, the Rusty Scimitar). Some
 * classes have more data before it and carry it further on: 0x96 (Steel Club and other 606-byte records), 0x112,
 * 0x140 / 0x148 (the bow; the word at +0x68 counts a list in front of it). `itemHandleAt` finds it per record.
 */
export const ITEM_HANDLE_AT = 0x86;
/** Header byte ranges a carried item keeps when its body is replaced (the same offset in every layout). */
export const KEEP_RANGES: readonly [number, number][] = [
    [0x0c, 0x18], // position
    [0x48, 0x4c], // serial
    [0x52, 0x5e], // state words
];
/** Ranges kept relative to the self handle: the handle, its second copy and the carried slot kind 0x10 after it. */
export const KEEP_HANDLE_RANGES: readonly [number, number][] = [
    [0, 4],
    [ITEM_PTR_GAP, ITEM_PTR_GAP + 4],
    [0x10, 0x14],
];
const ITEM_RESOURCE_BASE = 0x20000;
/** Search window for the self handle (the largest offset seen in the game's saves is 0x148). */
const HANDLE_SEARCH = [0x60, 0x200] as const;

const u32 = (d: Uint8Array, o: number): number => new DataView(d.buffer, d.byteOffset, d.byteLength).getUint32(o, true);
const heap = (w: number): boolean => w > 0x600000 && w < 0x2000000 && w % 16 === 0;

/** Offset of an item record's self handle (a heap word repeated 0xe0 bytes later), or null when there is none. */
export function itemHandleAt(d: Uint8Array): number | null {
    for (let o = HANDLE_SEARCH[0]; o <= HANDLE_SEARCH[1] && o + ITEM_PTR_GAP + 4 <= d.length; o += 2) {
        const h = u32(d, o);
        if (heap(h) && u32(d, o + ITEM_PTR_GAP) === h) return o;
    }
    return null;
}

/** Resource id (0x3xxxx) of an item record, or null when the record is not shaped like an item. */
export function itemResource(e: SaveEntry): number | null {
    const d = e.data;
    if (e.type !== TYPE_RECORD || d.length < ITEM_HANDLE_AT + ITEM_PTR_GAP + 4 || isRynnClass(u32(d, CLASS_OFFSET))) {
        return null;
    }
    const res = u32(d, 0) + ITEM_RESOURCE_BASE;
    if (res < 0x30000 || res >= 0x40000) return null;
    return itemHandleAt(d) === null ? null : res;
}

export interface ItemTemplate {
    /** Item class (resource id, e.g. 0x33cbc). */
    resource: number;
    /** Entry id of the record whose body is used. */
    entry: number;
    size: number;
}

/** One template record per item class found in the save. A record lying in the world is preferred to a carried one. */
export function itemTemplates(save: SaveFile): ItemTemplate[] {
    const carried = new Set<number>();
    try {
        for (const c of listInventory(save))
            if (c.handle !== undefined && c.marker === undefined) carried.add(c.handle);
    } catch {
        /* no inventory */
    }
    const best = new Map<number, { t: ItemTemplate; rank: number }>();
    for (const e of save.entries) {
        const resource = itemResource(e);
        if (resource === null) continue;
        const state = u32(e.data, 0x56);
        const rank = (carried.has(u32(e.data, itemHandleAt(e.data)!)) ? 2 : 0) + (state === 0 ? 0 : 1);
        const have = best.get(resource);
        if (!have || rank < have.rank) best.set(resource, { t: { resource, entry: e.id, size: e.data.length }, rank });
    }
    return [...best.values()].map((b) => b.t).sort((a, b) => a.resource - b.resource);
}

/** The record body that `cellItem` would get from `template` (pure; used by the tests and by `replaceItem`). */
export function replacedBody(carried: Uint8Array, template: Uint8Array): Uint8Array {
    const out = template.slice();
    for (const [a, b] of KEEP_RANGES) out.set(carried.subarray(a, b), a);
    // The handle block sits at each record's own offset (the classes differ in what comes before it).
    const from = itemHandleAt(carried) ?? ITEM_HANDLE_AT;
    const to = itemHandleAt(template) ?? ITEM_HANDLE_AT;
    for (const [a, b] of KEEP_HANDLE_RANGES) out.set(carried.subarray(from + a, from + b), to + a);
    return out;
}

/**
 * Replace the item in inventory cell `cell` by the record body of template entry `templateEntry`. Input untouched.
 * `rows` is the new class's height in cells; when it is given and differs from the item's height now, the height word
 * in Rynn's item stream and the markers of the cells under the item follow (a `SaveFieldError` when the cells it needs
 * are taken). Without `rows` the height stays as it is.
 */
export function replaceItem(save: SaveFile, cell: number, templateEntry: number, rows?: number): SaveFile {
    const { entry: rynn, base } = cellTable(save);
    const handle = u32(rynn.data, base + 16 * cell + 8);
    if (!heap(handle)) throw new SaveFieldError(`cell ${cell} holds no item`);
    const { entry: item, at } = itemRecord(save, handle);
    if (at !== itemHandleAt(item.data)) {
        throw new SaveFieldError(`item record of cell ${cell} has an unrecognised layout`);
    }
    const tpl = save.entries.find((e) => e.type === TYPE_RECORD && e.id === templateEntry && itemResource(e) !== null);
    if (!tpl) throw new SaveFieldError(`entry ${templateEntry} is not an item record`);
    const data = replacedBody(item.data, tpl.data);
    const rynnData = rows === undefined ? null : resizedRynn(save, cell, rows);
    return {
        ...save,
        entries: save.entries.map((e) => {
            if (e.index === item.index && !sameBytes(e.data, data)) return { ...e, data, raw: zlibSync(data) };
            if (rynnData && e.index === rynn.index) return { ...e, data: rynnData, raw: zlibSync(rynnData) };
            return e;
        }),
    };
}

/**
 * Cell index -> slot (entry id - 1) of every carried item, and the record a new item copies its carried state from:
 * the first carried item that is not equipped, else the first one.
 */
function carriedSlots(save: SaveFile): { slots: Map<number, number>; donor: SaveEntry | null } {
    const slots = new Map<number, number>();
    let donor: SaveEntry | null = null;
    let equippedDonor: SaveEntry | null = null;
    for (const c of listInventory(save)) {
        if (c.marker !== undefined || c.handle === undefined) continue;
        const { entry } = itemRecord(save, c.handle);
        slots.set(c.cell, entry.id - 1);
        if (c.equipped) equippedDonor ??= entry;
        else donor ??= entry;
    }
    return { slots, donor: donor ?? equippedDonor };
}

/**
 * Header words of an item record that name what holds it (u16 +0x52, u16 +0x54). An equipped item holds a reference
 * there (0xc0, 0xd6, 0x1e4 ... with +0x54 = 0); every carried item that is not equipped has 0xffff, 0xffff (all saves
 * the game wrote). A new item copied from an equipped donor kept the reference and was drawn on Rynn's body (a potion
 * in her left hand, a club at her hip) although it sat in the pack.
 */
const HOLDER_AT = 0x52;
const NO_HOLDER = 0xffff;

/** Height in cells of the item whose first cell is `cell`, as Rynn's item stream records it. */
export function itemRows(save: SaveFile, cell: number): number {
    const { entry: rynn } = cellTable(save);
    const start = findCellStream(rynn.data, carriedSlots(save).slots);
    const at = streamOffset(rynn.data, start, streamPosition(cell));
    if (u32(rynn.data, at) === EMPTY) throw new SaveFieldError(`cell ${cell} holds no item`);
    return u32(rynn.data, at + 4);
}

/**
 * Rynn's record with the item in `cell` made `rows` cells tall (stream height word and the markers under it), or null
 * when that is already its height. Throws when a cell it needs is outside the grid or taken by another item.
 */
function resizedRynn(save: SaveFile, cell: number, rows: number): Uint8Array | null {
    if (!Number.isInteger(rows) || rows < 1 || rows > MAX_ITEM_CELLS) {
        throw new SaveFieldError(`an item is 1 to ${MAX_ITEM_CELLS} cells tall, not ${rows}`);
    }
    const { entry: rynn, base } = cellTable(save);
    const start = findCellStream(rynn.data, carriedSlots(save).slots);
    const at = streamOffset(rynn.data, start, streamPosition(cell));
    const old = u32(rynn.data, at + 4);
    if (old === rows) return null;
    const cells = itemCells(cell, rows);
    if (!cells) throw new SaveFieldError(`an item ${rows} cells tall does not fit below cell ${cell}`);
    const own = new Set(itemCells(cell, Math.max(1, Math.min(old, MAX_ITEM_CELLS))) ?? [cell]);
    const used = usedCells(save);
    if (cells.some((c) => used.has(c) && !own.has(c))) {
        throw new SaveFieldError(
            `the cells under cell ${cell} are taken: an item ${rows} cells tall does not fit there`,
        );
    }
    const out = rynn.data.slice();
    const dv = dvOf(out);
    dv.setUint32(at + 4, rows, true);
    for (let dy = 1; dy < MAX_ITEM_CELLS; dy++) {
        const c = cell + dy * INV_GRID_COLS;
        if (c >= INV_CELL_COUNT) break;
        const o = base + 16 * c;
        if (dy < rows) [0, 0, dy, 0].forEach((v, i) => dv.setUint32(o + 4 * i, v, true));
        else if (own.has(c)) [0, 0, 0, 0].forEach((_, i) => dv.setUint32(o + 4 * i, 0, true));
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Add item
// ---------------------------------------------------------------------------------------------------------------------
//
// What the game's loader needs for a NEW carried item (found 2026-11-01, docs/journal/2026-11-01/save-add-item.md):
//  1. the item record, appended as the next saved record (entry id K + 1, slot K, K = number of 0x102 records);
//  2. the slot space is [0, K) saved records followed by the level's untouched ("static") objects, which take slot
//     K + their position in the 0x24 table. Adding a record shifts every static slot by one, so the FIRST static
//     (0x24 word 1) is dropped from the 0x24 table and from the 0x21 uid -> slot pairs: all other static slots stay;
//  3. Rynn's record serialises the 24 cells as a word stream (column-major: position = column * 3 + row): an empty cell
//     is ONE word (-1), an item is TWO words (slot, height in cells). The new item turns the -1 of a free cell into
//     (slot, rows), so the record grows by 4 bytes (writing the pair over two words desynchronises the rest of the
//     record: the game then reads a garbage count, `[memset] bad size 0xe570a3d0`, and jumps to 0). The second word
//     is the item's height (1..3; 2 for the bow, club and scimitar in every save the game wrote): the loader passes
//     it to the cell placement, which marks the cells under a tall item. Writing 1 for a tall item loads it as a
//     one-cell item (journal/2026-11-02/save-editor-items-in-game.md);
//  4. the u16 counter 8 bytes before the cell table goes up by one (it counts items gained, not items held).
const EMPTY = 0xffffffff;
const T_STATICS = 0x24;
const T_PAIRS = 0x21;

const dvOf = (d: Uint8Array): DataView => new DataView(d.buffer, d.byteOffset, d.byteLength);

/** Stream position (column-major) of a cell index. */
export const streamPosition = (cell: number): number => (cell % 8) * 3 + Math.floor(cell / 8);

/** Offset of the cell word stream inside Rynn's record, found from the items the cell table lists (cell -> slot). */
export function findCellStream(rynn: Uint8Array, slots: Map<number, number>): number {
    const expect = new Array<number>(INV_CELL_COUNT).fill(EMPTY);
    for (const [cell, slot] of slots) expect[streamPosition(cell)] = slot;
    const dv = dvOf(rynn);
    for (let o = 0; o + 4 * INV_CELL_COUNT <= rynn.length; o += 2) {
        let p = o;
        let ok = true;
        for (let k = 0; k < INV_CELL_COUNT && ok; k++) {
            if (p + 4 > rynn.length) {
                ok = false;
                break;
            }
            const w = dv.getUint32(p, true);
            if (w !== expect[k]) ok = false;
            else if (w === EMPTY) p += 4;
            else if (p + 8 <= rynn.length && dv.getUint32(p + 4, true) <= 3) p += 8;
            else ok = false;
        }
        if (ok) return o;
    }
    throw new SaveFieldError('the carried-item list was not found in Rynn’s record');
}

/** Byte offset of stream position `pos` inside the stream that starts at `start`. */
function streamOffset(d: Uint8Array, start: number, pos: number): number {
    const dv = dvOf(d);
    let p = start;
    for (let k = 0; k < pos; k++) p += dv.getUint32(p, true) === EMPTY ? 4 : 8;
    return p;
}

/** Free cells (cell index), in the order "Add item" fills them. Cells covered by multi-cell items are not free. */
export function freeCells(save: SaveFile): number[] {
    const used = usedCells(save);
    const out: number[] = [];
    for (let k = 0; k < INV_CELL_COUNT; k++) if (!used.has(k)) out.push(k);
    return out;
}

/** The Tab screen grid: 8 columns, 3 rows; cell index = column + 8 * row. */
export const INV_GRID_COLS = 8;
export const INV_GRID_ROWS = 3;
/**
 * Largest item height in cells: the Resources class field "Icon Slot Size (1-3)". A multi-cell item spans DOWN from
 * its first cell (the bow takes three cells of one column, the Rusty Scimitar two; save-add-item.md), and the cells
 * below it hold the continuation markers dx * 10 + dy (dx is always 0 for these).
 */
export const MAX_ITEM_CELLS = INV_GRID_ROWS;

/** Cells an item `rows` cells tall takes when its first cell is `cell`, or null when it would leave the grid. */
export function itemCells(cell: number, rows: number): number[] | null {
    if (!Number.isInteger(rows) || rows < 1 || cell < 0 || cell >= INV_CELL_COUNT) return null;
    if (Math.floor(cell / INV_GRID_COLS) + rows > INV_GRID_ROWS) return null;
    return Array.from({ length: rows }, (_, dy) => cell + dy * INV_GRID_COLS);
}

/** First cell (in cell order) where an item `rows` cells tall fits with every one of its cells free, else null. */
export function placeItem(used: ReadonlySet<number>, rows: number): number | null {
    for (let k = 0; k < INV_CELL_COUNT; k++) {
        const cells = itemCells(k, rows);
        if (cells && cells.every((c) => !used.has(c))) return k;
    }
    return null;
}

/** Cells taken by the items in the save (first cells and the cells their markers cover). */
export function usedCells(save: SaveFile): Set<number> {
    return new Set(listInventory(save).map((c) => c.cell));
}

export interface AddItemResult {
    save: SaveFile;
    /** Inventory cell the item went into. */
    cell: number;
    /** Slot (entry id - 1) of the new record. */
    slot: number;
    /** The level object (0x24 uid) that made room for it and is no longer created. */
    droppedStatic: number;
}

/**
 * Add a new carried item of the class of `templateEntry` (an item record of this save) to the first place where all
 * of its `rows` cells (the item's height, 1..3) are free. Pure: the input save is untouched. Throws a
 * `SaveFieldError` when the item does not fit or the save does not have the structure above.
 */
export function addItem(save: SaveFile, templateEntry: number, rows = 1): AddItemResult {
    const { entry: rynn, base } = cellTable(save);
    const records = save.entries.filter((e) => e.type === TYPE_RECORD);
    const K = records.length;
    if (Math.max(...records.map((e) => e.id)) !== K) throw new SaveFieldError('record ids are not contiguous');
    const tpl = records.find((e) => e.id === templateEntry && itemResource(e) !== null);
    if (!tpl) throw new SaveFieldError(`entry ${templateEntry} is not an item record`);
    const cells = listInventory(save);
    if (!Number.isInteger(rows) || rows < 1 || rows > MAX_ITEM_CELLS) {
        throw new SaveFieldError(`an item is 1 to ${MAX_ITEM_CELLS} cells tall, not ${rows}`);
    }
    const used = new Set(cells.map((c) => c.cell));
    if (used.size >= INV_CELL_COUNT) throw new SaveFieldError('the inventory is full');
    const cell = placeItem(used, rows);
    if (cell === null) throw new SaveFieldError(`no free column has ${rows} empty cells for this item`);
    // Donor: an item that is carried now (state words, handle layout, position).
    const { slots, donor } = carriedSlots(save);
    if (!donor) throw new SaveFieldError('add item needs one carried item to copy the carried state from');
    const statics = save.entries.find((e) => e.type === T_STATICS);
    const pairs = save.entries.find((e) => e.type === T_PAIRS);
    if (!statics || !pairs) throw new SaveFieldError('the static object tables (0x24 / 0x21) are missing');

    // 1. the record
    const body = replacedBody(donor.data, tpl.data);
    const handles = new Set<number>();
    let serial = 0;
    for (const e of records) {
        const at = itemResource(e) !== null ? itemHandleAt(e.data) : null;
        if (at !== null) handles.add(u32(e.data, at));
        else if (e.data.length > ITEM_HANDLE_AT + 4) handles.add(u32(e.data, ITEM_HANDLE_AT));
        if (itemResource(e) !== null) {
            const s = u32(e.data, 0x48);
            if (s < 0x10000 && s > serial) serial = s;
        }
    }
    let handle = 0x00a60010;
    while (handles.has(handle)) handle += 0x10;
    const bv = dvOf(body);
    const handleAt = itemHandleAt(body) ?? ITEM_HANDLE_AT;
    bv.setUint32(handleAt, handle, true);
    bv.setUint32(handleAt + ITEM_PTR_GAP, handle, true);
    bv.setUint32(0x48, serial + 1, true);
    bv.setUint16(HOLDER_AT, NO_HOLDER, true);
    bv.setUint16(HOLDER_AT + 2, NO_HOLDER, true);
    const slot = K;

    // 2. statics
    const sd = dvOf(statics.data);
    const nStatic = sd.getUint32(0, true);
    if (nStatic < 2 || statics.data.length !== 4 + 4 * nStatic)
        throw new SaveFieldError('unexpected 0x24 table layout');
    const dropped = sd.getUint32(4, true);
    const newStatics = new Uint8Array(statics.data.length - 4);
    dvOf(newStatics).setUint32(0, nStatic - 1, true);
    newStatics.set(statics.data.subarray(8), 4);
    const pd = dvOf(pairs.data);
    const nPairs = pd.getUint32(12, true);
    if (16 + 8 * nPairs > pairs.data.length) throw new SaveFieldError('unexpected 0x21 table layout');
    let pairAt = -1;
    for (let i = 0; i < nPairs; i++) if (pd.getUint32(16 + 8 * i, true) === dropped) pairAt = 16 + 8 * i;
    const newPairs = new Uint8Array(pairs.data.length - (pairAt >= 0 ? 8 : 0));
    if (pairAt >= 0) {
        newPairs.set(pairs.data.subarray(0, pairAt), 0);
        newPairs.set(pairs.data.subarray(pairAt + 8), pairAt);
        dvOf(newPairs).setUint32(12, nPairs - 1, true);
    } else newPairs.set(pairs.data);

    // 3 + 4. Rynn's record
    const start = findCellStream(rynn.data, slots);
    const at = streamOffset(rynn.data, start, streamPosition(cell));
    if (at <= base) throw new SaveFieldError('unexpected position of the item stream');
    if (u32(rynn.data, at) !== EMPTY) throw new SaveFieldError(`cell ${cell} is not empty in the item stream`);
    const grown = new Uint8Array(rynn.data.length + 4);
    grown.set(rynn.data.subarray(0, at), 0);
    dvOf(grown).setUint32(at, slot, true);
    dvOf(grown).setUint32(at + 4, rows, true);
    grown.set(rynn.data.subarray(at + 4), at + 8);
    const gv = dvOf(grown);
    gv.setUint16(base - 8, gv.getUint16(base - 8, true) + 1, true);
    // the cell table image (the game rebuilds it from the stream; kept in step so the next edit sees the item)
    const cellAt = base + 16 * cell;
    gv.setUint32(cellAt, 0, true);
    gv.setUint32(cellAt + 4, 0, true);
    gv.setUint32(cellAt + 8, handle, true);
    gv.setUint32(cellAt + 12, 0, true);
    // The cells under a tall item: continuation markers (dx * 10 + dy, dx = 0). The stream keeps them as empty words,
    // as in the game's own save of the tall scimitar (Rynn's record kept its size).
    for (let dy = 1; dy < rows; dy++) {
        const o = base + 16 * (cell + dy * INV_GRID_COLS);
        gv.setUint32(o, 0, true);
        gv.setUint32(o + 4, 0, true);
        gv.setUint32(o + 8, dy, true);
        gv.setUint32(o + 12, 0, true);
    }

    // The entry list: the new record goes after the last record, like the game's own ordering.
    const lastRecord = save.entries
        .filter((e) => e.type === TYPE_RECORD || e.type === 0x104)
        .reduce((a, b) => (b.position > a.position ? b : a));
    const created: SaveEntry = {
        index: 0,
        type: TYPE_RECORD,
        id: K + 1,
        sub: 0,
        position: lastRecord.position + 1,
        gap: new Uint8Array(0),
        raw: zlibSync(body),
        data: body,
    };
    const swap = (e: SaveEntry, data: Uint8Array): SaveEntry => ({ ...e, data, raw: zlibSync(data) });
    const mapped = save.entries.map((e) => {
        const shifted = e.position > lastRecord.position ? { ...e, position: e.position + 1 } : e;
        if (e === rynn) return swap(shifted, grown);
        if (e === statics) return swap(shifted, newStatics);
        if (e === pairs) return swap(shifted, newPairs);
        return shifted;
    });
    mapped.splice(mapped.findIndex((e) => e.index === lastRecord.index) + 1, 0, created);
    const entries = mapped.map((e, i) => ({ ...e, index: i }));
    return { save: { ...save, entries }, cell, slot, droppedStatic: dropped };
}
