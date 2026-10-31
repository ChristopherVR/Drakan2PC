// Synthetic only: multiplayer map drafts, the INI writer / reader, the world filter plan, validation, game modes.
import { describe, expect, it } from 'vitest';
import {
    cloneDraft,
    defaultMode,
    newDraft,
    parseArenaIni,
    parseModes,
    planArenaFilter,
    previewFileName,
    reachableCells,
    serializeArena,
    serializeMapFile,
    serializeMode,
    slugify,
    validateArena,
    validateMode,
    type ArenaDraft,
    type ClassInfo,
    type LevelObject,
    type ModeDef,
    type ValidateArenaEnv,
} from '../src/index.ts';

const obj = (id: number, cls: string, kind: LevelObject['kind'], pos: [number, number, number]): LevelObject => ({
    id,
    class: cls,
    type: 0,
    kind,
    resource: `0x${id}`,
    model: '',
    transform: { pos, rot: [0, 0, 0] },
    props: {},
});

function sample(): ArenaDraft {
    const d = newDraft('Level 1A - Surdana');
    d.id = 'test-yard';
    d.name = 'Test Yard';
    d.description = 'A small yard.';
    d.players = 4;
    d.bounds = [0, 0, 10000, 10000];
    d.loadout = [0x3372b];
    d.spawns = [
        { uid: 1, pos: [1000, 100, 1000], yaw: 90, team: 1 },
        { uid: 2, pos: [9000, 100, 9000], yaw: 270, team: 2 },
    ];
    d.pickups = [
        { uid: 3, pos: [5000, 100, 5000], respawn: 30, res: 0x31f30, label: 'Great Sword' },
        { uid: 4, pos: [4000, 100, 5000], respawn: 20, res: 0, label: '' },
    ];
    d.markers = [{ uid: 5, kind: 'zone', pos: [5000, 100, 5200], radius: 700, team: 0, label: 'Hill' }];
    d.keepAt = [[1, 2, 3]];
    d.removeAt = [[4, 5, 6]];
    d.nextUid = 6;
    return d;
}

describe('arena INI', () => {
    it('writes the documented keys', () => {
        const t = serializeArena(sample());
        expect(t).toContain('[arena test-yard]\nname = Test Yard\nlevel = Level 1A - Surdana\n');
        expect(t).toContain(
            'modes = dm, tdm\nplayers = 4\nhealth = 100\nbounds = 0 0 10000 10000\nloadout = 0x3372b\n',
        );
        expect(t).toContain('spawn = 1000 100 1000 90 team=1\n');
        expect(t).toContain('pickup = 5000 100 5000 30 res=0x31f30 name=Great Sword\n');
        expect(t).toContain('pickup = 4000 100 5000 20\n');
        expect(t).toContain('marker = zone 5000 100 5200 radius=700 name=Hill\n');
        expect(t).toContain('keep = 1 2 3\nremove = 4 5 6\n');
    });

    it('round trips through the reader', () => {
        const d = sample();
        const r = parseArenaIni(serializeArena(d));
        expect(r.errors).toEqual([]);
        const back = r.arenas[0]!;
        expect(back.id).toBe('test-yard');
        expect(back.spawns.map((s) => [s.pos, s.yaw, s.team])).toEqual(d.spawns.map((s) => [s.pos, s.yaw, s.team]));
        expect(back.pickups.map((p) => [p.pos, p.respawn, p.res, p.label])).toEqual(
            d.pickups.map((p) => [p.pos, p.respawn, p.res, p.label]),
        );
        expect(back.markers[0]).toMatchObject({ kind: 'zone', radius: 700, label: 'Hill' });
        expect(back.bounds).toEqual([0, 0, 10000, 10000]);
        expect(back.keepAt).toEqual([[1, 2, 3]]);
        expect(serializeArena(back)).toBe(serializeArena(d));
    });

    it('reads the compiled-in format (no yaw, no team, comments, name with spaces)', () => {
        const r = parseArenaIni(
            '; c\n[arena a]\nname = A\nlevel = L\nspawn = 1 2 3\npickup = 1 2 3 30 res=0x33732 name=Iron Axe\nbogus = 1\n',
        );
        expect(r.arenas[0]!.spawns[0]).toMatchObject({ yaw: 0, team: 0 });
        expect(r.arenas[0]!.pickups[0]).toMatchObject({ res: 0x33732, label: 'Iron Axe' });
        expect(r.errors).toEqual(['text:7: unknown key bogus']);
    });

    it('slugs and preview names', () => {
        expect(slugify('Surdana Village!')).toBe('surdana-village');
        expect(previewFileName('Surdana Village')).toBe('Surdana_Village.png');
    });

    it('map file carries the modes the map uses', () => {
        const d = sample();
        d.modes = ['dm', 'koth'];
        const koth: ModeDef = { ...defaultMode('koth'), name: 'King of the Hill' };
        const other: ModeDef = { ...defaultMode('other'), name: 'Other' };
        const text = serializeMapFile(d, [koth, other]);
        expect(text).toContain('[mode koth]');
        expect(text).not.toContain('[mode other]');
        expect(parseArenaIni(text).modes.map((m) => m.id)).toEqual(['koth']);
    });
});

describe('arena filter plan', () => {
    const classes = new Map<string, ClassInfo>([
        ['exit', { rflClass: 0x37, kind: 'usable' }],
        ['chest', { rflClass: 0xc5, kind: 'weapon' }],
        ['orc', { rflClass: null, kind: 'creature' }],
        ['sword', { rflClass: null, kind: 'weapon' }],
        ['tree', { rflClass: null, kind: 'static' }],
        ['rynn', { rflClass: null, kind: 'player' }],
        ['Troll PlateAnimWay Point', { rflClass: null, kind: 'creature' }],
    ]);
    const objs = [
        obj(1, 'exit', 'usable', [0, 0, 0]),
        obj(2, 'chest', 'weapon', [300, 0, 0]),
        obj(3, 'orc', 'creature', [600, 0, 0]),
        obj(4, 'sword', 'weapon', [50000, 0, 0]),
        obj(5, 'sword', 'weapon', [1000, 0, 0]),
        obj(6, 'tree', 'static', [900, 0, 0]),
        obj(7, 'rynn', 'player', [0, 0, 100]),
        obj(8, 'sword', 'weapon', [80000, 0, 0]),
        obj(9, 'Troll PlateAnimWay Point', 'creature', [700, 0, 0]),
    ];
    const info = (o: LevelObject): ClassInfo | undefined => classes.get(o.class);

    it('removes creatures, exits, chests and distant loose items', () => {
        const d = newDraft('L');
        const p = planArenaFilter(objs, info, d);
        expect([...p.entries].map(([id, e]) => [id, e.reason])).toEqual([
            [1, 'exit'],
            [2, 'chest'],
            [3, 'creature'],
            [4, 'loose-item'],
            [8, 'loose-item'],
        ]);
        expect(p.removed.get('loose-item')).toBe(2);
    });

    it('keeps adopted pickups, honours keep / remove entries', () => {
        const d = newDraft('L');
        d.pickups = [{ uid: 1, pos: [50000, 0, 0], respawn: 30, res: 0, label: '' }];
        d.keepAt = [[600, 0, 0]];
        d.removeAt = [[900, 0, 0]];
        const p = planArenaFilter(objs, info, d);
        expect(p.entries.has(4)).toBe(false);
        expect(p.entries.get(3)).toMatchObject({ reason: 'creature', kept: true });
        expect(p.entries.get(6)).toMatchObject({ reason: 'manual', kept: false, auto: false });
        expect(p.keptCount).toBe(1);
    });
});

describe('arena validation', () => {
    const flat = (_x: number, _z: number): number | null => 100;
    const env = (over: Partial<ValidateArenaEnv> = {}): ValidateArenaEnv => ({
        groundAt: flat,
        hasGround: true,
        modes: [],
        reach: true,
        ...over,
    });
    const codes = (d: ArenaDraft, e = env()): string[] => validateArena(d, e).map((i) => i.code);

    it('accepts a sound map', () => {
        const d = sample();
        d.pickups[1]!.res = 0x33732;
        d.players = 2;
        expect(validateArena(d, env()).filter((i) => i.severity !== 'info')).toEqual([]);
    });

    it('flags too few spawns, outside bounds, floating, void, spacing', () => {
        const d = sample();
        d.spawns = [{ uid: 1, pos: [20000, 100, 1000], yaw: 0, team: 0 }];
        expect(codes(d)).toContain('few-spawns');
        expect(codes(d)).toContain('spawn-outside');
        d.spawns = [
            { uid: 1, pos: [1000, 900, 1000], yaw: 0, team: 0 },
            { uid: 2, pos: [1100, 100, 1000], yaw: 0, team: 0 },
        ];
        const c = codes(d);
        expect(c).toContain('spawn-floating');
        expect(c).toContain('spawn-spacing');
        const v = validateArena(sample(), env({ groundAt: () => null }));
        expect(v.some((i) => i.code === 'spawn-void')).toBe(true);
        expect(v.some((i) => i.code === 'pickup-void')).toBe(true);
    });

    it('warns when spawns are fewer than players and when a team has none', () => {
        const d = sample();
        d.players = 8;
        d.spawns[1]!.team = 1;
        const c = codes(d);
        expect(c).toContain('spawns-vs-players');
        expect(c).toContain('team-spawns');
    });

    it('checks adopted pickups against the level items', () => {
        const d = sample();
        expect(codes(d, env({ items: [] }))).toContain('pickup-adopt');
        expect(codes(d, env({ items: [[4000, 100, 5000]] }))).not.toContain('pickup-adopt');
    });

    it('checks capture and hold objectives against markers', () => {
        const d = sample();
        d.modes = ['ctf'];
        const ctf: ModeDef = {
            ...defaultMode('ctf'),
            base: 'tdm',
            teams: 2,
            objectives: [{ kind: 'capture', points: 1, seconds: 0, radius: 300 }],
        };
        expect(codes(d, env({ modes: [ctf] }))).toContain('no-flag');
        for (const t of [1, 2]) {
            d.markers.push({ uid: 20 + t, kind: 'flag', pos: [1, 1, 1], radius: 150, team: t, label: '' });
            d.markers.push({ uid: 30 + t, kind: 'base', pos: [1, 1, 1], radius: 600, team: t, label: '' });
        }
        expect(codes(d, env({ modes: [ctf] }))).not.toContain('no-flag');
        expect(codes(d, env({ modes: [ctf] }))).not.toContain('no-base');
    });

    it('finds pickups across a cliff unreachable', () => {
        const cliff = (x: number): number => (x < 5000 ? 0 : 3000);
        const d = sample();
        d.spawns = [
            { uid: 1, pos: [1000, 0, 1000], yaw: 0, team: 0 },
            { uid: 2, pos: [2000, 0, 3000], yaw: 0, team: 0 },
        ];
        d.pickups = [{ uid: 9, pos: [9000, 3000, 5000], respawn: 30, res: 1, label: '' }];
        expect(codes(d, env({ groundAt: cliff }))).toContain('pickup-unreachable');
        const r = reachableCells(d.bounds!, [d.spawns[0]!.pos], cliff);
        expect(r.seen.some((v) => v === 1)).toBe(true);
    });

    it('clones deeply', () => {
        const d = sample();
        const c = cloneDraft(d);
        c.spawns[0]!.pos[0] = 5;
        expect(d.spawns[0]!.pos[0]).toBe(1000);
    });
});

describe('game modes', () => {
    const ctf: ModeDef = {
        ...defaultMode('ctf'),
        name: 'Capture the Flag',
        description: 'Two teams.',
        base: 'tdm',
        teams: 2,
        scoreObjective: 5,
        fragLimit: 0,
        timeLimit: 600,
        respawnDelay: 2,
        friendlyFire: true,
        startHealth: 150,
        loadout: [0x3372b],
        weapons: [0x3372b, 0x31f30],
        objectives: [
            { kind: 'capture', points: 5, seconds: 10, radius: 300 },
            { kind: 'hold', points: 1, seconds: 15, radius: 700 },
        ],
    };

    it('round trips', () => {
        const t = serializeMode(ctf);
        expect(t).toContain('[mode ctf]\nname = Capture the Flag\n');
        expect(t).toContain('objective = capture points=5 radius=300\n');
        expect(t).toContain('objective = hold points=1 seconds=15 radius=700\n');
        const r = parseModes(t);
        expect(r.errors).toEqual([]);
        expect(r.modes).toEqual([ctf]);
    });

    it('rejects out of range values and reserved ids', () => {
        expect(validateMode({ ...ctf, fragLimit: 5000 })).toEqual(['frag_limit must be 0..1000']);
        expect(validateMode({ ...ctf, id: 'dm' })).toContain('id dm / tdm is reserved for the built-in modes');
        expect(validateMode({ ...ctf, base: 'dm' })).toContain('a dm based mode has no teams (teams = 0)');
        const r = parseModes('[mode x]\nname = X\nfrag_limit = 5000\n');
        expect(r.modes).toEqual([]);
        expect(r.errors[0]).toContain('frag_limit must be 0..1000');
        expect(parseModes('[mode x]\nname = X\nbase = ctf\n').errors[0]).toContain('base must be dm or tdm');
    });
});

describe('arena pack', () => {
    it('lays out manifest, map ini and preview', async () => {
        const { buildArenaPackFiles } = await import('../src/index.ts');
        const d = sample();
        const files = buildArenaPackFiles(
            d,
            [],
            { id: 'my-pack', target: { profile: '*' } },
            new Uint8Array([1, 2, 3]),
        );
        expect(Object.keys(files).sort()).toEqual(['maps/test-yard.ini', 'mod.toml', 'previews/Test_Yard.png']);
        expect(new TextDecoder().decode(files['maps/test-yard.ini'])).toContain('[arena test-yard]');
        expect(new TextDecoder().decode(files['mod.toml'])).toContain('profile = "*"');
        expect(Object.keys(buildArenaPackFiles(d, [], { id: 'p', target: { profile: '*' } }, null))).not.toContain(
            'previews/Test_Yard.png',
        );
    });
});

describe('spawn suggestions', () => {
    it('spreads points over flat ground and skips cliffs and voids', async () => {
        const { suggestSpawns } = await import('../src/index.ts');
        const g = (x: number, z: number): number | null => (x > 9000 ? null : x < 3000 ? x * 2 : 50);
        const pts = suggestSpawns([0, 0, 12000, 12000], g, 4);
        expect(pts).toHaveLength(4);
        for (const p of pts) {
            expect(p[0]).toBeGreaterThanOrEqual(3000);
            expect(p[0]).toBeLessThanOrEqual(9000);
            expect(p[1]).toBe(50);
        }
        const d = (a: number[], b: number[]) => Math.hypot(a[0]! - b[0]!, a[2]! - b[2]!);
        expect(Math.min(d(pts[0]!, pts[1]!), d(pts[1]!, pts[2]!), d(pts[2]!, pts[3]!))).toBeGreaterThan(2000);
    });
});
