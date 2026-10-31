import {
    HANDLER_PHASES,
    addActor,
    insertAction,
    newActionBytes,
    newSequence,
    parseActions,
    sequenceNameLen,
    splitLineName,
    writeSequence,
    type HandlerPhaseKey,
    type SsdActor,
    type SsdSequence,
} from '@drakan/formats';
import type { LevelObject } from './document.ts';
import { addLink, schemaField, type LogicEnv } from './logic.ts';
import { ModError } from './mod.ts';
import { setObjectField } from './schema.ts';

/**
 * Authoring helpers for NEW story content: a dialogue line (speaker, text, sound) joined to a conversation, a cut scene
 * sequence (camera points and ordered steps) and a quest placed in a level. Pure and synthetic-testable; the panels in
 * `app/src/story/compose.ts` call them. What the bytes are checked against is in docs/reference/level-format.md section 14.
 */

// ---- dialogue ---------------------------------------------------------------------------------------------------------

/** Longest class name the line editor writes (the longest disc name is far shorter). */
export const MAX_LINE_NAME = 60;

/** Class name of a new line: `Speaker - SOUNDNAME` (the disc's own pattern; the sound part is informational). */
export function dialogLineName(speaker: string, soundName: string): string {
    const who = speaker.replace(/\s+/g, ' ').trim();
    if (!who) throw new ModError('Give the line a speaker (for example Farmer).');
    if (/ - /.test(who)) throw new ModError('The speaker cannot contain " - ".');
    const snd = soundName.trim().replace(/\s+/g, '_');
    const name = snd ? `${who} - ${snd}` : who;
    if (name.length > MAX_LINE_NAME) throw new ModError(`The line name is longer than ${MAX_LINE_NAME} characters.`);
    return name;
}

export interface DialogueDraft {
    speaker: string;
    /** Subtitle shown while the sound plays (stored on the sound's descriptor, shared by every user of that sound). */
    text: string;
    /** `(db << 16) | id` of the sound to play. */
    soundRef: number;
    soundName: string;
    handlerId: number;
    phase: HandlerPhaseKey;
    /** Index in the phase list the line is inserted at; the end when omitted. */
    position?: number;
}

export interface DialoguePlan {
    name: string;
    speaker: string;
    soundRef: number;
    /** Phase lines after the insert (refs as stored: db 0 = the level ODB). */
    phaseLines: number[];
    insertedAt: number;
}

/** What adding the line does, without touching anything: its class name and the new phase list. */
export function planDialogueLine(
    d: DialogueDraft,
    currentPhaseLines: readonly number[],
    newClassId: number,
): DialoguePlan {
    if (!d.soundRef || (d.soundRef & 0xffff) === 0) throw new ModError('Pick the sound the line plays.');
    if (!HANDLER_PHASES.some((p) => p.key === d.phase)) throw new ModError(`Unknown conversation phase ${d.phase}.`);
    const name = dialogLineName(d.speaker, d.soundName);
    const at = Math.max(0, Math.min(currentPhaseLines.length, d.position ?? currentPhaseLines.length));
    const phaseLines = [...currentPhaseLines];
    phaseLines.splice(at, 0, newClassId >>> 0);
    return { name, speaker: splitLineName(name).speaker, soundRef: d.soundRef >>> 0, phaseLines, insertedAt: at };
}

// ---- cut scenes -------------------------------------------------------------------------------------------------------

export interface CameraPoint {
    /** Seconds from the start of the scene. */
    time: number;
    /** Game units. */
    pos: [number, number, number];
    /** Degrees about the vertical axis / tilt. The quaternion convention is inferred (see level-format.md 14). */
    yaw: number;
    pitch: number;
}

export type CutsceneStep =
    | { kind: 'sound'; time: number; soundRef: number }
    | { kind: 'animation'; time: number; actorName: string; actorRef: number; animation: number }
    | { kind: 'message'; time: number; actorName: string; actorRef: number; message: number; param: number };

export interface CutsceneSpec {
    id: number;
    /** `0x312` folder (SRSC sub) the sequence is listed in. */
    folder: number;
    name: string;
    /** Level object id of the `Camera` object (every level has its own: 910, 2191, 360, ...). */
    cameraRef: number;
    camera: CameraPoint[];
    steps: CutsceneStep[];
    /** Seconds the scene lasts at least (the end marker goes at the later of this and the last point / step). */
    duration?: number;
}

/** Sound action parameter word seen on 809 of the disc's sound actions (`0x82710`: level 10000 in the low half). */
export const SOUND_PARAMS_DEFAULT = 0x82710;

const D2R = Math.PI / 180;

/** Quaternion (x y z w) of a yaw about the vertical axis followed by a pitch about X. Convention inferred. */
export function yawPitchQuat(yawDeg: number, pitchDeg: number): [number, number, number, number] {
    const hy = (yawDeg * D2R) / 2;
    const hp = (pitchDeg * D2R) / 2;
    const [sy, cy, sp, cp] = [Math.sin(hy), Math.cos(hy), Math.sin(hp), Math.cos(hp)];
    // q = qyaw * qpitch, with qyaw = (0, sy, 0, cy) and qpitch = (sp, 0, 0, cp)
    return [cy * sp, sy * cp, -sy * sp + 0, cy * cp];
}

/** Yaw and pitch (degrees) that point from `from` to `to` (identity looks along +Z, Y up; inferred convention). */
export function lookAtAngles(
    from: readonly [number, number, number],
    to: readonly [number, number, number],
): { yaw: number; pitch: number } {
    const dx = to[0] - from[0];
    const dy = to[1] - from[1];
    const dz = to[2] - from[2];
    return { yaw: Math.atan2(dx, dz) / D2R, pitch: -Math.atan2(dy, Math.hypot(dx, dz)) / D2R };
}

export interface CutsceneIssue {
    level: 'error' | 'warn';
    message: string;
}

/** Problems of a spec: errors stop the export, warnings are shown beside the form. */
export function checkCutscene(spec: CutsceneSpec): CutsceneIssue[] {
    const out: CutsceneIssue[] = [];
    const err = (message: string): void => void out.push({ level: 'error', message });
    const warn = (message: string): void => void out.push({ level: 'warn', message });
    if (!spec.name.trim()) err('Name the cut scene.');
    if (spec.name.length > 90) err('The name is longer than 90 characters.');
    if (!/^[\x20-\x7e]*$/.test(spec.name)) err('The name may only use plain ASCII characters.');
    if (!Number.isFinite(spec.id) || spec.id < 1 || spec.id > 0xffff) err('The sequence id must be 1 to 65535.');
    const times = [...spec.camera.map((c) => c.time), ...spec.steps.map((s) => s.time)];
    if (times.some((t) => !Number.isFinite(t) || t < 0)) err('Every time must be a number of seconds, 0 or more.');
    if (!spec.camera.length) warn('No camera points: the scene plays without a camera path.');
    else if (!spec.cameraRef) err('Pick the level’s Camera object (read from its other cut scenes).');
    for (const p of spec.camera) if (p.pos.some((v) => !Number.isFinite(v))) err('A camera point has no position.');
    for (const s of spec.steps) {
        if (s.kind === 'sound' && (s.soundRef & 0xffff) === 0) err('A sound step has no sound.');
        if (s.kind !== 'sound' && !s.actorRef) err(`A ${s.kind} step needs the object it acts on.`);
        if (s.kind === 'animation' && !s.animation) err('An animation step needs an animation id.');
    }
    return out;
}

const byTime = <T extends { time: number }>(a: readonly T[]): T[] => [...a].sort((x, y) => x.time - y.time);

function put(actor: SsdActor, bytes: Uint8Array): void {
    const r = insertAction(actor, bytes);
    actor.actions = r.actions;
    actor.actionCount = r.actionCount;
}

/**
 * The sequence a spec describes, shaped like the disc's: a `Camera` actor (start marker 0 at 0, a transform key per point,
 * the sound cues, an end marker 1) and one actor per acted-on object with its animations and messages between the same two
 * markers (disc census, 2026-11-01: markers 0 / 1 open and close 540 / 419 actors, sound params `0x82710`).
 */
export function buildCutscene(spec: CutsceneSpec): SsdSequence {
    const issues = checkCutscene(spec).filter((i) => i.level === 'error');
    if (issues.length) throw new ModError(issues[0]!.message);
    const seq = newSequence(spec.id, spec.folder, spec.name.trim());
    const end = Math.max(spec.duration ?? 0, ...spec.camera.map((c) => c.time), ...spec.steps.map((s) => s.time));
    const marker = (a: SsdActor, value: number, t: number): void => put(a, newActionBytes(5, t, { value }));
    const cam = addActor(seq, 'Camera', spec.cameraRef);
    marker(cam, 0, 0);
    for (const p of byTime(spec.camera)) {
        const [qx, qy, qz, qw] = yawPitchQuat(p.yaw, p.pitch);
        put(cam, newActionBytes(0, p.time, { qx, qy, qz, qw, x: p.pos[0], y: p.pos[1], z: p.pos[2] }));
    }
    for (const s of byTime(spec.steps))
        if (s.kind === 'sound')
            put(cam, newActionBytes(3, s.time, { sound: s.soundRef, params: SOUND_PARAMS_DEFAULT }));
    const actors = new Map<number, SsdActor>();
    for (const s of byTime(spec.steps)) {
        if (s.kind === 'sound') continue;
        let a = actors.get(s.actorRef);
        if (!a) {
            a = addActor(seq, s.actorName.trim() || `Object ${s.actorRef}`, s.actorRef);
            marker(a, 0, 0);
            actors.set(s.actorRef, a);
        }
        put(
            a,
            s.kind === 'animation'
                ? newActionBytes(1, s.time, { mode: 0, animation: s.animation })
                : newActionBytes(7, s.time, { message: s.message, param: s.param }),
        );
    }
    marker(cam, 1, end);
    for (const a of actors.values()) marker(a, 1, end);
    return seq;
}

/** The sequence as the exported entry bytes, plus a re-read check (a spec the layout cannot round-trip throws). */
export function cutsceneBytes(spec: CutsceneSpec): Uint8Array {
    const seq = buildCutscene(spec);
    const bytes = writeSequence(seq);
    for (const a of seq.actors) {
        const list = parseActions(a);
        if (!list || list.length !== a.actionCount) throw new ModError(`actor ${a.name}: actions do not re-read`);
    }
    return bytes;
}

/** Header check used by the tests and the panel: the stored name length follows the disc's padding rule. */
export const storedNameLen = (name: string): number => sequenceNameLen(name);

// ---- quest placement --------------------------------------------------------------------------------------------------

/** Message values by game name (`logic.ts` ordinals, level-format.md section 15). */
export const QUEST_MESSAGE = { initiate: 31, complete: 18 } as const;

export interface QuestGiverWiring {
    /** Fields written on the placed conversation handler, by field name. */
    fields: Record<string, number>;
    /** The handler gets a link to the quest object. */
    linkTo: number;
}

/**
 * How a quest giver is wired to the quest state: after its Beginning dialog it sends Quest Initiate and after its Complete
 * dialog Quest Complete along its links, so it links to the quest object. Inferred from the disc's quest givers (the fields
 * and ordinals are decoded; that the handler's messages drive the quest flag was not run in game).
 */
export function questGiverWiring(questObjectId: number): QuestGiverWiring {
    const begin = HANDLER_PHASES.find((p) => p.key === 'beginning')!.message;
    const done = HANDLER_PHASES.find((p) => p.key === 'complete')!.message;
    return {
        fields: { 'Is there a quest?': 1, [begin]: QUEST_MESSAGE.initiate, [done]: QUEST_MESSAGE.complete },
        linkTo: questObjectId,
    };
}

/**
 * Apply `questGiverWiring` to a placed conversation handler: the three fields and the link. Returns the field names the
 * handler's class does not have (a handler without a quest field is left alone for that field).
 */
export function wireQuestGiver(o: LevelObject, env: LogicEnv, questObjectId: number): string[] {
    const w = questGiverWiring(questObjectId);
    const schema = env.schemaFor(o.resource);
    const missing: string[] = [];
    for (const [name, value] of Object.entries(w.fields)) {
        const f = schemaField(schema, name);
        if (f) setObjectField(o, f, value);
        else missing.push(name);
    }
    addLink(o, w.linkTo);
    return missing;
}
