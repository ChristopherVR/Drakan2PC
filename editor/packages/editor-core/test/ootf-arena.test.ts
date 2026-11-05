// Synthetic only: the multiplayer map of a converted Order of the Flame level (spawns from the launch pads, Drakan 1 tag).
import { describe, expect, it } from 'vitest';
import type { PcLevel } from '@drakan/formats';
import { ootfArenaFromPc, parseArenaIni, serializeMapFile } from '../src/index.ts';

const level = (path: string, pads: number): PcLevel => {
    const classes = new Map([[0, new Map([[1, { name: 'Rynn Launch Pad (multiplayer spawn)' }], [2, { name: 'Tree' }]])]]);
    const objects = Array.from({ length: pads + 1 }, (_, i) => ({
        id: i + 1,
        classId: i < pads ? 1 : 2,
        dbIndex: 0,
        pos: [1000 * i, 5, 2000 * i],
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
        const d = ootfArenaFromPc(level('Multiplayer/Air/Test/Test.lvl', 3), 'OotF Test Arena')!;
        expect(d.spawns.length).toBe(3);
        expect(d.game).toBe('Drakan 1');
        expect(d.name).toBe('Test Arena');
        const back = parseArenaIni(serializeMapFile(d, [])).arenas[0]!;
        expect(back.game).toBe('Drakan 1');
        expect(back.level).toBe('OotF Test Arena');
        expect(back.spawns.length).toBe(3);
    });
    it('lists a pad that the PC level repeats at one spot once', () => {
        const pc = level('Multiplayer/Ground/Test/Test.lvl', 3);
        pc.list.objects.push({ ...pc.list.objects[0]!, id: 99 });
        expect(ootfArenaFromPc(pc, 'OotF Test Arena')!.spawns.length).toBe(3);
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
    it('grants a loadout and puts weapon pickups on the Weapon Pads', () => {
        const l = level('Multiplayer/Ground/Test/Test.lvl', 2);
        l.classes.get(0)!.set(5, { name: 'Weapon Pad' } as never);
        (l.list.objects as unknown as { id: number; classId: number; dbIndex: number; pos: number[] }[]).push(
            { id: 80, classId: 5, dbIndex: 0, pos: [100, 7, 200] },
            { id: 81, classId: 5, dbIndex: 0, pos: [900, 7, 800] },
        );
        const d = ootfArenaFromPc(l, 'x')!;
        expect(d.loadout.length).toBeGreaterThan(0);
        expect(d.pickups.length).toBe(2);
        expect(d.pickups.every((p) => p.res > 0 && p.respawn > 0)).toBe(true);
        expect(parseArenaIni(serializeMapFile(d, [])).arenas[0]!.pickups.length).toBe(2);
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
