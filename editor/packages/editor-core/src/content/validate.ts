import schemaJson from '../../schema/drakan-content.schema.json' with { type: 'json' };

/**
 * A small JSON Schema validator for exactly the keywords `schema/drakan-content.schema.json` uses (type, enum, const,
 * required, properties, additionalProperties, items, minItems, maxItems, minimum, maximum, minLength, pattern, oneOf,
 * anyOf, local `$ref`). It lets the editor check a package before it writes it and lets the unit tests run without a
 * validator dependency; any full draft-07 validator (Ajv, the UE Python `jsonschema`) accepts the same file.
 */

export const CONTENT_SCHEMA: Readonly<Record<string, unknown>> = schemaJson as Record<string, unknown>;

type Schema = Record<string, unknown>;

export interface SchemaError {
    path: string;
    message: string;
}

const typeOf = (v: unknown): string =>
    v === null ? 'null' : Array.isArray(v) ? 'array' : Number.isInteger(v) ? 'integer' : typeof v;

function typeMatches(t: string, v: unknown): boolean {
    const actual = typeOf(v);
    return t === actual || (t === 'number' && actual === 'integer');
}

const deepEqual = (a: unknown, b: unknown): boolean => JSON.stringify(a) === JSON.stringify(b);

function resolve(root: Schema, ref: string): Schema {
    if (!ref.startsWith('#/')) throw new Error(`unsupported $ref ${ref}`);
    let cur: unknown = root;
    for (const part of ref.slice(2).split('/')) cur = (cur as Record<string, unknown>)[part];
    if (!cur || typeof cur !== 'object') throw new Error(`unresolved $ref ${ref}`);
    return cur as Schema;
}

const regexCache = new Map<string, RegExp>();
function regex(p: string): RegExp {
    let r = regexCache.get(p);
    if (!r) regexCache.set(p, (r = new RegExp(p)));
    return r;
}

function check(root: Schema, s: Schema, v: unknown, path: string, out: SchemaError[], limit: number): void {
    if (out.length >= limit) return;
    const fail = (message: string): void => {
        if (out.length < limit) out.push({ path: path || '/', message });
    };
    if (typeof s.$ref === 'string') {
        check(root, resolve(root, s.$ref), v, path, out, limit);
        return;
    }
    if (s.type !== undefined) {
        const types = Array.isArray(s.type) ? (s.type as string[]) : [s.type as string];
        if (!types.some((t) => typeMatches(t, v))) {
            fail(`expected ${types.join(' or ')}, got ${typeOf(v)}`);
            return;
        }
    }
    if ('const' in s && !deepEqual(s.const, v)) fail(`must equal ${JSON.stringify(s.const)}`);
    if (Array.isArray(s.enum) && !s.enum.some((e) => deepEqual(e, v)))
        fail(`must be one of ${(s.enum as unknown[]).map((e) => JSON.stringify(e)).join(', ')}`);
    if (typeof v === 'number') {
        if (typeof s.minimum === 'number' && v < s.minimum) fail(`must be >= ${s.minimum}`);
        if (typeof s.maximum === 'number' && v > s.maximum) fail(`must be <= ${s.maximum}`);
    }
    if (typeof v === 'string') {
        if (typeof s.minLength === 'number' && v.length < s.minLength) fail(`must be at least ${s.minLength} long`);
        if (typeof s.pattern === 'string' && !regex(s.pattern).test(v)) fail(`must match ${s.pattern}`);
    }
    if (Array.isArray(v)) {
        if (typeof s.minItems === 'number' && v.length < s.minItems) fail(`needs at least ${s.minItems} items`);
        if (typeof s.maxItems === 'number' && v.length > s.maxItems) fail(`allows at most ${s.maxItems} items`);
        if (s.items && typeof s.items === 'object')
            for (let i = 0; i < v.length; i++) check(root, s.items as Schema, v[i], `${path}/${i}`, out, limit);
    }
    if (v && typeof v === 'object' && !Array.isArray(v)) {
        const o = v as Record<string, unknown>;
        for (const k of (s.required as string[] | undefined) ?? []) if (!(k in o)) fail(`missing required "${k}"`);
        const props = (s.properties as Record<string, Schema> | undefined) ?? {};
        for (const [k, sub] of Object.entries(props)) if (k in o) check(root, sub, o[k], `${path}/${k}`, out, limit);
        const extra = s.additionalProperties;
        for (const k of Object.keys(o)) {
            if (k in props) continue;
            if (extra === false) fail(`unexpected property "${k}"`);
            else if (extra && typeof extra === 'object') check(root, extra as Schema, o[k], `${path}/${k}`, out, limit);
        }
    }
    for (const key of ['oneOf', 'anyOf'] as const) {
        const alts = s[key] as Schema[] | undefined;
        if (!alts) continue;
        let matches = 0;
        let best: SchemaError[] | null = null;
        for (const alt of alts) {
            const sub: SchemaError[] = [];
            check(root, alt, v, path, sub, 8);
            if (sub.length === 0) matches++;
            else if (!best || sub.length < best.length) best = sub;
        }
        if (matches === 0) {
            // report the closest alternative's errors, which names the real problem
            if (best && best.length) {
                for (const e of best) if (out.length < limit) out.push(e);
            } else fail(`matches none of the ${alts.length} alternatives`);
        } else if (matches > 1 && key === 'oneOf') fail(`matches ${matches} alternatives, expected exactly one`);
    }
}

/** Validate a parsed DCF document (scene or manifest). Returns the first `limit` errors; empty = valid. */
export function validateContent(doc: unknown, limit = 50): SchemaError[] {
    const out: SchemaError[] = [];
    check(CONTENT_SCHEMA as Schema, CONTENT_SCHEMA as Schema, doc, '', out, limit);
    return out;
}
