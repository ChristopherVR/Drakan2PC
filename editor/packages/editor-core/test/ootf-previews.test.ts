// Synthetic only: per-arena menu pictures in an Order of the Flame pack (the LVL builder is stubbed, no game data).
import { describe, expect, it, vi } from 'vitest';

vi.mock('@drakan/formats', async (orig) => ({
    ...(await orig<typeof import('@drakan/formats')>()),
    buildPs2Lvl: async () => new Uint8Array([1, 2, 3]),
}));

import type { SrscReader } from '@drakan/formats';
import { buildOotfPack, newDraft, previewFileName } from '../src/index.ts';

const template = { entries: [] } as unknown as SrscReader;
const blob = (async () => new Uint8Array()) as never;
const meta = { id: 'ootf-test', name: 'Test', target: { profile: 'pal' } } as never;
const png = new Uint8Array([0x89, 0x50, 0x4e, 0x47]);
const arena = (name: string) => ({ ...newDraft('x'), name });
const level = (title: string, extra: object) => ({ title, parts: { report: { warnings: [] } } as never, ...extra });

describe('buildOotfPack arena pictures', () => {
    it('ships a picture under previews/<safe arena name>.png next to the map', async () => {
        const pack = await buildOotfPack(
            meta,
            template,
            [level('OotF - Test Arena (air)', { arena: arena('Test Arena (air)'), preview: png })],
            blob,
            { splats: {} },
        );
        const key = `previews/${previewFileName('Test Arena (air)')}`;
        expect(key).toBe('previews/Test_Arena__air_.png');
        expect(pack.files[key]).toEqual(png);
        expect(Object.keys(pack.files).some((f) => f.startsWith('maps/'))).toBe(true);
    });

    it('ignores a picture on a level without an arena and skips empty ones', async () => {
        const pack = await buildOotfPack(
            meta,
            template,
            [level('OotF 01', { preview: png }), level('OotF - B', { arena: arena('B'), preview: new Uint8Array() })],
            blob,
            { splats: {} },
        );
        expect(Object.keys(pack.files).filter((f) => f.startsWith('previews/'))).toEqual([]);
        expect(pack.warnings.some((w) => w.includes('no arena'))).toBe(true);
    });
});
