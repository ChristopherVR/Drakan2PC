// Instance bookkeeping for the model layer (no three.js): which objects are drawn as models, which fall back to
// markers, which are hidden helper classes, and which classes still need their model loaded.

import { resourceParts } from '@drakan/editor-core';

/** Editor-only helper classes: real models in the data (boxes, speakers, cameras) that a player never sees. */
export type HelperGroup = 'trigger' | 'sound' | 'camera' | 'light';

export const HELPER_GROUPS: { id: HelperGroup; label: string }[] = [
    { id: 'trigger', label: 'Triggers' },
    { id: 'sound', label: 'Sound' },
    { id: 'camera', label: 'Cameras' },
    { id: 'light', label: 'Lights' },
];

// Name rules checked on the disc classes of Surdana: "Trigger Detector", "Looping Sound", "Static (Vertex) Light" and
// "Camera" are boxes/helpers with real models; "Light Chair" or "Boulder Wide (Light)" are ordinary scenery. Sound first:
// "Triggered Sound" is a sound.
const HELPER_RULES: [RegExp, HelperGroup][] = [
    [/sound|music|ambient|looping|triggered loop/i, 'sound'],
    [/trigger|detector/i, 'trigger'],
    [/^camera/i, 'camera'],
    [/^(static|dynamic) \(vertex\) light|light ?rays?/i, 'light'],
];

/** Helper group of a class by name, or null for a normal class. */
export function helperGroupOf(className: string): HelperGroup | null {
    for (const [re, g] of HELPER_RULES) if (re.test(className)) return g;
    return null;
}

export interface Placed {
    id: number;
    resource: string;
    class: string;
}

export interface Partition<T> {
    /** Objects drawn with their model, grouped per class resource (document order inside a group). */
    byResource: Map<string, T[]>;
    /** Objects that need a marker: no model for the class (yet, or it failed to decode). */
    markers: T[];
    /** Objects not drawn at all: helper classes switched off. */
    hidden: T[];
}

export function partitionObjects<T extends Placed>(
    objects: Iterable<T>,
    hasModel: (resource: string) => boolean,
    helperVisible: (g: HelperGroup) => boolean,
): Partition<T> {
    const out: Partition<T> = { byResource: new Map(), markers: [], hidden: [] };
    const groupCache = new Map<string, HelperGroup | null>();
    for (const o of objects) {
        let g = groupCache.get(o.class);
        if (g === undefined) groupCache.set(o.class, (g = helperGroupOf(o.class)));
        if (g && !helperVisible(g)) {
            out.hidden.push(o);
            continue;
        }
        if (hasModel(o.resource)) {
            let list = out.byResource.get(o.resource);
            if (!list) out.byResource.set(o.resource, (list = []));
            list.push(o);
        } else out.markers.push(o);
    }
    return out;
}

/** Distinct (classId, dbIndex) pairs among `resources` that are neither loaded, failed nor already requested. */
export function classesToLoad(
    resources: Iterable<string>,
    known: (resource: string) => boolean,
): { resource: string; classId: number; dbIndex: number }[] {
    const seen = new Set<string>();
    const out: { resource: string; classId: number; dbIndex: number }[] = [];
    for (const r of resources) {
        if (seen.has(r) || known(r)) continue;
        seen.add(r);
        out.push({ resource: r, ...resourceParts(r) });
    }
    return out;
}
