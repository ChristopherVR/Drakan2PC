import {
    addObject,
    deleteObject,
    objectParamSpecs,
    paramValue,
    setObjectLinks,
    setObjectParams,
    type ParamSpec,
    duplicateObject,
    findObject,
    FLAG_SCALED,
    linkedFrom,
    moveObject,
    rotateObject,
    scaleObject,
    resolveClass,
    type Lvl,
    type LvlObject,
    type LvlObjectList,
    type OdbIndex,
    type Vec3,
} from '@drakan/formats';
import { kindOfClassName, resourceParts } from './catalog.ts';
import { sameFieldValue } from './schema.ts';
import { LevelDocument, type DocField, type LevelObject } from './document.ts';
import type { EntityKind } from './world.ts';

/**
 * LVL object list <-> LevelDocument. Document objects keep the real LVL ids. The document's yaw follows the world
 * dump convention (`yaw = rot_y - 90`, docs/reference/level-format.md section 3) so a level loaded from the disc and a
 * world dump of the same level line up; the other two angles are stored as they are. Document rotations are radians,
 * the LVL stores whole signed degrees.
 *
 * Export is diff based: the current document is compared with the list it was loaded from, so undo/redo and any command
 * sequence give the same result and unchanged objects keep every byte.
 */

const D2R = Math.PI / 180;
const R2D = 180 / Math.PI;
/** Positions closer than this (game units) count as unmoved. */
export const POS_EPSILON = 1e-3;

export function lvlRotToDoc(rot: readonly number[]): [number, number, number] {
    return [rot[0]! * D2R, (rot[1]! - 90) * D2R, rot[2]! * D2R];
}

/** Whole degrees for the LVL from a document rotation (inverse of `lvlRotToDoc`, rounded). */
export function docRotToLvl(rot: readonly number[]): [number, number, number] {
    return [Math.round(rot[0]! * R2D), Math.round(rot[1]! * R2D) + 90, Math.round(rot[2]! * R2D)];
}

const mod360 = (d: number): number => ((d % 360) + 360) % 360;

function kindOf(name: string): EntityKind {
    return kindOfClassName(name);
}

/** Cells (2048 units) per max_w / max_h step is unverified; only used for the optional extent warning. */
export const LAYER_CELL = 2048;

const fieldsOf = (o: LvlObject): DocField[] =>
    o.params.map((p, slot) => ({
        slot,
        fieldIndex: p.fieldIndex,
        type: p.type,
        name: p.name,
        value: paramValue(o, slot),
    }));

export interface LvlDocumentInfo {
    doc: LevelDocument;
    /** Objects whose class name could not be resolved through the ODBs. */
    unresolved: number;
}

/** Build a document from a read LVL, resolving class names through the ODBs (`odbs` may be empty). */
export function documentFromLvl(lvl: Lvl, odbs: OdbIndex, levelName: string): LvlDocumentInfo {
    const doc = new LevelDocument();
    doc.level = levelName;
    if (lvl.header.maxWidth > 0 && lvl.header.maxHeight > 0) {
        doc.bounds = {
            minX: 0,
            maxX: lvl.header.maxWidth * LAYER_CELL,
            minZ: 0,
            maxZ: lvl.header.maxHeight * LAYER_CELL,
        };
    }
    let unresolved = 0;
    for (const o of lvl.list.objects) {
        const r = resolveClass(lvl.header, odbs, o.classId, o.dbIndex);
        const name = r.class?.name;
        if (!name) unresolved++;
        const obj: LevelObject = {
            id: o.id,
            class: name ?? `class ${o.classId}`,
            type: -1,
            kind: kindOf(name ?? ''),
            resource: `0x${(((o.dbIndex << 16) | o.classId) >>> 0).toString(16)}`,
            model: '',
            transform: { pos: [...o.pos], rot: lvlRotToDoc(o.rot) },
            props: {},
            origin: o.id,
            links: [...o.links],
            fields: fieldsOf(o),
        };
        if (r.db) {
            obj.props.db = r.db;
            obj.db = r.db;
        }
        if (o.scale) obj.scale = [...o.scale];
        doc.insert(obj);
    }
    return { doc, unresolved };
}

export interface DocumentDiff {
    moved: number[];
    rotated: number[];
    scaled: number[];
    /** Ids in the document but not in the list: duplicates (with their source id). */
    added: { id: number; origin: number | undefined }[];
    /** Ids in the list but no longer in the document. */
    deleted: number[];
    /** Existing objects whose link list changed. */
    linked: number[];
    /** Existing objects with an edited or removed parameter. */
    params: number[];
}

const sameLinks = (a: readonly number[], b: readonly number[]): boolean =>
    a.length === b.length && a.every((v, i) => v === b[i]);

/** The parameters a document object wants written: its overrides, by class field index. */
function desiredParams(o: LevelObject): ParamSpec[] {
    return (o.fields ?? []).map((f) => ({ fieldIndex: f.fieldIndex, type: f.type, name: f.name, value: f.value }));
}

/**
 * True when the document object's overrides differ from the parameters of `b` (compared per class field index, so the
 * order or the data-area layout of the stored parameters does not matter). Objects without `fields` are not touched.
 */
function paramsChanged(b: LvlObject, o: LevelObject): boolean {
    if (!o.fields) return false;
    const want = desiredParams(o);
    const have = objectParamSpecs(b);
    if (want.length !== have.length) return true;
    const byIndex = new Map(have.map((p) => [p.fieldIndex, p]));
    return want.some((w) => {
        const h = byIndex.get(w.fieldIndex);
        return !h || h.type !== w.type || !sameFieldValue(w.type, w.value, h.value);
    });
}

function rotChanged(base: readonly number[], doc: readonly number[]): boolean {
    const want = docRotToLvl(doc);
    return want.some((w, i) => mod360(w) !== mod360(base[i]!));
}

/** What the document changed relative to `base`. */
export function diffDocument(base: LvlObjectList, doc: LevelDocument): DocumentDiff {
    const d: DocumentDiff = { moved: [], rotated: [], scaled: [], added: [], deleted: [], linked: [], params: [] };
    const seen = new Set<number>();
    const byId = new Map(base.objects.map((b) => [b.id, b]));
    for (const o of doc.objects()) {
        seen.add(o.id);
        const b = byId.get(o.id);
        if (!b) {
            d.added.push({ id: o.id, origin: o.origin });
            continue;
        }
        if (o.transform.pos.some((v, i) => Math.abs(Math.fround(v) - b.pos[i]!) > POS_EPSILON)) d.moved.push(o.id);
        if (rotChanged(b.rot, o.transform.rot)) d.rotated.push(o.id);
        const bs = b.scale;
        if (o.scale && bs && o.scale.some((v, i) => Math.fround(v) !== bs[i]!)) d.scaled.push(o.id);
        if (o.links && !sameLinks(o.links, b.links)) d.linked.push(o.id);
        if (paramsChanged(b, o)) d.params.push(o.id);
    }
    for (const b of base.objects) if (!seen.has(b.id)) d.deleted.push(b.id);
    d.added.sort((a, b) => a.id - b.id);
    return d;
}

export interface ApplyReport {
    diff: DocumentDiff;
    /** Links dropped because their target was deleted (policy `unlink`). */
    unlinked: { from: number; to: number }[];
    /** Problems that make the export unsafe or surprising; empty when clean. */
    warnings: string[];
}

export interface ApplyOptions {
    /** What to do with links that point at a deleted object (default `unlink`). */
    onReferenced?: 'unlink' | 'ignore' | 'throw';
}

/**
 * Apply the document's changes to a copy of `base` through the `lvl.ts` edit functions. Order: duplicates (from the
 * pristine sources), deletes (with link handling), then transforms. `base` is not modified.
 */
export function applyDocumentToLvl(
    base: LvlObjectList,
    doc: LevelDocument,
    opts: ApplyOptions = {},
): { list: LvlObjectList; report: ApplyReport } {
    const diff = diffDocument(base, doc);
    const list: LvlObjectList = { objects: structuredClone(base.objects), warnings: [...base.warnings] };
    const report: ApplyReport = { diff, unlinked: [], warnings: [] };
    const policy = opts.onReferenced ?? 'unlink';

    // New objects. A copy of an existing object duplicates it (every byte, strings included); a new instance of a class
    // is templated from an existing one (or the class defaults) with `addObject`.
    const idMap = new Map<number, number>();
    const added: { want: number; obj: LvlObject; doc: LevelObject }[] = [];
    for (const a of diff.added) {
        const o = doc.get(a.id)!;
        const src = a.origin !== undefined ? findObject(list, a.origin) : undefined;
        let copy: LvlObject;
        if (src) {
            copy = duplicateObject(list, src.id);
        } else if (a.origin !== undefined) {
            report.warnings.push(`object ${a.id}: no LVL source object (origin ${a.origin}); not exported`);
            continue;
        } else {
            const { classId, dbIndex } = resourceParts(o.resource);
            if (!o.resource || (classId === 0 && dbIndex === 0)) {
                report.warnings.push(`object ${a.id}: no class to instantiate; not exported`);
                continue;
            }
            const res = addObject(list, {
                classId,
                dbIndex,
                pos: o.transform.pos,
                ...(o.template !== undefined && findObject(list, o.template) ? { templateId: o.template } : {}),
            });
            copy = res.object;
            if (res.from === 'default') {
                report.warnings.push(
                    `object ${a.id} (${o.class}): no instance of the class in this level; written with a default body (unverified in game)`,
                );
            }
        }
        const maxOther = list.objects.reduce((m, x) => (x === copy ? m : Math.max(m, x.id)), 0);
        if (a.id > maxOther) copy.id = a.id;
        copy.pos = o.transform.pos.map(Math.fround) as Vec3;
        copy.rot = docRotToLvl(o.transform.rot);
        if (o.scale && copy.scale) copy.scale = o.scale.map(Math.fround) as Vec3;
        if (copy.id !== a.id) report.warnings.push(`object ${a.id} was exported as id ${copy.id}`);
        idMap.set(a.id, copy.id);
        added.push({ want: a.id, obj: copy, doc: o });
    }
    // Links and parameters of the new objects (links to other new objects are remapped to the exported ids).
    for (const { obj, doc: o } of added) {
        if (o.links) obj.links = [...new Set(o.links.map((l) => idMap.get(l) ?? l))];
        if (o.fields && paramsChanged(obj, o)) {
            try {
                setObjectParams(list, obj.id, desiredParams(o));
            } catch (e) {
                report.warnings.push(`object ${o.id}: ${(e as Error).message}`);
            }
        }
    }
    for (const id of diff.deleted) {
        for (const from of linkedFrom(list, id)) if (from !== id) report.unlinked.push({ from, to: id });
        deleteObject(list, id, policy);
    }
    const alive = (id: number): boolean => !!findObject(list, id);
    for (const id of diff.moved) if (alive(id)) moveObject(list, id, doc.get(id)!.transform.pos);
    for (const id of diff.rotated) if (alive(id)) rotateObject(list, id, docRotToLvl(doc.get(id)!.transform.rot));
    for (const id of diff.scaled) {
        const o = doc.get(id);
        const t = findObject(list, id);
        if (o?.scale && t && t.flags & FLAG_SCALED) scaleObject(list, id, o.scale);
    }
    for (const id of diff.linked) {
        const o = doc.get(id)!;
        if (alive(id))
            setObjectLinks(
                list,
                id,
                o.links!.map((l) => idMap.get(l) ?? l).filter((l) => alive(l) || !diff.deleted.includes(l)),
            );
    }
    for (const id of diff.params) {
        const o = doc.get(id)!;
        const t = findObject(list, id);
        if (!t) continue;
        try {
            setObjectParams(list, id, desiredParams(o));
        } catch (e) {
            report.warnings.push(`object ${id}: ${(e as Error).message}`);
        }
    }
    return { list, report };
}

// ---------------------------------------------------------------- comparison with a world dump

export interface WorldComparison {
    matched: number;
    /** LVL objects with no live actor of the same resource within `tolerance`. */
    lvlOnly: number;
    /** Live world entities not matched to any LVL object (runtime children, spawns, moved actors). */
    worldOnly: number;
    /** LVL id -> world document object id. */
    pairs: Map<number, number>;
}

/**
 * Pair LVL objects with world-dump entities by resource id and position: greedy nearest match inside `tolerance`
 * game units (settled or moved actors differ a little from the stored placement).
 */
export function compareWithWorld(lvl: LevelDocument, world: LevelDocument, tolerance = 300): WorldComparison {
    const byRes = new Map<string, LevelObject[]>();
    for (const w of world.objects()) {
        const l = byRes.get(w.resource);
        if (l) l.push(w);
        else byRes.set(w.resource, [w]);
    }
    const used = new Set<number>();
    const pairs = new Map<number, number>();
    for (const o of lvl.objects()) {
        let best: LevelObject | undefined;
        let bd = tolerance;
        for (const w of byRes.get(o.resource) ?? []) {
            if (used.has(w.id)) continue;
            const d = Math.hypot(
                w.transform.pos[0] - o.transform.pos[0],
                w.transform.pos[1] - o.transform.pos[1],
                w.transform.pos[2] - o.transform.pos[2],
            );
            if (d <= bd) {
                bd = d;
                best = w;
            }
        }
        if (best) {
            used.add(best.id);
            pairs.set(o.id, best.id);
        }
    }
    return { matched: pairs.size, lvlOnly: lvl.size - pairs.size, worldOnly: world.size - pairs.size, pairs };
}
