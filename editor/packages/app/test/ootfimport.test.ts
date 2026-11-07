import { describe, expect, it } from 'vitest';
import { installFilesOf, languageNote, ootfTemplateLevels, progressText } from '../src/editor/ootfimport.ts';
import { filesSource } from '../src/editor/ootfconvert.ts';

describe('importer progress text', () => {
    it('names the level being converted instead of the converter bookkeeping', () => {
        expect(progressText('sequences OotF 07 - Alwarren: 7 referenced, 7 converted', 'OotF ')).toBe(
            'Converting OotF 07 - Alwarren...',
        );
        expect(progressText('puzzles OotF - Relic (ground): 0 plates -> 0 detectors', 'OotF - ')).toBe(
            'Converting OotF - Relic (ground)...',
        );
    });

    it('turns the language probe into a plain sentence', () => {
        expect(progressText('source language: german (stop words: german 140, english 3)', 'OotF ')).toBe(
            'Install language: German. Converting...',
        );
    });

    it('falls back to a plain line for anything else', () => {
        expect(progressText('retarget Primitive Giant: PC 44 joints', 'OotF ')).toBe('Converting...');
    });
});

describe('install language note', () => {
    it('says nothing when the install has no readable Dragon.rrc', async () => {
        expect(await languageNote({ list: async () => ['a/level.lvl'], read: async () => null })).toBe('');
    });
    it('does not fail on an unreadable Dragon.rrc', async () => {
        const src = { list: async () => ['Dragon.rrc'], read: async () => new Uint8Array([1, 2, 3]) };
        expect(await languageNote(src)).toBe('');
    });
});

describe('install files for the conversion worker', () => {
    it('drops the picked folder name and reads files case-insensitively', async () => {
        const f = new File([new Uint8Array([1, 2, 3])], 'Ruined Village.lvl');
        Object.defineProperty(f, 'webkitRelativePath', {
            value: 'Drakan/Mountain World/Ruined Village/Ruined Village.lvl',
        });
        const files = installFilesOf([f]);
        expect(files.map((x) => x.path)).toEqual(['Mountain World/Ruined Village/Ruined Village.lvl']);
        const src = filesSource(files);
        expect([...(await src.read('mountain world/ruined village/RUINED VILLAGE.LVL'))!]).toEqual([1, 2, 3]);
        expect(await src.read('missing.lvl')).toBeNull();
    });
});

describe('conversion templates', () => {
    const disc = (lang: string) => [
        { name: 'Level 0 - Intro', dirPath: `${lang}\\LEVELS\\LEVEL0RO` },
        { name: 'Level 1A - Surdana', dirPath: `${lang}\\LEVELS\\LEVEL1NA` },
        { name: 'Level 2 - Shadowmire', dirPath: `${lang}\\LEVELS\\LEVEL2RE` },
        { name: 'OotF 00 - Intro', dirPath: `${lang}\\LEVELS\\OOTF00RO`, virtual: true },
    ];
    it.each(['ENGLISH', 'GERMAN', 'FRENCH', 'ITALIAN', 'SPANISH'])(
        'offers Surdana first and never the intro or a converted level (%s tree)',
        (lang) => {
            expect(ootfTemplateLevels(disc(lang)).map((l) => l.name)).toEqual([
                'Level 1A - Surdana',
                'Level 2 - Shadowmire',
            ]);
        },
    );
    it('keeps the disc order when Surdana is missing', () => {
        const levels = disc('ENGLISH').filter((l) => !/LEVEL1NA/.test(l.dirPath));
        expect(ootfTemplateLevels(levels).map((l) => l.name)).toEqual(['Level 2 - Shadowmire']);
    });
});
