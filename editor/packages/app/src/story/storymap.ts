// What the story overlay draws in the level view, worked out from the level document alone (no three.js, no DOM):
//   cut scene players  a badge, the camera path of every sequence they play, lines to the objects those sequences move,
//                      and the trigger boxes (or other objects) that start them
//   shops              a badge with the shop name and how many items it sells
//   quests             a badge on the quest object, its start and finish boxes coloured, lines to other starters
//   conversations      a badge on every character the player can talk to
// The level data decides what is a cut scene player (logic role), a quest (logic role), a shop (vendor schema) and a
// talking character (conversation fields). Names and paths come from the story sessions through `StoryMapSource`.

import {
    fieldValueOf,
    lookAtAngles,
    logicOf,
    outgoingMessages,
    resourceParts,
    volumesOf,
    type BoxVolume,
    type CameraPoint,
    type LevelDocument,
    type LevelObject,
    type LogicEnv,
} from '@drakan/editor-core';
import { MESSAGE_QUEST_COMPLETE } from '@drakan/formats';
import type { StoryGroup } from '../scene/viewopts.ts';

export type Vec3 = [number, number, number];

/** The parts of a sequence the overlay draws (from the content session). */
export interface StoryMapSequence {
    id: number;
    name: string;
    /** Actors by name and level object ref (the `Camera` actor included). */
    actors: { name: string; ref: number }[];
    /** Camera keyframe positions in game units, in time order. */
    camera: Vec3[];
    seconds: number;
}

export interface StoryMapSource {
    sequence?(id: number): StoryMapSequence | null;
    /** Journal / map title of a quest class. */
    questTitle?(classId: number): string | null;
    /** Stock size of a shop object, or null when the object is not a shop. */
    vendor?(o: LevelObject): { items: number } | null;
    /**
     * The conversation of a talking object: its handler class's name, whether it gives a quest, and who speaks its first
     * lines (from the object's own phase list, which overrides the class's).
     */
    conversation?(o: LevelObject): { name: string; hasQuest: boolean; speakers: string[] } | null;
}

/** Which story editor a mark opens on double-click. */
export type StoryFocus =
    | { kind: 'sequence'; id: number }
    | { kind: 'vendor'; object: number }
    | { kind: 'quest'; classId: number }
    | { kind: 'conversation'; classId: number };

export interface StoryArea {
    /** The volume object (a trigger detector). */
    id: number;
    box: BoxVolume;
    tone: 'trigger' | 'start' | 'complete';
    label: string;
}

export interface StoryLine {
    from: Vec3;
    to: Vec3;
    /** The object at the far end. */
    target: number;
    label: string;
}

export interface StoryMark {
    group: StoryGroup;
    /** The object the badge sits on. */
    id: number;
    pos: Vec3;
    /** Badge label. */
    title: string;
    /** Tooltip lines (plain sentences). */
    detail: string[];
    areas: StoryArea[];
    lines: StoryLine[];
    /** Camera paths (one per sequence that has one), game units. */
    paths: Vec3[][];
    focus: StoryFocus | null;
}

const TALK_FIELD = 'Beginning Dialog Exchange';
/** Quest journal / map entry class (`logic.ts` in formats); the other quest role (0xdc) marks story events. */
const JOURNAL_RFL = 0x68;
const SEQ_FIELD = 'Sequence List';

const pos3 = (o: LevelObject): Vec3 => [o.transform.pos[0], o.transform.pos[1], o.transform.pos[2]];
const plural = (n: number, one: string, many = `${one}s`): string => `${n} ${n === 1 ? one : many}`;

function seqIds(o: LevelObject, env: LogicEnv): number[] {
    const v = fieldValueOf(o, env, SEQ_FIELD);
    const list = Array.isArray(v) ? v : typeof v === 'number' ? [v] : [];
    return list.map((x) => Number(x) & 0xffff).filter((x) => x > 0);
}

/** Objects whose link list includes `id`, in document order. */
function sendersTo(doc: LevelDocument, id: number): LevelObject[] {
    const out: LevelObject[] = [];
    for (const o of doc.objects()) if (o.id !== id && o.links?.includes(id)) out.push(o);
    return out;
}

const boxOf = (o: LevelObject, env: LogicEnv): BoxVolume | null =>
    (volumesOf(o, env).find((v) => v.kind === 'box') as BoxVolume | undefined) ?? null;

export interface BuildOptions {
    /** Leave out objects the view hides. */
    skip?: (id: number) => boolean;
    /** Only these groups (default: all). */
    groups?: ReadonlySet<StoryGroup>;
}

/** Every story mark of the level. Pure: the same document and sources give the same marks. */
export function buildStoryMarks(
    doc: LevelDocument,
    env: LogicEnv,
    src: StoryMapSource = {},
    opts: BuildOptions = {},
): StoryMark[] {
    const want = (g: StoryGroup): boolean => !opts.groups || opts.groups.has(g);
    const out: StoryMark[] = [];
    for (const o of doc.objects()) {
        if (opts.skip?.(o.id)) continue;
        const spec = logicOf(o, env);
        if (spec?.role === 'cutscene' && want('cutscene')) out.push(cutsceneMark(doc, env, src, o));
        else if (spec?.role === 'quest' && want('quest')) {
            // Journal entries always; the many story-event quest markers only when something in the level drives them.
            const m = questMark(doc, env, src, o);
            if (env.rflOf(o.resource) === JOURNAL_RFL || m.areas.length || m.lines.length) out.push(m);
        }
        if (want('vendor')) {
            const v = src.vendor?.(o);
            if (v) {
                out.push({
                    group: 'vendor',
                    id: o.id,
                    pos: pos3(o),
                    title: `${o.class}, ${plural(v.items, 'item')}`,
                    detail: [
                        `Shop: ${o.class}`,
                        v.items ? `Sells ${plural(v.items, 'item')}.` : 'Sells nothing yet.',
                        'Double-click to change what it sells.',
                    ],
                    areas: [],
                    lines: [],
                    paths: [],
                    focus: { kind: 'vendor', object: o.id },
                });
            }
        }
        if (want('talk') && env.schemaFor(o.resource)?.fields.some((f) => f.name === TALK_FIELD)) {
            const classId = resourceParts(o.resource).classId;
            const c = src.conversation?.(o) ?? null;
            const who = c?.speakers.length ? c.speakers.join(' and ') : null;
            out.push({
                group: 'talk',
                id: o.id,
                pos: pos3(o),
                title: who ? `Talk: ${who}` : (c?.name ?? o.class),
                detail: [
                    who ? `${who} talks to the player here (${c!.name}).` : `Conversation: ${c?.name ?? o.class}`,
                    ...(c?.hasQuest ? ['Gives a quest.'] : []),
                    'Double-click to edit what they say.',
                ],
                areas: [],
                lines: [],
                paths: [],
                focus: { kind: 'conversation', classId },
            });
        }
    }
    return out;
}

function cutsceneMark(doc: LevelDocument, env: LogicEnv, src: StoryMapSource, o: LevelObject): StoryMark {
    const at = pos3(o);
    const ids = seqIds(o, env);
    const seqs = ids.map((id) => src.sequence?.(id) ?? null);
    const names = ids.map((id, i) => seqs[i]?.name || `sequence ${id}`);
    const lines: StoryLine[] = [];
    const seen = new Set<number>();
    for (const s of seqs) {
        for (const a of s?.actors ?? []) {
            if (/^camera$/i.test(a.name.trim()) || seen.has(a.ref) || a.ref === o.id) continue;
            const t = doc.get(a.ref);
            if (!t) continue;
            seen.add(a.ref);
            lines.push({ from: at, to: pos3(t), target: t.id, label: a.name.trim() || t.class });
        }
    }
    const areas: StoryArea[] = [];
    const starters: string[] = [];
    for (const s of sendersTo(doc, o.id)) {
        const box = boxOf(s, env);
        if (box) areas.push({ id: s.id, box, tone: 'trigger', label: `Starts ${names[0] ?? 'the cut scene'}` });
        else {
            lines.push({ from: pos3(s), to: at, target: s.id, label: 'starts it' });
            starters.push(s.class);
        }
    }
    const paths = seqs.map((s) => s?.camera ?? []).filter((p) => p.length > 0);
    const secs = seqs.reduce((n, s) => Math.max(n, s?.seconds ?? 0), 0);
    const detail = [
        ids.length ? `Plays ${names.join(', ')}` : 'Plays nothing yet: its sequence list is empty.',
        ...(secs > 0 ? [`About ${secs.toFixed(1)} seconds.`] : []),
        lines.length - starters.length
            ? `Moves ${plural(lines.length - starters.length, 'object')} (the lines).`
            : 'Moves no objects but the camera.',
        areas.length
            ? `Starts when the player walks into the ${areas.length === 1 ? 'box' : `${areas.length} boxes`}.`
            : starters.length
              ? `Started by ${starters.join(', ')}.`
              : 'Nothing starts it yet.',
        'Double-click to open its timeline.',
    ];
    return {
        group: 'cutscene',
        id: o.id,
        pos: at,
        title: names[0] ?? 'Cut scene',
        detail,
        areas,
        lines,
        paths,
        focus: ids.length ? { kind: 'sequence', id: ids[0]! } : null,
    };
}

function questMark(doc: LevelDocument, env: LogicEnv, src: StoryMapSource, o: LevelObject): StoryMark {
    const at = pos3(o);
    const classId = resourceParts(o.resource).classId;
    const title = src.questTitle?.(classId) || o.class;
    const areas: StoryArea[] = [];
    const lines: StoryLine[] = [];
    let starts = 0;
    let completes = 0;
    for (const s of sendersTo(doc, o.id)) {
        const sp = logicOf(s, env);
        const done = (sp ? outgoingMessages(s, env, sp)[0] : undefined)?.value === MESSAGE_QUEST_COMPLETE;
        if (done) completes++;
        else starts++;
        const box = boxOf(s, env);
        if (box)
            areas.push({
                id: s.id,
                box,
                tone: done ? 'complete' : 'start',
                label: `${done ? 'Finishes' : 'Starts'}: ${title}`,
            });
        else lines.push({ from: pos3(s), to: at, target: s.id, label: done ? 'finishes it' : 'starts it' });
    }
    return {
        group: 'quest',
        id: o.id,
        pos: at,
        title,
        detail: [
            `Quest: ${title}`,
            starts ? `Started by ${plural(starts, 'thing')} (green).` : 'Nothing starts it in this level.',
            completes ? `Finished by ${plural(completes, 'thing')} (gold).` : 'Nothing finishes it in this level.',
            'Double-click to edit the quest.',
        ],
        areas,
        lines,
        paths: [],
        focus: { kind: 'quest', classId },
    };
}

/** Count of marks per group (Show menu). */
export function countMarks(marks: readonly StoryMark[]): Record<StoryGroup, number> {
    const n: Record<StoryGroup, number> = { cutscene: 0, vendor: 0, quest: 0, talk: 0 };
    for (const m of marks) n[m.group]++;
    return n;
}

/**
 * Two camera points for a cut scene made in one click: from where the viewport camera is, looking at `target`, moving
 * 40% of the way in over four seconds. Without a camera position the shot starts 6000 units back and 2500 up.
 */
export function starterCameraPoints(target: Vec3, camera: Vec3 | null): CameraPoint[] {
    const from: Vec3 = camera ?? [target[0], target[1] + 2500, target[2] + 6000];
    const to: Vec3 = [
        from[0] + (target[0] - from[0]) * 0.4,
        from[1] + (target[1] - from[1]) * 0.4,
        from[2] + (target[2] - from[2]) * 0.4,
    ];
    const round = (v: Vec3): Vec3 => [Math.round(v[0]), Math.round(v[1]), Math.round(v[2])];
    const a = lookAtAngles(from, target);
    const b = lookAtAngles(to, target);
    return [
        { time: 0, pos: round(from), yaw: a.yaw, pitch: a.pitch },
        { time: 4, pos: round(to), yaw: b.yaw, pitch: b.pitch },
    ];
}
