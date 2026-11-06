import {
    ModError,
    applyDocumentToLvl,
    buildModFiles,
    checkEntrySize,
    hasTerrainEdits,
    terrainPackEntry,
    updateModsIni,
    zipModFiles,
    type ApplyReport,
    type ChangedEntry,
    type DocumentDiff,
    type LevelDocument,
    type ModMeta,
} from '@drakan/editor-core';
import { writeObjectList } from '@drakan/formats';
import type { LevelLoadResult } from './rpc.ts';
import { launchEnv, type PlayOptions } from './launch.ts';
import * as platform from './platform.ts';

/** Minimal File System Access shapes for writing a pack into a chosen folder. */
interface Writable {
    write(data: Uint8Array | string): Promise<void>;
    close(): Promise<void>;
}
interface FileHandle {
    getFile(): Promise<File>;
    createWritable(): Promise<Writable>;
}
interface DirHandle {
    name: string;
    getFileHandle(name: string, o?: { create?: boolean }): Promise<FileHandle>;
    getDirectoryHandle(name: string, o?: { create?: boolean }): Promise<DirHandle>;
}
const picker = (): ((o?: unknown) => Promise<DirHandle>) | undefined =>
    (window as unknown as { showDirectoryPicker?: (o?: unknown) => Promise<DirHandle> }).showDirectoryPicker;

export const canWriteFolder = (): boolean =>
    platform.isDesktop || (typeof picker() === 'function' && !/[?&]nofsa\b/.test(location.search));

/** Number of changed objects in a diff (every category counted once per object per category). */
export const changeCount = (d: DocumentDiff): number =>
    d.moved.length +
    d.rotated.length +
    d.scaled.length +
    d.added.length +
    d.deleted.length +
    d.linked.length +
    d.params.length;

export interface LevelPack {
    meta: ModMeta;
    files: Record<string, Uint8Array>;
    entry: { file: string; type: number; id: number; sub: number; bytes: number; originalBytes: number };
    report: ApplyReport;
    warnings: string[];
    /** Summary of the terrain edits in the pack (LVL 0x0001), when there are any. */
    terrain?: string;
}

/** True when the document has object or terrain edits to export. */
export const hasLevelEdits = (src: LevelLoadResult, doc: LevelDocument): boolean =>
    changeCount(applyDocumentToLvl(src.lvl.list, doc, { onReferenced: 'unlink' }).report.diff) > 0 ||
    hasTerrainEdits(doc);

/** The rebuilt terrain entry (LVL 0x0001) for the document's terrain edits, or null. Throws ModError when it cannot be built. */
export function levelTerrainEntry(src: LevelLoadResult, doc: LevelDocument): ReturnType<typeof terrainPackEntry> {
    if (!hasTerrainEdits(doc)) return null;
    if (!src.layers) throw new ModError(`${src.file} has no terrain entry to rebuild.`);
    return terrainPackEntry(src.file, src.layers, src.layers.data, doc);
}

/** Apply the document to the level it was loaded from and lay out the pack. Throws ModError when it is unsafe. */
export function buildLevelPack(src: LevelLoadResult, doc: LevelDocument, meta: ModMeta): LevelPack {
    const { list, report } = applyDocumentToLvl(src.lvl.list, doc, { onReferenced: 'unlink' });
    const d = report.diff;
    const terrain = levelTerrainEntry(src, doc);
    if (changeCount(d) === 0 && !terrain) {
        throw new ModError('No level edits to export.');
    }
    const warnings = [...report.warnings];
    const changes: ChangedEntry[] = [];
    let entry: LevelPack['entry'];
    if (changeCount(d) > 0) {
        const payload = writeObjectList(list);
        const size = checkEntrySize(src.file, src.objects.size, payload.length);
        if (size) throw new ModError(size);
        if (report.unlinked.length) warnings.push(`${report.unlinked.length} link(s) to deleted objects were removed.`);
        entry = {
            file: src.file,
            type: src.objects.type,
            id: src.objects.id,
            sub: src.objects.sub,
            bytes: payload.length,
            originalBytes: src.objects.size,
        };
        changes.push({ file: entry.file, entry, data: payload });
    } else {
        const t = terrain!.change;
        entry = { file: t.file, ...t.entry, bytes: t.data.length, originalBytes: src.layers!.size };
    }
    if (terrain) {
        changes.push(terrain.change);
        warnings.push(...terrain.report.warnings);
    }
    const files = buildModFiles(meta, changes);
    return { meta, files, entry, report, warnings, ...(terrain ? { terrain: terrain.line } : {}) };
}

export const zipPack = (pack: LevelPack): Uint8Array => zipModFiles(pack.files);

async function writeFile(dir: DirHandle, name: string, data: Uint8Array | string): Promise<void> {
    const w = await (await dir.getFileHandle(name, { create: true })).createWritable();
    await w.write(data);
    await w.close();
}

/**
 * Write the pack as a folder `<mods>/<id>/` and enable it in `<mods>/mods.ini`. The user picks the play folder or its
 * `mods` folder; `mods` is created when missing. Returns a short description of where it went.
 */
export async function writePackToModsFolder(pack: Pick<LevelPack, 'meta' | 'files'>): Promise<string | null> {
    if (platform.isDesktop) return writePackDesktop(pack);
    const pick = picker();
    if (!pick) throw new ModError('This browser cannot write into a folder; use the zip export.');
    let root: DirHandle;
    try {
        root = await pick.call(window, { mode: 'readwrite', id: 'drakan-mods' });
    } catch (e) {
        if (e instanceof DOMException && e.name === 'AbortError') return null;
        throw e;
    }
    const mods = root.name.toLowerCase() === 'mods' ? root : await root.getDirectoryHandle('mods', { create: true });
    const packDir = await mods.getDirectoryHandle(pack.meta.id, { create: true });
    for (const [path, data] of Object.entries(pack.files)) {
        let dir = packDir;
        const parts = path.split('/');
        for (const p of parts.slice(0, -1)) dir = await dir.getDirectoryHandle(p, { create: true });
        await writeFile(dir, parts[parts.length - 1]!, data);
    }
    let ini = '';
    try {
        ini = await (await (await mods.getFileHandle('mods.ini')).getFile()).text();
    } catch {
        // no mods.ini yet
    }
    await writeFile(mods, 'mods.ini', updateModsIni(ini, pack.meta.id, true));
    return `${root.name}/mods/${pack.meta.id}`;
}

/**
 * Report a save as a toast: where it landed, or why it failed. A null result (the user cancelled) shows nothing.
 * Every save button uses it, so a failed write is never silent.
 */
export function toastSaveResult(
    save: Promise<string | null>,
    toast: (msg: string, kind: 'ok' | 'error') => void,
): Promise<void> {
    return save.then(
        (where) => {
            if (where) toast(`Saved to ${where}`, 'ok');
        },
        (e: unknown) => toast(e instanceof Error ? e.message : String(e), 'error'),
    );
}

// ---- desktop app: the shell writes into the remembered game folder -------------------------------------------------------

/** The remembered play folder, asking for it the first time. Null when the user cancels. */
async function ensureGameFolder(): Promise<string | null> {
    const s = await platform.getSettings();
    if (s.gameDir) return s.gameDir;
    return (await platform.pickFolder('game'))?.path ?? null;
}

/** The folder the shell writes packs into without asking: the chosen mods folder, else `<game>/mods`. */
export const knownModsFolder = (s: Pick<platform.DesktopSettings, 'modsDir' | 'gameDir'>): string | null =>
    s.modsDir || (s.gameDir ? `${s.gameDir}/mods` : null);

/** Write the pack into the mods folder as `<id>/` and enable it in `mods.ini` (the same edit as the browser path). */
async function writePackDesktop(pack: Pick<LevelPack, 'meta' | 'files'>): Promise<string | null> {
    // A chosen mods folder is enough; only without one and without a game folder is the game folder asked for.
    if (!knownModsFolder(await platform.getSettings()) && !(await ensureGameFolder())) return null;
    const ini = updateModsIni(await platform.readModsIni(), pack.meta.id, true);
    return platform.writeModPack(pack.meta.id, pack.files, ini);
}

/**
 * Desktop "Play": write the pack (when there are edits) into the mods folder, enabled, then start the game from the
 * play folder with the options' environment. Returns a one-line summary, or null when cancelled.
 */
export async function playInGame(
    pack: Pick<LevelPack, 'meta' | 'files'> | null,
    opts: PlayOptions,
    level: string | null,
    extraEnv: Record<string, string> = {},
): Promise<string | null> {
    if (!platform.isDesktop) throw new ModError('Play needs the desktop app.');
    const env = { ...launchEnv(opts, level), ...extraEnv }; // validate before touching the disk
    if (!(await ensureGameFolder())) return null;
    if (await platform.gameRunning()) throw new ModError('The game is already running. Close it first.');
    const where = pack ? await writePackDesktop(pack) : null;
    await platform.launchGame(env);
    const parts = [where ? `mod '${pack!.meta.id}' installed` : 'no edits'];
    if (env.DRAKAN_START_LEVEL)
        parts.push(`starting in ${env.DRAKAN_START_LEVEL}${env.DRAKAN_START_LEVEL_FROM ? ' (no intro)' : ''}`);
    if (env.DRAKAN_NET_HOST) parts.push(`hosting on port ${env.DRAKAN_NET_HOST}`);
    if (env.DRAKAN_NET_CONNECT) parts.push(`joining ${env.DRAKAN_NET_CONNECT}`);
    return `Game started (${parts.join(', ')}).`;
}
