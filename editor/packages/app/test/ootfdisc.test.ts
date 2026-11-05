import { describe, expect, it } from 'vitest';
import { encodeString, SrscReader, writePs2Srsc, type LvlHeader } from '@drakan/formats';
import { OOTF_DIALOG_OPTIONS, ootfDiscContext } from '../src/editor/ootfdisc.ts';

const header = {
    dependencies: [
        { path: encodeString('..\\..\\Common\\Resources\\Resources') },
        { path: encodeString('Level 1A - Surdana_splats') },
    ],
} as LvlHeader;

describe('Order of the Flame disc context', () => {
    it('resolves relative dependencies, reads host containers and keeps splats for packing', async () => {
        const container = writePs2Srsc([]);
        const db = new TextEncoder().encode('synthetic DB');
        const files = new Map([
            ['LEVELS/LEVEL1NA/LEVEL1TS.DB', db],
            ['LEVELS/LEVEL1NA/LEVEL1TS.TXD', container],
            ...['ODB', 'MOD', 'TXD', 'ADB', 'SDB', 'SSD'].map((ext): [string, Uint8Array] => [
                `COMMON/RESOURES/RESOURES.${ext}`,
                container,
            ]),
            ['COMMON/RESOURES/RESOURES.DB', db],
        ]);
        const reads: string[] = [];
        const result = await ootfDiscContext(
            await SrscReader.fromBytes(container),
            header,
            'ENGLISH\\LEVELS\\LEVEL1NA',
            async (path) => {
                reads.push(path);
                return files.get(path) ?? null;
            },
        );
        expect(result.context.odbs.has('RESOURES')).toBe(true);
        expect(result.context.hostMod).toBeDefined();
        expect(result.context.hostAdb).toBeDefined();
        expect(result.context.dbFiles?.get('RESOURES')?.dir).toBe('ENGLISH/COMMON/RESOURES');
        expect(result.context.procTxd).toBeUndefined();
        expect(result.splats['DB']).toBe(db);
        expect(reads.some((path) => path.includes('..'))).toBe(false);
        expect(reads.some((path) => path.includes('SPLATS'))).toBe(false);
    });

    it('builds the same context as the CLI: host ODBs, creature containers, XDBs and sounds', async () => {
        const deps = {
            dependencies: [
                { path: encodeString('..\\..\\Common\\System\\System') },
                { path: encodeString('..\\..\\Common\\Resources\\Resources') },
                { path: encodeString('..\\..\\Common\\FX\\FX') },
                { path: encodeString('..\\..\\Common\\NPC\\NPCs') },
                { path: encodeString('Level 1A - Surdana_splats') },
            ],
        } as LvlHeader;
        const container = writePs2Srsc([]);
        const files = new Map<string, Uint8Array>([
            ['LEVELS/LEVEL1NA/LEVEL1TS.DB', new Uint8Array()],
            ['LEVELS/LEVEL1NA/LEVEL1TS.TXD', container],
        ]);
        // Every container has its MOD / TXD / ADB; only FX has no XDB (as on the disc).
        for (const dir of ['COMMON/SYSTEM/SYSTEM', 'COMMON/RESOURES/RESOURES', 'COMMON/FX/FX', 'COMMON/NPC/NPCS'])
            for (const ext of ['ODB', 'MOD', 'TXD', 'ADB', 'SDB', ...(dir.endsWith('FX') ? [] : ['XDB'])])
                files.set(`${dir}.${ext}`, container);
        const { context } = await ootfDiscContext(
            await SrscReader.fromBytes(container),
            deps,
            'ENGLISH\\LEVELS\\LEVEL1NA',
            async (path) => files.get(path) ?? null,
        );
        expect([...context.odbs.keys()].sort()).toEqual(['FX', 'NPCS', 'RESOURES', 'SYSTEM']);
        // SYSTEM (Rynn, System.mod) and the host itself never take converted models or retargeted clips.
        expect([...(context.hostFiles?.keys() ?? [])]).toEqual(['FX']);
        expect([...(context.mods?.keys() ?? [])]).toEqual(['NPCS']);
        expect([...(context.adbs?.keys() ?? [])]).toEqual(['NPCS']);
        expect(context.hostMod).toBeDefined();
        expect(context.hostAdb).toBeDefined();
        expect(context.hostSdb).toBeDefined();
        // A class of an ODB without an XDB (FX) must get its text defaults blanked, not new FX.XDB strings.
        expect([...(context.xdbFiles ?? [])].sort()).toEqual(['NPCS.XDB', 'RESOURES.XDB', 'SYSTEM.XDB']);
        // Sounds are opt-in in the pipeline; the CLI converts them, so the dialog must too.
        expect(OOTF_DIALOG_OPTIONS.sounds).toBe(true);
    });

    it('rejects a missing required splat before exporting a pack', async () => {
        const template = await SrscReader.fromBytes(writePs2Srsc([]));
        await expect(ootfDiscContext(template, header, 'LEVELS\\LEVEL1NA', async () => null)).rejects.toThrow(
            'missing its splat DB',
        );
        await expect(
            ootfDiscContext(template, header, 'LEVELS\\LEVEL1NA', async (p) =>
                p.endsWith('.DB') ? new Uint8Array() : null,
            ),
        ).rejects.toThrow('missing its splat TXD');
    });
});
