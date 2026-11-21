// Synthetic only: the multiplayer map of a converted Order of the Flame level (spawns from the launch pads, Drakan 1 tag).
import { describe, expect, it } from 'vitest';
import type { PcLevel } from '@drakan/formats';
import { ootfArenaFromPc, parseArenaIni, serializeMapFile } from '../src/index.ts';

const level = (path: string, pads: number): PcLevel => {
    const classes = new Map([
        [
            0,
            new Map([
                [1, { name: 'Rynn Launch Pad (multiplayer spawn)' }],
                [2, { name: 'Tree' }],
            ]),
        ],
    ]);
    const objects = Array.from({ length: pads + 1 }, (_, i) => ({
        id: i + 1,
        classId: i < pads ? 1 : 2,
        dbIndex: 0,
        pos: [1000 * i, 5, 2000 * i],
        rot: [0, 90 * i, 0],
    }));
    return {
        path,
        header: { name: { text: '<0x1234> Test Arena' } },
        list: { objects },
        terrain: [],
        classes,
    } as unknown as PcLevel;
};

describe('ootfArenaFromPc', () => {
    it('turns launch pads of a Multiplayer level into spawns tagged Drakan 1', () => {
        const pc = level('Multiplayer/Air/Test/Test.lvl', 3);
        pc.classes.get(0)!.set(3, { name: 'DragonLaunch Pad', rflClass: 127 } as never);
        for (const pad of pc.list.objects.slice(0, 3))
            pc.list.objects.push({ ...pad, id: pad.id + 10, classId: 3, pos: [pad.pos[0], 16000, pad.pos[2]] });
        const d = ootfArenaFromPc(pc, 'OotF Test Arena')!;
        expect(d.spawns.length).toBe(3);
        expect(d.game).toBe('Drakan 1');
        expect(d.name).toBe('Test Arena');
        const back = parseArenaIni(serializeMapFile(d, [])).arenas[0]!;
        expect(back.game).toBe('Drakan 1');
        expect(back.level).toBe('OotF Test Arena');
        expect(back.spawns.length).toBe(3);
        expect(back.ootfRules).toBe('air');
        expect(back.spawns.map((s) => [s.pos[1], s.yaw])).toEqual([
            [16000, 0],
            [16000, 90],
            [16000, 180],
        ]);
    });
    it('leaves colocated launch pads with different headings in their original order', () => {
        const pc = level('Multiplayer/Ground/Test/Test.lvl', 3);
        pc.list.objects.push({ ...pc.list.objects[0]!, id: 99, rot: [0, 270, 0] });
        const d = ootfArenaFromPc(pc, 'OotF Test Arena')!;
        expect(d.spawns.map((s) => s.yaw)).toEqual([0, 90, 180, 270]);
        expect(d.spawns[3]?.pos).toEqual(d.spawns[0]?.pos);
    });
    it('keeps dragon pads and the Soul Crystal as markers', () => {
        const l = level('Multiplayer/QOD/Test/Test.lvl', 2);
        const cls = l.classes.get(0)!;
        cls.set(3, { name: 'DragonLaunch Pad' } as never);
        cls.set(4, { name: 'Fire Dragon Soul Crystal' } as never);
        (l.list.objects as unknown as { id: number; classId: number; dbIndex: number; pos: number[] }[]).push(
            { id: 90, classId: 3, dbIndex: 0, pos: [10, 20, 30] },
            { id: 91, classId: 4, dbIndex: 0, pos: [40, 50, 60] },
        );
        const d = ootfArenaFromPc(l, 'x')!;
        expect(d.markers.map((m) => [m.kind, m.label])).toEqual([
            ['base', 'Dragon launch pad'],
            ['flag', 'Fire Dragon Soul Crystal'],
        ]);
        expect(d.spawns.length).toBe(2);
    });
    it('writes converted rewards at the original item position with the authored respawn time', () => {
        const l = level('Multiplayer/Ground/Test/Test.lvl', 2);
        l.classes.get(0)!.set(5, { name: 'Weapon Pad' } as never);
        (l.list.objects as unknown as { id: number; classId: number; dbIndex: number; pos: number[] }[]).push(
            { id: 80, classId: 5, dbIndex: 0, pos: [100, 7, 200] },
            { id: 81, classId: 5, dbIndex: 0, pos: [900, 7, 800] },
        );
        const pickup = {
            pos: [100.125, 701, 200] as [number, number, number],
            name: 'Random Weapon',
            respawn: 45,
            rewards: [
                { kind: 'item' as const, resource: 0x3372b },
                { kind: 'item' as const, resource: 0x3372a },
            ],
        };
        const d = ootfArenaFromPc(l, 'x', { arenaPickups: [pickup] })!;
        const read = parseArenaIni(serializeMapFile(d, []));
        expect(read.errors).toEqual([]);
        expect(read.arenas[0]?.pickups).toMatchObject([{ pos: pickup.pos, respawn: 45, rewards: pickup.rewards }]);
    });
    it('finds the arena of a German install by its name id and its pads by engine class', () => {
        const l = level('Mehrspieler/Boden/Relikt/Relikt.lvl', 3);
        (l.header.name as { text: string }).text = '<0x9011>Relikt (Boden)';
        const cls = l.classes.get(0)!;
        cls.set(1, { name: 'Startfeld für Rynn', rflClass: 18 } as never);
        const d = ootfArenaFromPc(l, 'OotF - Relikt (Boden)')!;
        expect(d.spawns.length).toBe(3);
        expect(d.name).toBe('Relikt (Boden)');
    });
    it('ignores campaign levels and levels with fewer than two starts', () => {
        expect(ootfArenaFromPc(level('Mountain World/Intro/Intro.lvl', 4), 'x')).toBeNull();
        expect(ootfArenaFromPc(level('Multiplayer/Ground/Relic/Relic.lvl', 1), 'x')).toBeNull();
    });
});
