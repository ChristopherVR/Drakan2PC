import { cloneObject, type LevelDocument, type LevelObject } from './document.ts';
import { resourceParts, type AssetClass } from './catalog.ts';

export const CLIPBOARD_FORMAT = 'drakan-editor-clipboard';

export interface ClipboardPayload {
    format: typeof CLIPBOARD_FORMAT;
    version: 1;
    /** Level the objects were copied from (same level = origin ids and links stay valid). */
    level: string;
    /** Centroid of the copied positions, game units. */
    center: [number, number, number];
    objects: LevelObject[];
}

/** Copy objects (ids kept for reference only). Null when nothing is selected. */
export function copyObjects(doc: LevelDocument, ids: readonly number[]): ClipboardPayload | null {
    const objs = ids.map((i) => doc.get(i)).filter((o): o is LevelObject => !!o);
    if (!objs.length) return null;
    const c: [number, number, number] = [0, 0, 0];
    for (const o of objs) for (let a = 0; a < 3; a++) c[a] = c[a]! + o.transform.pos[a]! / objs.length;
    return { format: CLIPBOARD_FORMAT, version: 1, level: doc.level, center: c, objects: objs.map(cloneObject) };
}

export function parseClipboard(text: string): ClipboardPayload | null {
    try {
        const j = JSON.parse(text) as Partial<ClipboardPayload>;
        if (j.format !== CLIPBOARD_FORMAT || j.version !== 1 || !Array.isArray(j.objects) || !j.center) return null;
        return j as ClipboardPayload;
    } catch {
        return null;
    }
}

export interface PasteResult {
    objects: LevelObject[];
    /** Classes that do not exist in the target level (cross-level paste): skipped. */
    skipped: string[];
}

/**
 * Objects ready for `AddCommand` (id 0), with the group's centroid moved to `at` (or offset by `offset` when `at` is
 * null). Within the same level, `origin` and links stay (the copy exports as a duplicate of the source); in another
 * level each class is looked up in `catalog` (by ODB stem + class id, then by name), `origin`/`template`/links are
 * dropped and the object exports as a new instance of that class.
 */
export function pasteObjects(
    clip: ClipboardPayload,
    target: LevelDocument,
    catalog: readonly AssetClass[],
    at: readonly [number, number, number] | null,
    offset = 500,
): PasteResult {
    const same = clip.level === target.level;
    const out: LevelObject[] = [];
    const skipped: string[] = [];
    for (const src of clip.objects) {
        const o = cloneObject(src);
        o.id = 0;
        if (at) for (let a = 0; a < 3; a++) o.transform.pos[a] = src.transform.pos[a]! - clip.center[a]! + at[a]!;
        else {
            o.transform.pos[0] += offset;
            o.transform.pos[2] += offset;
        }
        if (!same) {
            const { classId } = resourceParts(o.resource);
            const hit =
                catalog.find((c) => c.db === o.props.db && c.classId === classId) ??
                catalog.find((c) => c.name === o.class);
            if (!hit) {
                skipped.push(o.class || o.resource);
                continue;
            }
            o.resource = hit.resource;
            o.class = hit.name;
            o.kind = hit.kind;
            o.props.db = hit.db;
            delete o.origin;
            delete o.template;
            o.links = [];
            // Template: the target level's own instance of the class (fields come from it on export).
            const tpl = [...target.objects()].find((t) => t.resource === hit.resource && t.origin !== undefined);
            if (tpl) {
                o.template = tpl.origin;
                if (tpl.fields) o.fields = tpl.fields.map((f) => ({ ...f }));
                else delete o.fields;
            } else delete o.fields;
        }
        out.push(o);
    }
    return { objects: out, skipped };
}
