/**
 * Custom multiplayer game modes: the `[mode <id>]` descriptor of the map INI files (docs/reference/netcode.md "Custom
 * game modes"). Parsed by the runtime (runtime/net GameModes.{h,cpp}); this is the editor's twin of that parser and
 * writer. Only numbers and names; no game data.
 */

export type ModeBase = 'dm' | 'tdm';
export type ObjectiveKind = 'capture' | 'hold';

export interface ModeObjective {
    kind: ObjectiveKind;
    /** Score the team (or player, in a free-for-all) gets per completion. */
    points: number;
    /** `hold`: seconds inside the zone for one completion. */
    seconds: number;
    /** Capture / hold radius in world units around the marker. */
    radius: number;
}

export interface ModeDef {
    id: string;
    name: string;
    description: string;
    /** The engine rule set the mode builds on: `dm` free for all, `tdm` teams. */
    base: ModeBase;
    /** 0 = free for all, otherwise 2..4 teams. */
    teams: number;
    scoreKill: number;
    scoreDeath: number;
    scoreObjective: number;
    /** 0 = none. */
    fragLimit: number;
    /** Seconds, 0 = none. */
    timeLimit: number;
    respawnDelay: number;
    friendlyFire: boolean;
    /** 0 = the map's value. */
    startHealth: number;
    loadout: number[];
    /** Allowed weapon resource ids; empty = everything. */
    weapons: number[];
    objectives: ModeObjective[];
}

export const MODE_ID_PATTERN = /^[A-Za-z0-9_-]{1,32}$/;
export const BASE_MODES = ['dm', 'tdm'] as const;

export const MODE_LIMITS = {
    fragLimit: [0, 1000],
    timeLimit: [0, 7200],
    respawnDelay: [0, 60],
    score: [-1000, 1000],
    teams: [0, 4],
    startHealth: [0, 1000],
    objectiveSeconds: [1, 600],
    objectiveRadius: [50, 5000],
} as const;

export function defaultMode(id = 'my-mode'): ModeDef {
    return {
        id,
        name: 'My Mode',
        description: '',
        base: 'dm',
        teams: 0,
        scoreKill: 1,
        scoreDeath: 0,
        scoreObjective: 0,
        fragLimit: 20,
        timeLimit: 0,
        respawnDelay: 3,
        friendlyFire: false,
        startHealth: 0,
        loadout: [],
        weapons: [],
        objectives: [],
    };
}

export const cloneMode = (m: ModeDef): ModeDef => ({
    ...m,
    loadout: [...m.loadout],
    weapons: [...m.weapons],
    objectives: m.objectives.map((o) => ({ ...o })),
});

const hexId = (n: number): string => `0x${(n >>> 0).toString(16)}`;
const oneLine = (s: string): string => s.replace(/[\r\n]+/g, ' ').trim();

export function serializeMode(m: ModeDef): string {
    const l: string[] = [`[mode ${m.id}]`];
    l.push(`name = ${oneLine(m.name)}`);
    if (m.description) l.push(`description = ${oneLine(m.description)}`);
    l.push(`base = ${m.base}`, `teams = ${m.teams}`);
    l.push(`score_kill = ${m.scoreKill}`, `score_death = ${m.scoreDeath}`, `score_objective = ${m.scoreObjective}`);
    l.push(`frag_limit = ${m.fragLimit}`, `time_limit = ${m.timeLimit}`, `respawn_delay = ${m.respawnDelay}`);
    l.push(`friendly_fire = ${m.friendlyFire ? 'on' : 'off'}`);
    if (m.startHealth) l.push(`start_health = ${m.startHealth}`);
    if (m.loadout.length) l.push(`loadout = ${m.loadout.map(hexId).join(' ')}`);
    if (m.weapons.length) l.push(`weapons = ${m.weapons.map(hexId).join(' ')}`);
    for (const o of m.objectives) {
        l.push(
            `objective = ${o.kind} points=${o.points}` +
                (o.kind === 'hold' ? ` seconds=${o.seconds}` : '') +
                ` radius=${o.radius}`,
        );
    }
    return l.join('\n') + '\n';
}

const num = (s: string): number => (/^[-+]?\d+(\.\d+)?$/.test(s.trim()) ? Number(s) : NaN);
const hexList = (s: string): number[] =>
    s
        .split(/[\s,]+/)
        .filter(Boolean)
        .map((t) => (/^(0x[0-9a-f]+|\d+)$/i.test(t) ? Number(t) >>> 0 : NaN));

/** Problems with a mode's values (the runtime drops a section with any of these). */
export function validateMode(m: ModeDef): string[] {
    const out: string[] = [];
    const range = (label: string, v: number, [lo, hi]: readonly [number, number]): void => {
        if (!Number.isFinite(v) || v < lo || v > hi) out.push(`${label} must be ${lo}..${hi}`);
    };
    if (!MODE_ID_PATTERN.test(m.id)) out.push('id must be 1-32 letters, digits, - or _');
    if ((BASE_MODES as readonly string[]).includes(m.id)) out.push('id dm / tdm is reserved for the built-in modes');
    if (!m.name.trim()) out.push('name is empty');
    if (m.name.length > 40) out.push('name is longer than 40 characters');
    if (m.teams !== 0 && (m.teams < 2 || m.teams > 4)) out.push('teams must be 0 or 2..4');
    if (m.base === 'tdm' && m.teams === 0) out.push('a tdm based mode needs 2..4 teams');
    if (m.base === 'dm' && m.teams !== 0) out.push('a dm based mode has no teams (teams = 0)');
    range('score_kill', m.scoreKill, MODE_LIMITS.score);
    range('score_death', m.scoreDeath, MODE_LIMITS.score);
    range('score_objective', m.scoreObjective, MODE_LIMITS.score);
    range('frag_limit', m.fragLimit, MODE_LIMITS.fragLimit);
    range('time_limit', m.timeLimit, MODE_LIMITS.timeLimit);
    range('respawn_delay', m.respawnDelay, MODE_LIMITS.respawnDelay);
    range('start_health', m.startHealth, MODE_LIMITS.startHealth);
    for (const o of m.objectives) {
        range('objective points', o.points, MODE_LIMITS.score);
        range('objective radius', o.radius, MODE_LIMITS.objectiveRadius);
        if (o.kind === 'hold') range('objective seconds', o.seconds, MODE_LIMITS.objectiveSeconds);
    }
    return out;
}

/** Parse every `[mode <id>]` section of an INI text. Sections that fail validation are dropped with a message. */
export function parseModes(text: string, source = 'text'): { modes: ModeDef[]; errors: string[] } {
    const modes: ModeDef[] = [];
    const errors: string[] = [];
    let cur: ModeDef | null = null;
    let bad = false;
    const finish = (): void => {
        if (cur && !bad) {
            const problems = validateMode(cur);
            if (problems.length) errors.push(`${source}: mode ${cur.id}: ${problems.join('; ')}`);
            else modes.push(cur);
        }
        cur = null;
        bad = false;
    };
    text.split(/\r?\n/).forEach((raw, i) => {
        const t = raw.replace(/^\s*[;#].*$/, '').trim();
        if (!t) return;
        const sec = /^\[\s*([^\]]+?)\s*\]$/.exec(t);
        if (sec) {
            finish();
            const m = /^mode\s+(\S+)$/i.exec(sec[1]!);
            if (m) cur = { ...defaultMode(m[1]!), name: '', fragLimit: 20, timeLimit: 600, respawnDelay: 3 };
            return;
        }
        const mode = cur as ModeDef | null;
        if (!mode) return;
        const eq = t.indexOf('=');
        if (eq < 0) return;
        const key = t.slice(0, eq).trim().toLowerCase();
        const val = t.slice(eq + 1).trim();
        const at = `${source}:${i + 1}`;
        const n = num(val);
        const setNum = (
            k:
                | 'teams'
                | 'scoreKill'
                | 'scoreDeath'
                | 'scoreObjective'
                | 'fragLimit'
                | 'timeLimit'
                | 'respawnDelay'
                | 'startHealth',
        ): void => {
            if (Number.isNaN(n)) {
                errors.push(`${at}: ${key} is not a number`);
                bad = true;
            } else mode[k] = n;
        };
        switch (key) {
            case 'name':
                mode.name = val;
                break;
            case 'description':
                mode.description = val;
                break;
            case 'base':
                if (val === 'dm' || val === 'tdm') mode.base = val;
                else {
                    errors.push(`${at}: base must be dm or tdm`);
                    bad = true;
                }
                break;
            case 'teams':
                setNum('teams');
                break;
            case 'score_kill':
                setNum('scoreKill');
                break;
            case 'score_death':
                setNum('scoreDeath');
                break;
            case 'score_objective':
                setNum('scoreObjective');
                break;
            case 'frag_limit':
                setNum('fragLimit');
                break;
            case 'time_limit':
                setNum('timeLimit');
                break;
            case 'respawn_delay':
                setNum('respawnDelay');
                break;
            case 'start_health':
                setNum('startHealth');
                break;
            case 'friendly_fire':
                mode.friendlyFire = /^(on|true|yes|1)$/i.test(val);
                break;
            case 'loadout':
            case 'weapons': {
                const ids = hexList(val);
                if (ids.some(Number.isNaN)) {
                    errors.push(`${at}: ${key} holds a value that is not a resource id`);
                    bad = true;
                } else mode[key] = ids;
                break;
            }
            case 'objective': {
                const [kind, ...rest] = val.split(/\s+/);
                if (kind !== 'capture' && kind !== 'hold') {
                    errors.push(`${at}: objective kind must be capture or hold`);
                    bad = true;
                    break;
                }
                const o: ModeObjective = { kind, points: 1, seconds: 10, radius: 600 };
                for (const kv of rest) {
                    const [k, v] = kv.split('=');
                    const x = num(v ?? '');
                    if (Number.isNaN(x) || !(k === 'points' || k === 'seconds' || k === 'radius')) {
                        errors.push(`${at}: bad objective option ${kv}`);
                        bad = true;
                    } else o[k] = x;
                }
                mode.objectives.push(o);
                break;
            }
            default:
                errors.push(`${at}: unknown mode key ${key}`);
        }
    });
    finish();
    return { modes, errors };
}
