import { nextBackupName, slotNumber } from '@drakan/formats';

/** Minimal File System Access API shapes (lib.dom lacks the pickers and has no async iteration for handles). */
export interface FsWritable {
    write(data: Uint8Array | Blob): Promise<void>;
    close(): Promise<void>;
}
export interface FsFileHandle {
    kind: 'file';
    name: string;
    getFile(): Promise<File>;
    createWritable(): Promise<FsWritable>;
}
export interface FsDirHandle {
    kind: 'directory';
    name: string;
    values(): AsyncIterable<FsFileHandle | FsDirHandle>;
    getFileHandle(name: string, options?: { create?: boolean }): Promise<FsFileHandle>;
}

interface FsaWindow {
    showOpenFilePicker?: (o?: unknown) => Promise<FsFileHandle[]>;
    showDirectoryPicker?: (o?: unknown) => Promise<FsDirHandle>;
    showSaveFilePicker?: (o?: unknown) => Promise<FsFileHandle>;
}
const fsa = (): FsaWindow => window as unknown as FsaWindow;

/** True when the browser can pick files with write access. `?nofsa` in the URL forces the plain `<input>` fallback. */
export function hasFsa(): boolean {
    if (/[?&]nofsa\b/.test(location.search)) return false;
    return typeof fsa().showOpenFilePicker === 'function' && typeof fsa().showDirectoryPicker === 'function';
}

/** A save found on disk (or picked by the user). */
export interface SaveRef {
    /** File name, e.g. `Level 1A - Surdana.rsg`. */
    name: string;
    /** Folder the file sits in, e.g. `BESCES-50006Slot3` (empty for a single picked file). */
    folder: string;
    size: number;
    /** `File.lastModified` (ms). */
    modified: number;
    read(): Promise<Uint8Array>;
    /** Present when the file can be written back in place. */
    handle?: FsFileHandle;
    /** The containing folder, when known; the `.bak` is written next to the file there. */
    dir?: FsDirHandle;
}

const isRsg = (name: string): boolean => /\.rsg$/i.test(name);

export function refFromFile(file: File, extra: Partial<SaveRef> = {}): SaveRef {
    return {
        name: file.name,
        folder: '',
        size: file.size,
        modified: file.lastModified,
        read: async () => new Uint8Array(await file.arrayBuffer()),
        ...extra,
    };
}

export async function pickSaveFile(): Promise<SaveRef | null> {
    const pick = fsa().showOpenFilePicker;
    if (!pick) return null;
    try {
        const [h] = await pick.call(window, {
            types: [{ description: 'Save file', accept: { 'application/octet-stream': ['.rsg'] } }],
        });
        if (!h) return null;
        return refFromFile(await h.getFile(), { handle: h, read: () => readHandle(h) });
    } catch (e) {
        if (e instanceof DOMException && e.name === 'AbortError') return null;
        throw e;
    }
}

/** Fresh read: a `File` from a handle is a snapshot and goes stale once the file is rewritten. */
async function readHandle(h: FsFileHandle): Promise<Uint8Array> {
    return new Uint8Array(await (await h.getFile()).arrayBuffer());
}

async function walk(dir: FsDirHandle, folder: string, depth: number, out: SaveRef[]): Promise<void> {
    for await (const entry of dir.values()) {
        if (entry.kind === 'file') {
            if (!isRsg(entry.name)) continue;
            const file = await entry.getFile();
            out.push(refFromFile(file, { folder, handle: entry, dir, read: () => readHandle(entry) }));
        } else if (depth > 0 && !entry.name.startsWith('.')) {
            await walk(entry, entry.name, depth - 1, out);
        }
    }
}

function sortRefs(refs: SaveRef[]): SaveRef[] {
    return refs.sort((a, b) => {
        const sa = slotNumber(a.folder) ?? Infinity;
        const sb = slotNumber(b.folder) ?? Infinity;
        return sa - sb || a.folder.localeCompare(b.folder) || a.name.localeCompare(b.name);
    });
}

/** Find memcard slot folders (`BESCES-50006SlotNN`) under a picked folder: the card root, `mc0`, or one slot. */
export async function pickSaveFolder(): Promise<{ root: string; refs: SaveRef[] } | null> {
    const pick = fsa().showDirectoryPicker;
    if (!pick) return null;
    try {
        const dir = await pick.call(window, { mode: 'readwrite' });
        const refs: SaveRef[] = [];
        await walk(dir, dir.name, 4, refs);
        return { root: dir.name, refs: sortRefs(refs) };
    } catch (e) {
        if (e instanceof DOMException && e.name === 'AbortError') return null;
        throw e;
    }
}

/** Fallback for browsers without the directory picker: `<input webkitdirectory>` file lists (read only). */
export function refsFromFileList(files: FileList | File[]): { root: string; refs: SaveRef[] } {
    const refs: SaveRef[] = [];
    let root = '';
    for (const f of Array.from(files)) {
        const parts = (f.webkitRelativePath || f.name).split('/');
        if (!root && parts.length > 1) root = parts[0]!;
        if (!isRsg(f.name)) continue;
        refs.push(refFromFile(f, { folder: parts.length > 1 ? parts[parts.length - 2]! : '' }));
    }
    return { root, refs: sortRefs(refs) };
}

export function download(name: string, data: Uint8Array, type = 'application/octet-stream'): void {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([data as BlobPart], { type }));
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 10_000);
}

async function writeAll(handle: FsFileHandle, data: Uint8Array): Promise<void> {
    const w = await handle.createWritable();
    await w.write(data);
    await w.close();
}

/**
 * Write `bytes` over the file, keeping the previous contents as a backup next to it first (`NAME.bak`, then
 * `.bak.1`, ...). With a folder handle the backup is automatic; with only a file handle the browser asks where to
 * put it (the dialog opens beside the file). Returns the backup name. Nothing is written if the backup fails.
 */
export async function saveWithBackup(ref: SaveRef, bytes: Uint8Array): Promise<string> {
    const handle = ref.handle;
    if (!handle) throw new Error('This save was opened read-only; use EXPORT.');
    const current = new Uint8Array(await (await handle.getFile()).arrayBuffer());
    let backup: string;
    if (ref.dir) {
        const names: string[] = [];
        for await (const e of ref.dir.values()) names.push(e.name);
        backup = nextBackupName(names, handle.name);
        await writeAll(await ref.dir.getFileHandle(backup, { create: true }), current);
    } else {
        const pick = fsa().showSaveFilePicker;
        if (!pick) throw new Error('Cannot write a backup in this browser; use EXPORT.');
        const bak = await pick.call(window, { suggestedName: `${handle.name}.bak`, startIn: handle });
        backup = bak.name;
        await writeAll(bak, current);
    }
    await writeAll(handle, bytes);
    return backup;
}
