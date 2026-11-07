import { describe, expect, it } from 'vitest';
import { bufferSource, Iso9660, SrscReader, writePs2Srsc } from '@drakan/formats';
import { buildIso } from '../../formats/test/fixtures.ts';
import { convertedStoryOf, overlayDisc, packPathToDisc, patchedContainers } from '../src/virtualdisc.ts';

const txt = (s: string): Uint8Array => new TextEncoder().encode(s);

async function disc(files: { path: string; data: Uint8Array }[]): Promise<Iso9660> {
    return Iso9660.open(bufferSource(buildIso(files)));
}

describe('packPathToDisc', () => {
    it('maps pack file paths to disc paths and ignores the rest', () => {
        expect(packPathToDisc('files/ENGLISH/LEVELS/OOTF00RO/OOTF00RO.LVL')).toBe(
            'ENGLISH\\LEVELS\\OOTF00RO\\OOTF00RO.LVL',
        );
        expect(packPathToDisc('maps/X.ini')).toBeNull();
        expect(packPathToDisc('overrides/NPCS.ADB/0501_1_0.bin')).toBeNull();
    });
});

describe('overlayDisc', () => {
    it('lists, finds and reads in-memory files next to disc files, in new folders too', async () => {
        const base = await disc([
            { path: 'ENGLISH/LEVELS/LEVEL1NA/LEVEL1NA.LVL', data: txt('disc level') },
            { path: 'ENGLISH/COMMON/RESOURES/RESOURES.MOD', data: txt('old models') },
        ]);
        const fs = overlayDisc(base, [
            { path: 'ENGLISH\\LEVELS\\OOTF00RO\\OOTF00RO.LVL', bytes: txt('converted level') },
            { path: 'ENGLISH\\COMMON\\RESOURES\\RESOURES.MOD', bytes: txt('wider models') },
        ]);
        const levels = (await fs.list('ENGLISH\\LEVELS')).map((e) => e.name).sort();
        expect(levels).toEqual(['LEVEL1NA', 'OOTF00RO']);
        const lvl = await fs.find('english\\levels\\ootf00ro\\ootf00ro.lvl');
        expect(lvl?.isDir).toBe(false);
        expect(new TextDecoder().decode(await fs.read(lvl!))).toBe('converted level');
        // A same-path file replaces the disc file; other disc files still read from the disc.
        expect(new TextDecoder().decode(await fs.read('ENGLISH\\COMMON\\RESOURES\\RESOURES.MOD'))).toBe('wider models');
        expect(new TextDecoder().decode(await fs.read('ENGLISH\\LEVELS\\LEVEL1NA\\LEVEL1NA.LVL'))).toBe('disc level');
        // Partial reads work through the source (readers seek into files).
        const src = (await fs.find('ENGLISH\\LEVELS\\OOTF00RO\\OOTF00RO.LVL'))!;
        expect(new TextDecoder().decode(await fs.source.read(src.extent * 2048 + 10, 5))).toBe('level');
    });

    it('leaves the disc untouched', async () => {
        const base = await disc([{ path: 'ENGLISH/A.TXT', data: txt('a') }]);
        overlayDisc(base, [{ path: 'ENGLISH\\B.TXT', bytes: txt('b') }]);
        expect(await base.find('ENGLISH\\B.TXT')).toBeNull();
    });
});

describe('patchedContainers', () => {
    it('applies overrides and new entries to the disc container of that name', async () => {
        const odb = writePs2Srsc([
            { type: 0x102, id: 1, sub: 0, data: txt('class one') },
            { type: 0x102, id: 2, sub: 0, data: txt('class two') },
        ]);
        const base = await disc([{ path: 'ENGLISH/COMMON/FX/FX.ODB', data: odb }]);
        const { files, skipped } = await patchedContainers(base, 'ENGLISH', {
            'overrides/FX.ODB/0102_2_0.bin': txt('class TWO'),
            'new/FX.ODB/0102_9_0.bin': txt('class nine'),
            'overrides/GONE.ODB/0102_1_0.bin': txt('x'),
            'overrides/LEVEL1NA.LVL/0020_0_0.bin': txt('ignored'),
        });
        expect(skipped).toEqual(['GONE.ODB']);
        expect(files.map((f) => f.path)).toEqual(['ENGLISH\\COMMON\\FX\\FX.ODB']);
        const r = await SrscReader.fromBytes(files[0]!.bytes);
        const got = await Promise.all(
            r.entries.map(async (e) => `${e.id}:${new TextDecoder().decode(await r.readRaw(e))}`),
        );
        expect(got).toEqual(['1:class one', '2:class TWO', '9:class nine']);
    });
});

describe('convertedStoryOf', () => {
    it('lists the cut scenes and classes a pack adds per level folder, leaving the quests to their own list', () => {
        const got = convertedStoryOf([
            'new/LEVEL1ON.SSD/0311_5_0.bin',
            'new/LEVEL1ON.SSD/0312_5_0.bin',
            'new/level1on.odb/0102_900_0.bin',
            'new/SYSTEM.ODB/0102_1000_0.bin',
            'overrides/LEVEL1ON.SSD/0311_6_0.bin',
            'files/ENGLISH/LEVELS/X/X.LVL',
        ]);
        expect([...got.keys()]).toEqual(['LEVEL1ON']);
        expect([...got.get('LEVEL1ON')!.sequences]).toEqual([5]);
        expect([...got.get('LEVEL1ON')!.classes]).toEqual([900]);
    });
});
