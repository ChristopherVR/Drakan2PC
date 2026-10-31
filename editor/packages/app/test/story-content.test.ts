// Synthetic only: the Story session's new sequences, new dialog lines and handler phase edits (no game bytes).
import { deflateSync } from 'node:zlib';
import { describe, expect, it } from 'vitest';
import {
    HANDLER_PHASES,
    ODB_CLASS_TYPE,
    ODB_FIELDS_TYPE,
    SSD_SEQUENCE,
    joinClassRecord,
    parseClassBody,
    parseSequence,
    splitClassRecord,
    type ClassFieldDef,
    type LevelStory,
    type OdbClass,
} from '@drakan/formats';
import { ContentSession, type ContentHost } from '../src/story/content.ts';

const host: ContentHost = { fs: () => null, language: () => 'ENGLISH', target: () => null, status: () => {} };

function record(name: string, data: number[]): Uint8Array {
    const body = new Uint8Array(30 + data.length * 4);
    const dv = new DataView(body.buffer);
    dv.setUint16(16, 0x1234, true);
    dv.setUint32(22, data.length, true);
    dv.setUint32(26, data.length, true);
    data.forEach((v, i) => dv.setUint32(30 + i * 4, v, true));
    return joinClassRecord(name, body);
}

const lineDefs: ClassFieldDef[] = [
    { type: 0x1009, name: 'Animation' },
    { type: 2, name: 'Delay' },
    { type: 7, name: 'Maintain special head animation during anim?' },
    { type: 0x1005, name: 'Sound' },
    { type: 7, name: 'Sound Play Mode' },
    { type: 0x0a, name: 'Debug Text' },
    { type: 7, name: 'Camera Position' },
    { type: 7, name: 'Camera Transition' },
    { type: 2, name: 'Time to Transition Camera (sec) (0 = default)' },
];
const handlerDefs: ClassFieldDef[] = [
    { type: 0x3003, name: 'Beginning Dialog Exchange' },
    { type: 7, name: 'Other' },
];

async function session(): Promise<{ s: ContentSession; story: LevelStory }> {
    const { parseDialogLine, parseConversationHandler, encodeFieldList } = await import('@drakan/formats');
    const lineData = [(1 << 16) | 9, 0, 1, (1 << 16) | 10, 1, 0, 5, 1, 0, 3009, 0x10];
    const lineCls: OdbClass = {
        id: 449,
        name: 'Farmer - C103FA01',
        group: 443,
        runtimeClass: 0x100ba,
        fields: lineDefs,
        data: lineData,
    };
    const handlerData = [(1 << 16) | 2, 7, 449];
    const handlerCls: OdbClass = {
        id: 445,
        name: 'Conversation Handler',
        group: 1,
        runtimeClass: 0x100b4,
        fields: handlerDefs,
        data: handlerData,
    };
    const story: LevelStory = {
        stem: 'LEVELX',
        language: 'ENGLISH',
        deps: {},
        folders: [{ id: 5, parent: 0, name: 'Folder' }],
        sequences: [],
        sounds: new Map(),
        lines: [parseDialogLine(lineCls)!],
        handlers: [parseConversationHandler(handlerCls)!],
        classes: new Map([
            [449, lineCls],
            [445, handlerCls],
        ]),
        files: { ssd: 'LEVELX.SSD', sdb: 'LEVELX.SDB', odb: 'LEVELX.ODB' },
        soundEntry: new Map(),
        sequenceEntry: new Map(),
        classRecord: new Map([
            [449, { sub: 443, raw: record(lineCls.name, lineData) }],
            [445, { sub: 1, raw: record(handlerCls.name, handlerData) }],
        ]),
        fieldLists: { id: 1, sub: 0, stored: new Uint8Array(deflateSync(encodeFieldList(lineDefs))) },
    };
    const s = new ContentSession(host);
    (s as unknown as { stories: Map<string, Promise<LevelStory>> }).stories.set('LEVELX', Promise.resolve(story));
    s.stems = ['LEVELX'];
    s.story = story;
    s.stem = 'LEVELX';
    return { s, story };
}

describe('story session: new content', () => {
    it('adds a sequence with an actor and exports it as a new SSD entry in its folder', async () => {
        const { s } = await session();
        const id = s.addSequence('My scene', 5);
        s.addSequenceActor(id, 'Camera', 910);
        expect(s.sequences().map((q) => q.id)).toEqual([id]);
        expect(s.sequence(id)!.actors.map((a) => a.name)).toEqual(['Camera']);
        expect(s.sequenceIsNew(id)).toBe(true);
        expect(s.count()).toBe(1);
        const ch = await s.changes();
        expect(ch).toHaveLength(1);
        expect(ch[0]).toMatchObject({ file: 'LEVELX.SSD', type: SSD_SEQUENCE, id, sub: 5, isNew: true });
        expect(parseSequence(id, 5, ch[0]!.data)!.name).toBe('My scene');
        s.revertSequence(id);
        expect(s.sequences()).toEqual([]);
        expect(() => s.addSequence('  ', 5)).toThrow(/name/);
    });

    it('adds a dialog line class and exports the class plus the extended field list', async () => {
        const { s } = await session();
        const id = s.addLine(449, 'Farmer - MYLINE', (0 << 16) | 33);
        expect(s.lineIsNew(id)).toBe(true);
        const l = s.lines().find((x) => x.classId === id)!;
        expect(l).toMatchObject({ name: 'Farmer - MYLINE', speaker: 'Farmer' });
        expect(s.lineSound(id, 0)).toBe(33);
        const ch = await s.changes();
        const cls = ch.find((c) => c.type === ODB_CLASS_TYPE)!;
        expect(cls).toMatchObject({ file: 'LEVELX.ODB', id, sub: 443, isNew: true });
        expect(splitClassRecord(cls.data).name).toBe('Farmer - MYLINE');
        const body = parseClassBody(splitClassRecord(cls.data).body)!;
        expect(body.data[10]).toBe(33);
        expect(ch.some((c) => c.type === ODB_FIELDS_TYPE && !c.isNew)).toBe(true);
        expect(() => s.addLine(999, 'x - y', 1)).toThrow(/existing dialog line/);
        s.removeNewLine(id);
        expect(s.lines().some((x) => x.classId === id)).toBe(false);
    });

    it('edits a handler phase (any length) and drops the edit when it equals the original', async () => {
        const { s } = await session();
        const newId = s.addLine(449, 'Farmer - B', 34);
        expect(s.phaseLines(445, 'beginning')).toEqual([449]);
        s.setPhaseLines(445, 'beginning', [449, newId, 449]);
        expect(s.phaseEdited(445, 'beginning')).toBe(true);
        const ch = await s.changes();
        const h = ch.find((c) => c.id === 445)!;
        expect(h.isNew).toBe(false);
        const b = parseClassBody(splitClassRecord(h.data).body)!;
        expect(b.data[0]! >>> 16).toBe(3);
        const off = b.data[0]! & 0xffff;
        expect(b.data.slice(off, off + 3)).toEqual([449, newId, 449]);
        // removing the new line also removes it from phases that use it
        s.removeNewLine(newId);
        expect(s.phaseLines(445, 'beginning')).toEqual([449, 449]);
        s.setPhaseLines(445, 'beginning', [449]);
        expect(s.phaseEdited(445)).toBe(false);
        expect(HANDLER_PHASES).toHaveLength(5);
    });
});
