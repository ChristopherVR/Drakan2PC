import { describe, expect, it } from 'vitest';
import { ootfSubtitleTable } from '@drakan/editor-core';
import { writePs2Srsc, xorRrcString } from '@drakan/formats';
import {
    installFilesOf,
    languageNote,
    leftOutText,
    ootfTemplateLevels,
    progressText,
} from '../src/editor/ootfimport.ts';
import { filesSource, loadBundledOotfSubtitles } from '../src/editor/ootfconvert.ts';

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

describe('left out column', () => {
    it('says none when every class converted, and names the objects kept out on purpose', () => {
        expect(leftOutText({ unmappedClasses: 0 })).toBe('none');
        expect(leftOutText({ unmappedClasses: 0, keptOut: 0 })).toBe('none');
        expect(leftOutText({ unmappedClasses: 0, keptOut: 10 })).toBe('none (10 kept out on purpose)');
        expect(leftOutText({ unmappedClasses: 1, keptOut: 3 })).toBe('1 class (3 kept out on purpose)');
        expect(leftOutText({ unmappedClasses: 2 })).toBe('2 classes');
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
    const bundled = { loadBundled: loadBundledOotfSubtitles };

    function localizedSource(sample: string) {
        const data = xorRrcString(new TextEncoder().encode(sample));
        const rrc = writePs2Srsc([{ type: 0x400, id: 1, sub: 0, data }]);
        return { list: async () => ['Dragon.rrc'], read: async (p: string) => (p === 'Dragon.rrc' ? rrc : null) };
    }

    it('loads English captions for an English install', async () => {
        const src = localizedSource('the and you of to is your with this are for have not from');
        expect((await ootfSubtitleTable(src, bundled))?.size).toBe(294);
    });

    it('loads German captions for a German install', async () => {
        const src = localizedSource('der die das und nicht ist sie mit der die das und');
        const table = await ootfSubtitleTable(src, bundled);
        expect(table?.language).toBe('german');
        expect(table?.size).toBeGreaterThan(0);
    });

    it('leaves captions empty when there is no matching language track', async () => {
        const src = localizedSource('les des avec sont dans vous pour une mais nous cette mon');
        const available = async (language: string) =>
            language === 'english' ? loadBundledOotfSubtitles(language) : undefined;
        expect(await ootfSubtitleTable(src, { loadBundled: available })).toBeUndefined();
    });

    it('reads scene captions from the selected install, including on a rebuild with edited text', async () => {
        expect(await ootfSubtitleTable(filesSource([]), bundled)).toBeUndefined();
        for (const text of ['First line', 'Edited line']) {
            const transcript = {
                format: 'drakan-subtitles',
                version: 1,
                language: 'english',
                lines: [{ bank: 'village', id: 2, cues: [{ start: 0, end: 2, text }] }],
            };
            const file = new File([JSON.stringify(transcript)], 'ootf-subtitles.json');
            const table = await ootfSubtitleTable(filesSource(installFilesOf([file])), bundled);
            expect(table?.clipText('village', 2)).toContain(text);
        }
    });

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
