import { describe, expect, it } from 'vitest';
import { FIELD_FLOAT, FIELD_INT, floatBits, type OdbClass } from '@drakan/formats';
import { itemFacts } from '../src/itemdata.ts';

const cls = (fields: [string, number, number][]): Pick<OdbClass, 'fields' | 'data'> => ({
    fields: fields.map(([name, type]) => ({ name, type })),
    data: fields.map(([, , v]) => v),
});

describe('item tooltip facts', () => {
    it('lists what the class fields say and skips unused or absent ones', () => {
        const k = cls([
            ['Damage Maximum', FIELD_INT, 8],
            ['Weapon Speed', FIELD_FLOAT, floatBits(0.9)],
            ['Total Durability Points', FIELD_INT, 150],
            ['Skill Level', FIELD_INT, 0],
            ['Droppable?', FIELD_INT, 0],
        ]);
        const at = (n: string): number => k.fields!.findIndex((f) => f.name === n);
        expect(itemFacts(k, at)).toEqual(['Damage: up to 8', 'Speed: 0.9', 'Durability: 150', 'Cannot be dropped']);
    });

    it('reads durability 0xffffffff as unbreakable', () => {
        const k = cls([['Total Durability Points', FIELD_INT, 0xffffffff]]);
        expect(itemFacts(k, (n) => (n === 'Total Durability Points' ? 0 : -1))).toEqual(['Durability: unbreakable']);
    });
});
