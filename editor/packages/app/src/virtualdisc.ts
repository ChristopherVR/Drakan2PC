// A disc image with extra in-memory files laid over it. Converted Order of the Flame levels exist only in memory until
// the player exports the pack; the viewer reads them through this overlay exactly like files of the disc. A virtual file
// that has the same path as a disc file replaces it (the pack ships widened model containers under their disc names).

import { isoStoryFs } from './story/storyfs.ts';
import { Iso9660, SrscReader, writePs2Srsc, type ByteSource, type IsoEntry } from '@drakan/formats';

export interface VirtualFile {
    /** Path from the disc root with `\`, e.g. `ENGLISH\LEVELS\OOTF00RO\OOTF00RO.LVL`. */
    path: string;
    bytes: Uint8Array;
}

const SECTOR = 2048;
/** Virtual files live above any real disc offset, one 256 MB slot each. */
const BASE = 2 ** 36;
const SLOT = 2 ** 28;

const norm = (p: string): string =>
    p
        .split(/[\\/]+/)
        .filter((s) => s.length > 0)
        .join('\\');
const upper = (s: string): string => s.toUpperCase();

/** `files/ENGLISH/LEVELS/X/Y.LVL` (a pack path) to `ENGLISH\LEVELS\X\Y.LVL`; null for entries outside `files/`. */
export function packPathToDisc(path: string): string | null {
    if (!/^files\//i.test(path)) return null;
    return norm(path.slice('files/'.length));
}

type IsoInternals = { readDir(dir: IsoEntry): Promise<IsoEntry[]> };

export function overlayDisc(base: Iso9660, files: readonly VirtualFile[]): Iso9660 {
    const byPath = new Map<string, { file: VirtualFile; slot: number; entry: IsoEntry }>();
    files.forEach((f, i) => {
        const path = norm(f.path);
        const name = path.slice(path.lastIndexOf('\\') + 1);
        byPath.set(upper(path), {
            file: { path, bytes: f.bytes },
            slot: i,
            entry: {
                rawName: name,
                name,
                path,
                isDir: false,
                extent: (BASE + i * SLOT) / SECTOR,
                size: f.bytes.length,
            },
        });
    });
    const slots = [...byPath.values()];
    const source: ByteSource = {
        size: Math.max(base.source.size, BASE + slots.length * SLOT),
        async read(offset, length) {
            if (offset < BASE) return base.source.read(offset, length);
            const slot = Math.floor((offset - BASE) / SLOT);
            const v = slots.find((s) => s.slot === slot);
            if (!v) return new Uint8Array(0);
            const at = offset - BASE - slot * SLOT;
            return v.file.bytes.subarray(at, Math.min(v.file.bytes.length, at + length));
        },
    };
    const overlay = Object.create(base) as Iso9660;
    Object.defineProperty(overlay, 'source', { value: source });
    const baseRead = (Iso9660.prototype as unknown as IsoInternals).readDir;
    const dirCache = new Map<string, IsoEntry[]>();
    Object.defineProperty(overlay, 'readDir', {
        value: async (dir: IsoEntry): Promise<IsoEntry[]> => {
            const hit = dirCache.get(dir.path);
            if (hit) return hit;
            const disc = dir.extent < BASE / SECTOR ? await baseRead.call(base, dir) : [];
            const here = upper(norm(dir.path));
            const merged = new Map<string, IsoEntry>(disc.map((e) => [upper(e.name), e]));
            for (const v of slots) {
                const p = v.entry.path;
                const parent = p.slice(0, Math.max(0, p.lastIndexOf('\\')));
                if (upper(parent) === here) merged.set(upper(v.entry.name), v.entry);
                else if (here === '' || upper(parent).startsWith(here + '\\')) {
                    // A folder that exists only in memory: list its first component under this directory.
                    const rest = here === '' ? p : p.slice(here.length + 1);
                    const comp = rest.split('\\')[0]!;
                    if (rest.includes('\\') && !merged.has(upper(comp))) {
                        const path = here === '' ? comp : `${dir.path}\\${comp}`;
                        merged.set(upper(comp), {
                            rawName: comp,
                            name: comp,
                            path,
                            isDir: true,
                            extent: BASE / SECTOR,
                            size: 0,
                        });
                    }
                }
            }
            const out = [...merged.values()];
            dirCache.set(dir.path, out);
            return out;
        },
    });
    return overlay;
}

/**
 * Read a language-relative file (`LEVELS\LEVEL1NA\LEVEL1NA.LVL`) of the pristine disc. The Order of the Flame converter
 * reads through this, never through `overlayDisc`, so a second conversion cannot see the first one's classes.
 */
export async function readPristineDiscFile(
    base: Iso9660 | null,
    language: string,
    path: string,
): Promise<Uint8Array | null> {
    if (!base) throw new Error('Open a disc first.');
    return isoStoryFs(base, language).read(path);
}

const ENTRY_FILE = /^(overrides|new)\/([^/]+)\/([0-9a-f]{4})_(\d+)_(\d+)\.bin$/i;

/**
 * Apply the entry patches of a pack (`overrides/<FILE>/<type>_<id>_<sub>.bin` replaces an entry, `new/...` adds one) to
 * the disc's containers of that name and return them as whole files. `.LVL` overrides belong to disc levels and are
 * left out. A container the disc does not have is skipped.
 */
export async function patchedContainers(
    base: Iso9660,
    language: string,
    packFiles: Readonly<Record<string, Uint8Array>>,
): Promise<{ files: VirtualFile[]; skipped: string[] }> {
    const byFile = new Map<string, { kind: string; type: number; id: number; sub: number; data: Uint8Array }[]>();
    for (const [path, data] of Object.entries(packFiles)) {
        const m = ENTRY_FILE.exec(path);
        if (!m || /\.LVL$/i.test(m[2]!)) continue;
        const list = byFile.get(m[2]!.toUpperCase()) ?? [];
        list.push({ kind: m[1]!.toLowerCase(), type: parseInt(m[3]!, 16), id: Number(m[4]), sub: Number(m[5]), data });
        byFile.set(m[2]!.toUpperCase(), list);
    }
    const out: VirtualFile[] = [];
    const skipped: string[] = [];
    if (!byFile.size) return { files: out, skipped };
    const discFiles = await base.walk(language);
    for (const [name, patches] of byFile) {
        const entry = discFiles.find((e) => e.name.toUpperCase() === name);
        if (!entry) {
            skipped.push(name);
            continue;
        }
        const reader = await SrscReader.fromBytes(await base.read(entry));
        const entries: { type: number; id: number; sub: number; data: Uint8Array }[] = [];
        const key = (t: number, i: number, s: number): string => `${t}:${i}:${s}`;
        const replace = new Map(patches.map((p) => [key(p.type, p.id, p.sub), p.data]));
        const used = new Set<string>();
        for (const e of reader.entries) {
            const k = key(e.type, e.id, e.sub);
            const r = replace.get(k);
            if (r) used.add(k);
            entries.push({ type: e.type, id: e.id, sub: e.sub, data: r ?? (await reader.readRaw(e)).slice() });
        }
        for (const p of patches) if (!used.has(key(p.type, p.id, p.sub))) entries.push(p);
        out.push({ path: entry.path, bytes: writePs2Srsc(entries) });
    }
    return { files: out, skipped };
}

/**
 * Story entries a converted pack adds to the disc's level folders: `new/LEVEL1ON.SSD/0311_<id>_...` is a cut scene
 * sequence, `new/LEVEL1ON.ODB/0102_<id>_...` a class (conversation handlers and lines among them). SYSTEM.ODB holds the
 * quests, which have their own list.
 */
export function convertedStoryOf(
    paths: Iterable<string>,
): Map<string, { sequences: Set<number>; classes: Set<number>; texts: Set<number> }> {
    const out = new Map<string, { sequences: Set<number>; classes: Set<number>; texts: Set<number> }>();
    for (const path of paths) {
        const x = /^new\/([^/]+)\.XDB\/0400_(\d+)_/i.exec(path);
        if (x) {
            const key = x[1]!.toUpperCase();
            const e = out.get(key) ?? { sequences: new Set<number>(), classes: new Set<number>(), texts: new Set<number>() };
            e.texts.add(Number(x[2]));
            out.set(key, e);
            continue;
        }
        const m = /^new\/([^/]+)\.(?:SSD|ODB)\/(0311|0102)_(\d+)_/i.exec(path);
        if (!m || m[1]!.toUpperCase() === 'SYSTEM') continue;
        const key = m[1]!.toUpperCase();
        const e = out.get(key) ?? { sequences: new Set<number>(), classes: new Set<number>(), texts: new Set<number>() };
        (m[2] === '0311' ? e.sequences : e.classes).add(Number(m[3]));
        out.set(key, e);
    }
    return out;
}
