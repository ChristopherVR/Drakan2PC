import { parsePs2Terrain, rebuildPs2Terrain, type SrscEntry, type TerrainRebuildReport } from '@drakan/formats';
import type { LevelDocument } from './document.ts';
import { ModError, checkEntrySize, type ChangedEntry } from './mod.ts';

/**
 * Terrain edits -> the LVL 0x0001 override of a level pack. The payload is rebuilt from the disc bytes by formats
 * `rebuildPs2Terrain` (unchanged layers copied byte for byte); see docs/journal/2026-11-02/terrain-editing-write.md.
 */

/** True when the document carries terrain edits (attached, or read from a saved document and not attached yet). */
export function hasTerrainEdits(doc: LevelDocument): boolean {
    return doc.terrainChanged.size > 0 || doc.hasPendingTerrain;
}

export interface TerrainPackEntry {
    change: ChangedEntry;
    report: TerrainRebuildReport;
    /** One line for the export summary. */
    line: string;
}

/**
 * The rebuilt 0x0001 entry for `doc`'s terrain edits, or null when there are none (or they rebuild to the disc bytes).
 * `original` is the disc payload of that entry; a document whose terrain is not attached yet gets it attached from there.
 * Throws ModError when the rebuild fails or the entry would cross the runtime's size limit.
 */
export function terrainPackEntry(
    file: string,
    entry: Pick<SrscEntry, 'type' | 'id' | 'sub'>,
    original: Uint8Array,
    doc: LevelDocument,
): TerrainPackEntry | null {
    if (!hasTerrainEdits(doc)) return null;
    let report: TerrainRebuildReport;
    let payload: Uint8Array;
    try {
        if (!doc.terrain) doc.attachTerrain(parsePs2Terrain(original));
        ({ payload, report } = rebuildPs2Terrain(original, doc.terrain!));
    } catch (e) {
        throw new ModError(`terrain of ${file} could not be written: ${e instanceof Error ? e.message : String(e)}`);
    }
    if (report.changed.size === 0) return null;
    const size = checkEntrySize(file, original.length, payload.length);
    if (size) throw new ModError(size);
    const added = [...report.changed.keys()].filter((i) => i >= doc.terrainBaseCount).length;
    const edited = report.changed.size - added;
    const parts = [`${edited} terrain layer(s) edited`];
    if (added) parts.push(`${added} added`);
    return {
        change: { file, entry: { type: entry.type, id: entry.id, sub: entry.sub }, data: payload },
        report,
        line: parts.join(', '),
    };
}
