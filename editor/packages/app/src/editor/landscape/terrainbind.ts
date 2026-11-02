// Joins the disc level's terrain, the document's editable copy (`LevelDocument.terrain`) and the live mesh in the
// viewport. The scene loader hands the decoded terrain over here (`bindLevelTerrain`); every document the view gets
// later (autosave restore, project open) is attached to the same disc layers, so its saved terrain edits show at once.

import type { LevelDocument } from '@drakan/editor-core';
import type { TerrainLayer } from '@drakan/formats';
import type { LevelView } from '../../level.ts';
import type { SceneTexture } from '../../scene/decode.ts';
import { EditableTerrain, type TerrainDirty } from '../../scene/terrainlive.ts';
import type { TextureLookup } from '../../scene/textures.ts';

interface Bound {
    view: LevelView;
    base: TerrainLayer[];
    textures: SceneTexture[];
    live: EditableTerrain;
}

let bound: Bound | null = null;
let flushRaf = 0;
const listeners = new Set<(d: TerrainDirty | null) => void>();
const hooked = new WeakSet<LevelView>();

/** Be told when the terrain changes (`null`: a whole new terrain or document). Returns the unsubscribe function. */
export function onTerrainChange(fn: (d: TerrainDirty | null) => void): () => void {
    listeners.add(fn);
    return () => listeners.delete(fn);
}

const emit = (d: TerrainDirty | null): void => listeners.forEach((l) => l(d));

function hookDoc(doc: LevelDocument): void {
    doc.onTerrain = (t) => {
        const b = bound;
        if (!b || b.view.doc !== doc) return;
        b.live.touch(t);
        if (!flushRaf)
            flushRaf = requestAnimationFrame(() => {
                flushRaf = 0;
                bound?.live.flush();
            });
        emit(t);
    };
}

function attach(v: LevelView, doc: LevelDocument): void {
    const b = bound;
    if (!b || b.view !== v) return;
    doc.attachTerrain(b.base);
    hookDoc(doc);
    b.live.rebuildAll();
    emit(null);
}

/**
 * The terrain of the level that just loaded: attach it to the view's document and return the mesh to show (it follows
 * the document's layers, edits included).
 */
export function bindLevelTerrain(
    v: LevelView,
    layers: TerrainLayer[],
    textures: SceneTexture[],
    lookup: TextureLookup,
): EditableTerrain {
    if (!hooked.has(v)) {
        hooked.add(v);
        v.documentListeners.add((doc) => attach(v, doc));
    }
    v.doc.attachTerrain(layers);
    const doc = (): LevelDocument => v.doc;
    const live = new EditableTerrain(() => doc().terrain ?? layers, lookup);
    bound = { view: v, base: layers, textures, live };
    hookDoc(v.doc);
    emit(null);
    return live;
}

/** Forget the level's terrain (another level is opening). */
export function clearLevelTerrain(): void {
    bound = null;
    emit(null);
}

/** The bound terrain, or null when no disc level terrain is loaded. */
export function levelTerrain(): Readonly<Bound> | null {
    return bound;
}

/** Rebuild touched chunks now (the brush calls this right after a dab, so the mesh never lags a frame). */
export function flushTerrain(): void {
    bound?.live.flush();
}
