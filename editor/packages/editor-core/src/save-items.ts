import { zlibSync } from 'fflate';
import {
    CLASS_OFFSET,
    ITEM_PTR_GAP,
    RYNN_CLASS,
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
export const ITEM_HANDLE_AT = 0x86;
/** Byte ranges of a carried item record that are kept when its body is replaced. */
export const KEEP_RANGES: readonly [number, number][] = [
    [0x0c, 0x18], // position
    [0x48, 0x4c], // serial
    [0x52, 0x5e], // state words
    [ITEM_HANDLE_AT, ITEM_HANDLE_AT + 4], // handle pointer
    [ITEM_HANDLE_AT + ITEM_PTR_GAP, ITEM_HANDLE_AT + ITEM_PTR_GAP + 4], // its second copy
    [0x96, 0x9a], // carried slot kind
];
const ITEM_RESOURCE_BASE = 0x20000;

const u32 = (d: Uint8Array, o: number): number => new DataView(d.buffer, d.byteOffset, d.byteLength).getUint32(o, true);
const heap = (w: number): boolean => w > 0x600000 && w < 0x2000000 && w % 16 === 0;

/** Resource id (0x3xxxx) of an item record, or null when the record is not shaped like an item. */
export function itemResource(e: SaveEntry): number | null {
    const d = e.data;
    if (e.type !== TYPE_RECORD || d.length < ITEM_HANDLE_AT + ITEM_PTR_GAP + 4 || u32(d, CLASS_OFFSET) === RYNN_CLASS) {
        return null;
    }
    const res = u32(d, 0) + ITEM_RESOURCE_BASE;
    if (res < 0x30000 || res >= 0x40000) return null;
    const h = u32(d, ITEM_HANDLE_AT);
    return heap(h) && u32(d, ITEM_HANDLE_AT + ITEM_PTR_GAP) === h ? res : null;
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
        const rank = (carried.has(u32(e.data, ITEM_HANDLE_AT)) ? 2 : 0) + (state === 0 ? 0 : 1);
        const have = best.get(resource);
        if (!have || rank < have.rank) best.set(resource, { t: { resource, entry: e.id, size: e.data.length }, rank });
    }
    return [...best.values()].map((b) => b.t).sort((a, b) => a.resource - b.resource);
}

/** The record body that `cellItem` would get from `template` (pure; used by the tests and by `replaceItem`). */
export function replacedBody(carried: Uint8Array, template: Uint8Array): Uint8Array {
    const out = template.slice();
    for (const [a, b] of KEEP_RANGES) out.set(carried.subarray(a, b), a);
    return out;
}

/** Replace the item in inventory cell `cell` by the record body of template entry `templateEntry`. Input untouched. */
export function replaceItem(save: SaveFile, cell: number, templateEntry: number): SaveFile {
    const { entry: rynn, base } = cellTable(save);
    const handle = u32(rynn.data, base + 16 * cell + 8);
    if (!heap(handle)) throw new SaveFieldError(`cell ${cell} holds no item`);
    const { entry: item, at } = itemRecord(save, handle);
    if (at !== ITEM_HANDLE_AT) throw new SaveFieldError(`item record of cell ${cell} has an unrecognised layout`);
    const tpl = save.entries.find((e) => e.type === TYPE_RECORD && e.id === templateEntry && itemResource(e) !== null);
    if (!tpl) throw new SaveFieldError(`entry ${templateEntry} is not an item record`);
    const data = replacedBody(item.data, tpl.data);
    return {
        ...save,
        entries: save.entries.map((e) =>
            e.index === item.index && !sameBytes(e.data, data) ? { ...e, data, raw: zlibSync(data) } : e,
        ),
    };
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
//     is ONE word (-1), an item is TWO words (slot, flag). The new item turns the -1 of a free cell into (slot, 1),
//     so the record grows by 4 bytes (writing the pair over two words desynchronises the rest of the record: the
//     game then reads a garbage count, `[memset] bad size 0xe570a3d0`, and jumps to 0);
//  4. the u16 item count 8 bytes before the cell table goes up by one.
const SLOT_FLAG = 1;
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
    const used = new Set(listInventory(save).map((c) => c.cell));
    const out: number[] = [];
    for (let k = 0; k < INV_CELL_COUNT; k++) if (!used.has(k)) out.push(k);
    return out;
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
 * Add a new carried item of the class of `templateEntry` (an item record of this save) to the first free cell.
 * Pure: the input save is untouched. Throws a `SaveFieldError` when the save does not have the structure above.
 */
export function addItem(save: SaveFile, templateEntry: number): AddItemResult {
    const { entry: rynn, base } = cellTable(save);
    const records = save.entries.filter((e) => e.type === TYPE_RECORD);
    const K = records.length;
    if (Math.max(...records.map((e) => e.id)) !== K) throw new SaveFieldError('record ids are not contiguous');
    const tpl = records.find((e) => e.id === templateEntry && itemResource(e) !== null);
    if (!tpl) throw new SaveFieldError(`entry ${templateEntry} is not an item record`);
    const cells = listInventory(save);
    const free = freeCells(save);
    if (free.length === 0) throw new SaveFieldError('the inventory is full');
    const cell = free[0]!;
    // Donor: an item that is carried now (state words, handle layout, position).
    const slots = new Map<number, number>();
    let donor: SaveEntry | null = null;
    for (const c of cells) {
        if (c.marker !== undefined || c.handle === undefined) continue;
        const { entry } = itemRecord(save, c.handle);
        slots.set(c.cell, entry.id - 1);
        donor ??= entry;
    }
    if (!donor) throw new SaveFieldError('add item needs one carried item to copy the carried state from');
    const statics = save.entries.find((e) => e.type === T_STATICS);
    const pairs = save.entries.find((e) => e.type === T_PAIRS);
    if (!statics || !pairs) throw new SaveFieldError('the static object tables (0x24 / 0x21) are missing');

    // 1. the record
    const body = replacedBody(donor.data, tpl.data);
    const handles = new Set<number>();
    let serial = 0;
    for (const e of records) {
        if (e.data.length > ITEM_HANDLE_AT + 4) handles.add(u32(e.data, ITEM_HANDLE_AT));
        if (itemResource(e) !== null) {
            const s = u32(e.data, 0x48);
            if (s < 0x10000 && s > serial) serial = s;
        }
    }
    let handle = 0x00a60010;
    while (handles.has(handle)) handle += 0x10;
    const bv = dvOf(body);
    bv.setUint32(ITEM_HANDLE_AT, handle, true);
    bv.setUint32(ITEM_HANDLE_AT + ITEM_PTR_GAP, handle, true);
    bv.setUint32(0x48, serial + 1, true);
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
    dvOf(grown).setUint32(at + 4, SLOT_FLAG, true);
    grown.set(rynn.data.subarray(at + 4), at + 8);
    const gv = dvOf(grown);
    gv.setUint16(base - 8, gv.getUint16(base - 8, true) + 1, true);
    // the cell table image (the game rebuilds it from the stream; kept in step so the next edit sees the item)
    const cellAt = base + 16 * cell;
    gv.setUint32(cellAt, 0, true);
    gv.setUint32(cellAt + 4, 0, true);
    gv.setUint32(cellAt + 8, handle, true);
    gv.setUint32(cellAt + 12, 0, true);

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
