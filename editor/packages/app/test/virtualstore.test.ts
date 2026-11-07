// Synthetic only: the converted-levels store (accumulation, disc keying, batched load, quota messages) on fake IndexedDB.
import 'fake-indexeddb/auto';
import { beforeEach, describe, expect, it } from 'vitest';
import { clearConverted, loadConverted, saveConverted, storeFailureMessage } from '../src/virtualstore.ts';

const disc = { volumeId: 'DRAKAN_DISC', language: 'ENGLISH' };
const lvl = (n: string) => ({ name: n, dirPath: 'LEVELS/' + n });
const file = (path: string, n: number, v = 1) => ({ path, bytes: new Uint8Array(n).fill(v) });

describe('persisted converted levels', () => {
    beforeEach(async () => {
        await clearConverted();
    });

    it('accumulates over several imports instead of keeping only the last', async () => {
        await saveConverted({ levels: [lvl('A')], files: [file('new/a.bin', 4)] }, disc);
        await saveConverted({ levels: [lvl('B')], files: [file('new/b.bin', 4)] }, disc);
        const got = await loadConverted(disc);
        expect(got?.levels.map((l) => l.name).sort()).toEqual(['A', 'B']);
        expect(got?.files.map((f) => f.path).sort()).toEqual(['new/a.bin', 'new/b.bin']);
    });

    it('a re-import of the same level replaces it and its changed file', async () => {
        await saveConverted({ levels: [lvl('A')], files: [file('new/a.bin', 4, 1)] }, disc);
        await saveConverted({ levels: [lvl('A')], files: [file('new/a.bin', 4, 9)] }, disc);
        const got = await loadConverted(disc);
        expect(got?.levels).toHaveLength(1);
        expect(got?.files[0]?.bytes[0]).toBe(9);
    });

    it('never hands one disc or language the files of another, and the next save replaces them', async () => {
        await saveConverted({ levels: [lvl('A')], files: [file('new/a.bin', 4)] }, disc);
        expect(await loadConverted({ ...disc, volumeId: 'OTHER' })).toBeNull();
        expect(await loadConverted({ ...disc, language: 'FRENCH' })).toBeNull();
        await saveConverted({ levels: [lvl('Z')], files: [file('new/z.bin', 4)] }, { ...disc, language: 'FRENCH' });
        expect(await loadConverted(disc)).toBeNull();
        const fr = await loadConverted({ ...disc, language: 'FRENCH' });
        expect(fr?.files.map((f) => f.path)).toEqual(['new/z.bin']);
    });

    it('loads in several batches (bounded transactions) and returns every file once, in order', async () => {
        const files = Array.from({ length: 25 }, (_, i) => file(`new/f${String(i).padStart(2, '0')}.bin`, 1000, i));
        await saveConverted({ levels: [lvl('A')], files }, disc);
        const got = await loadConverted(disc, 4096); // about 4 files per batch
        expect(got?.files.map((f) => f.path)).toEqual(files.map((f) => f.path));
        expect(got?.files[24]?.bytes[0]).toBe(24);
    });

    it('returns null for an empty store', async () => {
        expect(await loadConverted(disc)).toBeNull();
    });

    it('explains a full browser store in plain words', () => {
        const quota = Object.assign(new Error('The quota has been exceeded.'), { name: 'QuotaExceededError' });
        expect(storeFailureMessage(quota)).toMatch(/no room left/);
        expect(storeFailureMessage(new Error('boom'))).toMatch(/would not keep/);
        expect(storeFailureMessage(quota)).not.toMatch(/IndexedDB|QuotaExceeded/);
    });
});
