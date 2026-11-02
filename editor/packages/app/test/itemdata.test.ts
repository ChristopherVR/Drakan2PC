import { describe, expect, it } from 'vitest';
import { Iso9660, bufferSource } from '@drakan/formats';
import { buildIso, buildSrsc } from '../../formats/test/fixtures.ts';
import { loadItemCatalog, loadItemModels, resetItemData } from '../src/itemdata.ts';

const u16 = (n: number): number[] => [n & 255, (n >> 8) & 255];
const u32 = (n: number): number[] => [n & 255, (n >>> 8) & 255, (n >>> 16) & 255, (n >>> 24) & 255];
const str = (s: string, pad = 0): number[] => [
    ...u16(s.length + pad),
    ...[...s].map((c) => c.charCodeAt(0)),
    ...Array(pad).fill(0),
];

/** LVL header with System as db 2 and the shared Resources database as db 3, like every real level. */
const header = (): Uint8Array =>
    Uint8Array.from([
        ...str('Test', 2),
        ...u32(65536),
        ...u32(32768),
        ...u32(2),
        ...u16(2),
        ...u16(0),
        ...str('..\\..\\Common\\System\\System', 1),
        ...u16(3),
        ...u16(0),
        ...str('..\\..\\Common\\Resources\\Resources', 1),
    ]);

/** A 0x0102 class record: name field, then a body long enough for the runtime-class probe. */
const classRecord = (name: string): Uint8Array => Uint8Array.from([...str(name, 1), ...Array(24).fill(0)]);

/** A class record whose runtime class (u32 at +16 of the body) is `runtime`. */
const classRecordRt = (name: string, runtime: number): Uint8Array => {
    const b = Array(24).fill(0);
    b.splice(16, 4, ...u32(runtime));
    return Uint8Array.from([...str(name, 1), ...b]);
};

function disc(extra: { id: number; name: string; runtime: number }[] = []): Iso9660 | Promise<Iso9660> {
    const odb = buildSrsc([
        { type: 0x0102, id: 0x1bc9, sub: 0, data: classRecord('Wood Bow: Short') },
        { type: 0x0102, id: 0x3752, sub: 0, data: classRecord('Health Potion (Red)') },
        { type: 0x0102, id: 0x0042, sub: 0, data: Uint8Array.from([...u16(1), 0, ...Array(24).fill(0)]) }, // no name
        ...extra.map((x) => ({ type: 0x0102, id: x.id, sub: 0, data: classRecordRt(x.name, x.runtime) })),
    ]);
    const lst = new TextEncoder().encode('\\Levels\\Test\\Test.lvl\r\n');
    return Iso9660.open(
        bufferSource(
            buildIso([
                { path: 'ENGLISH/LEVELLST.TXT', data: lst },
                { path: 'ENGLISH/LEVELS/TEST/TEST.LVL', data: buildSrsc([{ type: 0, id: 0, sub: 0, data: header() }]) },
                { path: 'ENGLISH/COMMON/RESOURES/RESOURES.ODB', data: odb },
            ]),
        ),
    );
}

describe('item catalog without a level', () => {
    it('lists the named classes of the Resources ODB found through any level header', async () => {
        resetItemData();
        const fs = await disc();
        const c = await loadItemCatalog(fs, 'ENGLISH');
        expect(c.classes).toEqual([
            { classId: 0x1bc9, name: 'Wood Bow: Short' },
            { classId: 0x3752, name: 'Health Potion (Red)' },
        ]);
    });

    it('flags the mana power-up class (engine class 171) and no other', async () => {
        resetItemData();
        const fs = await disc([
            { id: 0x1b33, name: 'Full Mana PwrUp', runtime: 0x100ab },
            { id: 0x1b35, name: 'Some Sword', runtime: 0x1000e },
        ]);
        const c = await loadItemCatalog(fs, 'ENGLISH');
        const by = (id: number) => c.classes.find((k) => k.classId === id)!;
        expect(by(0x1b33).powerUp).toBe(true);
        expect(by(0x1b35).powerUp).toBeUndefined();
        expect(by(0x3752).powerUp).toBeUndefined();
    });

    it('returns an entry per requested class even when there is no model', async () => {
        resetItemData();
        const fs = await disc();
        const m = await loadItemModels(fs, 'ENGLISH', [0x1bc9]);
        expect(m).toHaveLength(1);
        expect(m[0]!.classId).toBe(0x1bc9);
        expect(m[0]!.model).toBeNull();
    });

    it('rejects a disc that has no Resources database', async () => {
        resetItemData();
        const fs = await Iso9660.open(
            bufferSource(buildIso([{ path: 'ENGLISH/LEVELLST.TXT', data: new Uint8Array(0) }])),
        );
        await expect(loadItemCatalog(fs, 'ENGLISH')).rejects.toThrow(/Resources/);
        resetItemData();
    });
});
