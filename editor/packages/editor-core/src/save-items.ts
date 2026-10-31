import { zlibSync } from 'fflate';
import {
    CLASS_OFFSET,
    ITEM_PTR_GAP,
    RYNN_CLASS,
    SaveFieldError,
    TYPE_RECORD,
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
