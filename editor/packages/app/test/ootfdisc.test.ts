import { describe, expect, it } from 'vitest';
import { encodeString, SrscReader, writePs2Srsc, type LvlHeader } from '@drakan/formats';
import { ootfDiscContext } from '../src/editor/ootfdisc.ts';

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
