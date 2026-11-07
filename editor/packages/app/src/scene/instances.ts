// Instance bookkeeping for the model layer (no three.js): which objects are drawn as models, which fall back to
// markers, which are hidden helper classes, and which classes still need their model loaded.

import { resourceParts } from '@drakan/editor-core';

/** Editor-only helper classes: real models in the data (boxes, speakers, cameras) that a player never sees. */
export type HelperGroup = 'trigger' | 'sound' | 'camera' | 'light' | 'waypoint' | 'timer' | 'spawn' | 'barrier';

/** Atlas order of the editor icons (scene/icons.ts). */
export const ICON_ORDER: readonly HelperGroup[] = [
    'trigger',
    'sound',
    'camera',
    'light',
    'waypoint',
    'timer',
    'spawn',
    'barrier',
];

export const HELPER_GROUPS: { id: HelperGroup; label: string }[] = [
    { id: 'trigger', label: 'Triggers' },
    { id: 'sound', label: 'Sound' },
    { id: 'camera', label: 'Cameras' },
    { id: 'light', label: 'Lights' },
    { id: 'waypoint', label: 'Waypoints' },
    { id: 'timer', label: 'Timers' },
    { id: 'spawn', label: 'Spawn points' },
    { id: 'barrier', label: 'Barriers' },
];

// Name rules checked on the disc classes of Surdana: "Trigger Detector", "Looping Sound", "Static (Vertex) Light" and
// "Camera" are boxes/helpers with real models; "Light Chair" or "Boulder Wide (Light)" are ordinary scenery. Sound first:
// "Triggered Sound" is a sound.
const HELPER_RULES: [RegExp, HelperGroup][] = [
    [/sound|music|ambient|looping|triggered loop/i, 'sound'],
    [/timer/i, 'timer'],
    [/trigger|detector/i, 'trigger'],
    [/way ?point|\bwp\b/i, 'waypoint'],
    [/spawn/i, 'spawn'],
    [/barrier/i, 'barrier'],
    [/^camera/i, 'camera'],
    [/^(static|dynamic) \(vertex\) light|light ?rays?/i, 'light'],
];

/**
 * Engine classes (RFL ids, docs/reference/level-format.md) that are editor helpers when the class has no model of its
 * own. Checked on the disc and on converted Order of the Flame levels, whose classes keep the PC names.
 */
const HELPER_RFL: ReadonlyMap<number, HelperGroup> = new Map([
    [6, 'sound'], // Looping / Triggered Sound
    [129, 'sound'], // Music Trigger
    [131, 'sound'], // Ambient Sound Transition
    [7, 'timer'], // Count Down To Zero
    [62, 'timer'],
    [27, 'camera'],
    [61, 'trigger'], // Detector
    [112, 'waypoint'],
    [72, 'waypoint'], // Null Marker / Attractor
    [63, 'spawn'], // Generator
    [132, 'light'],
    [133, 'light'],
]);

/** Helper group of a model-less class by its engine class id, or null. */
export function helperGroupOfRfl(rfl: number): HelperGroup | null {
    return HELPER_RFL.get(rfl) ?? null;
}

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
    /** Helper-class objects drawn as editor icons (billboards) instead of their model or a marker. */
    icons: { o: T; group: HelperGroup }[];
}

export function partitionObjects<T extends Placed>(
    objects: Iterable<T>,
    hasModel: (resource: string) => boolean,
    helperVisible: (g: HelperGroup) => boolean,
    asIcons = false,
    modelGroup?: (resource: string) => HelperGroup | null,
): Partition<T> {
    const out: Partition<T> = { byResource: new Map(), markers: [], hidden: [], icons: [] };
    const groupCache = new Map<string, HelperGroup | null>();
    for (const o of objects) {
        let g = groupCache.get(o.class);
        if (g === undefined) groupCache.set(o.class, (g = helperGroupOf(o.class)));
        if (!g && modelGroup) g = modelGroup(o.resource);
        if (g && !helperVisible(g)) {
            out.hidden.push(o);
            continue;
        }
        if (g && asIcons) {
            out.icons.push({ o, group: g });
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
