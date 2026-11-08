import { describe, expect, it } from 'vitest';
import { DEFAULT_PS2_MODEL_SUBSTITUTIONS, planPs2Models } from '@drakan/formats';
import {
    customModelPair,
    defaultModelChoice,
    modelChoiceToOptions,
    modelResultText,
    pairOn,
    readModelChoice,
} from '../src/editor/ootfps2model.ts';

describe('Drakan 2 characters and weapons in the import dialog', () => {
    it('is on by default with Drakan 2 animations', () => {
        const p = planPs2Models(modelChoiceToOptions(defaultModelChoice()));
        expect(p.enemy('Troll Leather')).toBe('drakan2');
        expect(p.weapon('Long Sword')?.ps2).toBe('Steel Longsword');
    });

    it('ticks pairs by their default and the player choice', () => {
        const scav = DEFAULT_PS2_MODEL_SUBSTITUTIONS.find((r) => r.ootf === 'Scavenger')!;
        const sword = DEFAULT_PS2_MODEL_SUBSTITUTIONS.find((r) => r.ootf === 'Long Sword')!;
        const c = defaultModelChoice();
        expect(pairOn(c, scav)).toBe(false);
        expect(pairOn(c, sword)).toBe(true);
        const mine = { ...c, on: ['Scavenger'], off: ['long sword'], categoriesOff: ['weapons' as const] };
        expect(pairOn(mine, scav)).toBe(true);
        expect(pairOn(mine, sword)).toBe(false);
        const p = planPs2Models(modelChoiceToOptions({ ...mine, enemyAnimations: 'ootf' }));
        expect(p.enemy('Scavenger')).toBe('ootf');
        expect(p.weapon('Mace')).toBeNull(); // weapons group off
    });

    it('reads a stored choice and explains a bad pair', () => {
        expect(readModelChoice(null)).toEqual(defaultModelChoice());
        const c = readModelChoice({
            enabled: false,
            categoriesOff: ['weapons', 'pets'],
            enemyAnimations: 'ootf',
            extra: [{ category: 'weapons', ootf: 'Flame Sword', ps2: 'Fire Blade' }, { ootf: '' }],
        });
        expect(c.enabled).toBe(false);
        expect(c.categoriesOff).toEqual(['weapons']);
        expect(c.enemyAnimations).toBe('ootf');
        expect(c.extra.map((r) => r.ps2)).toEqual(['Fire Blade']);
        expect(customModelPair('pets', 'A', 'B')).toMatch(/Enemies or Weapons/);
        expect(customModelPair('weapons', ' ', 'B')).toMatch(/Order of the Flame class/);
        expect(modelResultText({ used: [{ label: 'x' }, { label: 'y' }] }, true)).toMatch(/2 classes/);
        expect(modelResultText(undefined, false)).toMatch(/off/);
    });
});
