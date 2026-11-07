import { FIELD_ENUMS, MESSAGE_NAMES, type LogicRole } from '@drakan/formats';
import type { AssetClass } from './catalog.ts';
import { newObjectFromClass, resourceString } from './catalog.ts';
import type { Command } from './commands.ts';
import { cloneObject, type LevelDocument, type LevelObject } from './document.ts';
import { addLink, logicOf, numField, schemaField, type LogicEnv, type LogicVec3 } from './logic.ts';
import { setObjectField } from './schema.ts';

/**
 * Authoring templates for common logic setups. A template reads a few parameters, creates the objects a working setup
 * needs (with class defaults except the fields it sets) and links them. Building is pure: it returns the objects and the
 * links to add to existing objects, and `ApplyTemplateCommand` applies it as one undo step.
 */

/**
 * The message a counter / lever should send to open `target`: a cut scene plays, a timer starts, a door gets On unless it
 * is Locked (a locked door ignores On, so it gets Unlock and relies on Auto-Open), anything else gets Unlock.
 */
function openMessage(ctx: TemplateContext, target: LevelObject): string {
    const role = logicOf(target, ctx.env)?.role;
    if (role === 'cutscene') return 'Play Sequence';
    if (role === 'timer') return 'On';
    if (role === 'door') return (numField(target, ctx.env, 'Locked') ?? 0) !== 0 ? 'Unlock' : 'On';
    return 'Unlock';
}

export type ParamKind = 'number' | 'bool' | 'choice' | 'object' | 'objects' | 'class';

export interface TemplateParam {
    id: string;
    label: string;
    kind: ParamKind;
    hint?: string;
    default?: number | boolean | string;
    min?: number;
    max?: number;
    step?: number;
    /** `choice`: fixed options. */
    options?: { value: string; label: string }[];
    /** `object` / `objects`: roles an existing object must have. */
    roles?: LogicRole[];
    /** `class`: RFL id the class must have (the UI lists those classes of the level). */
    rfl?: number;
    /** `object`: leaving it empty is allowed (the template creates a new one). */
    optional?: boolean;
}

export type ParamValues = Record<string, number | boolean | string | number[] | undefined>;

export interface TemplateContext {
    doc: LevelDocument;
    env: LogicEnv;
    catalog: readonly AssetClass[];
    /** Where the setup goes (game units); the volume or lever is placed here. */
    at: LogicVec3;
    /** Selected object ids (used by `objects` parameters left empty). */
    selection: readonly number[];
}

export interface TemplateBuild {
    /** New objects, ids already assigned, links between them set. */
    objects: LevelObject[];
    /** Links to add to objects that already exist: `from` gets `to` appended. */
    links: { from: number; to: number }[];
    /** What was made, one sentence each. */
    notes: string[];
    warnings: string[];
}

export interface LogicTemplate {
    id: string;
    title: string;
    summary: string;
    params: TemplateParam[];
    build(ctx: TemplateContext, v: ParamValues): TemplateBuild | { error: string };
}

export const isError = (r: TemplateBuild | { error: string }): r is { error: string } => 'error' in r;

// ---------------------------------------------------------------------------------------------------------------------

/** Message value by name (throws on a typo so a template cannot silently send the wrong thing). */
export function msg(name: string): number {
    const i = MESSAGE_NAMES.indexOf(name);
    if (i < 0) throw new Error(`unknown message "${name}"`);
    return i;
}

const enumIndex = (key: string, item: string): number => {
    const i = FIELD_ENUMS[key]?.indexOf(item) ?? -1;
    if (i < 0) throw new Error(`unknown enum item ${key} / ${item}`);
    return i;
};

/** The class of RFL id `rfl` the level already uses most (so a body to copy exists), else any class of that id. */
export function pickClass(catalog: readonly AssetClass[], rfl: number, name?: RegExp): AssetClass | undefined {
    const c = catalog
        .filter((x) => x.rflClass === rfl && (!name || name.test(x.name)))
        .sort((a, b) => b.instances - a.instances || a.name.localeCompare(b.name));
    return c[0];
}

class Builder {
    readonly objects: LevelObject[] = [];
    readonly links: { from: number; to: number }[] = [];
    readonly notes: string[] = [];
    readonly warnings: string[] = [];
    private next: number;
    constructor(readonly ctx: TemplateContext) {
        this.next = ctx.doc.nextId;
    }
    /** A new instance of `cls` with only `fields` overridden (by class field name). */
    make(
        cls: AssetClass,
        pos: LogicVec3,
        fields: Record<string, number | number[]>,
        opts: { scale?: LogicVec3; yaw?: number } = {},
    ): LevelObject {
        const o = newObjectFromClass(this.ctx.doc, cls, pos, opts.yaw ?? 0);
        o.id = this.next++;
        o.fields = [];
        o.links = [];
        if (opts.scale && o.scale) o.scale = [...opts.scale];
        const schema = this.ctx.env.schemaFor(cls.resource);
        for (const [name, value] of Object.entries(fields)) {
            const f = schemaField(schema, name);
            if (!f) {
                this.warnings.push(`${cls.name} has no field "${name}"; not set`);
                continue;
            }
            setObjectField(o, f, value);
        }
        if (!o.template)
            this.warnings.push(`${cls.name} has no instance in this level: it uses generic class defaults`);
        this.objects.push(o);
        return o;
    }
    /** Link from a new object (direct) or an existing one (recorded). */
    link(from: number, to: number): void {
        const o = this.objects.find((x) => x.id === from);
        if (o) addLink(o, to);
        else this.links.push({ from, to });
    }
    done(): TemplateBuild {
        return { objects: this.objects, links: this.links, notes: this.notes, warnings: this.warnings };
    }
}

const need = (c: AssetClass | undefined, what: string): AssetClass => {
    if (!c) throw new TemplateError(`This level's classes include no ${what}, so the template cannot create one.`);
    return c;
};

class TemplateError extends Error {}

function guard(fn: () => TemplateBuild): TemplateBuild | { error: string } {
    try {
        return fn();
    } catch (e) {
        if (e instanceof TemplateError) return { error: e.message };
        throw e;
    }
}

const num = (v: ParamValues, id: string, d: number): number => (typeof v[id] === 'number' ? (v[id] as number) : d);
const flag = (v: ParamValues, id: string, d: boolean): boolean => (typeof v[id] === 'boolean' ? (v[id] as boolean) : d);
const objId = (v: ParamValues, id: string): number | undefined =>
    typeof v[id] === 'number' && (v[id] as number) > 0 ? (v[id] as number) : undefined;

const DETECT_WHICH = FIELD_ENUMS['0x3d:Detect Which?']!;
const detectOptions = DETECT_WHICH.map((label, i) => ({ value: String(i), label }));

const need2 = (ctx: TemplateContext, id: number, what: string): LevelObject => {
    const o = ctx.doc.get(id);
    if (!o) throw new TemplateError(`${what} #${id} is not in the level.`);
    return o;
};

// ---------------------------------------------------------------------------------------------------------------------

export const TEMPLATES: LogicTemplate[] = [
    {
        id: 'cutscene-trigger',
        title: 'Trigger volume plays cut scene',
        summary:
            'A box the player walks into that starts a cut scene player. Pick an existing one or let the template add one.',
        params: [
            {
                id: 'cutscene',
                label: 'Cut scene player',
                kind: 'object',
                roles: ['cutscene'],
                optional: true,
                hint: 'Leave empty to create a new cut scene player beside the volume.',
            },
            { id: 'sequence', label: 'Sequence id (new player)', kind: 'number', default: 0, min: 0, max: 65535 },
            { id: 'w', label: 'Width (cells)', kind: 'number', default: 3, min: 0.25, step: 0.25 },
            { id: 'h', label: 'Height (cells)', kind: 'number', default: 2, min: 0.25, step: 0.25 },
            { id: 'd', label: 'Depth (cells)', kind: 'number', default: 1, min: 0.25, step: 0.25 },
            { id: 'once', label: 'Only once', kind: 'bool', default: true },
            { id: 'who', label: 'Detects', kind: 'choice', default: '2', options: detectOptions },
        ],
        build: (ctx, v) =>
            guard(() => {
                const b = new Builder(ctx);
                const det = need(
                    pickClass(ctx.catalog, 0x3d, /^trigger detector$/i) ?? pickClass(ctx.catalog, 0x3d),
                    'trigger detector class',
                );
                let target = objId(v, 'cutscene');
                if (target !== undefined) need2(ctx, target, 'Cut scene player');
                else {
                    const cs = need(pickClass(ctx.catalog, 0x33), 'cut scene player class');
                    const seq = num(v, 'sequence', 0);
                    const p: LogicVec3 = [ctx.at[0] + 2048, ctx.at[1], ctx.at[2]];
                    const o = b.make(cs, p, {
                        'Initial State': enumIndex('0x33:Initial State', 'Triggered'),
                        ...(seq > 0 ? { 'Sequence List': [seq] } : {}),
                    });
                    target = o.id;
                    b.notes.push(
                        `Cut scene player #${o.id}${seq > 0 ? ` with sequence ${seq}` : ' (set its Sequence List)'}`,
                    );
                }
                const d = b.make(
                    det,
                    ctx.at,
                    {
                        'Trigger Message': msg('Play Sequence'),
                        'Detect Only Once?': flag(v, 'once', true) ? 1 : 0,
                        'Detect Which?': Number(v.who ?? 2),
                    },
                    { scale: [num(v, 'w', 3), num(v, 'h', 2), num(v, 'd', 1)] },
                );
                b.link(d.id, target);
                b.notes.push(`Trigger volume #${d.id} sends Play Sequence to #${target}`);
                return b.done();
            }),
    },
    {
        id: 'lever-door',
        title: 'Lever opens door',
        summary: 'A lever that opens a door when pulled and closes it when pulled back (or unlocks and locks it).',
        params: [
            { id: 'door', label: 'Door', kind: 'object', roles: ['door', 'mechanism'] },
            { id: 'lever', label: 'Lever class', kind: 'class', rfl: 0x0d },
            {
                id: 'mode',
                label: 'Lever does',
                kind: 'choice',
                default: 'open',
                options: [
                    { value: 'open', label: 'Open / close' },
                    { value: 'unlock', label: 'Unlock / lock' },
                ],
            },
        ],
        build: (ctx, v) =>
            guard(() => {
                const b = new Builder(ctx);
                const door = objId(v, 'door');
                if (door === undefined) throw new TemplateError('Pick the door the lever controls.');
                need2(ctx, door, 'Door');
                const wanted =
                    typeof v.lever === 'string' ? ctx.catalog.find((c) => c.resource === v.lever) : undefined;
                const cls = need(
                    wanted ?? pickClass(ctx.catalog, 0x0d, /lever/i) ?? pickClass(ctx.catalog, 0x0d),
                    'lever class',
                );
                const unlock = v.mode === 'unlock';
                // Switch Type defaults to One_Time (0): a second press sends nothing, so the door could never close
                // (measured in game, docs/journal/2026-11-07/door-proof.md). Toggle (1) alternates on / off.
                const lever = b.make(cls, ctx.at, {
                    'Switch Type': 1,
                    'Message to send when on': msg(unlock ? 'Unlock' : 'On'),
                    'Message to send when off': msg(unlock ? 'Lock' : 'Off'),
                });
                b.link(lever.id, door);
                b.notes.push(
                    `${cls.name} #${lever.id} sends ${unlock ? 'Unlock' : 'On'} / ${unlock ? 'Lock' : 'Off'} to door #${door}`,
                );
                if (!unlock && ctx.doc.get(door)) {
                    const dl = logicOf(ctx.doc.get(door)!, ctx.env);
                    if (dl?.role === 'door')
                        b.notes.push(
                            'Doors with Auto-Open on also open by themselves when the player is near; keep Locked off, a locked door ignores the lever (use Unlock mode for those).',
                        );
                }
                return b.done();
            }),
    },
    {
        id: 'kill-all-gate',
        title: 'Kill all enemies opens gate',
        summary: 'A counter the selected creatures report to when they die; at zero it unlocks or opens the gate.',
        params: [
            {
                id: 'enemies',
                label: 'Enemies',
                kind: 'objects',
                roles: ['creature'],
                hint: 'The creatures to kill; uses the current selection when empty.',
            },
            { id: 'gate', label: 'Gate / target', kind: 'object', roles: ['door', 'mechanism', 'cutscene', 'timer'] },
            {
                id: 'then',
                label: 'At zero send',
                kind: 'choice',
                default: 'auto',
                options: [
                    { value: 'auto', label: 'By gate (On for an unlocked door, Unlock for a locked one)' },
                    { value: 'Unlock', label: 'Unlock' },
                    { value: 'On', label: 'On (open)' },
                    { value: 'Play Sequence', label: 'Play Sequence' },
                ],
            },
        ],
        build: (ctx, v) =>
            guard(() => {
                const b = new Builder(ctx);
                const picked = Array.isArray(v.enemies) ? (v.enemies as number[]) : [...ctx.selection];
                const enemies = picked.filter((id) => {
                    const o = ctx.doc.get(id);
                    return o && logicOf(o, ctx.env)?.role === 'creature';
                });
                if (!enemies.length) throw new TemplateError('Select the creatures that have to die first.');
                const gate = objId(v, 'gate');
                if (gate === undefined) throw new TemplateError('Pick the gate the counter opens.');
                const thenMsg =
                    typeof v.then === 'string' && v.then !== 'auto' ? v.then : openMessage(ctx, need2(ctx, gate, 'Gate'));
                const cls = need(
                    pickClass(ctx.catalog, 0x07, /count/i) ?? pickClass(ctx.catalog, 0x07),
                    'counter class',
                );
                const counter = b.make(cls, ctx.at, {
                    'Initial Counter Value': enemies.length,
                    'When Triggered?': 1,
                    'Decrement (-1) Message': msg("I'm Dead"),
                    'Message To Send': msg(thenMsg),
                    'Destroy After Counter Hits Zero ?': 0,
                });
                for (const e of enemies) {
                    b.link(e, counter.id);
                    const eo = ctx.doc.get(e)!;
                    const sent = logicOf(eo, ctx.env)?.out[0];
                    const cur = sent ? ctx.env.schemaFor(eo.resource) : undefined;
                    if (sent && cur) {
                        const f = schemaField(cur, sent.field);
                        const ov = eo.fields?.find((x) => x.fieldIndex === f?.index);
                        const val = ov ? ov.value : f?.default;
                        if (val !== msg("I'm Dead"))
                            b.warnings.push(
                                `#${e} sends "${MESSAGE_NAMES[Number(val)] ?? val}" on death, not I'm Dead: the counter will not count it`,
                            );
                    }
                }
                b.link(counter.id, gate);
                b.notes.push(
                    `Counter #${counter.id} starts at ${enemies.length}, counts I'm Dead from ${enemies.length} creature(s), then sends ${String(v.then ?? 'Unlock')} to #${gate}`,
                );
                return b.done();
            }),
    },
    {
        id: 'pressure-plates',
        title: 'Pressure plates puzzle',
        summary:
            'N pressure plates feed a counter; when all are pressed it opens a target. Needs a plate class in the level.',
        params: [
            { id: 'count', label: 'Plates', kind: 'number', default: 3, min: 1, max: 8, step: 1 },
            { id: 'target', label: 'Target', kind: 'object', roles: ['door', 'mechanism', 'cutscene', 'timer'] },
            { id: 'plate', label: 'Plate class', kind: 'class', rfl: 0x1d },
            {
                id: 'hold',
                label: 'Door shuts again when the plate is released (one plate, no counter)',
                kind: 'bool',
                default: false,
            },
            { id: 'spacing', label: 'Plate spacing (units)', kind: 'number', default: 3000, min: 1000, step: 500 },
            {
                id: 'axis',
                label: 'Plates in a row along',
                kind: 'choice',
                default: 'x',
                options: [
                    { value: 'x', label: 'X' },
                    { value: 'z', label: 'Z' },
                ],
            },
            {
                id: 'then',
                label: 'Counter sends',
                kind: 'choice',
                default: 'auto',
                options: [
                    { value: 'auto', label: 'By target (Play Sequence for a cut scene, On for a timer or an unlocked door, Unlock for a locked door)' },
                    { value: 'Unlock', label: 'Unlock' },
                    { value: 'On', label: 'On (open)' },
                    { value: 'Play Sequence', label: 'Play Sequence' },
                ],
            },
        ],
        build: (ctx, v) =>
            guard(() => {
                const b = new Builder(ctx);
                const target = objId(v, 'target');
                if (target === undefined) throw new TemplateError('Pick the object the plates open.');
                const targetObj = need2(ctx, target, 'Target');
                const then = typeof v.then === 'string' && v.then !== 'auto' ? v.then : openMessage(ctx, targetObj);
                const hold = v.hold === true || v.hold === 1;
                const n = hold ? 1 : Math.max(1, Math.min(8, Math.round(num(v, 'count', 3))));
                const wanted =
                    typeof v.plate === 'string' ? ctx.catalog.find((c) => c.resource === v.plate) : undefined;
                const plate = need(wanted ?? pickClass(ctx.catalog, 0x1d), 'pressure plate class');
                const cc = need(
                    pickClass(ctx.catalog, 0x07, /count/i) ?? pickClass(ctx.catalog, 0x07),
                    'counter class',
                );
                const counter = hold
                    ? undefined
                    : b.make(cc, [ctx.at[0], ctx.at[1], ctx.at[2] + 3000], {
                          'Initial Counter Value': n,
                          'When Triggered?': 1,
                          'Decrement (-1) Message': msg('On'),
                          'Message To Send': msg(then),
                      });
                for (let i = 0; i < n; i++) {
                    const step = num(v, 'spacing', 3000) * i;
                    const along = v.axis === 'z';
                    const p = b.make(
                        plate,
                        [ctx.at[0] + (along ? 0 : step), ctx.at[1], ctx.at[2] + (along ? step : 0)],
                        {
                            'Sink Distance (wu)': 50, // the placed plates of Stratos use 50 (class default 100)
                            // Reached Bottom (the class default 0 only sends at the top); a held plate sends at both ends
                            'Send Message When': hold ? 2 : 1,
                            'Message To Send At Bottom': msg('On'),
                            'Message To Send At Top': msg(hold ? 'Off' : '<none>'),
                        },
                    );
                    b.link(p.id, counter ? counter.id : target);
                }
                if (!counter) {
                    b.notes.push(`The plate sends On when pressed and Off when released straight to #${target}`);
                    return b.done();
                }
                b.link(counter.id, target);
                b.notes.push(`${n} plate(s) feed counter #${counter.id}, which sends ${then} to #${target}`);
                return b.done();
            }),
    },
    {
        id: 'timer-door',
        title: 'Timed trigger (volume, delay, target)',
        summary:
            'A volume starts a timer when the player enters; after the delay the timer sends a message (default On, opens a door) to the target.',
        params: [
            { id: 'target', label: 'Target', kind: 'object', roles: ['door', 'mechanism', 'cutscene', 'visibility'] },
            { id: 'delay', label: 'Delay (seconds)', kind: 'number', default: 3, min: 0, step: 0.5 },
            {
                id: 'then',
                label: 'Timer sends',
                kind: 'choice',
                default: 'On',
                options: ['On', 'Off', 'Unlock', 'Lock', 'Play Sequence'].map((n) => ({ value: n, label: n })),
            },
            { id: 'w', label: 'Width (cells)', kind: 'number', default: 3, min: 0.25, step: 0.25 },
            { id: 'h', label: 'Height (cells)', kind: 'number', default: 2, min: 0.25, step: 0.25 },
            { id: 'd', label: 'Depth (cells)', kind: 'number', default: 1, min: 0.25, step: 0.25 },
            { id: 'who', label: 'Detects', kind: 'choice', default: '2', options: detectOptions },
        ],
        build: (ctx, v) =>
            guard(() => {
                const b = new Builder(ctx);
                const target = objId(v, 'target');
                if (target === undefined) throw new TemplateError('Pick the object the timer fires at.');
                need2(ctx, target, 'Target');
                const det = need(
                    pickClass(ctx.catalog, 0x3d, /^trigger detector$/i) ?? pickClass(ctx.catalog, 0x3d),
                    'trigger detector class',
                );
                const tc = need(
                    pickClass(ctx.catalog, 0x3e, /^timer$/i) ?? pickClass(ctx.catalog, 0x3e),
                    'timer class',
                );
                const then = String(v.then ?? 'On');
                const delay = num(v, 'delay', 3);
                const timer = b.make(tc, [ctx.at[0], ctx.at[1], ctx.at[2] + 3000], {
                    'Time Until Trigger': delay,
                    'Start Mode': enumIndex('0x3e:Start Mode', 'Run When Triggered By Any Message'),
                    'Trigger Message': msg(then),
                    'Destroy After Timeout?': 0,
                });
                const d = b.make(
                    det,
                    ctx.at,
                    {
                        'Trigger Message': msg('On'),
                        'Detect Only Once?': 1,
                        'Detect Which?': Number(v.who ?? 2),
                    },
                    { scale: [num(v, 'w', 3), num(v, 'h', 2), num(v, 'd', 1)] },
                );
                b.link(d.id, timer.id);
                b.link(timer.id, target);
                b.notes.push(
                    `Volume #${d.id} starts timer #${timer.id}; after ${delay} s it sends ${then} to #${target}`,
                );
                return b.done();
            }),
    },
    {
        id: 'key-lock',
        title: 'Key lock opens door',
        summary:
            'A lock plate that sends Unlock to a door when Rynn uses the matching key (the key item carries the same Lock Code). Needs a key lock class in the level.',
        params: [
            { id: 'door', label: 'Door', kind: 'object', roles: ['door', 'mechanism'] },
            { id: 'lock', label: 'Lock class', kind: 'class', rfl: 0x22 },
            { id: 'code', label: 'Lock code (key item code)', kind: 'number', default: 1, min: 0, step: 1 },
        ],
        build: (ctx, v) =>
            guard(() => {
                const b = new Builder(ctx);
                const door = objId(v, 'door');
                if (door === undefined) throw new TemplateError('Pick the door the lock opens.');
                need2(ctx, door, 'Door');
                const wanted = typeof v.lock === 'string' ? ctx.catalog.find((c) => c.resource === v.lock) : undefined;
                const cls = need(wanted ?? pickClass(ctx.catalog, 0x22), 'key lock class');
                const code = Math.round(num(v, 'code', 1));
                const lock = b.make(cls, ctx.at, { 'Lock Code': code, 'Trigger To Send': msg('Unlock') });
                b.link(lock.id, door);
                b.notes.push(`${cls.name} #${lock.id} (code ${code}) sends Unlock to door #${door}`);
                b.notes.push('Unlock only unlocks: leave Auto-Open on, or add a lever / volume that sends On.');
                return b.done();
            }),
    },
    {
        id: 'volume-message',
        title: 'Trigger volume sends message',
        summary: 'A box that sends any message to the selected objects when the player enters it.',
        params: [
            { id: 'targets', label: 'Targets', kind: 'objects', hint: 'Uses the current selection when empty.' },
            {
                id: 'message',
                label: 'Message',
                kind: 'choice',
                default: 'On',
                options: MESSAGE_NAMES.map((n) => ({ value: n, label: n })),
            },
            { id: 'w', label: 'Width (cells)', kind: 'number', default: 3, min: 0.25, step: 0.25 },
            { id: 'h', label: 'Height (cells)', kind: 'number', default: 2, min: 0.25, step: 0.25 },
            { id: 'd', label: 'Depth (cells)', kind: 'number', default: 1, min: 0.25, step: 0.25 },
            { id: 'once', label: 'Only once', kind: 'bool', default: true },
            { id: 'who', label: 'Detects', kind: 'choice', default: '2', options: detectOptions },
        ],
        build: (ctx, v) =>
            guard(() => {
                const b = new Builder(ctx);
                const targets = (Array.isArray(v.targets) ? (v.targets as number[]) : [...ctx.selection]).filter((id) =>
                    ctx.doc.has(id),
                );
                if (!targets.length)
                    throw new TemplateError('Select the objects that should receive the message first.');
                const det = need(
                    pickClass(ctx.catalog, 0x3d, /^trigger detector$/i) ?? pickClass(ctx.catalog, 0x3d),
                    'trigger detector class',
                );
                const d = b.make(
                    det,
                    ctx.at,
                    {
                        'Trigger Message': msg(String(v.message ?? 'On')),
                        'Detect Only Once?': flag(v, 'once', true) ? 1 : 0,
                        'Detect Which?': Number(v.who ?? 2),
                    },
                    { scale: [num(v, 'w', 3), num(v, 'h', 2), num(v, 'd', 1)] },
                );
                for (const t of targets) b.link(d.id, t);
                b.notes.push(
                    `Trigger volume #${d.id} sends ${String(v.message ?? 'On')} to ${targets.length} object(s)`,
                );
                return b.done();
            }),
    },
    {
        id: 'quest-object',
        title: 'Place quest object',
        summary:
            'Places a quest object (journal / map entry) and, optionally, trigger volumes that start (On) and complete (Quest Complete) it. Give a class id to place a quest class you added or edited in the Story editor.',
        params: [
            {
                id: 'quest',
                label: 'Quest class to copy',
                kind: 'class',
                rfl: 0x68,
                hint: 'Any quest class that already has an object in this level (its body is copied).',
            },
            {
                id: 'classId',
                label: 'Class id to use instead',
                kind: 'number',
                default: 0,
                min: 0,
                step: 1,
                hint: 'The id of a quest class from the Quests panel; 0 uses the class above. Same database as the class above.',
            },
            { id: 'start', label: 'Volume starts the quest', kind: 'bool', default: true },
            { id: 'complete', label: 'Second volume completes it', kind: 'bool', default: false },
            { id: 'w', label: 'Volume width (cells)', kind: 'number', default: 3, min: 0.25, step: 0.25 },
            { id: 'h', label: 'Volume height (cells)', kind: 'number', default: 2, min: 0.25, step: 0.25 },
            { id: 'd', label: 'Volume depth (cells)', kind: 'number', default: 3, min: 0.25, step: 0.25 },
            { id: 'who', label: 'Detects', kind: 'choice', default: '2', options: detectOptions },
        ],
        build: (ctx, v) =>
            guard(() => {
                const b = new Builder(ctx);
                const wanted =
                    typeof v.quest === 'string' ? ctx.catalog.find((c) => c.resource === v.quest) : undefined;
                const withInstance = ctx.catalog
                    .filter((c) => c.rflClass === 0x68 && c.instances > 0)
                    .sort((a, c) => c.instances - a.instances)[0];
                const src = need(wanted ?? withInstance ?? pickClass(ctx.catalog, 0x68), 'quest class');
                const q = b.make(src, ctx.at, {});
                const classId = Math.round(num(v, 'classId', 0));
                if (classId > 0 && classId !== src.classId) {
                    q.resource = resourceString(src.dbIndex, classId);
                    q.class = `Quest class ${classId}`;
                    b.notes.push(`Quest object #${q.id} uses class ${classId} (body copied from ${src.name})`);
                } else b.notes.push(`Quest object #${q.id} of class ${src.name}`);
                const det = need(
                    pickClass(ctx.catalog, 0x3d, /^trigger detector$/i) ?? pickClass(ctx.catalog, 0x3d),
                    'trigger detector class',
                );
                const size: LogicVec3 = [num(v, 'w', 3), num(v, 'h', 2), num(v, 'd', 3)];
                const volume = (message: string, dx: number): void => {
                    const d = b.make(
                        det,
                        [ctx.at[0] + dx, ctx.at[1], ctx.at[2]],
                        {
                            'Trigger Message': msg(message),
                            'Detect Only Once?': 1,
                            'Detect Which?': Number(v.who ?? 2),
                        },
                        { scale: size },
                    );
                    b.link(d.id, q.id);
                    b.notes.push(`Volume #${d.id} sends ${message} to quest object #${q.id}`);
                };
                if (flag(v, 'start', true)) volume('On', 0);
                if (flag(v, 'complete', false)) volume('Quest Complete', Math.round(size[0] * 2048 + 2048));
                return b.done();
            }),
    },
    {
        id: 'level-exit',
        title: 'Level exit to...',
        summary:
            'A level transition object. The destination is part of the exit class: pick the exit class that leads where you want.',
        params: [{ id: 'exit', label: 'Exit class', kind: 'class', rfl: 0x37 }],
        build: (ctx, v) =>
            guard(() => {
                const b = new Builder(ctx);
                const wanted = typeof v.exit === 'string' ? ctx.catalog.find((c) => c.resource === v.exit) : undefined;
                const cls = need(wanted ?? pickClass(ctx.catalog, 0x37), 'level exit class');
                const o = b.make(cls, ctx.at, {});
                b.notes.push(`${cls.name} #${o.id}`);
                b.notes.push(
                    'The destination level needs an exit that leads back here (an arrival marker); Validation checks it.',
                );
                return b.done();
            }),
    },
];

export const templateById = (id: string): LogicTemplate | undefined => TEMPLATES.find((t) => t.id === id);

// ---------------------------------------------------------------------------------------------------------------------

/** One undo step: insert the new objects (with their prepared ids) and add the links to existing objects. */
export class ApplyTemplateCommand implements Command {
    private before = new Map<number, number[]>();
    readonly label: string;
    /** `what` names the setup ("Place quest object"); the history entry adds the objects it creates ("... (Quest #12)"). */
    constructor(
        private readonly build: TemplateBuild,
        what: string,
    ) {
        const made = build.objects.slice(0, 3).map((o) => `${o.class} #${o.id}`);
        const more = build.objects.length > 3 ? ` and ${build.objects.length - 3} more` : '';
        this.label = made.length ? `${what}: ${made.join(', ')}${more}` : what;
    }
    get ids(): number[] {
        return this.build.objects.map((o) => o.id);
    }
    apply(doc: LevelDocument): void {
        this.before.clear();
        for (const o of this.build.objects) doc.insert(cloneObject(o));
        for (const l of this.build.links) {
            const o = doc.get(l.from);
            if (!o) continue;
            if (!this.before.has(l.from)) this.before.set(l.from, [...(o.links ?? [])]);
            addLink(o, l.to);
        }
    }
    revert(doc: LevelDocument): void {
        for (const o of this.build.objects) doc.remove(o.id);
        for (const [id, links] of this.before) {
            const o = doc.get(id);
            if (o) o.links = links;
        }
    }
}
