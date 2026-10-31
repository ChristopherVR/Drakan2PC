import type { DocField, LevelDocument, LevelObject } from './document.ts';

/**
 * Field schema for the inspector. The type word comes from the ODB 0x0103 / LVL `field_def` (level-format.md section 4):
 * low byte 1..0x0f, 0x1000 = array. Per-instance values are the LVL parameters (the fields that differ from the class
 * default); the class's full field list and default values are not decoded, so only overrides can be shown.
 */
export type Widget = 'int' | 'float' | 'bool' | 'enum' | 'colour' | 'object' | 'class' | 'id' | 'string' | 'array';

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
 * Widget for a type word. Class fields with a high byte of 0x20..0x50 (seen on the PS2 pool: 0x2003, 0x3003, 0x4003)
 * are treated as object references (value = id of another placed object); that reading follows OpenDrakan and is not
 * verified against the game.
 */
export function widgetFor(type: number): Widget {
    if (isArrayType(type)) return 'array';
    const base = typeCode(type);
    const hi = (type >> 8) & 0xef;
    if (base === 3 && hi >= 0x20) return 'object';
    switch (base) {
        case 1:
            return 'int';
        case 2:
            return 'float';
        case 3:
            return 'class';
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

/** Editable by the LVL writer (a plain dword): everything except strings and arrays. */
export const isEditableType = (type: number): boolean => !isArrayType(type) && typeCode(type) !== 0x0a;

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
