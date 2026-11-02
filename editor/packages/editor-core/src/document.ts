import type { TerrainLayer } from '@drakan/formats';
import { cloneTerrainLayer, terrainLayerFromJson, terrainLayerToJson, type TerrainJson } from './terrain-json.ts';
import { classifyType, type EntityKind, type WorldDump } from './world.ts';

export interface Transform {
    /** Game units (the dump's world coordinates; y is up). */
    pos: [number, number, number];
    /** Euler XYZ radians; the world dump only carries yaw (rotation about y). */
    rot: [number, number, number];
}

export interface LevelObject {
    id: number;
    class: string;
    type: number;
    kind: EntityKind;
    resource: string;
    model: string;
    transform: Transform;
    props: Record<string, string | number | boolean>;
    /** LVL object id this object was loaded as (or duplicated from); absent for world-dump objects. */
    origin?: number;
    /** Scale triple; only present for LVL objects that have flag 0x100 (scale cannot be added to others). */
    scale?: [number, number, number];
    /** Ids of other objects this one triggers (LVL link list). Absent for world-dump objects. */
    links?: number[];
    /** Per-instance parameters (the fields that differ from the class default), as stored in the LVL. */
    fields?: DocField[];
    /** For a new object (no `origin`): the LVL object whose body is copied when exporting; else any instance of the class. */
    template?: number;
    /** Editor-only hint: the class's ODB stem (`NPCS`), also kept in `props.db` for LVL objects. */
    db?: string;
}

/** One LVL parameter in document form. `value` is a number (scalar types), a string, or dwords (arrays). */
export interface DocField {
    /** Position in the object's parameter list. */
    slot: number;
    /** Index of the field in the class (stable key). */
    fieldIndex: number;
    /** field_def type word (low byte = type code 1..0x0f, 0x1000 = array). */
    type: number;
    name: string;
    value: number | string | number[];
}

export const DOCUMENT_FORMAT = 'drakan-level-document';

export interface LevelDocumentJson {
    format: typeof DOCUMENT_FORMAT;
    version: 1;
    level: string;
    nextId: number;
    objects: LevelObject[];
    /** Edited and appended terrain layers only (absent when the terrain is unchanged). */
    terrain?: TerrainJson;
}

/** Terrain that changed: a cell rectangle of one layer (`x1`/`z1` exclusive), or the whole layer (structural edits). */
export interface TerrainTouch {
    layer: number;
    rect?: { x0: number; z0: number; x1: number; z1: number };
}

export function cloneObject(o: LevelObject): LevelObject {
    return {
        ...o,
        transform: { pos: [...o.transform.pos], rot: [...o.transform.rot] },
        props: { ...o.props },
        ...(o.scale ? { scale: [...o.scale] as [number, number, number] } : {}),
        ...(o.links ? { links: [...o.links] } : {}),
        ...(o.fields
            ? { fields: o.fields.map((f) => ({ ...f, value: Array.isArray(f.value) ? [...f.value] : f.value })) }
            : {}),
    };
}

/** In-memory editable level. Mutate only through commands (commands.ts) so undo stays correct. */
export class LevelDocument {
    level = '';
    nextId = 1;
    /** Playable extent in game units (x/z), when known; used by validation only. */
    bounds?: { minX: number; maxX: number; minZ: number; maxZ: number };
    private readonly map = new Map<number, LevelObject>();

    /**
     * Terrain layers of the level (formats `parseTerrain`), null until the disc level's terrain is attached. Edits are
     * copy-on-write per layer: a layer's arrays are cloned on its first edit (`writableLayer`), so the disc read is never
     * changed. Mutate only through terrain commands (terrain-edit.ts).
     */
    terrain: TerrainLayer[] | null = null;
    /** Indices of layers edited or appended (what an export has to write back). */
    terrainChanged = new Set<number>();
    /** Layer count of the disc level; indices at or past it were appended. */
    terrainBaseCount = 0;
    /** Called after every terrain change (brush dab, command, undo, redo) so a view can rebuild what it shows. */
    onTerrain: ((t: TerrainTouch) => void) | null = null;
    private readonly ownedLayers = new WeakSet<TerrainLayer>();
    /** Changed layers read from JSON, waiting for the disc terrain (`attachTerrain`). */
    private pendingTerrain: TerrainLayer[] | null = null;
    /** Applied edit commands per base layer, so undoing every edit drops it from `terrainChanged` again. */
    private readonly terrainEdits = new Map<number, number>();

    /**
     * Use the disc level's terrain. Edited layers read from a saved document replace their disc copies and appended
     * ones are added after them. Calling it again with terrain already attached does nothing.
     */
    attachTerrain(base: readonly TerrainLayer[]): void {
        if (this.terrain) return;
        const t = [...base];
        this.terrainBaseCount = base.length;
        const pending = (this.pendingTerrain ?? []).slice().sort((a, b) => a.index - b.index);
        this.pendingTerrain = null;
        for (const L of pending) {
            if (L.index < t.length) t[L.index] = L;
            else if (L.index === t.length) t.push(L);
            else continue;
            this.ownedLayers.add(L);
            this.terrainChanged.add(L.index);
            this.terrainEdits.set(L.index, 1);
        }
        this.terrain = t;
    }

    /** Edited layers waiting for `attachTerrain` (a document read from JSON before its disc terrain). */
    get hasPendingTerrain(): boolean {
        return !!this.pendingTerrain?.length;
    }

    /** The layer at `i` with arrays this document owns (cloned from the disc copy on first use). */
    writableLayer(i: number): TerrainLayer {
        const t = this.terrain;
        const L = t?.[i];
        if (!t || !L) throw new Error(`no terrain layer ${i}`);
        if (this.ownedLayers.has(L)) return L;
        const c = cloneTerrainLayer(L);
        t[i] = c;
        this.ownedLayers.add(c);
        this.terrainChanged.add(i);
        return c;
    }

    /** True when layer `i` was appended by the editor (not on the disc). */
    isAppendedLayer(i: number): boolean {
        return i >= this.terrainBaseCount;
    }

    /**
     * Put a whole layer at `i` (an expanded copy, or a new one when `i` is the layer count), or remove the last layer
     * (`L` null, appended layers only). Used by the structural terrain commands.
     */
    setTerrainLayer(i: number, L: TerrainLayer | null): void {
        const t = this.terrain;
        if (!t) throw new Error('no terrain attached');
        if (!L) {
            if (i !== t.length - 1) throw new Error('only the last layer can be removed');
            t.pop();
            this.terrainChanged.delete(i);
            this.terrainEdits.delete(i);
        } else {
            if (i > t.length) throw new Error(`layer ${i} is past the end`);
            L.index = i;
            t[i] = L;
            this.ownedLayers.add(L);
            this.terrainChanged.add(i);
        }
        this.touchTerrain({ layer: i });
    }

    /** Count an edit command applied (+1) or reverted (-1) on layer `i`; at zero a disc layer is no longer changed. */
    markTerrainEdit(i: number, delta: number): void {
        const n = (this.terrainEdits.get(i) ?? 0) + delta;
        if (n > 0) {
            this.terrainEdits.set(i, n);
            this.terrainChanged.add(i);
        } else {
            this.terrainEdits.delete(i);
            if (!this.isAppendedLayer(i)) this.terrainChanged.delete(i);
        }
    }

    touchTerrain(t: TerrainTouch): void {
        this.onTerrain?.(t);
    }

    get size(): number {
        return this.map.size;
    }
    get(id: number): LevelObject | undefined {
        return this.map.get(id);
    }
    has(id: number): boolean {
        return this.map.has(id);
    }
    objects(): IterableIterator<LevelObject> {
        return this.map.values();
    }
    /** Insert as-is (keeps its id) or allocate a fresh id when `id` is 0. */
    insert(o: LevelObject): LevelObject {
        if (o.id <= 0) o = { ...o, id: this.nextId };
        if (o.id >= this.nextId) this.nextId = o.id + 1;
        this.map.set(o.id, o);
        return o;
    }
    /** Replace the contents of an existing object (same id), keeping its position in iteration order. */
    replace(o: LevelObject): void {
        if (this.map.has(o.id)) this.map.set(o.id, o);
    }
    remove(id: number): LevelObject | undefined {
        const o = this.map.get(id);
        this.map.delete(id);
        return o;
    }

    static fromWorld(w: WorldDump): LevelDocument {
        const d = new LevelDocument();
        d.level = w.levelName;
        for (const e of w.entities) {
            const hasHp = typeof e.hp === 'number';
            const p = e.pos ?? [0, 0, 0];
            const props: LevelObject['props'] = {};
            if (hasHp) props.hp = e.hp as number;
            if (typeof e.hpMax === 'number') props.hpMax = e.hpMax;
            if (e.flags) props.flags = e.flags;
            if (e.handle) props.handle = e.handle;
            d.insert({
                id: 0,
                class: e.class ?? '',
                type: e.type ?? -1,
                kind: classifyType(e.type, hasHp),
                resource: e.resId ?? '',
                model: e.model ?? '',
                transform: { pos: [p[0] ?? 0, p[1] ?? 0, p[2] ?? 0], rot: [0, e.yaw ?? 0, 0] },
                props,
            });
        }
        return d;
    }

    toJSON(): LevelDocumentJson {
        return {
            format: DOCUMENT_FORMAT,
            version: 1,
            level: this.level,
            nextId: this.nextId,
            objects: [...this.map.values()],
            ...this.terrainJson(),
        };
    }

    private terrainJson(): { terrain?: TerrainJson } {
        const layers = this.terrain
            ? [...this.terrainChanged]
                  .sort((a, b) => a - b)
                  .map((i) => this.terrain![i])
                  .filter((L): L is TerrainLayer => !!L)
            : (this.pendingTerrain ?? []);
        if (!layers.length) return {};
        return { terrain: { baseCount: this.terrainBaseCount, layers: layers.map(terrainLayerToJson) } };
    }

    static fromJSON(text: string | LevelDocumentJson): LevelDocument {
        const j = typeof text === 'string' ? (JSON.parse(text) as LevelDocumentJson) : text;
        if (j.format !== DOCUMENT_FORMAT || j.version !== 1) throw new Error('not a level document (version 1)');
        const d = new LevelDocument();
        d.level = j.level;
        for (const o of j.objects) d.insert(cloneObject(o));
        d.nextId = Math.max(d.nextId, j.nextId);
        if (j.terrain?.layers.length) {
            d.terrainBaseCount = j.terrain.baseCount;
            d.pendingTerrain = j.terrain.layers.map(terrainLayerFromJson);
        }
        return d;
    }
}
