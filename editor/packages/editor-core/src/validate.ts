import type { LevelDocument } from './document.ts';

export type IssueSeverity = 'error' | 'warning';
export type IssueCode =
    | 'dangling-link'
    | 'self-link'
    | 'duplicate-id'
    | 'outside-bounds'
    | 'bad-transform'
    | 'unresolved-class'
    | 'bad-id'
    | 'dangling-ref'
    | 'default-body'
    | 'trigger-no-target'
    | 'unreachable-cutscene'
    | 'exit-no-destination'
    | 'exit-no-arrival'
    | 'message-ignored';

export interface ValidationIssue {
    severity: IssueSeverity;
    code: IssueCode;
    /** Object ids to select when the issue is clicked (first one is focused). */
    ids: number[];
    message: string;
}

export interface ValidateOptions {
    /** Ids as stored in the source object list (a document map cannot hold duplicates; the list can). */
    rawIds?: readonly number[];
    /** Warn about objects outside this extent (default: `doc.bounds`). */
    bounds?: { minX: number; maxX: number; minZ: number; maxZ: number } | null;
    /**
     * The document edits a disc level: a new object whose class has no instance in the level (no template) is built
     * from generic defaults, which are unverified in game, so it is flagged.
     */
    sourceLevel?: boolean;
}

/**
 * Check a document for problems that would make the exported level wrong or surprising: links to missing objects,
 * duplicate ids, objects outside the level extent (when known), non-finite transforms and unresolved classes.
 */
export function validateDocument(doc: LevelDocument, opts: ValidateOptions = {}): ValidationIssue[] {
    const out: ValidationIssue[] = [];
    if (opts.rawIds) {
        const seen = new Set<number>();
        const dup = new Set<number>();
        for (const id of opts.rawIds) {
            if (seen.has(id)) dup.add(id);
            seen.add(id);
        }
        for (const id of dup) {
            out.push({
                severity: 'error',
                code: 'duplicate-id',
                ids: [id],
                message: `Object id ${id} appears more than once`,
            });
        }
    }
    const bounds = opts.bounds === undefined ? doc.bounds : opts.bounds;
    for (const o of doc.objects()) {
        if (!Number.isInteger(o.id) || o.id <= 0 || o.id > 0xffffffff) {
            out.push({
                severity: 'error',
                code: 'bad-id',
                ids: [o.id],
                message: `Object id ${o.id} is not a positive u32`,
            });
        }
        if (!o.transform.pos.every(Number.isFinite) || !o.transform.rot.every(Number.isFinite)) {
            out.push({
                severity: 'error',
                code: 'bad-transform',
                ids: [o.id],
                message: `${label(o)} has a non-finite position or rotation`,
            });
        } else if (
            bounds &&
            (o.transform.pos[0] < bounds.minX ||
                o.transform.pos[0] > bounds.maxX ||
                o.transform.pos[2] < bounds.minZ ||
                o.transform.pos[2] > bounds.maxZ)
        ) {
            out.push({
                severity: 'warning',
                code: 'outside-bounds',
                ids: [o.id],
                message: `${label(o)} is outside the level extent`,
            });
        }
        if (/^class \d+$/.test(o.class)) {
            out.push({
                severity: 'warning',
                code: 'unresolved-class',
                ids: [o.id],
                message: `${label(o)}: class name not found in the ODBs`,
            });
        }
        if (opts.sourceLevel && o.origin === undefined && o.template === undefined) {
            out.push({
                severity: 'warning',
                code: 'default-body',
                ids: [o.id],
                message: `${label(o)}: its class has no instance in this level, so it uses generic defaults (unverified in game)`,
            });
        }
        for (const to of o.links ?? []) {
            if (to === o.id) {
                out.push({
                    severity: 'warning',
                    code: 'self-link',
                    ids: [o.id],
                    message: `${label(o)} links to itself`,
                });
            } else if (!doc.has(to)) {
                out.push({
                    severity: 'error',
                    code: 'dangling-link',
                    ids: [o.id],
                    message: `${label(o)} links to missing object ${to}`,
                });
            }
        }
    }
    return out.sort(
        (a, b) => Number(a.severity === 'warning') - Number(b.severity === 'warning') || a.ids[0]! - b.ids[0]!,
    );
}

function label(o: { id: number; class: string }): string {
    return `${o.class || 'Object'} #${o.id}`;
}
