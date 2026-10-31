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
        };
    }

    static fromJSON(text: string | LevelDocumentJson): LevelDocument {
        const j = typeof text === 'string' ? (JSON.parse(text) as LevelDocumentJson) : text;
        if (j.format !== DOCUMENT_FORMAT || j.version !== 1) throw new Error('not a level document (version 1)');
        const d = new LevelDocument();
        d.level = j.level;
        for (const o of j.objects) d.insert(cloneObject(o));
        d.nextId = Math.max(d.nextId, j.nextId);
        return d;
    }
}
