// Keeps the converted Order of the Flame levels across page reloads: the pack's files in IndexedDB, in the browser's own
// storage for this page. Nothing leaves the machine. The disc worker writes and reads it, so the files never pass through
// the page thread.
//
// What is stored belongs to one disc: its volume id and the language tree the pack was converted against (`DiscKey`). A
// different disc or language never gets another disc's files; the next save replaces them.

const DB_NAME = 'drakan-converted-levels';
const FILES = 'files';
const META = 'meta';

/** Files are read back in transactions of about this many bytes, so a pack of hundreds of megabytes never sits in one result. */
export const LOAD_BATCH_BYTES = 48 * 1024 * 1024;

export interface StoredLevels {
    levels: { name: string; dirPath: string }[];
    files: { path: string; bytes: Uint8Array }[];
}

/** Which disc (and language tree) the stored pack was converted for. */
export interface DiscKey {
    volumeId: string;
    language: string;
}

const sameDisc = (a: DiscKey | undefined, b: DiscKey): boolean =>
    !!a && a.volumeId === b.volumeId && a.language === b.language;

function open(): Promise<IDBDatabase> {
    return new Promise((resolve, reject) => {
        const req = indexedDB.open(DB_NAME, 1);
        req.onupgradeneeded = () => {
            req.result.createObjectStore(FILES);
            req.result.createObjectStore(META);
        };
        req.onsuccess = () => resolve(req.result);
        req.onerror = () => reject(req.error ?? new Error('Could not open the converted levels store'));
    });
}

const done = (tx: IDBTransaction): Promise<void> =>
    new Promise((resolve, reject) => {
        tx.oncomplete = () => resolve();
        tx.onerror = () => reject(tx.error ?? new Error('Could not write the converted levels'));
        tx.onabort = () => reject(tx.error ?? new Error('Could not write the converted levels'));
    });

const get = <T>(store: IDBObjectStore, key: IDBValidKey): Promise<T | undefined> =>
    new Promise((resolve, reject) => {
        const r = store.get(key);
        r.onsuccess = () => resolve(r.result as T | undefined);
        r.onerror = () => reject(r.error);
    });

/** A sentence for the user when a write failed (quota, private window), without browser jargon. */
export function storeFailureMessage(e: unknown): string {
    const name =
        e instanceof Error || (typeof e === 'object' && e !== null && 'name' in e)
            ? String((e as { name: unknown }).name)
            : '';
    const full = name === 'QuotaExceededError' || /quota|full/i.test(e instanceof Error ? e.message : String(e));
    return full
        ? 'The browser has no room left to keep the converted levels, so they stay available for this visit only and must be converted again after a reload. Free some disk space or clear site data you no longer need.'
        : 'The browser would not keep the converted levels (a private window or blocked site data), so they stay available for this visit only.';
}

/**
 * Add these levels and files to what is stored for this disc (accumulating over several imports). Stored data of another
 * disc or language is replaced. The write is one transaction: a failure (quota, private window) leaves the store as it was.
 */
export async function saveConverted(data: StoredLevels, disc: DiscKey): Promise<void> {
    const db = await open();
    try {
        const tx = db.transaction([FILES, META], 'readwrite');
        const files = tx.objectStore(FILES);
        const meta = tx.objectStore(META);
        const [storedDisc, storedLevels] = await Promise.all([
            get<DiscKey>(meta, 'disc'),
            get<StoredLevels['levels']>(meta, 'levels'),
        ]);
        let levels = storedLevels ?? [];
        if (!sameDisc(storedDisc, disc)) {
            files.clear();
            levels = [];
        }
        const byDir = new Map(levels.map((l) => [l.dirPath.toUpperCase(), l]));
        for (const l of data.levels) byDir.set(l.dirPath.toUpperCase(), l);
        for (const f of data.files) files.put(f.bytes, f.path);
        meta.put([...byDir.values()], 'levels');
        meta.put({ volumeId: disc.volumeId, language: disc.language }, 'disc');
        await done(tx);
    } finally {
        db.close();
    }
}

/** One batch of stored files after `after` (exclusive), in key order, stopping once `maxBytes` are collected. */
async function readBatch(
    db: IDBDatabase,
    after: IDBValidKey | null,
    maxBytes: number,
): Promise<{ files: StoredLevels['files']; last: IDBValidKey | null; end: boolean }> {
    const tx = db.transaction(FILES, 'readonly');
    const range = after === null ? null : IDBKeyRange.lowerBound(after, true);
    return new Promise((resolve, reject) => {
        const files: StoredLevels['files'] = [];
        let bytes = 0;
        let last: IDBValidKey | null = null;
        const req = tx.objectStore(FILES).openCursor(range);
        req.onerror = () => reject(req.error);
        req.onsuccess = () => {
            const c = req.result;
            if (!c) return resolve({ files, last, end: true });
            const v = c.value as Uint8Array;
            files.push({ path: String(c.key), bytes: v });
            last = c.key;
            bytes += v.byteLength;
            if (bytes >= maxBytes) return resolve({ files, last, end: false });
            c.continue();
        };
    });
}

/** The stored pack for this disc, or null when nothing is stored or it belongs to another disc or language. */
export async function loadConverted(disc: DiscKey, batchBytes = LOAD_BATCH_BYTES): Promise<StoredLevels | null> {
    const db = await open();
    try {
        const tx = db.transaction(META, 'readonly');
        const [storedDisc, levels] = await Promise.all([
            get<DiscKey>(tx.objectStore(META), 'disc'),
            get<StoredLevels['levels']>(tx.objectStore(META), 'levels'),
        ]);
        if (!levels?.length || !sameDisc(storedDisc, disc)) return null;
        const files: StoredLevels['files'] = [];
        let after: IDBValidKey | null = null;
        for (;;) {
            const b = await readBatch(db, after, batchBytes);
            files.push(...b.files);
            if (b.end) break;
            after = b.last;
        }
        return { levels, files };
    } finally {
        db.close();
    }
}

export async function clearConverted(): Promise<void> {
    const db = await open();
    try {
        const tx = db.transaction([FILES, META], 'readwrite');
        tx.objectStore(FILES).clear();
        tx.objectStore(META).clear();
        await done(tx);
    } finally {
        db.close();
    }
}
