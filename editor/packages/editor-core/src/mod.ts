import { strToU8, zipSync } from 'fflate';
import { SrscReader, writeSrsc, type SrscEntry } from '@drakan/formats';

/**
 * PROVISIONAL mod pack layout. docs/reference/modding.md is the authority once it lands; keep the manifest shape
 * isolated here so only this file changes. Entries use the runtime's `overrides/<FILE>/<type>_<id>_<sub>.bin` layout
 * (numbers in decimal, provisional).
 */
export const MOD_MANIFEST_FORMAT = 'drakan-mod-provisional';

export interface ModManifest {
    format: typeof MOD_MANIFEST_FORMAT;
    version: 0;
    provisional: true;
    name: string;
    description: string;
    /** Disc profile the overrides were made against (hash filled in once the profile format exists). */
    disc: { profile: string | null };
    overrides: { file: string; path: string; type: number; id: number; sub: number; size: number }[];
}

export interface ChangedEntry {
    /** Container file name, e.g. `LEVEL.RRC`. */
    file: string;
    entry: Pick<SrscEntry, 'type' | 'id' | 'sub'>;
    data: Uint8Array;
}

export const overridePath = (file: string, e: ChangedEntry['entry']): string =>
    `overrides/${file}/${e.type}_${e.id}_${e.sub}.bin`;

export function buildModZip(opts: { name: string; description?: string; entries: ChangedEntry[] }): Uint8Array {
    const files: Record<string, Uint8Array> = {};
    const manifest: ModManifest = {
        format: MOD_MANIFEST_FORMAT,
        version: 0,
        provisional: true,
        name: opts.name,
        description: opts.description ?? '',
        disc: { profile: null },
        overrides: [],
    };
    for (const c of opts.entries) {
        const path = overridePath(c.file, c.entry);
        files[path] = c.data;
        manifest.overrides.push({
            file: c.file,
            path,
            type: c.entry.type,
            id: c.entry.id,
            sub: c.entry.sub,
            size: c.data.length,
        });
    }
    files['manifest.json'] = strToU8(JSON.stringify(manifest, null, 2));
    return zipSync(files, { level: 6 });
}

const same = (a: Uint8Array, b: Uint8Array): boolean => a.length === b.length && a.every((v, i) => v === b[i]);

/**
 * Self-test for the writer: an unmodified rewrite must equal the source byte for byte, and replacing the given
 * entries with their own bytes must leave every payload readable and identical. Gate for exporting real edits.
 */
export async function srscRoundTrips(reader: SrscReader, indices: number[]): Promise<boolean> {
    const orig = await reader.source.read(0, reader.source.size);
    if (!same(await writeSrsc(reader), orig)) return false;
    const rep = new Map<number, Uint8Array>();
    for (const i of indices) rep.set(i, await reader.readRaw(reader.entries[i]!));
    const again = await SrscReader.fromBytes(await writeSrsc(reader, rep));
    for (const i of indices) {
        if (!same(await reader.readRaw(reader.entries[i]!), await again.readRaw(again.entries[i]!))) return false;
    }
    return true;
}
