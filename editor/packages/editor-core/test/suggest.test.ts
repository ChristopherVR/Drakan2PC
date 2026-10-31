// Synthetic only: known values offered for list fields in the Details panel.
import { describe, expect, it } from 'vitest';
import { LevelDocument, suggestValues, type AssetClass, type LevelObject } from '../src/index.ts';

const obj = (id: number, cls: string): LevelObject => ({
    id,
    class: cls,
    type: 0,
    kind: 'static',
    resource: '0x20001',
    model: '',
    transform: { pos: [0, 0, 0], rot: [0, 0, 0] },
    props: {},
});

function docWith(...objs: LevelObject[]): LevelDocument {
    const d = new LevelDocument();
    for (const o of objs) d.insert(o);
    return d;
}

const ARR_INT = 0x1001;
const base = { type: ARR_INT, catalog: [] as AssetClass[], provider: null, owner: null };

describe('suggestValues', () => {
    it('offers message names for a message list, common ones first', () => {
        const s = suggestValues({ ...base, name: 'Messages To Send', doc: docWith() });
        expect(s.length).toBeGreaterThan(10);
        expect(s[0]!.group).toBe('Messages');
        expect(s[0]!.value).toBe(1);
        expect(new Set(s.map((x) => x.value)).size).toBe(s.length);
    });

    it('offers the level objects by name for target lists', () => {
        const s = suggestValues({ ...base, name: 'Target Objects', doc: docWith(obj(5, 'Door'), obj(9, 'Lever')) });
        expect(s.map((x) => x.value).sort()).toEqual([5, 9]);
        expect(s.find((x) => x.value === 5)!.label).toContain('Door');
    });

    it('offers nothing for unrelated float lists and non-arrays', () => {
        expect(suggestValues({ ...base, type: 0x1002, name: 'Messages', doc: docWith() })).toEqual([]);
        expect(suggestValues({ ...base, type: 7, name: 'Messages', doc: docWith() })).toEqual([]);
    });

    it('returns nothing for an unknown list in an empty level', () => {
        expect(suggestValues({ ...base, name: 'Weights', doc: docWith() })).toEqual([]);
    });
});
