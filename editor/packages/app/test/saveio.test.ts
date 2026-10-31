import { describe, expect, it } from 'vitest';
import { nextBackupName } from '@drakan/formats';
import { backupChain, saveWithBackup, type FsDirHandle, type FsFileHandle, type SaveRef } from '../src/saveio.ts';

describe('backup rotation', () => {
    it('lists .bak, .bak.1, .bak.2 oldest first and ignores look-alikes', () => {
        const names = ['a.rsg.bak.10', 'a.rsg', 'a.rsg.bak.2', 'a.rsg.bak', 'a.rsg.bak.x', 'b.rsg.bak', 'a.rsg.bak.1'];
        expect(backupChain(names, 'a.rsg')).toEqual(['a.rsg.bak', 'a.rsg.bak.1', 'a.rsg.bak.2', 'a.rsg.bak.10']);
    });

    it('never reuses a name', () => {
        expect(nextBackupName([], 'a.rsg')).toBe('a.rsg.bak');
        expect(nextBackupName(['a.rsg.bak'], 'a.rsg')).toBe('a.rsg.bak.1');
        expect(nextBackupName(['a.rsg.bak', 'a.rsg.bak.1'], 'a.rsg')).toBe('a.rsg.bak.2');
    });
});

/** In-memory folder with the File System Access surface saveWithBackup uses. */
function fakeDir(files: Map<string, Uint8Array>): FsDirHandle {
    const fileHandle = (name: string): FsFileHandle => ({
        kind: 'file',
        name,
        getFile: async () => new File([files.get(name) as BlobPart], name),
        createWritable: async () => {
            let buf = new Uint8Array();
            return {
                write: async (d) => {
                    buf = new Uint8Array(await new Blob([d as BlobPart]).arrayBuffer());
                },
                close: async () => {
                    files.set(name, buf);
                },
            };
        },
    });
    return {
        kind: 'directory',
        name: 'slot',
        async *values() {
            for (const n of [...files.keys()]) yield fileHandle(n);
        },
        getFileHandle: async (name, o) => {
            if (!files.has(name)) {
                if (!o?.create) throw new Error('missing');
                files.set(name, new Uint8Array());
            }
            return fileHandle(name);
        },
    };
}

describe('saveWithBackup (folder handle)', () => {
    it('keeps the original as .bak, then .bak.1, and writes the new bytes', async () => {
        const files = new Map<string, Uint8Array>([['s.rsg', Uint8Array.of(1, 1)]]);
        const dir = fakeDir(files);
        const handle = await dir.getFileHandle('s.rsg');
        const ref = {
            name: 's.rsg',
            folder: '',
            size: 2,
            modified: 0,
            read: async () => new Uint8Array(),
            handle,
            dir,
        } as SaveRef;
        expect(await saveWithBackup(ref, Uint8Array.of(2))).toBe('s.rsg.bak');
        expect(await saveWithBackup(ref, Uint8Array.of(3))).toBe('s.rsg.bak.1');
        expect([...files.get('s.rsg')!]).toEqual([3]);
        expect([...files.get('s.rsg.bak')!]).toEqual([1, 1]);
        expect([...files.get('s.rsg.bak.1')!]).toEqual([2]);
    });
});
