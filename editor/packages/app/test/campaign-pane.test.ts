// Project Settings > Campaign helpers (src/editor/project/campaign.ts): the level whose exits a copy has, and the
// exits a project retargets ("Only this exit" in the Logic panel), read from the level documents and the strings.
import { describe, expect, it } from 'vitest';
import type { LevelDocument, ModProject } from '@drakan/editor-core';
import type { ExitIndexData } from '../src/logicdata.ts';
import { exitSourceLevel, projectExitOverrides } from '../src/editor/project/campaign.ts';

const levels = [
    { title: 'Surdana Copy', copyOf: 'Level 1A - Surdana' },
    { title: 'Kragmor Copy', copyOf: 'Level 1B - Kragmor' },
    { title: 'Level 2 - Shadowmire' },
];

describe('exitSourceLevel', () => {
    it('gives a copy its source level', () => {
        expect(exitSourceLevel(levels, 'Surdana Copy')).toBe('Level 1A - Surdana');
        expect(exitSourceLevel(levels, 'kragmor copy')).toBe('Level 1B - Kragmor');
    });
    it('keeps disc levels and unknown names', () => {
        expect(exitSourceLevel(levels, 'Level 2 - Shadowmire')).toBe('Level 2 - Shadowmire');
        expect(exitSourceLevel(levels, 'Level 1A - Surdana')).toBe('Level 1A - Surdana');
        expect(exitSourceLevel([], 'Surdana Copy')).toBe('Surdana Copy');
    });
});

describe('projectExitOverrides', () => {
    const data = {
        levels: [
            {
                name: 'Level 1A - Surdana',
                exits: [{ id: 12250, name: 'Transition from 1(A) to 1 (B)', dest: 'Level 1B - Kragmor' }],
                classes: {
                    '0:1': {
                        name: 'Transition from 1(A) to 1 (B)',
                        dest: 'Level 1B - Kragmor',
                        destRef: { file: 'LEVEL1ON.XDB', id: 13 },
                    },
                },
            },
        ],
    } as unknown as ExitIndexData;
    const strings: Record<number, string> = {
        13: 'Levels/Level 1B - Kragmor/Level 1B - Kragmor.lvl',
        53: 'Levels/Kragmor Copy/Kragmor Copy.lvl',
    };
    const p = { levels } as unknown as ModProject;
    const docWith = (value: number | null) =>
        ({
            get: (id: number) =>
                id === 12250 ? { fields: value === null ? [] : [{ fieldIndex: 0, value }] } : undefined,
        }) as unknown as LevelDocument;

    it("reads a copy's own exit string (Only this exit)", async () => {
        const over = await projectExitOverrides(p, data, async (t) => (t === 'Surdana Copy' ? docWith(53) : null), {
            text: (_f, id) => strings[id],
            changed: () => false,
        });
        expect(over).toEqual([{ level: 'Surdana Copy', exitId: 12250, dest: 'Kragmor Copy' }]);
    });
    it('ignores unchanged shared strings', async () => {
        const over = await projectExitOverrides(p, data, async () => docWith(null), {
            text: (_f, id) => strings[id],
            changed: () => false,
        });
        expect(over).toEqual([]);
    });
});
