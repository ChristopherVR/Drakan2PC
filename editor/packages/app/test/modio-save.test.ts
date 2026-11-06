import { describe, expect, it } from 'vitest';
import { knownModsFolder, toastSaveResult } from '../src/modio.ts';

describe('save results', () => {
    const run = async (save: Promise<string | null>): Promise<[string, string][]> => {
        const toasts: [string, string][] = [];
        await toastSaveResult(save, (m, k) => toasts.push([m, k]));
        return toasts;
    };

    it('names where the pack landed', async () => {
        expect(await run(Promise.resolve('C:/mods/ootf-levels'))).toEqual([['Saved to C:/mods/ootf-levels', 'ok']]);
    });

    it('stays quiet when the user cancelled', async () => {
        expect(await run(Promise.resolve(null))).toEqual([]);
    });

    it('reports a failed write instead of dropping it', async () => {
        expect(await run(Promise.reject(new Error('C:/mods/x: Access is denied.')))).toEqual([
            ['C:/mods/x: Access is denied.', 'error'],
        ]);
        expect(await run(Promise.reject('No game folder is set.'))).toEqual([['No game folder is set.', 'error']]);
    });
});

describe('knownModsFolder', () => {
    it('prefers the chosen mods folder, then the game folder', () => {
        expect(knownModsFolder({ modsDir: 'D:/mods', gameDir: 'C:/Drakan' })).toBe('D:/mods');
        expect(knownModsFolder({ modsDir: 'D:/mods', gameDir: null })).toBe('D:/mods');
        expect(knownModsFolder({ modsDir: null, gameDir: 'C:/Drakan' })).toBe('C:/Drakan/mods');
        expect(knownModsFolder({ modsDir: null, gameDir: null })).toBeNull();
    });
});
