import {
    classFieldDefault,
    decodeElement,
    dependencyPath,
    odbStemForDependency,
    refParts,
    type LvlHeader,
    type OdbClass,
    type OdbIndex,
} from '@drakan/formats';
import { resourceParts, resourceString } from './catalog.ts';
import type { DocField, LevelDocument, LevelObject } from './document.ts';

/**
 * Field schema for the inspector. The type word comes from the ODB 0x0103 / LVL `field_def` (level-format.md section 4):
 * low byte 1..0x0f, 0x1000 = array. The full class field list and defaults come from the ODB class record
 * (level-format.md section 12, `ClassSchema` below); per-instance values are the LVL parameters, i.e. the fields that
 * differ from the class default (`LevelObject.fields`).
 */
export type Widget = 'int' | 'float' | 'bool' | 'enum' | 'colour' | 'class' | 'ref' | 'id' | 'string' | 'array';

export const FIELD_TYPE_NAMES: Record<number, string> = {
    1: 'int',
    2: 'float',
    3: 'class',
    4: 'model',
    5: 'sound',
    6: 'boolean',
    7: 'enum',
    8: 'character channel',
    9: 'animation',
    0x0a: 'string',
    0x0b: 'sequence',
    0x0c: 'layer',
    0x0d: 'unknown',
    0x0e: 'texture',
    0x0f: 'colour',
};

export const typeCode = (type: number): number => type & 0xff;
export const isArrayType = (type: number): boolean => (type & 0x1000) !== 0;

export function typeName(type: number): string {
    const base = FIELD_TYPE_NAMES[typeCode(type)] ?? `type 0x${typeCode(type).toString(16)}`;
    const hi = (type >> 8) & 0xef;
    return `${base}${isArrayType(type) ? ' array' : ''}${hi ? ` (0x${type.toString(16)})` : ''}`;
}

/**
 * Widget for a type word. Every class-typed field (type 3, also with the filter bits 0x2000/0x4000 the PS2 pool uses:
 * 0x2003, 0x3003, 0x4003, 0x5003) holds a class reference `(db << 16) | class id`, db relative to the ODB that owns the
 * field's class (checked against all 38415 level objects: 100% of the non-zero values name an existing class). Model,
 * sound, animation, sequence and texture fields are resource references of the same shape.
 */
export function widgetFor(type: number): Widget {
    if (isArrayType(type)) return 'array';
    const base = typeCode(type);
    switch (base) {
        case 1:
            return 'int';
        case 2:
            return 'float';
        case 3:
            return 'class';
        case 4:
        case 5:
        case 8:
        case 9:
        case 0x0b:
        case 0x0c:
        case 0x0e:
            return 'ref';
        case 6:
            return 'bool';
        case 7:
            return 'enum';
        case 0x0a:
            return 'string';
        case 0x0f:
            return 'colour';
        default:
            return 'id';
    }
}

/** Every field type can be written (scalars as one dword, strings and arrays through `setObjectParams`). */
export const isEditableType = (_type: number): boolean => true;

/** Colour fields are ARGB u32. */
export function argbToParts(v: number): { hex: string; alpha: number } {
    const n = v >>> 0;
    return { hex: `#${(n & 0xffffff).toString(16).padStart(6, '0')}`, alpha: n >>> 24 };
}
export function partsToArgb(hex: string, alpha: number): number {
    const rgb = Number.parseInt(hex.replace('#', ''), 16) & 0xffffff;
    return ((Math.max(0, Math.min(255, Math.round(alpha))) << 24) | rgb) >>> 0;
}

/** Display text for a value. */
export function formatFieldValue(f: DocField): string {
    if (Array.isArray(f.value))
        return `[${f.value.length}] ${f.value.slice(0, 8).join(', ')}${f.value.length > 8 ? ', ...' : ''}`;
    if (typeof f.value === 'number' && typeCode(f.type) === 2) return String(Number(f.value.toPrecision(7)));
    return String(f.value);
}

/** One field name across a selection: the selected objects that carry it (matched by name and type). */
export interface FieldGroup {
    name: string;
    type: number;
    /** (id, field) of every selected object with this field. */
    owners: { id: number; field: DocField }[];
    /** All selected objects have it. */
    complete: boolean;
    /** The owners hold different values. */
    mixed: boolean;
}

const sameValue = (a: DocField['value'], b: DocField['value']): boolean =>
    Array.isArray(a) && Array.isArray(b) ? a.length === b.length && a.every((v, i) => v === b[i]) : a === b;

/** Merge the fields of several objects for multi-edit display. */
export function groupFields(objs: readonly LevelObject[]): FieldGroup[] {
    const groups = new Map<string, FieldGroup>();
    for (const o of objs) {
        for (const f of o.fields ?? []) {
            const key = `${f.name}\u0000${f.type}`;
            let g = groups.get(key);
            if (!g) groups.set(key, (g = { name: f.name, type: f.type, owners: [], complete: false, mixed: false }));
            g.owners.push({ id: o.id, field: f });
        }
    }
    for (const g of groups.values()) {
        g.complete = g.owners.length === objs.length;
        g.mixed = g.owners.some((x) => !sameValue(x.field.value, g.owners[0]!.field.value));
    }
    return [...groups.values()].sort((a, b) => a.name.localeCompare(b.name));
}

/** Distinct values other objects in the document use for a field, most common first (suggestions for enums and ids). */
export function observedValues(doc: LevelDocument, name: string, type: number, limit = 24): (number | string)[] {
    const count = new Map<number | string, number>();
    for (const o of doc.objects()) {
        for (const f of o.fields ?? []) {
            if (f.name !== name || f.type !== type || Array.isArray(f.value)) continue;
            count.set(f.value, (count.get(f.value) ?? 0) + 1);
        }
    }
    return [...count.entries()]
        .sort((a, b) => b[1] - a[1] || String(a[0]).localeCompare(String(b[0])))
        .slice(0, limit)
        .map(([v]) => v);
}

// ---------------------------------------------------------------- class schemas (full field lists with defaults)

export type FieldCategory = 'Combat' | 'Movement' | 'Triggers' | 'Sound' | 'Animation' | 'Appearance' | 'General';
export const FIELD_CATEGORIES: FieldCategory[] = [
    'General',
    'Combat',
    'Movement',
    'Triggers',
    'Appearance',
    'Animation',
    'Sound',
];

const CAT_SOUND = /sound|music|noise|voice|ambient/i;
const CAT_COMBAT =
    /health|damage|attack|weapon|armou?r|hit |kill|death|projectile|aim|shoot|reload|ammo|magic|spell|enemy|aggress/i;
const CAT_MOVE =
    /speed|velocity|move|distance|turn|jump|pause|waypoint|path|radius|range|accel|gravity|friction|mass|height|rotat/i;
const CAT_TRIGGER =
    /message|trigger|event|state|switch|mode|conversation|dialog|quest|treasure|gold|item|lock|door|open|close|sequence|detect|target/i;
const CAT_LOOK = /model|texture|colou?r|alpha|scale|size|particle|effect|fx|light|glow|sky|fog|cloud|bright|visible/i;

/**
 * Display group of a field. The game data has no field groups, so this is a naming heuristic (type first, then keywords
 * in the editor-given field name) used only to keep long field lists readable.
 */
export function fieldCategory(name: string, type: number): FieldCategory {
    const base = typeCode(type);
    if (base === 5) return 'Sound';
    if (base === 8 || base === 9 || base === 0x0b || base === 0x0c) return 'Animation';
    if (base === 4 || base === 0x0e || base === 0x0f) return 'Appearance';
    if (CAT_SOUND.test(name)) return 'Sound';
    if (CAT_COMBAT.test(name)) return 'Combat';
    if (CAT_MOVE.test(name)) return 'Movement';
    if (CAT_TRIGGER.test(name)) return 'Triggers';
    if (CAT_LOOK.test(name)) return 'Appearance';
    return 'General';
}

/** One field of a class: its definition and the class default. */
export interface SchemaField {
    /** Index in the class (the key the LVL parameters use). */
    index: number;
    type: number;
    name: string;
    default: DocField['value'];
    category: FieldCategory;
    /** The class default is not known (no class schema): `default` is a placeholder and never matches. */
    unknownDefault?: boolean;
}

export interface ClassSchema {
    /** `0x<db_index<<16 | class_id>` like `LevelObject.resource`. */
    resource: string;
    classId: number;
    name: string;
    /** ODB stem that owns the class; reference fields are relative to its .DB dependency list. */
    db: string;
    fields: SchemaField[];
    /** `.DB` dependency index -> container stem of the owner (db 0 = the owner itself). */
    deps: ReadonlyMap<number, string>;
}

/** What a reference value points at. */
export interface RefTarget {
    /** Container stem (`RESOURES`, `SOUND`, ...) or null when the db index is unknown. */
    stem: string | null;
    id: number;
    db: number;
    /** Class name for class references that resolve. */
    name?: string;
    /** Catalog resource string (level db index) when the target class is in the level's catalog. */
    resource?: string;
}

/**
 * Class schemas of a loaded level: lazy `ClassSchema` per class resource, reference resolution through the owning ODB's
 * .DB dependency list, and the value sets enum fields take (the game stores no enum item names, so a dropdown offers
 * the values the shipped classes actually use).
 */
export class SchemaProvider {
    private readonly cache = new Map<string, ClassSchema | null>();
    private readonly stemToDb = new Map<string, number>();
    private enumCache: Map<string, number[]> | null = null;
    private readonly refCache = new Map<string, { stem: string; id: number; count: number; examples: string[] }[]>();

    constructor(
        private readonly header: LvlHeader,
        private readonly odbs: OdbIndex,
        private readonly odbDeps: Readonly<Record<string, Readonly<Record<number, string>>>> = {},
    ) {
        for (const d of header.dependencies) {
            const stem = odbStemForDependency(d.path.text);
            if (!this.stemToDb.has(stem)) this.stemToDb.set(stem, d.dbIndex);
        }
    }

    private classOf(resource: string): { cls: OdbClass; stem: string } | undefined {
        const { dbIndex, classId } = resourceParts(resource);
        const path = dependencyPath(this.header, dbIndex);
        if (path === undefined) return undefined;
        const stem = odbStemForDependency(path);
        const cls = this.odbs.get(stem)?.get(classId);
        return cls ? { cls, stem } : undefined;
    }

    schemaFor(resource: string): ClassSchema | undefined {
        if (this.cache.has(resource)) return this.cache.get(resource) ?? undefined;
        const hit = this.classOf(resource);
        let out: ClassSchema | null = null;
        if (hit?.cls.fields) {
            const cls = hit.cls;
            out = {
                resource,
                classId: cls.id,
                name: cls.name,
                db: hit.stem,
                deps: new Map(Object.entries(this.odbDeps[hit.stem] ?? {}).map(([k, v]) => [Number(k), v])),
                fields: cls.fields!.map((f, index) => ({
                    index,
                    type: f.type,
                    name: f.name,
                    default: classFieldDefault(cls, index) ?? 0,
                    category: fieldCategory(f.name, f.type),
                })),
            };
        }
        this.cache.set(resource, out);
        return out ?? undefined;
    }

    /** Resolve a `(db << 16) | id` value of a field owned by `owner`. */
    resolveRef(owner: ClassSchema, type: number, value: number): RefTarget {
        const { id, db } = refParts(value);
        const stem = db === 0 ? owner.db : (owner.deps.get(db) ?? null);
        const t: RefTarget = { stem, id, db };
        if (typeCode(type) === 3 && stem) {
            const c = this.odbs.get(stem)?.get(id);
            if (c) t.name = c.name;
            const lvlDb = this.stemToDb.get(stem);
            if (c && lvlDb !== undefined) t.resource = resourceString(lvlDb, id);
        }
        return t;
    }

    /** `(db << 16) | id` for a class of ODB `targetStem` as seen from `owner`, or null when the owner cannot name that ODB. */
    refValueFor(owner: ClassSchema, targetStem: string, id: number): number | null {
        if (targetStem === owner.db) return id & 0xffff;
        for (const [db, stem] of owner.deps) if (stem === targetStem) return ((db << 16) | (id & 0xffff)) >>> 0;
        return null;
    }

    /**
     * Resource references (model, sound, animation, ... fields) other classes use for a field name and type, resolved
     * to `(container stem, id)` with how many classes use each and a few class names; most used first.
     */
    refValues(name: string, type: number): { stem: string; id: number; count: number; examples: string[] }[] {
        const key = `${name}\u0000${type}`;
        let hit = this.refCache.get(key);
        if (hit) return hit;
        const acc = new Map<string, { stem: string; id: number; count: number; examples: string[] }>();
        for (const [odbStem, classes] of this.odbs) {
            const deps = this.odbDeps[odbStem] ?? {};
            for (const c of classes.values()) {
                c.fields?.forEach((f, i) => {
                    if (f.name !== name || f.type !== type) return;
                    const raw = c.data?.[i];
                    if (raw === undefined || raw === 0) return;
                    const { id, db } = refParts(raw);
                    const stem = db === 0 ? odbStem : deps[db];
                    if (!stem) return;
                    const k = `${stem}:${id}`;
                    let e = acc.get(k);
                    if (!e) acc.set(k, (e = { stem, id, count: 0, examples: [] }));
                    e.count++;
                    if (e.examples.length < 3) e.examples.push(c.name);
                });
            }
        }
        hit = [...acc.values()].sort((a, b) => b.count - a.count || a.stem.localeCompare(b.stem) || a.id - b.id);
        this.refCache.set(key, hit);
        return hit;
    }

    /** Values an enum field (name and type) takes across every class default decoded so far, ascending. */
    enumValues(name: string, type: number): number[] {
        if (!this.enumCache) {
            const m = new Map<string, Set<number>>();
            for (const classes of this.odbs.values()) {
                for (const c of classes.values()) {
                    c.fields?.forEach((f, i) => {
                        if (typeCode(f.type) !== 7 || isArrayType(f.type)) return;
                        const v = c.data?.[i];
                        if (v === undefined) return;
                        const key = `${f.name}\u0000${f.type}`;
                        let set = m.get(key);
                        if (!set) m.set(key, (set = new Set()));
                        set.add(v >>> 0);
                    });
                }
            }
            this.enumCache = new Map([...m].map(([k, v]) => [k, [...v].sort((a, b) => a - b)]));
        }
        return this.enumCache.get(`${name}\u0000${type}`) ?? [];
    }

    /** Enum fields that only ever hold 0 and 1 (in every class default and in `extra`) are shown as checkboxes. */
    isToggle(name: string, type: number, extra: Iterable<number> = []): boolean {
        if (typeCode(type) !== 7 || isArrayType(type)) return false;
        const vals = new Set([...this.enumValues(name, type), ...extra]);
        return vals.size > 0 && [...vals].every((v) => v === 0 || v === 1);
    }
}

// ---------------------------------------------------------------- reading and writing overrides

const nanSafe = (a: number, b: number): boolean => a === b || (Number.isNaN(a) && Number.isNaN(b));

/** Equal field values (floats compare as f32). */
export function sameFieldValue(type: number, a: DocField['value'], b: DocField['value']): boolean {
    if (Array.isArray(a) || Array.isArray(b)) {
        return Array.isArray(a) && Array.isArray(b) && a.length === b.length && a.every((v, i) => v === b[i]);
    }
    if (typeof a === 'number' && typeof b === 'number') {
        return typeCode(type) === 2 ? nanSafe(Math.fround(a), Math.fround(b)) : a === b;
    }
    return a === b;
}

export function findOverride(o: LevelObject, index: number): DocField | undefined {
    return o.fields?.find((f) => f.fieldIndex === index);
}

/** The value the object has for a schema field: its override, else the class default. */
export function effectiveValue(o: LevelObject, f: SchemaField): { value: DocField['value']; overridden: boolean } {
    const ov = findOverride(o, f.index);
    return ov ? { value: ov.value, overridden: true } : { value: f.default, overridden: false };
}

const cloneValue = (v: DocField['value']): DocField['value'] => (Array.isArray(v) ? [...v] : v);

/**
 * Set a field on an object (use inside `ctx.edit` / `editObjects`). A value equal to the class default drops the
 * override; anything else adds or updates it. Floats are rounded to f32 like the file stores them.
 */
export function setObjectField(o: LevelObject, f: SchemaField, value: DocField['value']): void {
    const v = typeof value === 'number' && typeCode(f.type) === 2 ? Math.fround(value) : cloneValue(value);
    if (!f.unknownDefault && sameFieldValue(f.type, v, f.default)) {
        resetObjectField(o, f);
        return;
    }
    const ov = findOverride(o, f.index);
    if (ov) ov.value = v;
    else (o.fields ??= []).push({ slot: -1, fieldIndex: f.index, type: f.type, name: f.name, value: v });
}

/** Remove the override so the field falls back to the class default. */
export function resetObjectField(o: LevelObject, f: Pick<SchemaField, 'index'>): void {
    if (!o.fields) return;
    o.fields = o.fields.filter((x) => x.fieldIndex !== f.index);
}

/** Array element values for display and editing (f32 elements decoded, ints signed). */
export function arrayElements(type: number, words: readonly number[]): number[] {
    return words.map((w) => decodeElement(type, w));
}
