// Loads the real visuals of a disc level into the viewport: terrain and per-class models through the worker, thumbnails
// for the Asset Browser, and the drag-preview models. Everything runs off the UI thread except building three.js
// objects, which is done in small batches so the editor stays interactive while a level fills in.

import { resourceParts, type AssetClass } from '@drakan/editor-core';
import type { LevelView } from './level.ts';
import type { Api, Method } from './rpc.ts';
import { TextureCache, classThumbnail, classesToLoad } from './scene/index.ts';
import { bindLevelTerrain, clearLevelTerrain } from './editor/landscape/terrainbind.ts';

type Call = <M extends Method>(method: M, ...args: Parameters<Api[M]>) => Promise<ReturnType<Api[M]>>;

export interface SceneLoaderHooks {
    status(msg: string): void;
    toast(msg: string, kind?: 'info' | 'error' | 'ok'): void;
    /** A thumbnail became available (`url` is a PNG data URL). */
    thumbnail(resource: string, url: string): void;
    /** Models were added or failed; the Asset Browser may refresh. */
    modelsChanged(): void;
}

/** How far a level load got, for the viewport's loading card (editor/loadprogress.ts). */
export type SceneProgress =
    | { step: 'terrain'; state: 'active' | 'done' | 'failed'; layers?: number }
    | {
          step: 'models';
          state: 'active' | 'done' | 'failed';
          done: number;
          total: number;
          /** Classes whose model reference did not resolve or decode (shown as markers). */
          missing?: number;
          /** Classes with no visible model of their own (logic, helpers): icons or markers, not a problem. */
          modelless?: number;
      };

/** Classes per worker request while a level loads (keeps each request short and progress visible). */
const BATCH = 24;
const THUMB_SIZE = 96;

export interface SceneLoader {
    /** Drop the current visuals and load terrain and the models of `resources` for the level in `dirPath`. */
    loadLevel(dirPath: string, resources: Iterable<string>, progress?: (p: SceneProgress) => void): Promise<void>;
    /** Stop loading the current level's models; what has loaded stays (the rest show as markers). */
    stop(): void;
    /** Make sure models exist for these classes (thumbnails, drag ghost, newly placed classes). */
    ensureModels(resources: string[]): Promise<void>;
    /** Ask for a thumbnail of a catalog class (loads its model first when needed). Returns at once. */
    wantThumbnail(cls: AssetClass): void;
    /** Cached thumbnail, or null. */
    thumbnail(resource: string): string | null;
    clear(): void;
}

/** The status line after a level's models have loaded. Classes without a model of their own are not a failure. */
export function modelsStatus(added: number, total: number, modelless: number, missing: number): string {
    let s = `Models: ${added} of ${total} classes decoded`;
    if (modelless) s += `; ${modelless} have no model of their own (shown as icons or markers)`;
    if (missing) s += `; ${missing} could not be loaded (shown as markers)`;
    return `${s}.`;
}

export function createSceneLoader(call: Call, view: () => LevelView, hooks: SceneLoaderHooks): SceneLoader {
    let token = 0;
    /** The load whose models were stopped (its terrain and the classes asked for one by one still come in). */
    let stoppedTk = -1;
    let dirPath = '';
    let cache = new TextureCache();
    const requested = new Set<string>();
    const thumbs = new Map<string, string>();
    const thumbWanted = new Set<string>();
    const renderQueue: string[] = [];
    let pumping = false;
    // One worker request at a time, in order: level loading first, thumbnail and ghost requests after it.
    let chain: Promise<void> = Promise.resolve();

    function pump(): void {
        if (pumping || !renderQueue.length) return;
        pumping = true;
        requestAnimationFrame(() => {
            pumping = false;
            const t0 = performance.now();
            const layer = view().models;
            while (renderQueue.length && performance.now() - t0 < 6) {
                const res = renderQueue.shift()!;
                const m = layer.modelOf(res);
                if (!m || thumbs.has(res)) continue;
                const url = classThumbnail(res, m.model, m.lookup, THUMB_SIZE, res);
                if (url) {
                    thumbs.set(res, url);
                    hooks.thumbnail(res, url);
                }
            }
            pump();
        });
    }

    function queueThumb(res: string): void {
        if (thumbs.has(res) || renderQueue.includes(res)) return;
        renderQueue.push(res);
        pump();
    }

    async function fetchModels(
        path: string,
        tk: number,
        list: { resource: string }[],
        label: string,
        onBatch?: (done: number) => void,
    ): Promise<number> {
        const stoppable = !!label;
        let added = 0;
        const layer = view().models;
        for (let i = 0; i < list.length; i += BATCH) {
            const part = list.slice(i, i + BATCH);
            if (label) hooks.status(`${label} ${Math.min(i + BATCH, list.length)}/${list.length}...`);
            const data = await call(
                'loadModels',
                path,
                part.map((c) => resourceParts(c.resource)),
            );
            if (tk !== token || (stoppable && tk === stoppedTk)) return added;
            const ok = layer.addModelData(data, cache);
            added += ok.length;
            view().modelsChanged();
            onBatch?.(Math.min(i + BATCH, list.length));
            for (const res of ok) if (thumbWanted.has(res)) queueThumb(res);
            // Let the viewport render between batches.
            await new Promise((r) => setTimeout(r, 0));
        }
        return added;
    }

    function ensureModels(resources: string[]): Promise<void> {
        const layer = view().models;
        const need = classesToLoad(resources, (r) => layer.known(r) || requested.has(r));
        if (!need.length || !dirPath) return chain;
        need.forEach((c) => requested.add(c.resource));
        const tk = token;
        const path = dirPath;
        chain = chain.then(async () => {
            if (tk !== token) return;
            try {
                await fetchModels(path, tk, need, '');
                hooks.modelsChanged();
            } catch {
                /* the icon stays */
            }
        });
        return chain;
    }

    return {
        async loadLevel(path, resources, progress) {
            const tk = ++token;
            dirPath = path;
            requested.clear();
            thumbs.clear();
            thumbWanted.clear();
            renderQueue.length = 0;
            const v = view();
            v.models.reset();
            v.setGround({});
            clearLevelTerrain();
            cache.dispose();
            cache = new TextureCache();
            v.modelsChanged();
            try {
                hooks.status('Loading terrain...');
                progress?.({ step: 'terrain', state: 'active' });
                const t = await call('loadTerrain', path);
                if (tk !== token) return;
                // Editable: follows the document's terrain (landscape edits, restored autosaves).
                const terrain = bindLevelTerrain(v, t.layers, t.textures, cache.add('terrain', t.textures));
                v.models.setTerrain(terrain);
                v.setGround({
                    height: (x, z) => {
                        const h = terrain.heightAt(x, z);
                        return Number.isFinite(h) ? h : null;
                    },
                    meshes: [terrain.group],
                });
                v.modelsChanged();
                v.frameAll();
                hooks.status(`Terrain: ${t.layers.length} layers, ${terrain.group.children.length} meshes.`);
                progress?.({ step: 'terrain', state: 'done', layers: t.layers.length });
            } catch (e) {
                if (tk !== token) return;
                progress?.({ step: 'terrain', state: 'failed' });
                hooks.toast(`Terrain failed to load: ${e instanceof Error ? e.message : String(e)}`, 'error');
            }
            if (tk === stoppedTk) return;
            const want = classesToLoad(resources, () => false);
            want.forEach((c) => requested.add(c.resource));
            const total = want.length;
            progress?.({ step: 'models', state: 'active', done: 0, total });
            try {
                const run = chain.then(() =>
                    fetchModels(path, tk, want, 'Loading models', (done) => {
                        if (tk === token) progress?.({ step: 'models', state: 'active', done, total });
                    }),
                );
                chain = run.then(
                    () => undefined,
                    () => undefined,
                );
                const added = await run;
                if (tk !== token || tk === stoppedTk) return;
                const layer = view().models;
                const missing = want.filter((c) => layer.failedResources.has(c.resource)).length;
                const modelless = want.filter((c) => layer.modellessResources.has(c.resource)).length;
                hooks.modelsChanged();
                hooks.status(modelsStatus(added, want.length, modelless, missing));
                progress?.({ step: 'models', state: 'done', done: total, total, missing, modelless });
                // The loading card says it when there is one.
                if (missing && !progress)
                    hooks.toast(`${missing} of ${want.length} classes could not load their model: shown as markers.`);
            } catch (e) {
                if (tk !== token) return;
                progress?.({ step: 'models', state: 'failed', done: 0, total });
                hooks.toast(`Models failed to load: ${e instanceof Error ? e.message : String(e)}`, 'error');
            }
        },

        stop() {
            // The batch on its way is the last; classes it did not reach can still be asked for one by one.
            stoppedTk = token;
            requested.clear();
        },

        ensureModels,

        wantThumbnail(cls) {
            const res = cls.resource;
            if (thumbs.has(res)) return;
            thumbWanted.add(res);
            const layer = view().models;
            if (layer.has(res)) queueThumb(res);
            else if (!layer.known(res)) void ensureModels([res]);
        },

        thumbnail: (res) => thumbs.get(res) ?? null,

        clear() {
            token++;
            clearLevelTerrain();
            cache.dispose();
            cache = new TextureCache();
            view().models.reset();
        },
    };
}
