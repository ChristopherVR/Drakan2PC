import {
    ModError,
    applyDocumentToLvl,
    buildModFiles,
    checkEntrySize,
    updateModsIni,
    zipModFiles,
    type ApplyReport,
    type LevelDocument,
    type ModMeta,
} from '@drakan/editor-core';
import { writeObjectList } from '@drakan/formats';
import type { LevelLoadResult } from './rpc.ts';

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

export const canWriteFolder = (): boolean => typeof picker() === 'function' && !/[?&]nofsa\b/.test(location.search);

export interface LevelPack {
    meta: ModMeta;
    files: Record<string, Uint8Array>;
    entry: { file: string; type: number; id: number; sub: number; bytes: number; originalBytes: number };
    report: ApplyReport;
    warnings: string[];
}

/** Apply the document to the level it was loaded from and lay out the pack. Throws ModError when it is unsafe. */
export function buildLevelPack(src: LevelLoadResult, doc: LevelDocument, meta: ModMeta): LevelPack {
    const { list, report } = applyDocumentToLvl(src.lvl.list, doc, { onReferenced: 'unlink' });
    const d = report.diff;
    if (!d.moved.length && !d.rotated.length && !d.scaled.length && !d.added.length && !d.deleted.length) {
        throw new ModError('No level edits to export.');
    }
    const payload = writeObjectList(list);
    const warnings = [...report.warnings];
    const size = checkEntrySize(src.file, src.objects.size, payload.length);
    if (size) throw new ModError(size);
    if (report.unlinked.length) warnings.push(`${report.unlinked.length} link(s) to deleted objects were removed.`);
    const entry = {
        file: src.file,
        type: src.objects.type,
        id: src.objects.id,
        sub: src.objects.sub,
        bytes: payload.length,
        originalBytes: src.objects.size,
    };
    const files = buildModFiles(meta, [{ file: entry.file, entry, data: payload }]);
    return { meta, files, entry, report, warnings };
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
export async function writePackToModsFolder(pack: LevelPack): Promise<string | null> {
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
