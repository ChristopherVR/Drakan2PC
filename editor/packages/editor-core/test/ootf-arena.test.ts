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
    it('ignores campaign levels and levels with fewer than two starts', () => {
        expect(ootfArenaFromPc(level('Mountain World/Intro/Intro.lvl', 4), 'x')).toBeNull();
        expect(ootfArenaFromPc(level('Multiplayer/Ground/Relic/Relic.lvl', 1), 'x')).toBeNull();
    });
});
