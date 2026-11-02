import {
    CORE_ROLES,
    isMessageField,
    logicClass,
    messageName,
    type LogicClass,
    type LogicRole,
    MESSAGE_LOAD,
    MESSAGE_PLAY_SEQUENCE,
} from '@drakan/formats';
import type { LevelDocument, LevelObject } from './document.ts';
import { effectiveValue, setObjectField, type ClassSchema, type SchemaField } from './schema.ts';
import type { ValidationIssue } from './validate.ts';

/**
 * Level logic model: objects that send each other messages through their link lists, read from a LevelDocument.
 * Nothing here touches the DOM. Semantics and evidence: docs/reference/level-format.md "Level logic".
 *
 * An object sends the message stored in one of its class's message fields (`LogicClass.out`) to *every* object in its
 * link list when its condition happens (trigger entered, timer elapsed, lever pulled, creature died ...). So the
 * message of an edge is a property of the source object, and the editor edits it on the source.
 */

/** What the logic code needs to know about classes; `SchemaProvider` satisfies it. */
export interface LogicEnv {
    rflOf(resource: string): number | null;
    schemaFor(resource: string): ClassSchema | undefined;
}

/** Terrain cell in game units, and the size of one `scale` unit of a box volume (model space is cell sized). */
export const CELL = 2048;

export function logicOf(o: LevelObject, env: LogicEnv): LogicClass | undefined {
    return logicClass(env.rflOf(o.resource));
}

export function schemaField(schema: ClassSchema | undefined, name: string): SchemaField | undefined {
    return schema?.fields.find((f) => f.name === name);
}

/** Effective value of a named field (override else class default), or undefined when the class has no such field. */
export function fieldValueOf(o: LevelObject, env: LogicEnv, name: string): number | string | number[] | undefined {
    const f = schemaField(env.schemaFor(o.resource), name);
    return f ? effectiveValue(o, f).value : undefined;
}

export function numField(o: LevelObject, env: LogicEnv, name: string): number | undefined {
    const v = fieldValueOf(o, env, name);
    return typeof v === 'number' ? v : undefined;
}

// ---------------------------------------------------------------------------------------------------------------------
// graph

export interface EdgeMessage {
    /** Class field on the source that holds the message. */
    field: string;
    fieldIndex: number;
    label: string;
    value: number;
    name: string;
    overridden: boolean;
}

export interface LogicEdge {
    from: number;
    to: number;
    key: string;
    messages: EdgeMessage[];
}

export interface LogicNode {
    id: number;
    role: LogicRole;
    spec: LogicClass;
    class: string;
    /** Number of outgoing links to objects that are not drawn (props toggled by a visibility toggler, gold ...). */
    hidden: number;
    inCount: number;
    outCount: number;
}

export interface LogicGraph {
    nodes: Map<number, LogicNode>;
    edges: LogicEdge[];
}

export interface GraphOptions {
    /** Roles drawn as nodes in level scope (default `CORE_ROLES`). */
    roles?: ReadonlySet<LogicRole>;
    /** Neighbourhood scope: objects reached from these ids over links in either direction. Absent = whole level. */
    focus?: readonly number[];
    /** Link distance from the focus (default 2). */
    depth?: number;
}

/** The messages an object sends along its links, with the values it currently has. */
export function outgoingMessages(o: LevelObject, env: LogicEnv, spec: LogicClass | undefined): EdgeMessage[] {
    const schema = env.schemaFor(o.resource);
    if (!schema || !spec) return [];
    const out: EdgeMessage[] = [];
    for (const port of spec.out) {
        const f = schemaField(schema, port.field);
        if (!f || !isMessageField(f.name, f.type)) continue;
        const { value, overridden } = effectiveValue(o, f);
        if (typeof value !== 'number') continue;
        out.push({
            field: f.name,
            fieldIndex: f.index,
            label: port.label,
            value,
            name: messageName(value),
            overridden,
        });
    }
    return out;
}

const isChain = (a: LogicClass, b: LogicClass): boolean => a.chain === true && b.chain === true;

/**
 * Build the graph of the objects that take part in logic. Level scope: every object with one of `roles` plus the
 * objects those link to or are linked from that are logic-relevant; neighbourhood scope: everything within `depth`
 * links of `focus`. Waypoint chains and plain props (what a visibility toggler shows and hides) are not drawn as nodes;
 * a node counts them in `hidden`.
 */
export function buildLogicGraph(doc: LevelDocument, env: LogicEnv, opts: GraphOptions = {}): LogicGraph {
    const roles = opts.roles ?? new Set<LogicRole>(CORE_ROLES);
    const specOf = new Map<number, LogicClass | undefined>();
    const spec = (o: LevelObject): LogicClass | undefined => {
        if (!specOf.has(o.id)) specOf.set(o.id, logicOf(o, env));
        return specOf.get(o.id);
    };
    // reverse index
    const incoming = new Map<number, number[]>();
    for (const o of doc.objects()) {
        for (const t of o.links ?? []) {
            let l = incoming.get(t);
            if (!l) incoming.set(t, (l = []));
            l.push(o.id);
        }
    }
    const drawable = (o: LevelObject): boolean => {
        const s = spec(o);
        return !!s && s.role !== 'waypoint';
    };
    const chosen = new Set<number>();
    if (opts.focus) {
        const depth = opts.depth ?? 2;
        let frontier = opts.focus.filter((id) => doc.has(id));
        for (const id of frontier) chosen.add(id);
        for (let d = 0; d < depth; d++) {
            const next: number[] = [];
            for (const id of frontier) {
                const o = doc.get(id)!;
                const nbrs = [...(o.links ?? []), ...(incoming.get(id) ?? [])];
                for (const n of nbrs) {
                    const t = doc.get(n);
                    if (!t || chosen.has(n) || !drawable(t)) continue;
                    chosen.add(n);
                    next.push(n);
                }
            }
            frontier = next;
        }
    } else {
        for (const o of doc.objects()) {
            const s = spec(o);
            if (s && roles.has(s.role)) chosen.add(o.id);
        }
        // pull in directly connected relevant objects (creatures that count, doors a lever opens ...)
        for (const id of [...chosen]) {
            const o = doc.get(id)!;
            for (const n of [...(o.links ?? []), ...(incoming.get(id) ?? [])]) {
                const t = doc.get(n);
                if (t && !chosen.has(n) && drawable(t) && spec(t)!.role !== 'sound') {
                    const mine = spec(o)!;
                    if (mine.role === 'creature' || spec(t)!.role === 'creature') chosen.add(n);
                    else if (roles.has(spec(t)!.role)) chosen.add(n);
                }
            }
        }
    }
    const nodes = new Map<number, LogicNode>();
    const edges: LogicEdge[] = [];
    for (const id of chosen) {
        const o = doc.get(id)!;
        const s = spec(o);
        if (!s) continue;
        nodes.set(id, { id, role: s.role, spec: s, class: o.class, hidden: 0, inCount: 0, outCount: 0 });
    }
    for (const n of nodes.values()) {
        const o = doc.get(n.id)!;
        const msgs = outgoingMessages(o, env, n.spec);
        for (const t of o.links ?? []) {
            const target = nodes.get(t);
            if (!target) {
                n.hidden++;
                continue;
            }
            if (isChain(n.spec, target.spec)) continue;
            edges.push({ from: n.id, to: t, key: `${n.id}>${t}`, messages: msgs });
            n.outCount++;
            target.inCount++;
        }
    }
    return { nodes, edges };
}

// ---------------------------------------------------------------------------------------------------------------------
// layout: layered, left to right

export interface LayoutOptions {
    colWidth?: number;
    rowHeight?: number;
}

export interface Placement {
    x: number;
    y: number;
    layer: number;
}

/**
 * Layered layout (sources on the left): longest-path ranks after breaking cycles by depth-first search, barycentre
 * ordering inside each layer (two sweeps), deterministic by id.
 */
export function layoutGraph(g: LogicGraph, opts: LayoutOptions = {}): Map<number, Placement> {
    const colW = opts.colWidth ?? 230;
    const rowH = opts.rowHeight ?? 84;
    const ids = [...g.nodes.keys()].sort((a, b) => a - b);
    const out = new Map<number, number[]>();
    const inn = new Map<number, number[]>();
    for (const id of ids) {
        out.set(id, []);
        inn.set(id, []);
    }
    for (const e of g.edges) {
        if (e.from === e.to) continue;
        out.get(e.from)!.push(e.to);
    }
    // break cycles: DFS, drop back edges
    const state = new Map<number, 0 | 1 | 2>();
    const dag = new Map<number, number[]>(ids.map((i) => [i, []]));
    const dfs = (u: number): void => {
        state.set(u, 1);
        for (const v of out.get(u)!) {
            const s = state.get(v) ?? 0;
            if (s === 1) continue;
            dag.get(u)!.push(v);
            if (s === 0) dfs(v);
        }
        state.set(u, 2);
    };
    // start from nodes without incoming edges first so sources stay on the left
    const hasIn = new Set<number>();
    for (const e of g.edges) if (e.from !== e.to) hasIn.add(e.to);
    for (const id of ids) if (!hasIn.has(id) && !state.has(id)) dfs(id);
    for (const id of ids) if (!state.has(id)) dfs(id);
    for (const [u, vs] of dag) for (const v of vs) inn.get(v)!.push(u);
    // longest path ranks via topological order (Kahn)
    const indeg = new Map<number, number>(ids.map((i) => [i, inn.get(i)!.length]));
    const rank = new Map<number, number>(ids.map((i) => [i, 0]));
    const queue = ids.filter((i) => indeg.get(i) === 0);
    while (queue.length) {
        const u = queue.shift()!;
        for (const v of dag.get(u)!) {
            rank.set(v, Math.max(rank.get(v)!, rank.get(u)! + 1));
            indeg.set(v, indeg.get(v)! - 1);
            if (indeg.get(v) === 0) queue.push(v);
        }
    }
    const layers: number[][] = [];
    for (const id of ids) {
        const r = rank.get(id)!;
        (layers[r] ??= []).push(id);
    }
    for (let i = 0; i < layers.length; i++) layers[i] ??= [];
    const order = new Map<number, number>();
    const reindex = (): void => layers.forEach((l) => l.forEach((id, i) => order.set(id, i)));
    reindex();
    const bary = (id: number, nbrs: number[]): number =>
        nbrs.length ? nbrs.reduce((s, n) => s + (order.get(n) ?? 0), 0) / nbrs.length : (order.get(id) ?? 0);
    for (let sweep = 0; sweep < 2; sweep++) {
        for (let r = 1; r < layers.length; r++) {
            layers[r]!.sort((a, b) => bary(a, inn.get(a)!) - bary(b, inn.get(b)!) || a - b);
            reindex();
        }
        for (let r = layers.length - 2; r >= 0; r--) {
            layers[r]!.sort((a, b) => bary(a, dag.get(a)!) - bary(b, dag.get(b)!) || a - b);
            reindex();
        }
    }
    const res = new Map<number, Placement>();
    layers.forEach((l, r) => l.forEach((id, i) => res.set(id, { x: r * colW, y: i * rowH, layer: r })));
    return res;
}

// ---------------------------------------------------------------------------------------------------------------------
// edits (use inside `ctx.edit` / `editObjects`, which snapshot and undo)

/** Add `to` to the source's links. Returns false when it is a duplicate or a self link. */
export function addLink(o: LevelObject, to: number): boolean {
    if (to === o.id) return false;
    const links = (o.links ??= []);
    if (links.includes(to)) return false;
    links.push(to);
    return true;
}

export function removeLink(o: LevelObject, to: number): boolean {
    if (!o.links?.includes(to)) return false;
    o.links = o.links.filter((l) => l !== to);
    return true;
}

/** Set a message field of the source (by class field name); false when the class has no such message field. */
export function setMessageField(o: LevelObject, env: LogicEnv, field: string, value: number): boolean {
    const f = schemaField(env.schemaFor(o.resource), field);
    if (!f || !isMessageField(f.name, f.type)) return false;
    setObjectField(o, f, value);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// volumes

export type LogicVec3 = [number, number, number];

export interface BoxVolume {
    kind: 'box';
    id: number;
    center: LogicVec3;
    /** Half extents in game units. */
    half: LogicVec3;
    /** Euler XYZ radians (the document rotation). */
    rot: LogicVec3;
}

export interface SphereVolume {
    kind: 'sphere';
    id: number;
    center: LogicVec3;
    radius: number;
    /** The class field the radius edits and the unit of that field in game units. */
    field: string;
    unit: number;
}

export type Volume = BoxVolume | SphereVolume;

/** Fields that hold a distance in level units (cells of 2048 game units) [inf from the class defaults]. */
export const RADIUS_FIELDS: { rfl: number; field: string; label: string }[] = [
    { rfl: 0x13, field: 'Auto-Open Distance (lu)', label: 'door opens' },
    { rfl: 0xb4, field: 'Distance From Rynn', label: 'talk distance' },
];

export function volumesOf(o: LevelObject, env: LogicEnv): Volume[] {
    const out: Volume[] = [];
    const spec = logicOf(o, env);
    if (spec?.volume && o.scale) {
        out.push({
            kind: 'box',
            id: o.id,
            center: [...o.transform.pos],
            half: [(o.scale[0] * CELL) / 2, (o.scale[1] * CELL) / 2, (o.scale[2] * CELL) / 2],
            rot: [...o.transform.rot],
        });
    }
    const rfl = env.rflOf(o.resource);
    for (const r of RADIUS_FIELDS) {
        if (r.rfl !== rfl) continue;
        const v = numField(o, env, r.field);
        if (v !== undefined && v > 0) {
            out.push({
                kind: 'sphere',
                id: o.id,
                center: [...o.transform.pos],
                radius: v * CELL,
                field: r.field,
                unit: CELL,
            });
        }
    }
    return out;
}

/** Rotate a vector by an Euler XYZ rotation (three.js order: R = Rx Ry Rz). */
export function rotateEulerXYZ(rot: readonly number[], v: readonly number[]): LogicVec3 {
    let [x, y, z] = v as [number, number, number];
    const [a, b, c] = rot as [number, number, number];
    // Rz
    let t = x * Math.cos(c) - y * Math.sin(c);
    y = x * Math.sin(c) + y * Math.cos(c);
    x = t;
    // Ry
    t = x * Math.cos(b) + z * Math.sin(b);
    z = -x * Math.sin(b) + z * Math.cos(b);
    x = t;
    // Rx
    t = y * Math.cos(a) - z * Math.sin(a);
    z = y * Math.sin(a) + z * Math.cos(a);
    y = t;
    return [x, y, z];
}

export const MIN_BOX_CELLS = 0.05;

/**
 * Resize a box volume by dragging one face: `axis` (0 x, 1 y, 2 z of the box's local frame), `sign` which face and
 * `local` the new local coordinate of that face in game units (relative to the box centre, before the resize). The
 * opposite face stays where it is, so both position and scale change. Mutates `o`; false when it has no scale.
 */
export function resizeBoxFace(o: LevelObject, axis: 0 | 1 | 2, sign: 1 | -1, local: number): boolean {
    if (!o.scale) return false;
    const half0 = (o.scale[axis] * CELL) / 2;
    const fixed = -sign * half0;
    const len = Math.max(MIN_BOX_CELLS * CELL, sign * (local - fixed));
    const dragged = fixed + sign * len;
    const mid = (dragged + fixed) / 2;
    const dir: LogicVec3 = [0, 0, 0];
    dir[axis] = mid;
    const w = rotateEulerXYZ(o.transform.rot, dir);
    o.transform.pos = [o.transform.pos[0] + w[0], o.transform.pos[1] + w[1], o.transform.pos[2] + w[2]];
    const s: LogicVec3 = [...o.scale];
    s[axis] = Math.fround(len / CELL);
    o.scale = s;
    return true;
}

/** Set the radius of a sphere volume (game units) through its class field. */
export function setVolumeRadius(o: LevelObject, env: LogicEnv, v: SphereVolume, radius: number): boolean {
    const f = schemaField(env.schemaFor(o.resource), v.field);
    if (!f) return false;
    setObjectField(o, f, Math.max(0.05, radius / v.unit));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// validation

/** Exits of every level, for the arrival-marker check: destination level name per exit object. */
export interface ExitIndexEntry {
    id: number;
    name: string;
    /** Destination level name (path and extension stripped), or null when it could not be read. */
    dest: string | null;
    /** "Transition To 'portal' object": the arrival object is the one named `portal`. */
    portal: boolean;
}
export interface ExitIndex {
    /** Level name (as the level list shows it) -> its exits. */
    levels: ReadonlyMap<string, readonly ExitIndexEntry[]>;
}

export interface LogicValidateOptions {
    /** Name of the open level (the key in `exits`). */
    level?: string;
    /** When the open level is a project copy: the level it was copied from (the game arrives at that level's marker too). */
    copyOf?: string;
    exits?: ExitIndex;
    /** Destination of an exit object of the open document, or null when unknown. */
    exitDestination?: (o: LevelObject) => { dest: string | null; portal: boolean } | null;
}

const lbl = (o: LevelObject): string => `${o.class || 'Object'} #${o.id}`;

const norm = (s: string): string => s.toLowerCase().replace(/[^a-z0-9]/g, '');

/** Level name from a destination text such as `Levels\Level 2 - Shadowmire\Level 2 - Shadowmire.lvl`. */
export function destinationLevelName(text: string): string {
    const base = text.replace(/\\/g, '/').split('/').pop() ?? text;
    return base.replace(/\.lvl$/i, '').trim();
}

/** Objects that can start a chain of messages on their own (the roots of reachability). */
function spontaneous(o: LevelObject, spec: LogicClass, env: LogicEnv, hasIncoming: boolean): boolean {
    switch (spec.role) {
        case 'detector':
            // a detector set to Disabled needs a message first
            return numField(o, env, 'Initial State') !== 1 || hasIncoming;
        case 'timer':
            return numField(o, env, 'Start Mode') === 0;
        case 'cutscene':
            return numField(o, env, 'Initial State') === 1;
        case 'counter':
        case 'relay':
        case 'visibility':
        case 'quest':
            return false;
        default:
            return true;
    }
}

/**
 * Logic checks: triggers whose message goes nowhere, cut scene players nothing can ever start, messages the target
 * ignores, exits without a destination or an arrival marker in the destination level.
 */
export function validateLogic(doc: LevelDocument, env: LogicEnv, opts: LogicValidateOptions = {}): ValidationIssue[] {
    const out: ValidationIssue[] = [];
    const incoming = new Map<number, number[]>();
    for (const o of doc.objects())
        for (const t of o.links ?? []) (incoming.get(t) ?? incoming.set(t, []).get(t)!).push(o.id);
    const specs = new Map<number, LogicClass | undefined>();
    for (const o of doc.objects()) specs.set(o.id, logicOf(o, env));

    // reachability from objects that act on their own
    const live = new Set<number>();
    const stack: number[] = [];
    for (const o of doc.objects()) {
        const s = specs.get(o.id);
        if (s && spontaneous(o, s, env, (incoming.get(o.id)?.length ?? 0) > 0)) {
            live.add(o.id);
            stack.push(o.id);
        }
    }
    while (stack.length) {
        const o = doc.get(stack.pop()!)!;
        for (const t of o.links ?? []) {
            if (!live.has(t) && doc.has(t)) {
                live.add(t);
                stack.push(t);
            }
        }
    }

    for (const o of doc.objects()) {
        const s = specs.get(o.id);
        if (!s) continue;
        const targets = (o.links ?? []).filter((t) => doc.has(t));
        if (['detector', 'timer', 'switch', 'plate'].includes(s.role) && s.out.length && targets.length === 0) {
            const task = s.role === 'detector' ? numField(o, env, 'Task') : 6;
            const ambient = s.rfl === 0x83;
            if ((task === 6 || task === undefined) && !ambient) {
                out.push({
                    severity: 'warning',
                    code: 'trigger-no-target',
                    ids: [o.id],
                    message: `${lbl(o)} (${s.title.toLowerCase()}) has no link, so its message goes nowhere`,
                });
            }
        }
        if (s.role === 'cutscene' && !live.has(o.id)) {
            out.push({
                severity: 'warning',
                code: 'unreachable-cutscene',
                ids: [o.id],
                message: `${lbl(o)}: nothing can start this cut scene (no trigger, timer, switch or creature reaches it and it does not start on)`,
            });
        }
        if (s.role === 'exit' && opts.exitDestination) {
            const d = opts.exitDestination(o);
            if (!d || !d.dest) {
                out.push({
                    severity: 'warning',
                    code: 'exit-no-destination',
                    ids: [o.id],
                    message: `${lbl(o)}: the destination of this exit could not be read`,
                });
            } else if (opts.exits && opts.level) {
                const issue = exitArrivalProblem(opts.level, d.dest, d.portal, opts.exits, opts.copyOf);
                if (issue)
                    out.push({
                        severity: 'warning',
                        code: 'exit-no-arrival',
                        ids: [o.id],
                        message: `${lbl(o)}: ${issue}`,
                    });
            }
        }
        // messages the target does not react to (only for classes whose reactions are known precisely)
        for (const t of targets) {
            const to = doc.get(t)!;
            const ts = specs.get(t);
            if (!ts || (ts.role !== 'cutscene' && ts.role !== 'door')) continue;
            const msgs = outgoingMessages(o, env, s);
            const m = msgs[0];
            if (!m) continue;
            const accepts = new Set(ts.accepts ?? []);
            if (ts.role === 'cutscene') {
                accepts.add(MESSAGE_PLAY_SEQUENCE);
                accepts.add(MESSAGE_LOAD);
                const own = numField(to, env, 'Message To Play Next');
                if (own !== undefined) accepts.add(own);
            }
            if (!accepts.has(m.value) && m.value !== 32) {
                out.push({
                    severity: 'warning',
                    code: 'message-ignored',
                    ids: [o.id, t],
                    message: `${lbl(o)} sends "${m.name}" (${m.field}) to ${lbl(to)}, which only reacts to ${[...accepts].map(messageName).join(', ')}`,
                });
            }
        }
    }
    return out;
}

/**
 * Problem text when an exit to `dest` has no arrival in the destination level, else null. `copyOf`: this level is a copy
 * of that level; the game then also arrives at the destination's way back to the source (the runtime's arrival lookup
 * falls back to it, `drakan_levelchange.cpp` arrivalEntry).
 */
export function exitArrivalProblem(
    thisLevel: string,
    dest: string,
    portal: boolean,
    index: ExitIndex,
    copyOf?: string,
): string | null {
    const entry = [...index.levels.entries()].find(([name]) => norm(name) === norm(dest));
    if (!entry) return `destination level "${dest}" is not on this disc`;
    const exits = entry[1];
    if (portal) {
        return exits.some((e) => /portal/i.test(e.name))
            ? null
            : `"${entry[0]}" has no object named "portal" to arrive at`;
    }
    const back = (e: { dest: string | null }): boolean =>
        !!e.dest && (norm(e.dest) === norm(thisLevel) || (!!copyOf && norm(e.dest) === norm(copyOf)));
    return exits.some(back)
        ? null
        : `"${entry[0]}" has no exit leading back to "${thisLevel}", so the player arrives with no marker (and falls)`;
}
