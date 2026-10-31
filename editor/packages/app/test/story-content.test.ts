// Synthetic only: the Story session's new sequences, new dialog lines and handler phase edits (no game bytes).
import { deflateSync } from 'node:zlib';
import { describe, expect, it } from 'vitest';
import {
    HANDLER_PHASES,
    ODB_CLASS_TYPE,
    ODB_FIELDS_TYPE,
    SSD_SEQUENCE,
    joinClassRecord,
    parseSdbText,
    parseClassBody,
    parseSequence,
    splitClassRecord,
    type ClassFieldDef,
    type LevelStory,
    type OdbClass,
} from '@drakan/formats';
import { ContentSession, type ContentHost } from '../src/story/content.ts';
import { StorySession } from '../src/story/session.ts';

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

/** A silent descriptor of the known layout: `u16 len + name`, 36 header bytes, 12 tail bytes. */
function silentDescriptor(name: string): Uint8Array {
    const n = name.length + 2; // NUL and padding so that 2 + n is a multiple of 4
    const out = new Uint8Array(2 + n + 36 + 12);
    const dv = new DataView(out.buffer);
    dv.setUint16(0, n, true);
    for (let i = 0; i < name.length; i++) out[2 + i] = name.charCodeAt(i);
    dv.setUint32(2 + n + 32, 2, true);
    return out;
}

describe('story session: composed dialogue and cut scenes', () => {
    it('adds a line with text and sound to a conversation phase in one step', async () => {
        const { s, story } = await session();
        story.sounds.set(33, {
            id: 33,
            sub: 1,
            name: 'MYSND01',
            text: '',
            rate: 0,
            seconds: 2,
            descriptor: silentDescriptor('MYSND01'),
        });
        story.soundEntry.set(33, { type: 0x302, id: 33, sub: 1, size: 100 } as never);
        const id = s.composeLine({
            speaker: 'Farmer',
            text: 'Welcome, traveller.',
            soundRef: 33,
            soundName: '',
            handlerId: 445,
            phase: 'beginning',
            position: 0,
        });
        expect(s.lines().find((l) => l.classId === id)).toMatchObject({ name: 'Farmer - MYSND01', speaker: 'Farmer' });
        expect(s.lineSound(id, 0)).toBe(33);
        expect(s.phaseLines(445, 'beginning')).toEqual([id, 449]);
        expect(s.text(33)).toBe('Welcome, traveller.');
        const ch = await s.changes();
        const sdb = ch.find((c) => c.file === 'LEVELX.SDB')!;
        expect(parseSdbText(sdb.data)!.text).toBe('Welcome, traveller.');
        expect(ch.filter((c) => c.file === 'LEVELX.ODB').map((c) => [c.id, c.isNew])).toEqual(
            expect.arrayContaining([
                [id, true],
                [445, false],
            ]),
        );
    });

    it('can create the line class alone, for a placed handler that keeps its own list', async () => {
        const { s } = await session();
        const id = s.composeLine(
            { speaker: 'Farmer', text: '', soundRef: 33, soundName: 'SND', handlerId: 0, phase: 'beginning' },
            { classOnly: true },
        );
        expect(s.lines().some((l) => l.classId === id && l.name === 'Farmer - SND')).toBe(true);
        expect(s.phaseEdited(445)).toBe(false);
        expect(s.phaseLines(445, 'beginning')).toEqual([449]);
    });

    it('leaves nothing behind when a part of the line is refused', async () => {
        const { s } = await session();
        const draft = {
            speaker: 'Farmer',
            text: '',
            soundRef: 33,
            soundName: 'X',
            handlerId: 445,
            phase: 'beginning' as const,
        };
        expect(() => s.composeLine({ ...draft, soundRef: 0 })).toThrow(/sound/);
        expect(() => s.composeLine({ ...draft, speaker: '' })).toThrow(/speaker/);
        expect(() => s.composeLine({ ...draft, handlerId: 1 })).toThrow(/conversation/);
        expect(() => s.composeLine({ ...draft, text: 'hi', soundRef: (3 << 16) | 4 })).toThrow(/subtitle/);
        expect(s.count()).toBe(0);
        expect(s.lines()).toHaveLength(1);
    });

    it('adds a cut scene from camera points and steps and finds the level camera object', async () => {
        const { s } = await session();
        const first = s.addSequence('Existing', 5);
        s.addSequenceActor(first, 'Camera', 2191);
        expect(s.cameraRef()).toBe(2191);
        const id = s.addCutscene({
            folder: 5,
            name: 'Gate ambush',
            cameraRef: s.cameraRef(),
            camera: [{ time: 0, pos: [10, 20, 30], yaw: 0, pitch: 0 }],
            steps: [{ kind: 'sound', time: 1, soundRef: 33 }],
        });
        expect(id).toBeGreaterThan(first);
        const ch = await s.changes();
        const e = ch.find((c) => c.id === id)!;
        expect(e).toMatchObject({ file: 'LEVELX.SSD', type: SSD_SEQUENCE, sub: 5, isNew: true });
        const seq = parseSequence(id, 5, e.data)!;
        expect(seq.name).toBe('Gate ambush');
        expect(seq.actors[0]).toMatchObject({ name: 'Camera', ref: 2191, actionCount: 4 });
        expect(() => s.addCutscene({ folder: 5, name: '', cameraRef: 1, camera: [], steps: [] })).toThrow(/Name/);
    });

    it('carries dialogue and cut scene entries in the Story pack session', async () => {
        const { s } = await session();
        s.addSequence('Scene', 5);
        const pack = new StorySession({ call: (async () => null) as never, target: () => null, status: () => {} });
        pack.addChangeSource({
            changes: () => s.changes(),
            subscribe: (fn) => s.subscribe(fn),
            count: () => s.count(),
        });
        const ch = await pack.changes();
        expect(ch.map((c) => c.file)).toEqual(['LEVELX.SSD']);
        expect(pack.editCount()).toBe(1);
        let told = 0;
        pack.subscribe(() => told++);
        s.addSequence('Another', 5);
        expect(told).toBe(1);
    });
});
