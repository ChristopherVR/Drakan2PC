import type { LevelDocumentJson } from '@drakan/editor-core';
import type { CameraBookmark } from '../level.ts';

/**
 * Autosave of the working document to IndexedDB, keyed by level name. Everything is wrapped in try/catch: private
 * windows, blocked site data and quota errors only mean "no autosave", never an editor error. The stored document is
 * the editor's own JSON (object ids, transforms, links, fields), not game data from the disc beyond what the user edited.
 */
export interface AutosaveRecord {
    key: string;
    level: string;
    savedAt: number;
    doc: LevelDocumentJson;
    bookmarks: CameraBookmark[];
    mod: { id: string; name: string; version: string; author: string };
}

const DB = 'drakan-editor';
const STORE = 'autosave';

function open(): Promise<IDBDatabase> {
    return new Promise((resolve, reject) => {
        try {
            const req = indexedDB.open(DB, 1);
            req.onupgradeneeded = () => req.result.createObjectStore(STORE, { keyPath: 'key' });
            req.onsuccess = () => resolve(req.result);
            req.onerror = () => reject(req.error ?? new Error('indexedDB open failed'));
            req.onblocked = () => reject(new Error('indexedDB blocked'));
        } catch (e) {
            reject(e instanceof Error ? e : new Error(String(e)));
        }
    });
}

async function tx<T>(mode: IDBTransactionMode, fn: (s: IDBObjectStore) => IDBRequest<T>): Promise<T | undefined> {
    try {
        const db = await open();
        return await new Promise<T | undefined>((resolve) => {
            try {
                const t = db.transaction(STORE, mode);
                const r = fn(t.objectStore(STORE));
                r.onsuccess = () => resolve(r.result);
                r.onerror = () => resolve(undefined);
                t.oncomplete = () => db.close();
                t.onabort = () => resolve(undefined);
            } catch {
                resolve(undefined);
            }
        });
    } catch {
        return undefined;
    }
}

export const autosaveKey = (level: string): string => `level:${level}`;

export async function saveAutosave(rec: AutosaveRecord): Promise<boolean> {
    return (await tx('readwrite', (s) => s.put(rec))) !== undefined;
}

export async function loadAutosave(key: string): Promise<AutosaveRecord | undefined> {
    return (
        (await tx<AutosaveRecord | undefined>(
            'readonly',
            (s) => s.get(key) as IDBRequest<AutosaveRecord | undefined>,
        )) ?? undefined
    );
}

export async function clearAutosave(key: string): Promise<void> {
    await tx('readwrite', (s) => s.delete(key));
}
