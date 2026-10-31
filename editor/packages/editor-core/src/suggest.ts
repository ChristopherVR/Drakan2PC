import { MESSAGE_NAMES, COMMON_MESSAGES } from '@drakan/formats';
import type { AssetClass } from './catalog.ts';
import type { LevelDocument } from './document.ts';
import { isArrayType, typeCode, type ClassSchema, type SchemaProvider } from './schema.ts';

/** One value a list element can take, offered in the Details panel's dropdown. */
export interface Suggestion {
    value: number;
    label: string;
    /** Section it belongs to (shown dimmed beside the label). */
    group: string;
}

export interface SuggestInput {
    /** Field name and the array's type word (element type is the low byte). */
    name: string;
    type: number;
    doc: LevelDocument;
    catalog: readonly AssetClass[];
    provider: SchemaProvider | null;
    /** Schema of the first selected object (reference values are relative to its ODB). */
    owner: ClassSchema | null;
}

const MESSAGE = /messag|msg/i;
const OBJECT = /object|target|link|entit|npc|actor|spawn|waypoint|path|trigger|camera|marker|door|switch/i;
const CLASS = /item|class|inventory|drop|weapon|loot|pickup/i;
const MAX = 80;

/**
 * Known values for the elements of a numeric list field. Sources, in this order: message ordinals (message fields),
 * objects of the level (by name, value = object id), item and object classes (by name, value = class reference as the
 * owner stores it) and the elements the same field holds in class defaults and in other objects of the level (most used
 * first). Every value appears once; the first source that names it wins. Float lists get nothing.
 */
export function suggestValues(inp: SuggestInput): Suggestion[] {
    const el = typeCode(inp.type);
    if (!isArrayType(inp.type) || el === 2) return [];
    const out = new Map<number, Suggestion>();
    const add = (value: number, label: string, group: string): void => {
        if (out.size < MAX && !out.has(value)) out.set(value, { value, label, group });
    };
    if (MESSAGE.test(inp.name) && !/\?/.test(inp.name)) {
        const order = [...COMMON_MESSAGES, ...MESSAGE_NAMES.map((_, i) => i)];
        for (const i of order) if (MESSAGE_NAMES[i]) add(i, `${i}  ${MESSAGE_NAMES[i]}`, 'Messages');
    }
    if (OBJECT.test(inp.name)) {
        const objs = [...inp.doc.objects()].sort((a, b) => a.class.localeCompare(b.class) || a.id - b.id);
        for (const o of objs) add(o.id, `${o.class} #${o.id}`, 'Objects in this level');
    }
    if (CLASS.test(inp.name) && inp.provider && inp.owner) {
        for (const c of [...inp.catalog].sort((a, b) => b.instances - a.instances || a.name.localeCompare(b.name))) {
            const v = inp.provider.refValueFor(inp.owner, c.db, c.classId);
            if (v !== null) add(v, c.name, 'Classes');
        }
    }
    // Used elsewhere: other objects of the level first (they are what the player sees), then class defaults.
    const seen = new Map<number, number>();
    for (const o of inp.doc.objects()) {
        const fields = inp.provider?.schemaFor(o.resource)?.fields;
        for (const f of o.fields ?? [])
            if (f.type === inp.type && Array.isArray(f.value) && fields?.[f.fieldIndex]?.name === inp.name)
                for (const v of f.value) seen.set(v, (seen.get(v) ?? 0) + 1);
    }
    for (const [v, n] of [...seen].sort((a, b) => b[1] - a[1] || a[0] - b[0]))
        add(v, `${v}  used ${n}x in this level`, 'Used in this level');
    for (const e of inp.provider?.arrayValues(inp.name, inp.type) ?? [])
        add(e.value, `${e.value}  ${e.examples.join(', ')}`, 'Used by class defaults');
    return [...out.values()];
}
