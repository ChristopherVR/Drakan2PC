// Stand-in models of model-less classes (no three.js, no disc access): a break-away floor draws the pieces its
// "Model List" names, a Morph Plug the first of its "Morphing Models". The class defaults give the usual pieces; an object
// can name its own in the same field, and is then drawn under a resource key of its own.

import { resourceParts } from '@drakan/editor-core';
import { FIELD_MODEL, decodeFieldValue, fieldBase, type ClassFieldDef } from '@drakan/formats';

/** A field of a model-less class whose models stand in for the class model in the viewport, and how they combine. */
const FIELD_STAND_INS: readonly { name: RegExp; use: 'all' | 'first' }[] = [
    // Break-away floors: every piece is modelled in place, together they are the intact floor.
    { name: /^model list$/i, use: 'all' },
    // Morph Plugs: the frames of one shape; the first is the shape at rest.
    { name: /^morphing models?$/i, use: 'first' },
];

/** Most pieces merged into one stand-in model (break-away floors have about 20). */
export const MAX_PIECES = 64;

/** The rule a field falls under, or undefined for any other field. */
function ruleOf(f: { type: number; name: string }): (typeof FIELD_STAND_INS)[number] | undefined {
    return fieldBase(f.type) === FIELD_MODEL ? FIELD_STAND_INS.find((r) => r.name.test(f.name.trim())) : undefined;
}

function pick(use: 'all' | 'first', values: readonly unknown[]): number[] {
    const refs = values.filter((r): r is number => typeof r === 'number' && (r & 0xffff) !== 0).map((r) => r >>> 0);
    return use === 'first' ? refs.slice(0, 1) : refs.slice(0, MAX_PIECES);
}

/**
 * Model refs (`(db << 16) | id`) that stand in for a model-less class: the pieces of a break-away floor or the first
 * frame of a Morph Plug, from the class defaults. Empty for every other class.
 */
export function standInModelRefs(fields: readonly ClassFieldDef[], data: readonly number[]): number[] {
    for (const rule of FIELD_STAND_INS) {
        const i = fields.findIndex((f) => fieldBase(f.type) === FIELD_MODEL && rule.name.test(f.name.trim()));
        if (i < 0 || data[i] === undefined) continue;
        const v = decodeFieldValue(fields[i]!.type, data[i]!, data);
        const refs = pick(rule.use, Array.isArray(v) ? v : [v]);
        if (refs.length) return refs;
    }
    return [];
}

/** An object parameter as the document keeps it (decoded: an array field holds its elements). */
export interface StandInField {
    type: number;
    name: string;
    value: number | string | number[];
}

/**
 * The stand-in models an object names itself (its own "Model List" or "Morphing Models" parameter), or null when it
 * uses the class default. An override that names no model is not used.
 */
export function objectStandInRefs(fields: readonly StandInField[] | undefined): number[] | null {
    for (const f of fields ?? []) {
        const rule = ruleOf(f);
        if (!rule || typeof f.value === 'string') continue;
        const refs = pick(rule.use, Array.isArray(f.value) ? f.value : [f.value]);
        if (refs.length) return refs;
    }
    return null;
}

const SEP = '@';

/** The resource key an object with its own stand-in models is drawn under: the class resource and the model refs. */
export function standInResource(resource: string, refs: readonly number[]): string {
    return `${resource}${SEP}${refs.map((r) => (r >>> 0).toString(16)).join('.')}`;
}

/** The class resource and the stand-in refs of a draw key (refs null for a plain class resource). */
export function parseStandInResource(key: string): { resource: string; refs: number[] | null } {
    const at = key.indexOf(SEP);
    if (at < 0) return { resource: key, refs: null };
    const refs = key
        .slice(at + 1)
        .split('.')
        .filter(Boolean)
        .map((s) => parseInt(s, 16) >>> 0);
    return { resource: key.slice(0, at), refs };
}

/** What the worker is asked to load for a draw key: the class, and the object's own stand-in models if any. */
export function modelRequest(key: string): { classId: number; dbIndex: number; standIn?: number[] } {
    const { resource, refs } = parseStandInResource(key);
    return refs ? { ...resourceParts(resource), standIn: refs } : resourceParts(resource);
}

/** The key an object is drawn under: its class resource, or a key of its own when it names its own stand-in models. */
export function drawResource(o: { resource: string; fields?: readonly StandInField[] }): string {
    const refs = o.fields ? objectStandInRefs(o.fields) : null;
    return refs ? standInResource(o.resource, refs) : o.resource;
}
