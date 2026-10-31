// Cut scene players and conversation handlers placed in the open level, as the story editors want them (fields by name).

import { effectiveValue, type LevelDocument, type SchemaProvider } from '@drakan/editor-core';
import type { PlacedStoryObject } from './content.ts';

const WANTED = ['Sequence List', 'Beginning Dialog Exchange'];

export function placedStoryObjects(doc: LevelDocument, schemas: SchemaProvider | null): PlacedStoryObject[] {
    if (!schemas || doc.size === 0) return [];
    const out: PlacedStoryObject[] = [];
    for (const o of doc.objects()) {
        const schema = schemas.schemaFor(o.resource);
        if (!schema || !schema.fields.some((f) => WANTED.includes(f.name))) continue;
        const fields = new Map<string, number | number[]>();
        for (const f of schema.fields) {
            const v = effectiveValue(o, f).value;
            if (typeof v === 'number') fields.set(f.name, v);
            else if (Array.isArray(v)) fields.set(f.name, [...v]);
        }
        out.push({ id: o.id, name: o.class, fields });
    }
    return out;
}
