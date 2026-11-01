// Synthetic only: authoring helpers for new dialogue, cut scenes and quest wiring (no game bytes).
import { describe, expect, it } from 'vitest';
import { parseActions, parseSequence, sequenceNameLen, writeSequence } from '@drakan/formats';
import {
    ApplyTemplateCommand,
    ModError,
    QUEST_MESSAGE,
    SOUND_PARAMS_DEFAULT,
    buildCutscene,
    checkCutscene,
    cutsceneBytes,
    dialogLineName,
    lookAtAngles,
    lookDirection,
    quatYawPitch,
    planDialogueLine,
    questGiverWiring,
    wireQuestGiver,
    yawPitchQuat,
    type CutsceneSpec,
    type LevelObject,
} from '../src/index.ts';

const D2R = Math.PI / 180;

describe('new dialogue', () => {
    it('names a line after its speaker and sound, and refuses bad names', () => {
        expect(dialogLineName('  Farmer ', 'MYLINE 01')).toBe('Farmer - MYLINE_01');
        expect(dialogLineName('Farmer', '')).toBe('Farmer');
        expect(() => dialogLineName('', 'X')).toThrow(ModError);
        expect(() => dialogLineName('A - B', 'X')).toThrow(/ - /);
        expect(() => dialogLineName('x'.repeat(70), 'X')).toThrow(/longer/);
    });

    it('inserts the new class into the phase list at a position', () => {
        const d = {
            speaker: 'Farmer',
            text: 'Hi',
            soundRef: 33,
            soundName: 'C1',
            handlerId: 5,
            phase: 'beginning' as const,
        };
        expect(planDialogueLine(d, [10, 11], 99).phaseLines).toEqual([10, 11, 99]);
        const mid = planDialogueLine({ ...d, position: 1 }, [10, 11], 99);
        expect(mid.phaseLines).toEqual([10, 99, 11]);
        expect(mid.insertedAt).toBe(1);
        expect(planDialogueLine({ ...d, position: 50 }, [10], 99).phaseLines).toEqual([10, 99]);
        expect(() => planDialogueLine({ ...d, soundRef: 0 }, [], 99)).toThrow(/sound/);
        expect(() => planDialogueLine({ ...d, soundRef: 1 << 16 }, [], 99)).toThrow(/sound/);
    });
});

const spec = (over: Partial<CutsceneSpec> = {}): CutsceneSpec => ({
    id: 700,
    folder: 5,
    name: 'Ambush at the gate',
    cameraRef: 910,
    camera: [
        { time: 0, pos: [1000, 2000, 3000], yaw: 0, pitch: 0 },
        { time: 4, pos: [1500, 2000, 3500], yaw: 90, pitch: 10 },
    ],
    steps: [
        { kind: 'message', time: 3, actorName: 'Orc', actorRef: 7726, message: 0x1001, param: 0 },
        { kind: 'sound', time: 1.5, soundRef: 33 },
        { kind: 'animation', time: 1, actorName: 'Orc', actorRef: 7726, animation: 3009 },
    ],
    ...over,
});

describe('new cut scene', () => {
    it('writes a camera actor and one actor per object, markers around ordered actions', () => {
        const seq = buildCutscene(spec());
        expect(seq.actors.map((a) => [a.name, a.slot, a.ref])).toEqual([
            ['Camera', 1, 910],
            ['Orc', 2, 7726],
        ]);
        expect(seq.slots).toBe(3);
        const cam = parseActions(seq.actors[0]!)!;
        expect(cam.map((a) => a.kind)).toEqual(['marker', 'transform', 'sound', 'transform', 'marker']);
        expect(cam.map((a) => a.time)).toEqual([0, 0, 1.5, 4, 4]);
        expect(cam[0]!.fields[0]!.value).toBe(0);
        expect(cam.at(-1)!.fields[0]!.value).toBe(1);
        expect(cam[2]!.fields.find((f) => f.name === 'params')!.value).toBe(SOUND_PARAMS_DEFAULT);
        const orc = parseActions(seq.actors[1]!)!;
        expect(orc.map((a) => a.kind)).toEqual(['marker', 'animation', 'message', 'marker']);
        expect(orc.map((a) => a.time)).toEqual([0, 1, 3, 4]);
        // times never go backwards inside an actor (disc rule)
        for (const a of seq.actors) {
            const t = parseActions(a)!.map((x) => x.time);
            expect(t).toEqual([...t].sort((x, y) => x - y));
        }
    });

    it('round-trips through the SSD record layout with the disc name padding', () => {
        const bytes = cutsceneBytes(spec());
        const back = parseSequence(700, 5, bytes)!;
        expect(back.name).toBe('Ambush at the gate');
        expect(back.nameLen).toBe(sequenceNameLen('Ambush at the gate'));
        expect((2 + back.nameLen) % 4).toBe(0);
        expect(back.actors).toHaveLength(2);
        expect(back.actors[0]!.actionCount).toBe(5);
        expect(Array.from(writeSequence(back))).toEqual(Array.from(bytes));
    });

    it('keeps the end marker at the later of the duration and the last time', () => {
        const long = buildCutscene(spec({ duration: 12 }));
        expect(parseActions(long.actors[0]!)!.at(-1)!.time).toBe(12);
    });

    it('reports problems before anything is built', () => {
        expect(checkCutscene(spec({ name: '  ' })).some((i) => i.level === 'error')).toBe(true);
        expect(
            checkCutscene(spec({ cameraRef: 0 }))
                .map((i) => i.message)
                .join(),
        ).toMatch(/Camera object/);
        expect(checkCutscene(spec({ camera: [], steps: [] })).map((i) => i.level)).toEqual(['warn']);
        const bad = checkCutscene(spec({ steps: [{ kind: 'sound', time: -1, soundRef: 0 }] }));
        expect(bad.filter((i) => i.level === 'error')).toHaveLength(2);
        expect(() => buildCutscene(spec({ name: 'café' }))).toThrow(/ASCII/);
        expect(() => buildCutscene(spec({ id: 0 }))).toThrow(/id/);
        expect(() =>
            buildCutscene(spec({ steps: [{ kind: 'animation', time: 0, actorName: 'x', actorRef: 0, animation: 5 }] })),
        ).toThrow(/object/);
    });

    it('builds unit quaternions and look-at angles', () => {
        for (const [y, p] of [
            [0, 0],
            [90, 0],
            [30, 45],
            [-170, -20],
        ] as const)
            expect(Math.hypot(...yawPitchQuat(y, p))).toBeCloseTo(1, 6);
        expect(yawPitchQuat(0, 0)).toEqual([0, 0, 0, 1]);
        // yaw 0 looks along -Z, yaw 90 along +X, pitch > 0 looks down (convention proven in game)
        expect(lookAtAngles([0, 0, 0], [0, 0, -100]).yaw).toBeCloseTo(0);
        expect(lookAtAngles([0, 0, 0], [100, 0, 0]).yaw).toBeCloseTo(90);
        expect(lookAtAngles([0, 0, 0], [-100, 0, 0]).yaw).toBeCloseTo(-90);
        expect(lookAtAngles([0, 0, 0], [0, 100, -100]).pitch).toBeCloseTo(-45);
    });

    it('key quaternions look where yaw and pitch say (row-vector convention, forward -Z)', () => {
        for (const [y, p] of [
            [0, 0],
            [90, 0],
            [-90, 10],
            [145, -20],
            [30, 45],
        ] as const) {
            const f = lookDirection(yawPitchQuat(y, p));
            const [sy, cy, sp, cp] = [Math.sin(y * D2R), Math.cos(y * D2R), Math.sin(p * D2R), Math.cos(p * D2R)];
            expect(f[0]).toBeCloseTo(cp * sy, 6);
            expect(f[1]).toBeCloseTo(-sp, 6);
            expect(f[2]).toBeCloseTo(-cp * cy, 6);
            const back = quatYawPitch(yawPitchQuat(y, p));
            expect(back.pitch).toBeCloseTo(p, 4);
            expect(Math.cos(back.yaw * D2R)).toBeCloseTo(cy, 4);
        }
        // the look-at angles and the quaternion agree for an arbitrary target
        const a = lookAtAngles([0, 0, 0], [300, -100, -200]);
        const f = lookDirection(yawPitchQuat(a.yaw, a.pitch));
        const n = Math.hypot(300, 100, 200);
        expect(f[0]).toBeCloseTo(300 / n, 5);
        expect(f[1]).toBeCloseTo(-100 / n, 5);
        expect(f[2]).toBeCloseTo(-200 / n, 5);
        // a disc key (Valley sequence 497 at 12.13 s): the camera at (604949, 239417, 161346) looks toward -X (+Z a little),
        // where Zola Dane and the two guards stand
        const d = lookDirection([0.016, -0.852, -0.025, 0.522]);
        expect(d[0]).toBeLessThan(-0.8);
        expect(Math.abs(d[1])).toBeLessThan(0.15);
    });
});

describe('quest wiring', () => {
    const handler = (): LevelObject =>
        ({
            id: 50,
            class: 'Quest Giver',
            resource: 'r',
            transform: { pos: [0, 0, 0], rot: [0, 0, 0] },
            fields: [],
            links: [],
        }) as unknown as LevelObject;

    it('describes the messages a quest giver sends', () => {
        const w = questGiverWiring(77);
        expect(w.linkTo).toBe(77);
        expect(w.fields['Message to send after Beginning dialog']).toBe(QUEST_MESSAGE.initiate);
        expect(w.fields['Message to send after Complete dialog']).toBe(QUEST_MESSAGE.complete);
        expect(w.fields['Is there a quest?']).toBe(1);
    });

    it('sets the fields the class has, links the quest object and reports the rest', () => {
        const o = handler();
        const env = {
            rflOf: () => 0x34,
            schemaFor: () => ({
                fields: [
                    { index: 0, name: 'Is there a quest?', type: 7, def: 0 },
                    { index: 1, name: 'Message to send after Beginning dialog', type: 7, def: 0 },
                ],
            }),
        };
        const missing = wireQuestGiver(o, env as never, 77);
        expect(missing).toEqual(['Message to send after Complete dialog']);
        expect(o.links).toEqual([77]);
        expect(o.fields!.map((f) => [f.name, f.value])).toEqual([
            ['Is there a quest?', 1],
            ['Message to send after Beginning dialog', QUEST_MESSAGE.initiate],
        ]);
    });
});

describe('undo labels', () => {
    it('name the objects a template creates', () => {
        const mk = (id: number, cls: string): LevelObject => ({ id, class: cls }) as unknown as LevelObject;
        const none = { links: [], notes: [], warnings: [] };
        const one = new ApplyTemplateCommand({ objects: [mk(12, 'Quest')], ...none }, 'Place quest "Find the gem"');
        expect(one.label).toBe('Place quest "Find the gem": Quest #12');
        const many = new ApplyTemplateCommand({ objects: [1, 2, 3, 4, 5].map((i) => mk(i, 'Box')), ...none }, 'Setup');
        expect(many.label).toBe('Setup: Box #1, Box #2, Box #3 and 2 more');
        expect(new ApplyTemplateCommand({ objects: [], ...none }, 'Nothing').label).toBe('Nothing');
    });
});
