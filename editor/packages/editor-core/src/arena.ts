/**
 * Multiplayer map ("arena") authoring model: the draft the editor edits, the INI writer / reader for the format of
 * runtime/net/include/net/ArenaMaps.h (`[arena <id>]`, docs/reference/netcode.md "Multiplayer maps"), the world filter
 * preview (what the runtime removes from the campaign level) and the map validation. No DOM, no three.js.
 */
import type { LevelObject } from './document.ts';
import type { EntityKind } from './world.ts';
import { MODE_ID_PATTERN, parseModes, serializeMode, type ModeDef } from './gamemodes.ts';

export type Vec3 = [number, number, number];

export interface ArenaSpawn {
    uid: number;
    pos: Vec3;
    /** Degrees about the up axis, 0 = +Z, clockwise seen from above (authored; the game keeps the player's own heading). */
    yaw: number;
    /** 0 = any, 1..4 = team. */
    team: number;
}
export interface ArenaPickup {
    uid: number;
    pos: Vec3;
    respawn: number;
    /** Resource id (`0x3372b`); 0 = adopt the level's own loose item nearest to `pos`. */
    res: number;
    label: string;
}
export type MarkerKind = 'base' | 'flag' | 'zone';
export interface ArenaMarker {
    uid: number;
    kind: MarkerKind;
    pos: Vec3;
    radius: number;
    team: number;
    label: string;
}

export interface ArenaDraft {
    id: string;
    name: string;
    /** The campaign (or mod pack) level whose terrain the map uses (LEVELLST title). */
    level: string;
    description: string;
    /** Which game the map belongs to (`Drakan 1` / `Drakan 2`); the Multiplayer menu shows it as a tag. Empty = untagged. */
    game?: string;
    /** `dm`, `tdm` and custom mode ids. */
    modes: string[];
    players: number;
    health: number;
    /** [minX, minZ, maxX, maxZ] */
    bounds: [number, number, number, number] | null;
    loadout: number[];
    spawns: ArenaSpawn[];
    pickups: ArenaPickup[];
    markers: ArenaMarker[];
    /** Level objects (by position) that stay although the filter would remove them, and the reverse. */
    keepAt: Vec3[];
    removeAt: Vec3[];
    nextUid: number;
}

export const ARENA_ID_PATTERN = /^[A-Za-z0-9_-]{1,40}$/;
export const MAX_PLAYERS = 16;
export const MIN_SPAWNS = 2;
export const TEAM_COLORS = [0x4ade80, 0xef4444, 0x38bdf8, 0xfacc15, 0xc084fc] as const;
export const TEAM_NAMES = ['Any', 'Red', 'Blue', 'Yellow', 'Purple'] as const;
/** How far the runtime lets a spawn or pickup rest from the ground before it counts as floating. */
export const GROUND_TOLERANCE = 250;

export function newDraft(level: string): ArenaDraft {
    return {
        id: '',
        name: '',
        level,
        description: '',
        modes: ['dm', 'tdm'],
        players: 8,
        health: 100,
        bounds: null,
        loadout: [],
        spawns: [],
        pickups: [],
        markers: [],
        keepAt: [],
        removeAt: [],
        nextUid: 1,
    };
}

export const cloneDraft = (d: ArenaDraft): ArenaDraft => ({
    ...d,
    modes: [...d.modes],
    bounds: d.bounds ? [...d.bounds] : null,
    loadout: [...d.loadout],
    spawns: d.spawns.map((s) => ({ ...s, pos: [...s.pos] })),
    pickups: d.pickups.map((p) => ({ ...p, pos: [...p.pos] })),
    markers: d.markers.map((m) => ({ ...m, pos: [...m.pos] })),
    keepAt: d.keepAt.map((p) => [...p]),
    removeAt: d.removeAt.map((p) => [...p]),
});

/** `Surdana Village` -> `surdana-village`. */
export function slugify(name: string): string {
    return name
        .toLowerCase()
        .replace(/[^a-z0-9]+/g, '-')
        .replace(/^-+|-+$/g, '')
        .slice(0, 40);
}

/** The file name the multiplayer menu looks a preview up by: the map NAME with anything but letters, digits, - _ as `_`. */
export const previewFileName = (name: string): string => `${name.replace(/[^A-Za-z0-9_-]/g, '_')}.png`;

const r1 = (n: number): number => Math.round(n * 10) / 10;
const ri = (n: number): number => Math.round(n);
const hexId = (n: number): string => `0x${(n >>> 0).toString(16)}`;
const oneLine = (s: string): string => s.replace(/[\r\n]+/g, ' ').trim();
const vec = (p: readonly number[]): string => `${ri(p[0]!)} ${ri(p[1]!)} ${ri(p[2]!)}`;

/** The `[arena <id>]` section. */
export function serializeArena(d: ArenaDraft): string {
    const l: string[] = [`[arena ${d.id}]`, `name = ${oneLine(d.name)}`, `level = ${oneLine(d.level)}`];
    if (d.description) l.push(`description = ${oneLine(d.description)}`);
    if (d.game) l.push(`game = ${oneLine(d.game)}`);
    l.push(`modes = ${d.modes.join(', ')}`, `players = ${d.players}`, `health = ${d.health}`);
    if (d.bounds) l.push(`bounds = ${d.bounds.map(ri).join(' ')}`);
    if (d.loadout.length) l.push(`loadout = ${d.loadout.map(hexId).join(' ')}`);
    for (const s of d.spawns) l.push(`spawn = ${vec(s.pos)} ${r1(s.yaw)}${s.team ? ` team=${s.team}` : ''}`);
    for (const p of d.pickups) {
        l.push(
            `pickup = ${vec(p.pos)} ${ri(p.respawn)}${p.res ? ` res=${hexId(p.res)}` : ''}${p.label ? ` name=${oneLine(p.label)}` : ''}`,
        );
    }
    for (const m of d.markers) {
        l.push(
            `marker = ${m.kind} ${vec(m.pos)} radius=${ri(m.radius)}${m.team ? ` team=${m.team}` : ''}${m.label ? ` name=${oneLine(m.label)}` : ''}`,
        );
    }
    for (const p of d.keepAt) l.push(`keep = ${vec(p)}`);
    for (const p of d.removeAt) l.push(`remove = ${vec(p)}`);
    return l.join('\n') + '\n';
}

/** The whole `maps/<id>.ini`: a header comment, the arena and the custom modes it uses. */
export function serializeMapFile(d: ArenaDraft, modes: readonly ModeDef[]): string {
    const used = modes.filter((m) => d.modes.includes(m.id));
    const head = `; ${d.name || d.id}: multiplayer map written by the Drakan editor (docs/reference/netcode.md).\n`;
    return [head, serializeArena(d), ...used.map(serializeMode)].join('\n');
}

const numTok = (s: string | undefined): number => (s !== undefined && /^[-+]?\d+(\.\d+)?$/.test(s) ? Number(s) : NaN);
const hexTok = (s: string): number => (/^(0x[0-9a-f]+|\d+)$/i.test(s) ? Number(s) >>> 0 : NaN);

/** Read the arenas (and custom modes) of an INI text. Lenient: a bad line is reported and skipped. */
export function parseArenaIni(
    text: string,
    source = 'text',
): { arenas: ArenaDraft[]; modes: ModeDef[]; errors: string[] } {
    const arenas: ArenaDraft[] = [];
    const errors: string[] = [];
    let cur: ArenaDraft | null = null;
    text.split(/\r?\n/).forEach((raw, i) => {
        const t = raw.replace(/^\s*[;#].*$/, '').trim();
        if (!t) return;
        const sec = /^\[\s*([^\]]+?)\s*\]$/.exec(t);
        if (sec) {
            const m = /^arena\s+(\S+)$/i.exec(sec[1]!);
            cur = m ? { ...newDraft(''), id: m[1]! } : null;
            if (cur) arenas.push(cur);
            return;
        }
        const d = cur as ArenaDraft | null;
        if (!d) return;
        const eq = t.indexOf('=');
        if (eq < 0) return;
        const key = t.slice(0, eq).trim().toLowerCase();
        const val = t.slice(eq + 1).trim();
        const at = `${source}:${i + 1}`;
        const words = val.split(/\s+/);
        const opts = new Map<string, string>();
        const pos: string[] = [];
        for (const w of words) {
            const k = w.indexOf('=');
            if (k > 0) opts.set(w.slice(0, k).toLowerCase(), w.slice(k + 1));
            else pos.push(w);
        }
        // name=Two Words keeps its tail: take everything after `name=` verbatim.
        const nameAt = val.search(/\bname=/i);
        const label = nameAt >= 0 ? val.slice(nameAt + 5).trim() : '';
        const p3 = (): Vec3 | null => {
            const v = [numTok(pos[0]), numTok(pos[1]), numTok(pos[2])];
            return v.some(Number.isNaN) ? null : (v as Vec3);
        };
        switch (key) {
            case 'name':
                d.name = val;
                break;
            case 'level':
                d.level = val;
                break;
            case 'description':
                d.description = val;
                break;
            case 'game':
                d.game = val;
                break;
            case 'modes':
                d.modes = val
                    .split(/[\s,]+/)
                    .filter(Boolean)
                    .map((s) => s.toLowerCase());
                break;
            case 'players':
                d.players = numTok(val) || 8;
                break;
            case 'health':
                d.health = numTok(val) || 100;
                break;
            case 'bounds': {
                const b = pos.map(numTok);
                if (b.length === 4 && !b.some(Number.isNaN)) d.bounds = b as ArenaDraft['bounds'] & number[];
                else errors.push(`${at}: bounds needs four numbers`);
                break;
            }
            case 'loadout':
                d.loadout = val
                    .split(/[\s,]+/)
                    .filter(Boolean)
                    .map(hexTok)
                    .filter(Number.isFinite);
                break;
            case 'spawn': {
                const p = p3();
                if (!p) errors.push(`${at}: spawn needs x y z`);
                else {
                    d.spawns.push({
                        uid: d.nextUid++,
                        pos: p,
                        yaw: Number.isNaN(numTok(pos[3])) ? 0 : numTok(pos[3]),
                        team: Number(opts.get('team') ?? 0) || 0,
                    });
                }
                break;
            }
            case 'pickup': {
                const p = p3();
                if (!p) errors.push(`${at}: pickup needs x y z seconds`);
                else {
                    d.pickups.push({
                        uid: d.nextUid++,
                        pos: p,
                        respawn: Number.isNaN(numTok(pos[3])) ? 30 : numTok(pos[3]),
                        res: opts.has('res') ? hexTok(opts.get('res')!) || 0 : 0,
                        label,
                    });
                }
                break;
            }
            case 'marker': {
                const kind = pos.shift();
                const p = p3();
                if (!p || (kind !== 'base' && kind !== 'flag' && kind !== 'zone')) {
                    errors.push(`${at}: marker needs base|flag|zone x y z`);
                } else {
                    d.markers.push({
                        uid: d.nextUid++,
                        kind,
                        pos: p,
                        radius: Number(opts.get('radius')) || (kind === 'flag' ? 150 : 600),
                        team: Number(opts.get('team') ?? 0) || 0,
                        label,
                    });
                }
                break;
            }
            case 'keep':
            case 'remove': {
                const p = p3();
                if (!p) errors.push(`${at}: ${key} needs x y z`);
                else (key === 'keep' ? d.keepAt : d.removeAt).push(p);
                break;
            }
            default:
                errors.push(`${at}: unknown key ${key}`);
        }
    });
    const m = parseModes(text, source);
    return { arenas, modes: m.modes, errors: [...errors, ...m.errors] };
}

// ---- world filter preview ------------------------------------------------------------------------------------------

export type FilterReason =
    | 'creature'
    | 'cutscene'
    | 'conversation'
    | 'movie'
    | 'exit'
    | 'quest'
    | 'treasure'
    | 'chest'
    | 'loose-item'
    | 'manual';

/** What the runtime removes, in the order of netcode.md "What counts as singleplayer only". `rfl` is the engine class. */
export const FILTER_REASONS: { reason: FilterReason; label: string; rfl: number | null; note: string }[] = [
    { reason: 'creature', label: 'Creatures and NPCs', rfl: null, note: 'every actor with creature hit points' },
    { reason: 'cutscene', label: 'Cut scene player', rfl: 0x33, note: 'class 0x10033' },
    { reason: 'conversation', label: 'Conversation handlers', rfl: 0xb4, note: 'class 0x100b4' },
    { reason: 'movie', label: 'Movie player', rfl: 0x16, note: 'class 0x10016' },
    { reason: 'exit', label: 'Level exits', rfl: 0x37, note: 'class 0x10037 (Level Transition, Portal)' },
    { reason: 'quest', label: 'Quest items', rfl: 0x21, note: 'class 0x10021' },
    { reason: 'treasure', label: 'Treasure', rfl: 0x6d, note: 'class 0x1006d' },
    { reason: 'chest', label: 'Chests', rfl: 0xc5, note: 'class 0x100c5' },
    {
        reason: 'loose-item',
        label: 'Loose weapons and items',
        rfl: null,
        note: 'swords, bows, potions...; kept within 2500 units of the player start and at map pickups',
    },
    {
        reason: 'manual',
        label: 'Marked by you',
        rfl: null,
        note: 'remove = x y z entries (authored; not read by the game yet)',
    },
];
const RFL_REASON = new Map(FILTER_REASONS.filter((r) => r.rfl !== null).map((r) => [r.rfl!, r.reason]));
/** Own gear near the player start is kept (netcode.md: within 2500 units). */
export const OWN_GEAR_RADIUS = 2500;
/** A pickup without `res=` adopts the level item nearest to it within this distance. */
export const ADOPT_RADIUS = 600;
/** An override matches the object within this distance (x, y, z). */
export const OVERRIDE_RADIUS = 50;

export interface ClassInfo {
    rflClass: number | null;
    kind: EntityKind;
}

export interface FilterEntry {
    reason: FilterReason;
    /** The runtime filter would remove it, but a `keep` entry saves it. */
    kept: boolean;
    /** Removed because of the map's own pickup list (adopted items are not removed). */
    auto: boolean;
}

const dist3 = (a: readonly number[], b: readonly number[]): number =>
    Math.hypot(a[0]! - b[0]!, a[1]! - b[1]!, a[2]! - b[2]!);
const dist2 = (a: readonly number[], b: readonly number[]): number => Math.hypot(a[0]! - b[0]!, a[2]! - b[2]!);

export function overrideIndex(list: readonly Vec3[], p: readonly number[]): number {
    return list.findIndex((q) => dist3(q, p) <= OVERRIDE_RADIUS);
}

/**
 * Class names that sound like creatures but are helper points (animation way points, spawn markers): the engine gives
 * them no creature hit points, so the filter keeps them. Posed skeletons are real actors in the game's count.
 */
const NOT_ACTOR = /way\s*point|animway|\bwp\b|spawn|marker/i;

/** The filter reason of one object by class alone (no map context), or null. */
export function baseFilterReason(o: LevelObject, info: ClassInfo | undefined): FilterReason | null {
    if (info?.rflClass != null) {
        const r = RFL_REASON.get(info.rflClass);
        if (r) return r;
    }
    const kind = info?.kind ?? o.kind;
    if (kind === 'creature') return NOT_ACTOR.test(o.class) ? null : 'creature';
    if (kind === 'weapon') return 'loose-item';
    return null;
}

export interface FilterPlan {
    entries: Map<number, FilterEntry>;
    /** Objects removed (reason, not kept) per reason. */
    removed: Map<FilterReason, number>;
    keptCount: number;
}

/**
 * Which objects of the level the arena mode removes, following the runtime filter. Approximate for creatures and
 * loose items (the engine decides by hit points and entity type; the editor goes by class name and kind).
 */
export function planArenaFilter(
    objects: Iterable<LevelObject>,
    infoOf: (o: LevelObject) => ClassInfo | undefined,
    draft: ArenaDraft,
): FilterPlan {
    const list = [...objects];
    const start = list.find((o) => (infoOf(o)?.kind ?? o.kind) === 'player' || o.kind === 'player');
    const entries = new Map<number, FilterEntry>();
    const removed = new Map<FilterReason, number>();
    let keptCount = 0;
    for (const o of list) {
        const p = o.transform.pos;
        let reason = baseFilterReason(o, infoOf(o));
        let auto = true;
        if (reason === 'loose-item') {
            const ownGear = start && dist3(start.transform.pos, p) <= OWN_GEAR_RADIUS;
            const atPickup = draft.pickups.some((k) => !k.res && dist3(k.pos, p) <= ADOPT_RADIUS);
            if (ownGear || atPickup) reason = null;
        }
        if (reason === null && overrideIndex(draft.removeAt, p) >= 0) {
            reason = 'manual';
            auto = false;
        }
        if (reason === null) continue;
        const kept = overrideIndex(draft.keepAt, p) >= 0;
        entries.set(o.id, { reason, kept, auto });
        if (kept) keptCount++;
        else removed.set(reason, (removed.get(reason) ?? 0) + 1);
    }
    return { entries, removed, keptCount };
}

// ---- validation ----------------------------------------------------------------------------------------------------

export type ArenaTarget = { kind: 'map' } | { kind: 'bounds' } | { kind: 'spawn' | 'pickup' | 'marker'; uid: number };

export interface ArenaIssue {
    severity: 'error' | 'warning' | 'info';
    code: string;
    message: string;
    target: ArenaTarget;
}

export interface ValidateArenaEnv {
    /** Terrain height under (x, z); null = no terrain there (a void) or none loaded. */
    groundAt(x: number, z: number): number | null;
    /** A terrain is loaded at all (without one the ground checks are skipped). */
    hasGround: boolean;
    /** Custom modes known to the editor. */
    modes: readonly ModeDef[];
    /** Loose items of the level (for adopted pickups). */
    items?: readonly Vec3[];
    /** Skip the walkability flood fill (it samples the terrain a few thousand times). */
    reach?: boolean;
}

export const insideBounds = (b: ArenaDraft['bounds'], x: number, z: number): boolean =>
    !b ||
    (x >= Math.min(b[0], b[2]) && x <= Math.max(b[0], b[2]) && z >= Math.min(b[1], b[3]) && z <= Math.max(b[1], b[3]));

/** Spawns closer than this are flagged (two players appearing inside each other). */
export const MIN_SPAWN_SPACING = 400;
/** Height step between neighbouring samples that a walking player is assumed unable to climb. */
export const MAX_STEP = 450;
const CELL = 500;

/** Cells of the bounds grid reachable on foot from the spawns (coarse: slope steps, no jumping, no swimming). */
export function reachableCells(
    b: NonNullable<ArenaDraft['bounds']>,
    starts: readonly Vec3[],
    groundAt: (x: number, z: number) => number | null,
    maxStep = MAX_STEP,
): { cell: number; w: number; h: number; x0: number; z0: number; seen: Uint8Array } {
    const x0 = Math.min(b[0], b[2]);
    const z0 = Math.min(b[1], b[3]);
    const w = Math.max(1, Math.min(160, Math.ceil(Math.abs(b[2] - b[0]) / CELL)));
    const h = Math.max(1, Math.min(160, Math.ceil(Math.abs(b[3] - b[1]) / CELL)));
    const cell = Math.max(CELL, Math.abs(b[2] - b[0]) / w, Math.abs(b[3] - b[1]) / h);
    const seen = new Uint8Array(w * h);
    const hgt = new Map<number, number | null>();
    const heightOf = (i: number): number | null => {
        if (!hgt.has(i)) hgt.set(i, groundAt(x0 + ((i % w) + 0.5) * cell, z0 + (Math.floor(i / w) + 0.5) * cell));
        return hgt.get(i)!;
    };
    const queue: number[] = [];
    for (const s of starts) {
        const cx = Math.floor((s[0] - x0) / cell);
        const cz = Math.floor((s[2] - z0) / cell);
        if (cx < 0 || cz < 0 || cx >= w || cz >= h) continue;
        const i = cz * w + cx;
        if (!seen[i]) {
            seen[i] = 1;
            queue.push(i);
        }
    }
    while (queue.length) {
        const i = queue.pop()!;
        const hi = heightOf(i);
        if (hi === null) continue;
        const cx = i % w;
        const cz = Math.floor(i / w);
        for (const [dx, dz] of [
            [1, 0],
            [-1, 0],
            [0, 1],
            [0, -1],
        ] as const) {
            const nx = cx + dx;
            const nz = cz + dz;
            if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
            const j = nz * w + nx;
            if (seen[j]) continue;
            const hj = heightOf(j);
            if (hj === null || Math.abs(hj - hi) > maxStep) continue;
            seen[j] = 1;
            queue.push(j);
        }
    }
    return { cell, w, h, x0, z0, seen };
}

export function validateArena(d: ArenaDraft, env: ValidateArenaEnv): ArenaIssue[] {
    const out: ArenaIssue[] = [];
    const add = (severity: ArenaIssue['severity'], code: string, message: string, target: ArenaTarget): void => {
        out.push({ severity, code, message, target });
    };
    const map: ArenaTarget = { kind: 'map' };
    if (!ARENA_ID_PATTERN.test(d.id)) add('error', 'bad-id', 'Map id must be 1-40 letters, digits, - or _', map);
    if (!d.name.trim()) add('error', 'no-name', 'The map has no name', map);
    else if (d.name.length > 64) add('error', 'long-name', 'The name is longer than 64 characters', map);
    if (!d.level.trim()) add('error', 'no-level', 'The map has no base level', map);
    if (!Number.isInteger(d.players) || d.players < 2 || d.players > MAX_PLAYERS) {
        add('error', 'players', `Max players must be 2..${MAX_PLAYERS}`, map);
    }
    if (!(d.health > 0)) add('error', 'health', 'Start health must be above 0', map);
    if (!d.modes.length) add('error', 'modes', 'Pick at least one supported mode', map);
    for (const id of d.modes) {
        if (id !== 'dm' && id !== 'tdm' && !MODE_ID_PATTERN.test(id))
            add('error', 'mode-id', `Mode id "${id}" is invalid`, map);
        else if (id !== 'dm' && id !== 'tdm' && !env.modes.some((m) => m.id === id)) {
            add(
                'warning',
                'unknown-mode',
                `Mode "${id}" is not defined here; export includes only the modes you created`,
                map,
            );
        }
    }
    if (!d.bounds) {
        add('warning', 'no-bounds', 'No bounds: players are never sent back when they leave the playable area', {
            kind: 'bounds',
        });
    } else if (Math.abs(d.bounds[2] - d.bounds[0]) < 2000 || Math.abs(d.bounds[3] - d.bounds[1]) < 2000) {
        add('warning', 'tiny-bounds', 'The bounds are smaller than 2000 x 2000 units', { kind: 'bounds' });
    }

    // spawns
    if (d.spawns.length < MIN_SPAWNS) {
        add('error', 'few-spawns', `Needs at least ${MIN_SPAWNS} spawn points (has ${d.spawns.length})`, map);
    } else if (d.spawns.length < d.players) {
        add(
            'warning',
            'spawns-vs-players',
            `${d.spawns.length} spawn points for up to ${d.players} players: late spawners share points (the server picks the one farthest from the others)`,
            map,
        );
    }
    const teamModes = d.modes.some((id) => id === 'tdm' || env.modes.find((m) => m.id === id)?.teams);
    const maxTeams = Math.max(2, ...d.modes.map((id) => env.modes.find((m) => m.id === id)?.teams ?? 0));
    if (teamModes && d.spawns.length) {
        for (let t = 1; t <= maxTeams; t++) {
            if (!d.spawns.some((s) => s.team === t) && d.spawns.some((s) => s.team !== 0)) {
                add('warning', 'team-spawns', `${TEAM_NAMES[t]} has no spawn point of its own`, map);
            }
        }
    }
    d.spawns.forEach((s, i) => {
        const t: ArenaTarget = { kind: 'spawn', uid: s.uid };
        const name = `Spawn ${i + 1}`;
        if (!insideBounds(d.bounds, s.pos[0], s.pos[2]))
            add('error', 'spawn-outside', `${name} is outside the bounds`, t);
        if (env.hasGround) {
            const g = env.groundAt(s.pos[0], s.pos[2]);
            if (g === null) add('error', 'spawn-void', `${name} is over no terrain`, t);
            else if (Math.abs(s.pos[1] - g) > GROUND_TOLERANCE) {
                add(
                    'warning',
                    'spawn-floating',
                    `${name} is ${Math.round(s.pos[1] - g)} units from the ground (use Snap to ground)`,
                    t,
                );
            }
        }
        for (let j = i + 1; j < d.spawns.length; j++) {
            const o = d.spawns[j]!;
            if (dist2(s.pos, o.pos) < MIN_SPAWN_SPACING) {
                add(
                    'warning',
                    'spawn-spacing',
                    `${name} and spawn ${j + 1} are closer than ${MIN_SPAWN_SPACING} units`,
                    t,
                );
            }
        }
    });

    // pickups
    d.pickups.forEach((p, i) => {
        const t: ArenaTarget = { kind: 'pickup', uid: p.uid };
        const name = `Pickup ${i + 1}${p.label ? ` (${p.label})` : ''}`;
        if (!insideBounds(d.bounds, p.pos[0], p.pos[2]))
            add('error', 'pickup-outside', `${name} is outside the bounds`, t);
        if (!(p.respawn >= 1 && p.respawn <= 3600))
            add('error', 'pickup-respawn', `${name}: respawn must be 1..3600 seconds`, t);
        if (!p.res) {
            if (env.items && !env.items.some((q) => dist3(q, p.pos) <= ADOPT_RADIUS)) {
                add(
                    'error',
                    'pickup-adopt',
                    `${name} adopts the level's loose item, but none lies within ${ADOPT_RADIUS} units; pick an item`,
                    t,
                );
            }
        }
        if (env.hasGround) {
            const g = env.groundAt(p.pos[0], p.pos[2]);
            if (g === null) add('error', 'pickup-void', `${name} is over no terrain`, t);
            else if (Math.abs(p.pos[1] - g) > GROUND_TOLERANCE * 2) {
                add('warning', 'pickup-floating', `${name} is ${Math.round(p.pos[1] - g)} units from the ground`, t);
            }
        }
    });
    if (!d.pickups.length) add('info', 'no-pickups', 'No pickups: players only have their loadout', map);

    // markers versus modes
    const modes = d.modes.map((id) => env.modes.find((m) => m.id === id)).filter((m): m is ModeDef => !!m);
    const has = (kind: MarkerKind, team?: number): boolean =>
        d.markers.some((m) => m.kind === kind && (team === undefined || m.team === team || m.team === 0));
    for (const m of modes) {
        for (const o of m.objectives) {
            if (o.kind === 'capture') {
                for (let t = 1; t <= Math.max(2, m.teams); t++) {
                    if (!has('flag', t))
                        add(
                            'error',
                            'no-flag',
                            `Mode ${m.id} captures flags: ${TEAM_NAMES[t]} has no flag marker`,
                            map,
                        );
                    if (!has('base', t))
                        add(
                            'error',
                            'no-base',
                            `Mode ${m.id} captures flags: ${TEAM_NAMES[t]} has no base marker`,
                            map,
                        );
                }
            } else if (!has('zone'))
                add('error', 'no-zone', `Mode ${m.id} has a hold objective but the map has no zone marker`, map);
        }
    }
    for (const m of d.markers) {
        const t: ArenaTarget = { kind: 'marker', uid: m.uid };
        if (!insideBounds(d.bounds, m.pos[0], m.pos[2]))
            add('error', 'marker-outside', `${m.kind} marker ${m.label || m.uid} is outside the bounds`, t);
    }

    // reachability on foot
    if (env.reach !== false && env.hasGround && d.bounds && d.spawns.length) {
        const r = reachableCells(
            d.bounds,
            d.spawns.map((s) => s.pos),
            env.groundAt,
        );
        const reached = (p: Vec3): boolean => {
            const cx = Math.floor((p[0] - r.x0) / r.cell);
            const cz = Math.floor((p[2] - r.z0) / r.cell);
            return cx >= 0 && cz >= 0 && cx < r.w && cz < r.h && r.seen[cz * r.w + cx] === 1;
        };
        for (const [i, p] of d.pickups.entries()) {
            if (!reached(p.pos)) {
                add(
                    'warning',
                    'pickup-unreachable',
                    `Pickup ${i + 1}${p.label ? ` (${p.label})` : ''} may not be reachable on foot from the spawn points (steps over ${MAX_STEP} units, cliffs and water are not crossed; jumping is not modelled)`,
                    { kind: 'pickup', uid: p.uid },
                );
            }
        }
        for (const [i, s] of d.spawns.entries()) {
            if (i > 0 && !reached(s.pos) && d.spawns.some((o) => o !== s && reached(o.pos))) {
                add(
                    'info',
                    'spawn-island',
                    `Spawn ${i + 1} sits on a separate island of terrain from the other spawns`,
                    { kind: 'spawn', uid: s.uid },
                );
            }
        }
    }
    return out;
}

/**
 * Spawn point suggestions: flat terrain inside the bounds, spread out by farthest-point sampling starting from the
 * points already placed. Heights come from `groundAt`; a candidate over no terrain or on a slope is skipped.
 */
export function suggestSpawns(
    bounds: NonNullable<ArenaDraft['bounds']>,
    groundAt: (x: number, z: number) => number | null,
    count: number,
    existing: readonly Vec3[] = [],
): Vec3[] {
    const x0 = Math.min(bounds[0], bounds[2]) + 600;
    const x1 = Math.max(bounds[0], bounds[2]) - 600;
    const z0 = Math.min(bounds[1], bounds[3]) + 600;
    const z1 = Math.max(bounds[1], bounds[3]) - 600;
    if (x1 <= x0 || z1 <= z0 || count <= 0) return [];
    const step = Math.max(400, Math.max(x1 - x0, z1 - z0) / 48);
    const cand: Vec3[] = [];
    for (let x = x0; x <= x1; x += step) {
        for (let z = z0; z <= z1; z += step) {
            const h = groundAt(x, z);
            if (h === null) continue;
            const around = [groundAt(x + 300, z), groundAt(x - 300, z), groundAt(x, z + 300), groundAt(x, z - 300)];
            if (around.some((a) => a === null || Math.abs(a - h) > 150)) continue;
            cand.push([Math.round(x), Math.round(h), Math.round(z)]);
        }
    }
    const chosen: Vec3[] = [];
    const placed: Vec3[] = [...existing];
    while (chosen.length < count && cand.length) {
        let best = -1;
        let bestD = -1;
        for (let i = 0; i < cand.length; i++) {
            const c = cand[i]!;
            const d = placed.length
                ? Math.min(...placed.map((p) => dist2(p, c)))
                : -dist2([(x0 + x1) / 2, 0, (z0 + z1) / 2], c);
            if (d > bestD) {
                bestD = d;
                best = i;
            }
        }
        const [c] = cand.splice(best, 1);
        chosen.push(c!);
        placed.push(c!);
    }
    return chosen;
}

/**
 * Default arena bounds for a level: the middle 96% of the object positions (objects parked at the world origin and
 * far outliers ignored), padded by 500 units. Null when the level has too few positioned objects.
 */
export function defaultArenaBounds(positions: Iterable<readonly number[]>): [number, number, number, number] | null {
    const xs: number[] = [];
    const zs: number[] = [];
    for (const [x, y, z] of positions) {
        if (Math.abs(x!) + Math.abs(y!) + Math.abs(z!) < 1000) continue;
        xs.push(x!);
        zs.push(z!);
    }
    if (xs.length < 4) return null;
    xs.sort((a, b) => a - b);
    zs.sort((a, b) => a - b);
    const q = (a: number[], p: number): number => a[Math.min(a.length - 1, Math.max(0, Math.floor(a.length * p)))]!;
    const pad = 500;
    return [
        Math.round(q(xs, 0.02) - pad),
        Math.round(q(zs, 0.02) - pad),
        Math.round(q(xs, 0.98) + pad),
        Math.round(q(zs, 0.98) + pad),
    ];
}
