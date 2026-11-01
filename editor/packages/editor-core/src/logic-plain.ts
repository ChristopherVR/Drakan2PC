import {
    MESSAGE_BLOW_UP,
    MESSAGE_IM_DEAD,
    MESSAGE_LOAD,
    MESSAGE_LOCK,
    MESSAGE_MOVE_TO_WAYPOINT,
    MESSAGE_OFF,
    MESSAGE_ON,
    MESSAGE_PLAY_SEQUENCE,
    MESSAGE_QUEST_COMPLETE,
    MESSAGE_QUEST_INITIATE,
    MESSAGE_START_CONVERSATION,
    MESSAGE_UNLOCK,
    messageName,
    type LogicClass,
    type LogicRole,
} from '@drakan/formats';
import type { LevelDocument, LevelObject } from './document.ts';
import { logicOf, numField, outgoingMessages, setMessageField, type LogicEnv, type LogicGraph } from './logic.ts';

/**
 * Plain-language layer over the level logic model: what a modder reads and picks instead of message numbers. Nothing here
 * touches the DOM or the file formats. A link means "when the source fires, it sends its message to the target", so the
 * words are chosen from what the TARGET does with the message ("opens", "starts the quest", "plays the cut scene").
 */

export interface PlainMessage {
    value: number;
    /** What the target does, as a verb phrase after its name: "opens". */
    verb: string;
    /** The same as a button label: "Open it". */
    label: string;
}

const M = (value: number, verb: string, label: string): PlainMessage => ({ value, verb, label });

/** The messages that make sense for a target of each role, best first (the first one is what a new link uses). */
const FOR_ROLE: Partial<Record<LogicRole, PlainMessage[]>> = {
    door: [
        M(MESSAGE_ON, 'opens', 'Open it'),
        M(MESSAGE_OFF, 'closes', 'Close it'),
        M(MESSAGE_UNLOCK, 'unlocks', 'Unlock it'),
        M(MESSAGE_LOCK, 'locks', 'Lock it'),
    ],
    cutscene: [M(MESSAGE_PLAY_SEQUENCE, 'plays the cut scene', 'Play the cut scene')],
    quest: [
        M(MESSAGE_QUEST_INITIATE, 'starts the quest', 'Start the quest'),
        M(MESSAGE_QUEST_COMPLETE, 'completes the quest', 'Complete the quest'),
    ],
    conversation: [
        M(MESSAGE_START_CONVERSATION, 'starts the conversation', 'Start the conversation'),
        M(MESSAGE_QUEST_COMPLETE, 'completes its quest', 'Complete its quest'),
        M(MESSAGE_QUEST_INITIATE, 'starts its quest', 'Start its quest'),
    ],
    counter: [M(MESSAGE_IM_DEAD, 'counts one down', 'Count one down')],
    timer: [M(MESSAGE_ON, 'starts the timer', 'Start the timer'), M(MESSAGE_OFF, 'stops the timer', 'Stop the timer')],
    creature: [
        M(MESSAGE_MOVE_TO_WAYPOINT, 'starts walking its path', 'Start walking its path'),
        M(MESSAGE_BLOW_UP, 'blows up', 'Blow it up'),
    ],
    visibility: [M(MESSAGE_ON, 'shows the objects', 'Show them'), M(MESSAGE_OFF, 'hides the objects', 'Hide them')],
    sound: [M(MESSAGE_ON, 'starts the sound', 'Start the sound'), M(MESSAGE_OFF, 'stops the sound', 'Stop the sound')],
    mechanism: [M(MESSAGE_ON, 'switches on', 'Switch it on'), M(MESSAGE_OFF, 'switches off', 'Switch it off')],
    switch: [M(MESSAGE_ON, 'switches on', 'Switch it on'), M(MESSAGE_OFF, 'switches off', 'Switch it off')],
    elevator: [M(MESSAGE_ON, 'sends it up', 'Send it up'), M(MESSAGE_OFF, 'sends it down', 'Send it down')],
    detector: [M(MESSAGE_ON, 'switches it on', 'Switch it on'), M(MESSAGE_OFF, 'switches it off', 'Switch it off')],
    relay: [M(MESSAGE_ON, 'passes it on', 'Pass it on')],
};

/** Short, general messages offered when the target has no list of its own (or under "More"). */
const GENERIC: PlainMessage[] = [
    M(MESSAGE_ON, 'switches on', 'Switch on'),
    M(MESSAGE_OFF, 'switches off', 'Switch off'),
    M(MESSAGE_UNLOCK, 'unlocks', 'Unlock'),
    M(MESSAGE_LOCK, 'locks', 'Lock'),
    M(MESSAGE_PLAY_SEQUENCE, 'plays the cut scene', 'Play the cut scene'),
    M(MESSAGE_QUEST_INITIATE, 'starts the quest', 'Start the quest'),
    M(MESSAGE_QUEST_COMPLETE, 'completes the quest', 'Complete the quest'),
    M(MESSAGE_START_CONVERSATION, 'starts the conversation', 'Start the conversation'),
    M(MESSAGE_MOVE_TO_WAYPOINT, 'starts walking its path', 'Start walking its path'),
    M(MESSAGE_BLOW_UP, 'blows up', 'Blow it up'),
];

/** The plain-language messages for a target of this role: its own list, else the general one. */
export function messagesFor(role: LogicRole | undefined): PlainMessage[] {
    return (role && FOR_ROLE[role]) || GENERIC;
}

/** Every message the game has, in plain words, for the "Something else" list (the raw game names, lower case). */
export function allPlainMessages(): PlainMessage[] {
    const seen = new Set<number>();
    const out: PlainMessage[] = [];
    for (const m of [...Object.values(FOR_ROLE).flat(), ...GENERIC]) {
        if (m && !seen.has(m.value)) {
            seen.add(m.value);
            out.push(m);
        }
    }
    for (let v = 0; v <= 32; v++) {
        if (seen.has(v) || v === 32) continue;
        out.push(M(v, `sends "${messageName(v)}"`, messageName(v)));
    }
    return out;
}

/** What the target does when it gets `value`: "opens", "plays the cut scene" ... or `sends "Gib"` when there is no plain word. */
export function plainMessage(value: number, targetRole?: LogicRole): string {
    const own = messagesFor(targetRole).find((m) => m.value === value);
    if (own) return own.verb;
    const any = allPlainMessages().find((m) => m.value === value);
    return any ? any.verb : `sends "${messageName(value)}"`;
}

/** The message a new link from `src` to `target` carries unless the modder picks another. */
export function defaultLinkMessage(target: LogicClass | undefined): number {
    return messagesFor(target?.role)[0]!.value;
}

// ---------------------------------------------------------------------------------------------------------------------
// names

const NPC_TITLES = new Set(['Villager', 'Lord', 'Mage', 'Archer / shaman']);

/** The role of an object in the words of the editor: Trigger, Door, Counter, Timer, NPC, Quest, Cut scene ... */
export function plainRoleName(spec: LogicClass): string {
    switch (spec.role) {
        case 'detector':
            return spec.rfl === 0x83 ? 'Sound zone' : 'Trigger';
        case 'creature':
            return NPC_TITLES.has(spec.title) ? 'NPC' : 'Enemy';
        case 'conversation':
            return 'NPC talk';
        case 'cutscene':
            return 'Cut scene';
        case 'switch':
            return 'Lever';
        case 'plate':
            return 'Plate';
        case 'lock':
            return 'Lock';
        case 'quest':
            return spec.title === 'Quest marker' ? 'Quest marker' : 'Quest';
        case 'exit':
            return 'Exit';
        case 'movie':
            return 'Movie';
        case 'relay':
            return 'Relay';
        case 'elevator':
            return 'Lift';
        case 'visibility':
            return 'Show/hide';
        case 'sound':
            return 'Sound';
        case 'chest':
            return 'Chest';
        case 'teleporter':
            return 'Teleporter';
        case 'mechanism':
            return 'Mechanism';
        case 'waypoint':
            return 'Waypoint';
        case 'door':
            return 'Door';
        case 'timer':
            return 'Timer';
        case 'counter':
            return 'Counter';
        default:
            return 'Object';
    }
}

/** The name shown on a node: the object's class name as the Outliner shows it, without the id. */
export function nodeName(o: Pick<LevelObject, 'class' | 'id'>, spec?: LogicClass): string {
    return o.class.trim() || (spec ? plainRoleName(spec) : `Object ${o.id}`);
}

/** The words on the output pin for a class's message port ("enters" -> "when the player steps in"). */
export function pinPhrase(spec: LogicClass, portLabel: string): string {
    const map: Record<string, string> = {
        enters: 'when the player steps in',
        sees: 'when it sees the player',
        'after delay': 'when time is up',
        'at zero': 'when the count reaches zero',
        pressed: 'when stepped on',
        released: 'when stepped off',
        on: 'when switched on',
        off: 'when switched off',
        opened: 'when it opens',
        closed: 'when it closes',
        'when dead': 'when it dies',
        sends: 'when it fires',
        solved: 'when solved',
        failed: 'when it fails',
        'when done': 'when it finishes',
        'on exit': 'when the player leaves',
        begun: 'after the first talk',
        incomplete: 'after the reminder talk',
        complete: 'after the finished talk',
        'complete (skipped)': 'when finished without a talk',
        done: 'after the last talk',
        unlocked: 'when unlocked',
        'when broken': 'when it breaks',
        'sound ends': 'when the sound ends',
        arrives: 'when it arrives',
        up: 'when it reaches the top',
        down: 'when it reaches the bottom',
        front: 'when crossed from the front',
        back: 'when crossed from the back',
    };
    void spec;
    return map[portLabel] ?? `when ${portLabel}`;
}

/** One line under the node name that says when the object fires, from the class: "fires when the player steps in". */
export function nodeSummary(spec: LogicClass): string {
    const first = spec.out[0];
    if (first) return `Fires ${pinPhrase(spec, first.label)}`;
    const msgs = messagesFor(spec.role);
    if (FOR_ROLE[spec.role]) return `Waits for a message: ${msgs[0]!.verb}`;
    return 'Takes part in the logic';
}

// ---------------------------------------------------------------------------------------------------------------------
// links

/** Message values the target reacts to, or null when its reactions are not known well enough to judge. */
export function acceptedMessages(target: LevelObject, spec: LogicClass, env: LogicEnv): Set<number> | null {
    switch (spec.role) {
        case 'door':
        case 'quest':
        case 'conversation':
        case 'cutscene': {
            const set = new Set<number>(spec.accepts ?? []);
            for (const m of messagesFor(spec.role)) set.add(m.value);
            if (spec.role === 'cutscene') {
                set.add(MESSAGE_LOAD);
                const own = numField(target, env, 'Message To Play Next');
                if (own !== undefined) set.add(own);
            }
            return set;
        }
        case 'counter': {
            const set = new Set<number>();
            for (const p of spec.in) {
                const v = numField(target, env, p.field);
                if (v !== undefined) set.add(v);
            }
            return set.size ? set : null;
        }
        default:
            return null;
    }
}

export interface NodeProblem {
    id: number;
    /** Another object the problem is about (the link target), when it is about a link. */
    other?: number;
    code: 'no-target' | 'ignored' | 'never-started' | 'never-counts' | 'nothing-starts-quest';
    text: string;
}

const TRIGGER_ROLES: ReadonlySet<LogicRole> = new Set(['detector', 'timer', 'switch', 'plate']);

/**
 * Inline checks for the objects of a graph, worded for the object they sit on: a trigger that is linked to nothing, a
 * message the target ignores, a counter nothing tells to count, a quest nothing starts. Detectors that only play a
 * "Trigger Only" job or ambient zones are left alone, like the level validation does.
 */
export function graphProblems(doc: LevelDocument, env: LogicEnv, g: LogicGraph): Map<number, NodeProblem[]> {
    const out = new Map<number, NodeProblem[]>();
    const add = (p: NodeProblem): void => {
        const l = out.get(p.id) ?? [];
        l.push(p);
        out.set(p.id, l);
    };
    const incoming = new Map<number, number>();
    for (const o of doc.objects()) for (const t of o.links ?? []) incoming.set(t, (incoming.get(t) ?? 0) + 1);
    for (const n of g.nodes.values()) {
        const o = doc.get(n.id);
        if (!o) continue;
        const spec = n.spec;
        const linked = (o.links ?? []).filter((t) => doc.has(t));
        if (TRIGGER_ROLES.has(spec.role) && spec.out.length && linked.length === 0) {
            const task = spec.role === 'detector' ? numField(o, env, 'Task') : 6;
            if ((task === 6 || task === undefined) && spec.rfl !== 0x83)
                add({ id: n.id, code: 'no-target', text: 'Not linked to anything: nothing happens when it fires.' });
        }
        if (spec.role === 'counter' && !incoming.get(n.id))
            add({ id: n.id, code: 'never-counts', text: 'Nothing tells this counter to count, so it never finishes.' });
        if (spec.role === 'quest' && spec.rfl === 0x68 && !incoming.get(n.id))
            add({ id: n.id, code: 'nothing-starts-quest', text: 'Nothing starts or completes this quest.' });
        if (spec.role === 'cutscene' && !incoming.get(n.id) && numField(o, env, 'Initial State') !== 1)
            add({ id: n.id, code: 'never-started', text: 'Nothing plays this cut scene.' });
        const msgs = outgoingMessages(o, env, spec);
        const m = msgs[0];
        if (!m) continue;
        for (const t of linked) {
            const to = doc.get(t)!;
            const ts = g.nodes.get(t)?.spec;
            if (!ts) continue;
            const accepts = acceptedMessages(to, ts, env);
            if (accepts && m.value !== 32 && !accepts.has(m.value)) {
                const list = [...accepts].map((v) => plainMessage(v, ts.role));
                add({
                    id: n.id,
                    other: t,
                    code: 'ignored',
                    text: `${nodeName(to, ts)} ignores "${plainMessage(m.value, ts.role)}": it only ${list.join(' or ')}.`,
                });
            }
        }
    }
    return out;
}

/**
 * Link `o` to `to` carrying `message`: adds the link and sets the message the source sends. Use inside an edit.
 * Returns what changed ("linked", "message", or both), for the toast.
 */
export function linkWithMessage(o: LevelObject, env: LogicEnv, to: number, message: number | null): string[] {
    const changed: string[] = [];
    if (to !== o.id && !(o.links ?? []).includes(to)) {
        (o.links ??= []).push(to);
        changed.push('linked');
    }
    if (message !== null) {
        const port = outgoingMessageField(o, env);
        if (port && setMessageField(o, env, port, message)) changed.push('message');
    }
    return changed;
}

/** The field of the first message port a source sends along its links (null when its class sends none). */
export function outgoingMessageField(o: LevelObject, env: LogicEnv): string | null {
    const spec = logicOf(o, env);
    if (!spec || !env.schemaFor(o.resource)) return null;
    return outgoingMessages(o, env, spec)[0]?.field ?? null;
}

// ---------------------------------------------------------------------------------------------------------------------
// things to add

export interface NewNodeKind {
    id: string;
    title: string;
    /** What it does, one short line. */
    summary: string;
    /** Engine class family the object is made from. */
    rfl: number;
    role: LogicRole;
}

/** What the "Add a node" menu can make from scratch (each needs a class of that family in the level's class list). */
export const NEW_NODE_KINDS: readonly NewNodeKind[] = [
    {
        id: 'trigger',
        title: 'Trigger zone',
        summary: 'Fires when the player steps into it',
        rfl: 0x3d,
        role: 'detector',
    },
    { id: 'plate', title: 'Pressure plate', summary: 'Fires when it is stepped on', rfl: 0x1d, role: 'plate' },
    { id: 'lever', title: 'Lever', summary: 'Fires when the player pulls it', rfl: 0x0d, role: 'switch' },
    { id: 'timer', title: 'Timer', summary: 'Fires after a delay', rfl: 0x3e, role: 'timer' },
    { id: 'counter', title: 'Counter', summary: 'Fires when enough things happened', rfl: 0x07, role: 'counter' },
    { id: 'door', title: 'Door', summary: 'Opens, closes, locks and unlocks', rfl: 0x13, role: 'door' },
    {
        id: 'cutscene',
        title: 'Cut scene player',
        summary: 'Plays a cut scene when told to',
        rfl: 0x33,
        role: 'cutscene',
    },
    { id: 'relay', title: 'Relay', summary: 'Turns one message into another', rfl: 0xe0, role: 'relay' },
];

/** Rank a text query against an object's name, role and id; 0 = no match. */
export function matchScore(query: string, name: string, role: string, id: number): number {
    const q = query.trim().toLowerCase();
    if (!q) return 1;
    const n = name.toLowerCase();
    const r = role.toLowerCase();
    const parts = q.split(/\s+/);
    let score = 0;
    for (const p of parts) {
        if (p.startsWith('#') || /^\d+$/.test(p)) {
            if (String(id) === p.replace('#', '')) score += 6;
            else return 0;
        } else if (n.startsWith(p)) score += 5;
        else if (n.includes(p)) score += 3;
        else if (r.includes(p)) score += 2;
        else return 0;
    }
    return score;
}
