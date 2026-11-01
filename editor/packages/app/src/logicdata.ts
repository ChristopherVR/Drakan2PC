// Worker side of the logic tools: where every level's exits lead, read from the class defaults and the level strings.
// Destination of an exit = the string id in field 0 of its class ("Transition To..."): a level-local XDB string whose
// text is a level name or path; the arrival marker is an exit of the destination whose own destination is this level.

import type { Iso9660, OdbClass } from '@drakan/formats';
import {
    SrscReader,
    decodeFieldValue,
    odbStemForDependency,
    parseXdbStrings,
    resolveClass,
    resolveDependencyStem,
    sliceSource,
    xdbText,
    type XdbString,
} from '@drakan/formats';
import { destinationLevelName, type ExitIndexEntry } from '@drakan/editor-core';
import type { LevelLoadResult } from './rpc.ts';

export const EXIT_RFL = 0x37;

export interface ExitClassInfo {
    dest: string | null;
    portal: boolean;
    /** The destination string of the class default: its XDB file (`LEVEL1ON.XDB`) and id, for editing the target. */
    destRef?: { file: string; id: number };
}

export interface ExitLevelData {
    name: string;
    /** Placed exits. */
    exits: ExitIndexEntry[];
    /** Destination per exit class, keyed `<db index>:<class id>` (the document's resource without the 0x prefix math). */
    classes: Record<string, ExitClassInfo & { name: string }>;
}

export interface ExitIndexData {
    levels: ExitLevelData[];
    /** Every level of the disc (only set for a single-level answer, where `levels` holds just that level). */
    levelNames?: string[];
}

/** String id or inline text of an exit's destination dword (strings keep `id` in the low half when the length is 0). */
function destText(
    raw: number | undefined,
    data: readonly number[],
    strings: ReadonlyMap<number, XdbString>,
): string | null {
    if (raw === undefined) return null;
    if (raw >>> 16 === 0) {
        const t = xdbText(strings, raw & 0xffff);
        if (t) return destinationLevelName(t);
    }
    const inline = decodeFieldValue(0x0a, raw, data);
    return typeof inline === 'string' && inline ? destinationLevelName(inline) : null;
}

export async function levelExitData(
    fs: Iso9660,
    dirPath: string,
    name: string,
    r: LevelLoadResult,
): Promise<ExitLevelData> {
    const strings = new Map<number, XdbString>();
    const stringFile = new Map<number, string>();
    for (const d of r.lvl.header.dependencies) {
        const stem = odbStemForDependency(d.path.text);
        if (!/^LEVEL/i.test(stem)) continue;
        const f = await fs.find(`${resolveDependencyStem(dirPath, d.path.text)}.XDB`);
        if (!f || f.isDir) continue;
        try {
            const x = await parseXdbStrings(await SrscReader.open(sliceSource(fs.source, f.extent * 2048, f.size)));
            for (const [k, v] of x) {
                strings.set(k, v);
                stringFile.set(k, f.name.toUpperCase());
            }
        } catch {
            // an unreadable XDB only leaves destinations unresolved
        }
    }
    const info = (c: OdbClass): ExitClassInfo => {
        const portalRaw = c.data?.[1];
        const raw = c.data?.[0];
        const id = raw !== undefined && raw >>> 16 === 0 ? raw & 0xffff : 0;
        const file = id ? stringFile.get(id) : undefined;
        return {
            dest: destText(raw, c.data ?? [], strings),
            portal: portalRaw !== undefined && portalRaw !== 0,
            ...(file ? { destRef: { file, id } } : {}),
        };
    };
    const out: ExitLevelData = { name, exits: [], classes: {} };
    const dbOf = new Map<string, number>();
    for (const d of r.lvl.header.dependencies) {
        const stem = odbStemForDependency(d.path.text);
        if (!dbOf.has(stem)) dbOf.set(stem, d.dbIndex);
    }
    for (const [stem, classes] of r.odbs) {
        const db = dbOf.get(stem);
        if (db === undefined) continue;
        for (const c of classes.values()) {
            if (c.rflClass === EXIT_RFL) out.classes[`${db}:${c.id}`] = { ...info(c), name: c.name };
        }
    }
    for (const o of r.lvl.list.objects) {
        const cls = resolveClass(r.lvl.header, r.odbs, o.classId, o.dbIndex).class;
        if (!cls || cls.rflClass !== EXIT_RFL) continue;
        const base = info(cls);
        const slot = o.params.findIndex((p) => p.fieldIndex === 0);
        const over = slot >= 0 ? destText(o.data[slot], o.data, strings) : null;
        const pslot = o.params.findIndex((p) => p.fieldIndex === 1);
        out.exits.push({
            id: o.id,
            name: cls.name,
            dest: over ?? base.dest,
            portal: pslot >= 0 ? o.data[pslot] !== 0 : base.portal,
        });
    }
    return out;
}
