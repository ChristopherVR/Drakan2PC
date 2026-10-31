// Story pack layout and quest/string change sets (synthetic data only).
import { describe, expect, it } from 'vitest';
import { strFromU8, unzipSync } from 'fflate';
import {
    FIELD_INT,
    FIELD_STRING,
    decodeXdbSlots,
    draftOf,
    encodeFieldList,
    joinClassRecord,
    parseClassBody,
    splitClassRecord,
    type ClassFieldDef,
    type QuestClass,
    type XdbString,
} from '@drakan/formats';
import { ModError, buildStoryPackFiles, questChanges, stringChanges, zipModFiles } from '../src/index.ts';

const DEFS: ClassFieldDef[] = [
    { type: 7, name: 'Type' },
    { type: 7, name: 'SubQuest?' },
    { type: FIELD_STRING, name: 'Journal Entry' },
    { type: FIELD_STRING, name: 'Map Text' },
    { type: FIELD_INT, name: 'Map X Coordinate' },
    { type: FIELD_INT, name: 'Map Y Coordinate' },
];

function record(name: string, data: number[]): Uint8Array {
    const body = new Uint8Array(30 + data.length * 4);
    const dv = new DataView(body.buffer);
    dv.setUint32(22, data.length, true);
    dv.setUint32(26, data.length, true);
    data.forEach((v, i) => dv.setUint32(30 + i * 4, v, true));
    return joinClassRecord(name, body);
}

async function zlib(raw: Uint8Array): Promise<Uint8Array> {
    const cs = new CompressionStream('deflate');
    const w = cs.writable.getWriter();
    void w.write(raw as Uint8Array<ArrayBuffer>).then(() => w.close());
    return new Uint8Array(await new Response(cs.readable).arrayBuffer());
}

const cls: QuestClass = {
    classId: 4167,
    name: 'Speak',
    kind: 'journal',
    optional: false,
    journalId: 22,
    mapTextId: 0,
    mapX: 0,
    mapY: 0,
    groupId: 3055,
    groupName: '1 - Surdana',
    chapter: 1,
    chapterName: 'Surdana',
};
const meta = { id: 'story.test', target: { profile: 'pal' } };

describe('story pack', () => {
    it('lays out overrides and new entries and rejects empty ones', () => {
        const files = buildStoryPackFiles(meta, [
            { file: 'SYSTEM.XDB', type: 0x400, id: 22, sub: 4, data: new Uint8Array([1]), isNew: false },
            { file: 'SYSTEM.XDB', type: 0x400, id: 114, sub: 4, data: new Uint8Array([2]), isNew: true },
        ]);
        expect(Object.keys(files).sort()).toEqual([
            'mod.toml',
            'new/SYSTEM.XDB/0400_114_4.bin',
            'overrides/SYSTEM.XDB/0400_22_4.bin',
        ]);
        expect(strFromU8(files['mod.toml']!)).toContain('id = "story.test"');
        expect(Object.keys(unzipSync(zipModFiles(files))).length).toBe(3);
        expect(() =>
            buildStoryPackFiles(meta, [
                { file: 'A.XDB', type: 0x400, id: 1, sub: 0, data: new Uint8Array(0), isNew: false },
            ]),
        ).toThrow(ModError);
    });

    it('turns string edits into entries', () => {
        const a = new Map<number, XdbString>([[22, { id: 22, sub: 4, text: ['old', '', '', '', '', '', '', ''] }]]);
        const b = new Map(a);
        b.set(22, { id: 22, sub: 4, text: ['new', '', '', '', '', '', '', ''] });
        b.set(114, { id: 114, sub: 4, text: ['added', '', '', '', '', '', '', ''] });
        const c = stringChanges('SYSTEM.XDB', a, b);
        expect(c.map((x) => [x.id, x.isNew])).toEqual([
            [22, false],
            [114, true],
        ]);
        expect(decodeXdbSlots(c[1]!.data)[0]).toBe('added');
    });
});

describe('quest changes', () => {
    const rec = record('Speak', [1, 0, 22, 0, 0, 0]);
    const src = async (withLists: boolean) => ({
        file: 'SYSTEM.ODB',
        classes: new Map([[4167, { id: 4167, group: 3055, record: rec, defs: DEFS, draft: draftOf(cls) }]]),
        usedIds: new Set([4167, 4300]),
        ...(withLists ? { fieldLists: { id: 0, sub: 0, stored: await zlib(encodeFieldList(DEFS)) } } : {}),
    });

    it('edits an existing class in place and skips unchanged drafts', async () => {
        const s = await src(false);
        const same = await questChanges(s, new Map([[4167, draftOf(cls)]]), []);
        expect(same.changes).toEqual([]);
        const r = await questChanges(
            s,
            new Map([[4167, { ...draftOf(cls), mapX: 40, mapY: 50, kind: 'both' as const }]]),
            [],
        );
        expect(r.changes.length).toBe(1);
        const c = r.changes[0]!;
        expect([c.type, c.id, c.sub, c.isNew]).toEqual([0x102, 4167, 3055, false]);
        expect(parseClassBody(splitClassRecord(c.data).body)!.data).toEqual([2, 0, 22, 0, 40, 50]);
    });

    it('adds a quest with a free id and appends its field list', async () => {
        const r = await questChanges(await src(true), new Map(), [
            { templateId: 4167, group: 3056, name: 'My New Quest', draft: { ...draftOf(cls), journalId: 114 } },
        ]);
        expect(r.newIds).toEqual([4000]);
        const [rec2, lists] = r.changes;
        expect([rec2!.type, rec2!.id, rec2!.sub, rec2!.isNew]).toEqual([0x102, 4000, 3056, true]);
        expect(splitClassRecord(rec2!.data).name).toBe('My New Quest');
        expect([lists!.type, lists!.isNew]).toEqual([0x103, false]);
        await expect(
            questChanges(await src(false), new Map(), [{ templateId: 1, group: 1, name: 'x', draft: draftOf(cls) }]),
        ).rejects.toThrow(/template/);
    });
});
