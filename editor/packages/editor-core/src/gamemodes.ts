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
    /** 0 = unlimited; else lives per player: out of lives = out of the match, the last player (team) left wins. */
    lives: number;
    /** Points for killing a team mate (friendly fire on). */
    scoreTeamKill: number;
    /** Seconds a fresh spawn cannot be hurt; -1 = the server's (2). */
    spawnProtection: number;
    /** -1 = each pickup's own time (the map), 0 = never back, else seconds. */
    pickupRespawn: number;
    /** HUD team names (up to 4), empty = Fire / Water / Earth / Air. */
    teamNames: string[];
    /** HUD team colours, `rrggbb` hex (up to 4), empty = the built-in ones. */
    teamColors: string[];
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
    objectivePoints: [0, 1000],
    lives: [0, 99],
    spawnProtection: [-1, 10],
    pickupRespawn: [-1, 3600],
} as const;

export const DEFAULT_TEAM_NAMES = ['Fire', 'Water', 'Earth', 'Air'] as const;
export const DEFAULT_TEAM_COLORS = ['ce4628', '4682dc', '4caa5a', 'd2bee6'] as const;

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
        lives: 0,
        scoreTeamKill: 0,
        spawnProtection: -1,
        pickupRespawn: -1,
        teamNames: [],
        teamColors: [],
    };
}

export const cloneMode = (m: ModeDef): ModeDef => ({
    ...m,
    loadout: [...m.loadout],
    weapons: [...m.weapons],
    objectives: m.objectives.map((o) => ({ ...o })),
    teamNames: [...m.teamNames],
    teamColors: [...m.teamColors],
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
    if (m.lives) l.push(`lives = ${m.lives}`);
    if (m.scoreTeamKill) l.push(`score_team_kill = ${m.scoreTeamKill}`);
    if (m.spawnProtection >= 0) l.push(`spawn_protection = ${m.spawnProtection}`);
    if (m.pickupRespawn >= 0) l.push(`pickup_respawn = ${m.pickupRespawn === 0 ? 'never' : m.pickupRespawn}`);
    if (m.teamNames.some((s) => s.trim())) l.push(`team_names = ${m.teamNames.map((s) => oneLine(s)).join(', ')}`);
    if (m.teamColors.length) l.push(`team_colors = ${m.teamColors.join(' ')}`);
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
    range('lives', m.lives, MODE_LIMITS.lives);
    range('score_team_kill', m.scoreTeamKill, MODE_LIMITS.score);
    range('spawn_protection', m.spawnProtection, MODE_LIMITS.spawnProtection);
    range('pickup_respawn', m.pickupRespawn, MODE_LIMITS.pickupRespawn);
    if (m.teamNames.length > 4) out.push('team_names: at most 4 names');
    for (const n of m.teamNames) {
        if (n.length > 16) out.push(`team name "${n}" is longer than 16 characters`);
        if (/[,;#]/.test(n)) out.push(`team name "${n}" may not hold , ; or #`);
    }
    if (m.teamColors.length > 4) out.push('team_colors: at most 4 colours');
    for (const c of m.teamColors) if (!/^[0-9a-f]{6}$/i.test(c)) out.push(`team colour "${c}" is not rrggbb hex`);
    if (/[;#]/.test(m.name) || /[;#]/.test(m.description)) out.push('name and description may not hold ; or #');
    if (m.objectives.length > 8) out.push('at most 8 objectives');
    for (const o of m.objectives) {
        range('objective points', o.points, MODE_LIMITS.objectivePoints);
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
                | 'startHealth'
                | 'lives'
                | 'scoreTeamKill'
                | 'spawnProtection',
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
            case 'lives':
                setNum('lives');
                break;
            case 'score_team_kill':
                setNum('scoreTeamKill');
                break;
            case 'spawn_protection':
                setNum('spawnProtection');
                break;
            case 'pickup_respawn':
                if (/^map$/i.test(val)) mode.pickupRespawn = -1;
                else if (/^never$/i.test(val)) mode.pickupRespawn = 0;
                else if (/^\d+$/.test(val) && Number(val) >= 1) mode.pickupRespawn = Number(val);
                else {
                    errors.push(`${at}: pickup_respawn must be map, never or 1..3600 seconds`);
                    bad = true;
                }
                break;
            case 'team_names':
                mode.teamNames = val
                    .split(',')
                    .map((s) => s.trim())
                    .filter(Boolean);
                break;
            case 'team_colors':
            case 'team_colours':
                mode.teamColors = val.split(/\s+/).filter(Boolean);
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

/** Teams the mode plays with: 0 free for all, else 2..4 (the runtime's ModeDef::teamCount). */
export const teamCount = (m: ModeDef): number => (m.base !== 'tdm' ? 0 : m.teams >= 2 ? m.teams : 2);

const plural = (n: number, one: string, many: string): string => `${n} ${n === 1 ? one : many}`;
const minutesText = (sec: number): string =>
    sec % 60 === 0 ? plural(sec / 60, 'minute', 'minutes') : plural(sec, 'second', 'seconds');
const signed = (n: number): string => `${n >= 0 ? '+' : ''}${n}`;

/** One short line: what wins. Same text as the runtime's net::modeGoal (shown on the HUD and the scoreboard). */
export function modeGoal(m: ModeDef): string {
    const teams = teamCount(m) >= 2;
    let goal =
        m.lives > 0
            ? `${teams ? 'Last team standing' : 'Last one standing'}, ${plural(m.lives, 'life', 'lives')} each`
            : '';
    if (!goal && m.objectives.some((o) => o.kind === 'capture')) goal = 'Capture the flag';
    if (!goal && m.objectives.some((o) => o.kind === 'hold')) goal = 'Hold the zone';
    if (m.fragLimit > 0) {
        const first = `first to ${plural(m.fragLimit, 'point', 'points')}`;
        goal = goal ? `${goal}, or ${first}` : first[0]!.toUpperCase() + first.slice(1);
    }
    return goal || (m.timeLimit > 0 ? 'Most points when the time is up' : 'Score points');
}

/** The rules in plain words. Same text as the runtime's net::modeSummary (shown under the Host form). */
export function modeSummary(m: ModeDef): string {
    const teams = teamCount(m);
    let s = teams >= 2 ? `${teams} teams.` : 'Free for all.';
    if (m.lives > 0)
        s += ` ${plural(m.lives, 'life', 'lives')} each; the last ${teams >= 2 ? 'team' : 'player'} standing wins.`;
    if (m.fragLimit > 0) s += ` First to ${plural(m.fragLimit, 'point', 'points')} wins.`;
    if (m.timeLimit > 0) s += ` ${minutesText(m.timeLimit)}.`;
    if (m.scoreKill !== 1 || m.scoreDeath !== 0)
        s += ` Kill ${signed(m.scoreKill)}${m.scoreDeath ? `, death ${signed(m.scoreDeath)}` : ''}.`;
    if (teams >= 2 && m.friendlyFire)
        s += ` Friendly fire${m.scoreTeamKill ? ` (team kill ${m.scoreTeamKill > 0 ? '+' : ''}${m.scoreTeamKill})` : ''}.`;
    if (m.pickupRespawn === 0) s += ' Pickups do not come back.';
    else if (m.pickupRespawn > 0) s += ` Pickups come back after ${minutesText(m.pickupRespawn)}.`;
    if (m.weapons.length) s += ' Limited weapons.';
    if (m.loadout.length) s += ' Extra starting items.';
    return s;
}

/** What a map needs to play a mode (the runtime's modeFitsArena): the markers and room for the teams. */
export interface ModeMapInfo {
    /** Mode ids the map lists in `modes =` (dm, tdm and custom ids). */
    modes: readonly string[];
    players: number;
    markers: readonly { kind: 'flag' | 'base' | 'zone' }[];
}

/** Why a map cannot play the mode, or null when it can (same rules as the runtime's modeFitsArena). */
export function modeMapProblem(m: ModeDef, map: ModeMapInfo): string | null {
    const listed = map.modes.includes(m.id);
    if (!listed && !map.modes.includes(m.base))
        return m.base === 'tdm' ? 'the map has no team games' : 'the map has no deathmatch';
    const has = (k: 'flag' | 'base' | 'zone'): boolean => map.markers.some((x) => x.kind === k);
    if (m.objectives.some((o) => o.kind === 'capture') && !(has('flag') && has('base')))
        return 'the map has no flag and base markers';
    if (m.objectives.some((o) => o.kind === 'hold') && !has('zone')) return 'the map has no zone marker';
    if (teamCount(m) > map.players) return 'the map holds too few players for that many teams';
    return null;
}

/** Starting points for a new mode (the Game modes panel's "New from template"). */
export const MODE_TEMPLATES: readonly { key: string; label: string; hint: string; make: (id: string) => ModeDef }[] = [
    {
        key: 'last-standing',
        label: 'Last one standing',
        hint: 'A few lives each; whoever is left at the end wins',
        make: (id) => ({
            ...defaultMode(id),
            name: 'Last Rynn Standing',
            description: 'Three lives each. The last one standing wins.',
            lives: 3,
            fragLimit: 0,
            timeLimit: 300,
            pickupRespawn: 0,
        }),
    },
    {
        key: 'frag-race',
        label: 'Frag race',
        hint: 'First to a number of points; kills score, deaths cost',
        make: (id) => ({
            ...defaultMode(id),
            name: 'Frag Race',
            description: 'Two points a kill, one off for dying. First to 20.',
            scoreKill: 2,
            scoreDeath: -1,
            fragLimit: 20,
            timeLimit: 600,
            respawnDelay: 1,
        }),
    },
    {
        key: 'team-battle',
        label: 'Team battle',
        hint: 'Two named teams with their own colours, friendly fire on',
        make: (id) => ({
            ...defaultMode(id),
            name: 'Red vs Blue',
            description: 'Two teams. Watch your swing: friendly fire is on.',
            base: 'tdm',
            teams: 2,
            friendlyFire: true,
            scoreTeamKill: -1,
            fragLimit: 30,
            timeLimit: 900,
            teamNames: ['Red', 'Blue'],
            teamColors: ['c83c28', '3c78dc'],
        }),
    },
    {
        key: 'capture',
        label: 'Capture the flag',
        hint: 'Needs flag and base markers on the map',
        make: (id) => ({
            ...defaultMode(id),
            name: 'Capture the Flag',
            description: "Bring the other team's flag to your base.",
            base: 'tdm',
            teams: 2,
            scoreKill: 0,
            fragLimit: 5,
            timeLimit: 900,
            objectives: [{ kind: 'capture', points: 1, seconds: 10, radius: 300 }],
        }),
    },
    {
        key: 'hold',
        label: 'King of the hill',
        hint: 'Needs a zone marker on the map',
        make: (id) => ({
            ...defaultMode(id),
            name: 'King of the Hill',
            description: 'Stand alone in the zone to score.',
            scoreKill: 0,
            fragLimit: 10,
            timeLimit: 600,
            objectives: [{ kind: 'hold', points: 1, seconds: 15, radius: 700 }],
        }),
    },
];
