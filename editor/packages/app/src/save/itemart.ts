import type { Api, Method } from '../rpc.ts';
import { TextureCache, classThumbnail } from '../scene/index.ts';
import type { ModelData } from '../scene/decode.ts';
import type { ClassInfo } from './inventory.ts';

type Call = <M extends Method>(method: M, ...args: Parameters<Api[M]>) => Promise<ReturnType<Api[M]>>;

/** Items live in the shared Resources database, which the game (and every save) addresses as db index 3. */
const RESOURCES_DB = 3;
const THUMB = 96;

export interface ItemArt {
    /** Name and picture of a save resource id (`0x31bc9`); null when it is not a Resources class or no disc is open. */
    info(resource: string): ClassInfo | null;
    /** Class names (sorted) for pickers; empty until the catalog has loaded. */
    classes(): { classId: number; name: string }[];
    /** Forget everything (a different disc was opened) and start loading again on the next request. */
    reset(): void;
    onChange(fn: () => void): void;
}

/**
 * Item names and thumbnails without a level: the Resources ODB gives the names, the models are decoded in the worker
 * and rendered with the same thumbnail renderer the Level tab uses. Cached for the session.
 */
export function createItemArt(call: Call): ItemArt {
    let names: Map<number, { name: string; detail: string; cells?: number; powerUp?: boolean }> | null = null;
    let loading: Promise<void> | null = null;
    let failed = false;
    let cache = new TextureCache();
    const thumbs = new Map<number, string>();
    const queued = new Set<number>();
    const listeners: (() => void)[] = [];
    let chain: Promise<void> = Promise.resolve();
    let epoch = 0;

    const notify = (): void => listeners.forEach((f) => f());

    function ensureCatalog(): void {
        if (names || loading || failed) return;
        const ep = epoch;
        loading = call('loadItemCatalog')
            .then((c) => {
                if (ep !== epoch) return;
                names = new Map(
                    c.classes.map((k) => [
                        k.classId,
                        {
                            name: k.displayName || k.name,
                            ...(k.cells !== undefined ? { cells: k.cells } : {}),
                            ...(k.powerUp ? { powerUp: true } : {}),
                            // The ODB class name is the editor-facing one; the game shows `displayName`.
                            detail: [
                                ...(k.facts ?? []),
                                k.displayName && k.displayName !== k.name ? `Class: ${k.name}` : '',
                                k.price ? `Shop price: ${k.price} gold` : '',
                            ]
                                .filter(Boolean)
                                .join('\n'),
                        },
                    ]),
                );
                notify();
            })
            .catch((e) => {
                console.warn('item catalog failed', e);
                if (ep === epoch) failed = true; // no disc yet: retried after reset()
            })
            .finally(() => {
                if (ep === epoch) loading = null;
            });
    }

    function render(data: ModelData[]): void {
        for (const d of data) {
            if (!d.model || !d.model.triTexture.length) continue;
            const lookup = cache.addShared(d.textures);
            const url = classThumbnail(`item:${d.classId}`, d.model, lookup, THUMB, `item:${d.classId}`);
            if (url) thumbs.set(d.classId, url);
        }
    }

    function want(classId: number): void {
        if (queued.has(classId)) return;
        queued.add(classId);
        const ep = epoch;
        chain = chain.then(async () => {
            if (ep !== epoch) return;
            try {
                const data = await call('loadItemModels', [classId]);
                if (ep !== epoch) return;
                render(data);
                notify();
            } catch {
                /* the placeholder stays */
            }
        });
    }

    return {
        info(resource) {
            const n = Number(resource) >>> 0;
            if (n >>> 16 !== RESOURCES_DB) return null;
            ensureCatalog();
            const classId = n & 0xffff;
            const item = names?.get(classId);
            if (!item) return null;
            want(classId);
            return {
                name: item.name,
                detail: item.detail,
                thumb: thumbs.get(classId) ?? null,
                ...(item.cells !== undefined ? { cells: item.cells } : {}),
                ...(item.powerUp ? { powerUp: true } : {}),
            };
        },
        classes: () =>
            names
                ? [...names]
                      .map(([classId, v]) => ({ classId, name: v.name }))
                      .sort((a, b) => a.name.localeCompare(b.name))
                : [],
        reset() {
            epoch++;
            names = null;
            loading = null;
            failed = false;
            cache.dispose();
            cache = new TextureCache();
            thumbs.clear();
            queued.clear();
            chain = Promise.resolve();
        },
        onChange: (fn) => void listeners.push(fn),
    };
}
