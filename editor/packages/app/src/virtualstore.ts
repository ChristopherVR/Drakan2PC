// Keeps the converted Order of the Flame levels across page reloads: the pack's files in IndexedDB, in the browser's own
// storage for this page. Nothing leaves the machine. The disc worker writes and reads it, so the files never pass through
// the page thread.

const DB_NAME = 'drakan-converted-levels';
const FILES = 'files';
const META = 'meta';

export interface StoredLevels {
    levels: { name: string; dirPath: string }[];
    files: { path: string; bytes: Uint8Array }[];
}

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

/** Replace what is stored with these levels and files. A failure (private window, full disk) leaves memory as it is. */
export async function saveConverted(data: StoredLevels): Promise<void> {
    const db = await open();
    try {
        const tx = db.transaction([FILES, META], 'readwrite');
        tx.objectStore(FILES).clear();
        for (const f of data.files) tx.objectStore(FILES).put(f.bytes, f.path);
        tx.objectStore(META).put(data.levels, 'levels');
        await done(tx);
    } finally {
        db.close();
    }
}

export async function loadConverted(): Promise<StoredLevels | null> {
    const db = await open();
    try {
        const tx = db.transaction([FILES, META], 'readonly');
        const levels = await new Promise<StoredLevels['levels'] | undefined>((resolve, reject) => {
            const r = tx.objectStore(META).get('levels');
            r.onsuccess = () => resolve(r.result as StoredLevels['levels'] | undefined);
            r.onerror = () => reject(r.error);
        });
        if (!levels?.length) return null;
        const keys = await new Promise<IDBValidKey[]>((resolve, reject) => {
            const r = tx.objectStore(FILES).getAllKeys();
            r.onsuccess = () => resolve(r.result);
            r.onerror = () => reject(r.error);
        });
        const values = await new Promise<Uint8Array[]>((resolve, reject) => {
            const r = tx.objectStore(FILES).getAll();
            r.onsuccess = () => resolve(r.result as Uint8Array[]);
            r.onerror = () => reject(r.error);
        });
        return { levels, files: keys.map((k, i) => ({ path: String(k), bytes: values[i]! })) };
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
