import { mangleComponent } from '@drakan/formats';
import type { LevelDocumentJson } from './document.ts';
import {
    CAMPAIGN_DESCRIPTION_MAX,
    CAMPAIGN_SAVES_PATTERN,
    MOD_ID_PATTERN,
    RESERVED_CAMPAIGN_IDS,
    campaignIdProblem,
    type ModMeta,
    type ModTarget,
} from './mod.ts';
import { COVER_PALETTES } from './cover.ts';
import { exitArrivalProblem, type ExitIndex, type ExitIndexEntry } from './logic.ts';

/**
 * A mod project: the one thing a modder creates, saves and exports. It names the mod (the fields of `mod.toml`),
 * lists the levels the mod touches in campaign order and remembers which step of the current task the user is on.
 * It never holds game data: only titles, ids and the user's own settings. The level edits themselves live in the
 * editor's autosave (per level) and the story edits in the story session; the project says which of them to pack.
 */
export const PROJECT_FORMAT = 1;

export type TaskId = 'campaign' | 'multiplayer' | 'newlevel' | 'story' | 'save';

export interface ProjectLevel {
    /** Title in the level list (`Level 1A - Surdana`). For a copy it is the new level's own title. */
    title: string;
    /** Set for a copy: the disc level it starts from. */
    copyOf?: string;
}

export interface ModProject {
    format: typeof PROJECT_FORMAT;
    id: string;
    name: string;
    author: string;
    version: string;
    description: string;
    /** Levels in campaign order (the order a player is meant to meet them). */
    levels: ProjectLevel[];
    /** Level the Play button starts in; null = the open level. */
    start: string | null;
    /** The task the user chose, for the checklist. */
    task: TaskId | null;
    /** Checklist steps ticked by hand, per task. */
    ticks: Partial<Record<TaskId, string[]>>;
    createdAt: number;
    /** Where each level's card sits on the Campaign map (level title -> top-left corner); absent until arranged. */
    mapLayout?: Record<string, MapPoint>;
    /** "Make this a campaign": the export offers the levels as a campaign of their own on the game's title card. */
    campaign?: ProjectCampaign;
}

/** The cover picture of a campaign: made in the editor from a palette and a seed, or a picture of the user's own. */
export type CampaignCover =
    | { kind: 'generated'; palette: string; seed: number; dragon: boolean }
    /** A PNG (base64) the user picked from their own files, already scaled to 960 x 540. */
    | { kind: 'picked'; png: string; name: string };

/** A project exported as a campaign (docs/reference/modding.md "Campaign packs"). */
export interface ProjectCampaign {
    enabled: boolean;
    /** `campaign = "<id>"` (CAMPAIGN_ID_PATTERN). */
    id: string;
    /** Title on the game card. */
    title: string;
    /** One line under it. */
    description: string;
    /** Level New Game opens (a project copy). */
    start: string | null;
    /** Memory card folder suffix: the saves live in `<card>_<saves>`. */
    saves: string;
    cover: CampaignCover | null;
}

/** A position on the Campaign map, in map units (pixels at 100% zoom). */
export interface MapPoint {
    x: number;
    y: number;
}

export class ProjectError extends Error {}

const norm = (s: string): string => s.toLowerCase().replace(/[^a-z0-9]/g, '');

/** `My Map!` -> `my-map`: a valid mod id (letters, digits . _ -). */
export function slugId(name: string): string {
    const s = name
        .trim()
        .toLowerCase()
        .replace(/[^a-z0-9._-]+/g, '-')
        .replace(/^-+|-+$/g, '')
        .slice(0, 64);
    return s || 'my-mod';
}

export function newProject(name = 'My mod', task: TaskId | null = null, now = Date.now()): ModProject {
    return {
        format: PROJECT_FORMAT,
        id: slugId(name),
        name,
        author: '',
        version: '1.0.0',
        description: '',
        levels: [],
        start: null,
        task,
        ticks: {},
        createdAt: now,
    };
}

export const serializeProject = (p: ModProject): string => JSON.stringify(p, null, 2) + '\n';

/** Read a project file. Unknown or missing fields fall back to defaults; a file of another kind throws. */
export function parseProject(text: string): ModProject {
    let raw: unknown;
    try {
        raw = JSON.parse(text);
    } catch {
        throw new ProjectError('This is not a project file (it is not valid JSON).');
    }
    if (!raw || typeof raw !== 'object' || (raw as { kind?: unknown }).kind === 'level') {
        throw new ProjectError('This is not a project file.');
    }
    const r = raw as Record<string, unknown>;
    if (typeof r['format'] !== 'number' || typeof r['id'] !== 'string') {
        throw new ProjectError('This is not a project file (no format or id).');
    }
    if (r['format'] > PROJECT_FORMAT) throw new ProjectError('This project was made by a newer editor.');
    const str = (k: string, d: string): string => (typeof r[k] === 'string' ? (r[k] as string) : d);
    const levels: ProjectLevel[] = [];
    if (Array.isArray(r['levels'])) {
        for (const l of r['levels'] as unknown[]) {
            const o = l as { title?: unknown; copyOf?: unknown } | null;
            if (!o || typeof o.title !== 'string' || !o.title.trim()) continue;
            levels.push(typeof o.copyOf === 'string' ? { title: o.title, copyOf: o.copyOf } : { title: o.title });
        }
    }
    const tasks: TaskId[] = ['campaign', 'multiplayer', 'newlevel', 'story', 'save'];
    const task = tasks.find((t) => t === r['task']) ?? null;
    const ticks: ModProject['ticks'] = {};
    if (r['ticks'] && typeof r['ticks'] === 'object') {
        for (const t of tasks) {
            const v = (r['ticks'] as Record<string, unknown>)[t];
            if (Array.isArray(v)) ticks[t] = v.filter((x): x is string => typeof x === 'string');
        }
    }
    const mapLayout = parseMapLayout(r['mapLayout']);
    const campaign = parseCampaign(r['campaign']);
    return {
        format: PROJECT_FORMAT,
        id: str('id', 'my-mod'),
        name: str('name', 'My mod'),
        author: str('author', ''),
        version: str('version', '1.0.0'),
        description: str('description', ''),
        levels,
        start: typeof r['start'] === 'string' ? r['start'] : null,
        task,
        ticks,
        createdAt: typeof r['createdAt'] === 'number' ? r['createdAt'] : Date.now(),
        ...(mapLayout ? { mapLayout } : {}),
        ...(campaign ? { campaign } : {}),
    };
}

/** The `mapLayout` field: entries with a finite x and y are kept, anything else is dropped; empty -> undefined. */
function parseMapLayout(v: unknown): Record<string, MapPoint> | undefined {
    if (!v || typeof v !== 'object' || Array.isArray(v)) return undefined;
    const out: Record<string, MapPoint> = {};
    for (const [k, p] of Object.entries(v as Record<string, unknown>)) {
        const o = p as { x?: unknown; y?: unknown } | null;
        if (!o || typeof o !== 'object') continue;
        if (typeof o.x !== 'number' || typeof o.y !== 'number' || !Number.isFinite(o.x) || !Number.isFinite(o.y))
            continue;
        out[k] = { x: o.x, y: o.y };
    }
    return Object.keys(out).length ? out : undefined;
}

// ---- project file with level edits ---------------------------------------------------------------------------------

/** Edits of one level as the editor keeps them (the autosave record, without its storage key). */
export interface ProjectLevelEdit {
    savedAt: number;
    doc: LevelDocumentJson;
    bookmarks: unknown[];
}

/** Level title -> the edits made to it. Only levels with edits appear. */
export type ProjectEdits = Record<string, ProjectLevelEdit>;

/** File name of a project inside its folder (desktop) and the suggested name of a downloaded one (browser). */
export const PROJECT_FILE = 'project.drakan.json';

/**
 * The story session's edits as plain JSON (strings, quests, dialogue, cut scenes). Sequences are base64 of their SSD
 * bytes; everything else is numbers and text. Empty when the user made no story edits.
 */
export interface ProjectStory {
    /** Edited or added string-table entries: file, id, sub, the eight language slots. */
    strings: { file: string; id: number; sub: number; text: string[] }[];
    /** Edited quest records by class id. */
    quests: { classId: number; draft: ProjectQuestDraft }[];
    newQuests: { templateId: number; group: number; name: string; classId: number; draft: ProjectQuestDraft }[];
    /** Dialogue and cut scene edits per level folder (stem). */
    content: ProjectContentEdits[];
    /** Item price edits of the Vendors tab (container stem, item class id, gold). */
    prices?: { stem: string; id: number; price: number }[];
}

export interface ProjectQuestDraft {
    kind: 'map' | 'journal' | 'both';
    optional: boolean;
    journalId: number;
    mapTextId: number;
    mapX: number;
    mapY: number;
}

export interface ProjectContentEdits {
    stem: string;
    texts: [number, string][];
    lineSounds: [number, [number, number][]][];
    seqs: { id: number; folder: number; data: string }[];
    newSeqs: { id: number; folder: number; data: string }[];
    newLines: { classId: number; templateId: number; name: string; soundRef: number }[];
    phases: [number, [string, number[]][]][];
}

export const emptyProjectStory = (): ProjectStory => ({ strings: [], quests: [], newQuests: [], content: [] });

export const isEmptyStory = (s: ProjectStory | null | undefined): boolean =>
    !s || (!s.strings.length && !s.quests.length && !s.newQuests.length && !s.content.length && !s.prices?.length);

/** The project plus its level edits (and story edits) as one file: the project fields, and `edits`/`story` beside them. */
export function serializeProjectFile(p: ModProject, edits: ProjectEdits, story?: ProjectStory | null): string {
    return JSON.stringify({ ...p, edits, ...(story && !isEmptyStory(story) ? { story } : {}) }, null, 1) + '\n';
}

const num = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);
const isObj = (v: unknown): v is Record<string, unknown> => !!v && typeof v === 'object' && !Array.isArray(v);
const arr = (v: unknown): unknown[] => (Array.isArray(v) ? v : []);

function parseDraft(v: unknown): ProjectQuestDraft | null {
    if (!isObj(v)) return null;
    const kind = v['kind'];
    if (kind !== 'map' && kind !== 'journal' && kind !== 'both') return null;
    if (![v['journalId'], v['mapTextId'], v['mapX'], v['mapY']].every(num)) return null;
    return {
        kind,
        optional: v['optional'] === true,
        journalId: v['journalId'] as number,
        mapTextId: v['mapTextId'] as number,
        mapX: v['mapX'] as number,
        mapY: v['mapY'] as number,
    };
}

/** Read the `story` part of a project file; anything malformed is dropped, never thrown. */
export function parseProjectStory(raw: unknown): ProjectStory | null {
    if (!isObj(raw)) return null;
    const out = emptyProjectStory();
    for (const x of arr(raw['strings'])) {
        if (!isObj(x) || typeof x['file'] !== 'string' || !num(x['id']) || !num(x['sub'])) continue;
        const text = arr(x['text']).filter((t): t is string => typeof t === 'string');
        out.strings.push({ file: x['file'], id: x['id'], sub: x['sub'], text });
    }
    for (const x of arr(raw['quests'])) {
        if (!isObj(x) || !num(x['classId'])) continue;
        const draft = parseDraft(x['draft']);
        if (draft) out.quests.push({ classId: x['classId'], draft });
    }
    for (const x of arr(raw['newQuests'])) {
        if (!isObj(x) || !num(x['classId']) || !num(x['templateId']) || !num(x['group'])) continue;
        const draft = parseDraft(x['draft']);
        if (draft)
            out.newQuests.push({
                classId: x['classId'],
                templateId: x['templateId'],
                group: x['group'],
                name: typeof x['name'] === 'string' ? x['name'] : 'New Quest',
                draft,
            });
    }
    for (const c of arr(raw['content'])) {
        if (!isObj(c) || typeof c['stem'] !== 'string') continue;
        const e: ProjectContentEdits = {
            stem: c['stem'],
            texts: [],
            lineSounds: [],
            seqs: [],
            newSeqs: [],
            newLines: [],
            phases: [],
        };
        for (const t of arr(c['texts']))
            if (Array.isArray(t) && num(t[0]) && typeof t[1] === 'string') e.texts.push([t[0], t[1]]);
        for (const t of arr(c['lineSounds'])) {
            if (!Array.isArray(t) || !num(t[0])) continue;
            const m = arr(t[1]).filter((p): p is [number, number] => Array.isArray(p) && num(p[0]) && num(p[1]));
            if (m.length) e.lineSounds.push([t[0], m.map((p) => [p[0], p[1]])]);
        }
        for (const [key, dst] of [
            ['seqs', e.seqs],
            ['newSeqs', e.newSeqs],
        ] as const)
            for (const q of arr(c[key]))
                if (isObj(q) && num(q['id']) && num(q['folder']) && typeof q['data'] === 'string')
                    dst.push({ id: q['id'], folder: q['folder'], data: q['data'] });
        for (const l of arr(c['newLines']))
            if (isObj(l) && num(l['classId']) && num(l['templateId']) && num(l['soundRef']))
                e.newLines.push({
                    classId: l['classId'],
                    templateId: l['templateId'],
                    name: typeof l['name'] === 'string' ? l['name'] : '',
                    soundRef: l['soundRef'],
                });
        for (const t of arr(c['phases'])) {
            if (!Array.isArray(t) || !num(t[0])) continue;
            const m: [string, number[]][] = [];
            for (const p of arr(t[1]))
                if (Array.isArray(p) && typeof p[0] === 'string' && Array.isArray(p[1]) && p[1].every(num))
                    m.push([p[0], p[1] as number[]]);
            if (m.length) e.phases.push([t[0], m]);
        }
        out.content.push(e);
    }
    for (const x of arr(raw['prices'])) {
        if (isObj(x) && typeof x['stem'] === 'string' && num(x['id']) && num(x['price']) && x['price'] >= 0) {
            (out.prices ??= []).push({ stem: x['stem'], id: x['id'], price: x['price'] });
        }
    }
    return isEmptyStory(out) ? null : out;
}

/** Read a project file written by `serializeProjectFile` (or the older settings-only one: no edits). */
export function parseProjectFile(text: string): {
    project: ModProject;
    edits: ProjectEdits;
    story: ProjectStory | null;
} {
    const project = parseProject(text);
    const parsed = JSON.parse(text) as { edits?: unknown; story?: unknown };
    const raw = parsed.edits;
    const edits: ProjectEdits = {};
    if (raw && typeof raw === 'object') {
        for (const [title, v] of Object.entries(raw as Record<string, unknown>)) {
            const o = v as Partial<ProjectLevelEdit> | null;
            if (!o || typeof o !== 'object' || !o.doc || typeof o.doc !== 'object') continue;
            edits[title] = {
                savedAt: typeof o.savedAt === 'number' ? o.savedAt : Date.now(),
                doc: o.doc,
                bookmarks: Array.isArray(o.bookmarks) ? o.bookmarks : [],
            };
        }
    }
    return { project, edits, story: parseProjectStory(parsed.story) };
}

// ---- levels --------------------------------------------------------------------------------------------------------

export const hasLevel = (p: ModProject, title: string): boolean => p.levels.some((l) => norm(l.title) === norm(title));

export function addDiscLevel(p: ModProject, title: string): ModProject {
    if (hasLevel(p, title)) return p;
    return { ...p, levels: [...p.levels, { title }] };
}

export function addLevelCopy(p: ModProject, copyOf: string, title: string): ModProject {
    const t = title.trim();
    if (!t || /[\r\n]/.test(t)) throw new ProjectError('A level title must be one non-empty line.');
    if (hasLevel(p, t)) throw new ProjectError(`The project already has a level called "${t}".`);
    return { ...p, levels: [...p.levels, { title: t, copyOf }] };
}

export function removeLevel(p: ModProject, title: string): ModProject {
    const out: ModProject = {
        ...p,
        levels: p.levels.filter((l) => l.title !== title),
        start: p.start === title ? null : p.start,
        ...(p.campaign
            ? { campaign: { ...p.campaign, start: p.campaign.start === title ? null : p.campaign.start } }
            : {}),
    };
    if (p.mapLayout && title in p.mapLayout) {
        const rest = Object.fromEntries(Object.entries(p.mapLayout).filter(([k]) => k !== title));
        if (Object.keys(rest).length) out.mapLayout = rest;
        else delete out.mapLayout;
    }
    return out;
}

export function moveLevel(p: ModProject, title: string, delta: -1 | 1): ModProject {
    const i = p.levels.findIndex((l) => l.title === title);
    const j = i + delta;
    if (i < 0 || j < 0 || j >= p.levels.length) return p;
    const levels = p.levels.slice();
    [levels[i], levels[j]] = [levels[j]!, levels[i]!];
    return { ...p, levels };
}

/** A default title for a copy of `source`: `Level 1A - Surdana (copy)`, numbered when taken. */
export function suggestCopyTitle(p: ModProject, source: string, discTitles: readonly string[]): string {
    const taken = (t: string): boolean => hasLevel(p, t) || discTitles.some((d) => norm(d) === norm(t));
    let t = `${source} (copy)`;
    for (let n = 2; taken(t); n++) t = `${source} (copy ${n})`;
    return t;
}

// ---- exits and campaign flow ---------------------------------------------------------------------------------------

export interface CampaignExit {
    id: number;
    name: string;
    /** Destination level name, null when it could not be read. */
    dest: string | null;
    portal: boolean;
    /** Plain-language problem with the way the exit arrives, null when it is fine. */
    problem: string | null;
    /** The destination is one of the project's levels. */
    inProject: boolean;
}

/** The destination text of a "Transition To 'portal' object" exit is the word portal, not a level. */
export const isPortalWord = (dest: string | null | undefined): boolean => norm(dest ?? '') === 'portal';

/** Where an exit leads, in plain language (never the raw word "portal"). */
export function exitDestinationText(e: Pick<CampaignExit, 'dest' | 'portal'>): string {
    if (!e.dest) return e.portal ? 'Portal inside this level' : 'unknown destination';
    return isPortalWord(e.dest) ? 'Portal inside this level' : e.dest;
}

/** The exit index with each copy's exits added under the copy's title (a copy has the exits of its source). */
export function projectExitIndex(p: ModProject, index: ExitIndex): ExitIndex {
    const levels = new Map(index.levels);
    for (const l of p.levels) {
        if (!l.copyOf || levels.has(l.title)) continue;
        const src = [...index.levels.entries()].find(([n]) => norm(n) === norm(l.copyOf!));
        if (src) levels.set(l.title, src[1]);
    }
    return { levels };
}

/** The exits of one project level with their arrival check. */
export function levelExits(p: ModProject, level: ProjectLevel, index: ExitIndex): CampaignExit[] {
    const full = projectExitIndex(p, index);
    const own =
        full.levels.get(level.title) ?? [...full.levels.entries()].find(([n]) => norm(n) === norm(level.title))?.[1];
    if (!own) return [];
    return own.map((e: ExitIndexEntry) => {
        // "Transition To 'portal' object": the destination text is the word portal, not a level; nothing to check.
        const intra = e.portal || norm(e.dest ?? '') === 'portal';
        return {
            id: e.id,
            name: e.name,
            dest: e.dest,
            portal: e.portal || intra,
            problem: intra
                ? null
                : e.dest
                  ? exitArrivalProblem(level.title, e.dest, e.portal, full, level.copyOf)
                  : 'the destination of this exit could not be read',
            inProject:
                intra ||
                (!!e.dest &&
                    p.levels.some((l) => norm(l.title) === norm(e.dest!) || norm(l.copyOf ?? '') === norm(e.dest!))),
        };
    });
}

/** Levels in the order a player reaches them from `start` (or the first level), then the rest in list order. */
export function routeOrder(p: ModProject, index: ExitIndex): ProjectLevel[] {
    if (!p.levels.length) return [];
    const first = p.levels.find((l) => l.title === p.start) ?? p.levels[0]!;
    const seen = new Set<string>([first.title]);
    const out = [first];
    for (let i = 0; i < out.length; i++) {
        for (const e of levelExits(p, out[i]!, index)) {
            const next = e.dest ? p.levels.find((l) => norm(l.title) === norm(e.dest!)) : undefined;
            if (next && !seen.has(next.title)) {
                seen.add(next.title);
                out.push(next);
            }
        }
    }
    for (const l of p.levels) if (!seen.has(l.title)) out.push(l);
    return out;
}

// ---- validation ----------------------------------------------------------------------------------------------------

export interface ProjectIssue {
    severity: 'error' | 'warning' | 'info';
    code: string;
    message: string;
    /** The project level it is about. */
    level?: string;
    exitId?: number;
}

export interface ProjectCheckContext {
    /** Titles of the levels on the disc. */
    discLevels: readonly string[];
    /** Exits of every disc level (null while it is not loaded). */
    exits: ExitIndex | null;
}

export function validateProject(p: ModProject, ctx: ProjectCheckContext): ProjectIssue[] {
    const out: ProjectIssue[] = [];
    const add = (i: ProjectIssue): void => void out.push(i);
    if (!MOD_ID_PATTERN.test(p.id)) {
        add({
            severity: 'error',
            code: 'id',
            message: 'The mod id may only hold letters, digits and . _ - (1 to 64).',
        });
    }
    if (!p.name.trim()) add({ severity: 'warning', code: 'name', message: 'The project has no name.' });
    if (p.version && !/^\d+(\.\d+)*$/.test(p.version)) {
        add({ severity: 'error', code: 'version', message: 'The version must be dotted numbers such as 1.0.0.' });
    }
    const seen = new Map<string, string>();
    for (const l of p.levels) {
        const key = mangleComponent(l.title);
        const clash = seen.get(key);
        if (clash) {
            add({
                severity: 'error',
                code: 'title-clash',
                message: `"${l.title}" and "${clash}" end up with the same file name (${key}); rename one.`,
                level: l.title,
            });
        }
        seen.set(key, l.title);
        if (l.copyOf) {
            if (!ctx.discLevels.some((d) => norm(d) === norm(l.copyOf!))) {
                add({
                    severity: 'error',
                    code: 'copy-source',
                    message: `"${l.title}" is a copy of "${l.copyOf}", which is not on this disc.`,
                    level: l.title,
                });
            }
            const onDisc = ctx.discLevels.find((d) => mangleComponent(d) === key);
            if (onDisc) {
                add({
                    severity: 'error',
                    code: 'copy-title',
                    message: `"${l.title}" would use the same file name (${key}) as the disc level "${onDisc}"; change its title.`,
                    level: l.title,
                });
            } else if (ctx.discLevels.some((d) => norm(d) === norm(l.title))) {
                add({
                    severity: 'error',
                    code: 'copy-title',
                    message: `"${l.title}" is already the name of a level on the disc; give the copy its own title.`,
                    level: l.title,
                });
            }
        } else if (ctx.discLevels.length && !ctx.discLevels.some((d) => norm(d) === norm(l.title))) {
            add({
                severity: 'error',
                code: 'missing-level',
                message: `"${l.title}" is not a level of the open disc.`,
                level: l.title,
            });
        }
    }
    if (p.start && !hasLevel(p, p.start)) {
        add({
            severity: 'error',
            code: 'start',
            message: `The play test start level "${p.start}" is not in the project.`,
        });
    }
    if (ctx.exits) {
        for (const l of p.levels) {
            for (const e of levelExits(p, l, ctx.exits)) {
                if (e.problem) {
                    add({
                        // Exits of untouched disc levels have problems of their own; they never block an export.
                        severity: 'warning',
                        code: 'exit',
                        message: `${l.title}: exit "${e.name || `#${e.id}`}" to ${e.dest ?? '?'}: ${e.problem}.`,
                        level: l.title,
                        exitId: e.id,
                    });
                }
            }
        }
        // Levels no exit of the project leads to: only the level list (or Play test) starts them.
        const route = routeOrder(p, ctx.exits);
        if (p.levels.length > 1) {
            const reached = new Set<string>();
            const first = p.levels.find((l) => l.title === p.start) ?? p.levels[0]!;
            const queue = [first];
            reached.add(first.title);
            for (let i = 0; i < queue.length; i++) {
                for (const e of levelExits(p, queue[i]!, ctx.exits)) {
                    const next = e.dest ? p.levels.find((l) => norm(l.title) === norm(e.dest!)) : undefined;
                    if (next && !reached.has(next.title)) {
                        reached.add(next.title);
                        queue.push(next);
                    }
                }
            }
            for (const l of route) {
                if (!reached.has(l.title)) {
                    add({
                        severity: 'info',
                        code: 'unreachable',
                        message: l.copyOf
                            ? `Nothing leads to the copy "${l.title}" yet. Point an exit at it in the Logic panel, or start it from the level list.`
                            : `No exit of the project leads to "${l.title}" yet. A player reaches it only from the level list.`,
                        level: l.title,
                    });
                }
            }
        }
    }
    return out;
}

/** The manifest fields of a project. */
export function projectMeta(p: ModProject, target: ModTarget): ModMeta {
    return {
        id: p.id,
        name: p.name.trim() || p.id,
        ...(p.version ? { version: p.version } : {}),
        ...(p.author.trim() ? { author: p.author.trim() } : {}),
        description: p.description.trim() || `Made with the Drakan editor: ${p.name.trim() || p.id}.`,
        target,
    };
}

// ---- tasks and checklists ------------------------------------------------------------------------------------------

/** What the editor knows right now; the checklist reads it to tick steps off. */
export interface StepFacts {
    disc: boolean;
    levelOpen: boolean;
    levelEdits: boolean;
    /** Validation errors of the open level / map. */
    errors: number;
    projectLevels: number;
    projectCopies: number;
    campaignErrors: number;
    /** The exits of the disc levels have been read (the campaign checks are meaningful). */
    exitsChecked: boolean;
    mpActive: boolean;
    mpNamed: boolean;
    mpSpawns: number;
    mpPickups: number;
    mpErrors: number;
    storyChanges: number;
    saveOpen: boolean;
    saveEdited: boolean;
    exported: boolean;
    played: boolean;
}

export interface WorkflowStep {
    id: string;
    title: string;
    text: string;
    /** Ticks itself when this is true; steps without it get a checkbox. */
    auto?: (f: StepFacts) => boolean;
    /** A command id (see the command palette) that does or shows the step. */
    action?: { label: string; command: string };
    /** A guide id that explains the step. */
    guide?: string;
}

export interface Workflow {
    id: TaskId;
    title: string;
    summary: string;
    /** Mode the task opens in. */
    mode: 'level' | 'save';
    guide: string;
    steps: WorkflowStep[];
}

const exportStep: WorkflowStep = {
    id: 'export',
    title: 'Export the mod',
    text: 'Writes a mod pack. Your game disc and its files are never changed.',
    auto: (f) => f.exported,
    action: { label: 'Export', command: 'file.export' },
    guide: 'package',
};
const playStep: WorkflowStep = {
    id: 'play',
    title: 'Play it',
    text: 'The desktop app starts the game with your mod. In the browser, copy the zip into the game folder.',
    auto: (f) => f.played,
    action: { label: 'Play', command: 'project.play' },
    guide: 'package',
};

export const WORKFLOWS: Record<TaskId, Workflow> = {
    campaign: {
        id: 'campaign',
        title: 'Edit a campaign level',
        summary: 'Move, add or remove creatures and items in a level of the game, then play it.',
        mode: 'level',
        guide: 'objects',
        steps: [
            {
                id: 'open',
                title: 'Open a level',
                text: 'Pick a level from the list on the start page, or press Ctrl+P and type its name.',
                auto: (f) => f.levelOpen,
                action: { label: 'Choose level', command: 'project.chooseLevel' },
            },
            {
                id: 'edit',
                title: 'Change something',
                text: 'Click an object and press W to move it, or drag a class from Place into the world.',
                auto: (f) => f.levelEdits,
                guide: 'objects',
            },
            {
                id: 'links',
                title: 'Check triggers and exits',
                text: 'Select a trigger to see what it sends. Open the Logic panel to change it.',
                guide: 'triggers',
                action: { label: 'Show Logic', command: 'panel.logic' },
            },
            {
                id: 'validate',
                title: 'Fix the problems listed',
                text: 'Validation lists anything that would break in game.',
                auto: (f) => f.levelOpen && f.levelEdits && f.errors === 0,
                action: { label: 'Show Validation', command: 'panel.show.validation' },
            },
            exportStep,
            playStep,
        ],
    },
    multiplayer: {
        id: 'multiplayer',
        title: 'Create a multiplayer map',
        summary: 'Turn a level into a deathmatch or team arena with spawn points and pickups.',
        mode: 'level',
        guide: 'multiplayer',
        steps: [
            {
                id: 'open',
                title: 'Open the Multiplayer Map tab and pick a level',
                text: 'The level provides the terrain. Choose one that is open and flat enough to fight in.',
                auto: (f) => f.mpActive,
                action: { label: 'Open World Settings', command: 'panel.multiplayer' },
            },
            {
                id: 'details',
                title: 'Set the map details',
                text: 'Name, modes, player count and starting health, on the Map page.',
                auto: (f) => f.mpActive && f.mpNamed,
            },
            {
                id: 'spawns',
                title: 'Place spawn points',
                text: 'At least two. Suggest spawns can do it for you.',
                auto: (f) => f.mpSpawns >= 2,
                action: { label: 'Spawn tool', command: 'mp.spawn' },
            },
            {
                id: 'pickups',
                title: 'Place pickups',
                text: 'Weapons, armour and health that respawn during a match.',
                auto: (f) => f.mpPickups >= 1,
                action: { label: 'Pickup tool', command: 'mp.pickup' },
            },
            {
                id: 'validate',
                title: 'Validate the map',
                text: 'Fix every error in the map issues list.',
                auto: (f) => f.mpActive && f.mpSpawns >= 2 && f.mpErrors === 0,
                action: { label: 'Show Validation', command: 'panel.show.validation' },
            },
            {
                id: 'test',
                title: 'Play test it',
                text: 'Start the game and run the map from the multiplayer menu.',
                auto: (f) => f.played,
                action: { label: 'Play', command: 'project.play' },
            },
            {
                id: 'export',
                title: 'Export the map',
                text: 'On the Export page: pack zip, mods folder, or the .ini only.',
                guide: 'multiplayer',
            },
        ],
    },
    newlevel: {
        id: 'newlevel',
        title: 'Build a new level from an existing one',
        summary: 'Copy a level under a new name, change the copy and keep the original untouched.',
        mode: 'level',
        guide: 'campaign',
        steps: [
            {
                id: 'project',
                title: 'Name your project',
                text: 'In the Project panel, give the mod a name and an id.',
                auto: (f) => f.projectLevels > 0 || f.projectCopies > 0,
                action: { label: 'Project Settings', command: 'panel.project' },
            },
            {
                id: 'copy',
                title: 'Add a copy of a level',
                text: 'Choose the level to start from and give the copy a title.',
                auto: (f) => f.projectCopies > 0,
                action: { label: 'Add a copy', command: 'project.addCopy' },
            },
            {
                id: 'edit',
                title: 'Open the copy and change it',
                text: 'Use Open next to the copy in the Project panel.',
                auto: (f) => f.levelEdits,
            },
            {
                id: 'exits',
                title: 'Fix the exits',
                text: 'Exits of a copy still lead where the original did. The Project panel lists each one.',
                auto: (f) => f.projectCopies > 0 && f.exitsChecked && f.campaignErrors === 0,
                guide: 'campaign',
            },
            exportStep,
            playStep,
        ],
    },
    story: {
        id: 'story',
        title: 'Change the story',
        summary: 'Edit text, quests, conversations and cut scenes.',
        mode: 'level',
        guide: 'story',
        steps: [
            {
                id: 'open',
                title: 'Open the Story editors',
                text: 'Strings, Quests, Dialogue, Cinematics and Story pack are tabs inside it.',
                action: { label: 'Open Story', command: 'panel.story' },
                guide: 'story',
            },
            {
                id: 'change',
                title: 'Change a string, quest, line or scene',
                text: 'Changed entries are marked.',
                auto: (f) => f.storyChanges > 0,
            },
            {
                id: 'pack',
                title: 'Export the Story pack',
                text: 'On the Story pack tab. It is a separate pack from level edits.',
                action: { label: 'Story pack', command: 'story.pack' },
            },
            playStep,
        ],
    },
    save: {
        id: 'save',
        title: 'Edit a save',
        summary: 'Change a character, items or quests in a .rsg save file.',
        mode: 'save',
        guide: 'save',
        steps: [
            {
                id: 'open',
                title: 'Open a save',
                text: 'Open one .rsg file, or the memory card folder to list every slot.',
                auto: (f) => f.saveOpen,
            },
            {
                id: 'edit',
                title: 'Change the character, items or world',
                text: 'Pending changes are listed on the right and can be undone.',
                auto: (f) => f.saveEdited,
            },
            {
                id: 'save',
                title: 'Press SAVE or EXPORT',
                text: 'SAVE writes the file you opened. EXPORT downloads a copy.',
            },
        ],
    },
};

/** Whether each step is done: its own test, or a tick the user made. */
export function stepStates(w: Workflow, f: StepFacts, ticks: readonly string[]): boolean[] {
    return w.steps.map((s) => (s.auto ? s.auto(f) : false) || ticks.includes(s.id));
}

/** Index of the first step not done, or -1 when all are. */
export const nextStep = (states: readonly boolean[]): number => states.findIndex((d) => !d);

// ---- campaign ------------------------------------------------------------------------------------------------------

/** `The Lost Valley` -> `the-lost-valley`: a valid campaign id (never one of the game's own words). */
export function campaignSlug(name: string): string {
    let s = name
        .trim()
        .toLowerCase()
        .replace(/[^a-z0-9_-]+/g, '-')
        .replace(/^[-_]+/g, '')
        .slice(0, 32)
        .replace(/-+$/g, '');
    if (!s) s = 'my-campaign';
    if (RESERVED_CAMPAIGN_IDS.includes(s)) s = `${s}-campaign`;
    return s;
}

/** Campaign settings for a project that has none yet: named after the project, starting at its first copy. */
export function newCampaign(p: ModProject, seed = Math.floor(Math.random() * 0x7fffffff)): ProjectCampaign {
    const id = campaignSlug(p.name || p.id);
    const first = p.levels.find((l) => l.title === p.start && l.copyOf) ?? p.levels.find((l) => l.copyOf);
    return {
        enabled: true,
        id,
        title: p.name.trim() || id,
        description: p.description
            .trim()
            .replace(/\s*[\r\n]+\s*/g, ' ')
            .slice(0, CAMPAIGN_DESCRIPTION_MAX),
        start: first?.title ?? null,
        saves: id,
        cover: { kind: 'generated', palette: COVER_PALETTES[0]!.id, seed, dragon: true },
    };
}

function parseCampaign(v: unknown): ProjectCampaign | undefined {
    if (!v || typeof v !== 'object' || Array.isArray(v)) return undefined;
    const o = v as Record<string, unknown>;
    const s = (k: string): string => (typeof o[k] === 'string' ? (o[k] as string) : '');
    let cover: CampaignCover | null = null;
    const c = o['cover'] as Record<string, unknown> | null | undefined;
    if (c && typeof c === 'object') {
        if (c['kind'] === 'generated' && typeof c['seed'] === 'number' && Number.isFinite(c['seed'])) {
            cover = {
                kind: 'generated',
                palette: typeof c['palette'] === 'string' ? c['palette'] : COVER_PALETTES[0]!.id,
                seed: c['seed'] >>> 0,
                dragon: c['dragon'] !== false,
            };
        } else if (c['kind'] === 'picked' && typeof c['png'] === 'string' && c['png']) {
            cover = { kind: 'picked', png: c['png'], name: typeof c['name'] === 'string' ? c['name'] : 'picture' };
        }
    }
    return {
        enabled: o['enabled'] === true,
        id: s('id'),
        title: s('title'),
        description: s('description'),
        start: typeof o['start'] === 'string' ? (o['start'] as string) : null,
        saves: s('saves'),
        cover,
    };
}

/** The manifest's campaign keys of a project whose campaign is on (empty otherwise). */
export function campaignMeta(p: ModProject): Partial<ModMeta> {
    const c = p.campaign;
    if (!c?.enabled) return {};
    return {
        campaign: c.id,
        campaignName: c.title.trim() || c.id,
        ...(c.description.trim() ? { campaignDescription: c.description.trim() } : {}),
        ...(c.start ? { campaignStart: c.start } : {}),
        ...(c.saves && c.saves !== c.id ? { campaignSaves: c.saves } : {}),
    };
}

/** One exit whose destination the project changed (an "Only this exit" retarget, or a shared string edit). */
export interface ExitOverride {
    /** Project level title (a copy's own title). */
    level: string;
    exitId: number;
    /** Destination level name. */
    dest: string;
}

/** The exit index with the copies' exits added and the project's own exit edits applied. */
export function withExitOverrides(p: ModProject, index: ExitIndex, overrides: readonly ExitOverride[]): ExitIndex {
    const full = projectExitIndex(p, index);
    if (!overrides.length) return full;
    const levels = new Map(full.levels);
    for (const [name, exits] of full.levels) {
        const mine = overrides.filter((o) => norm(o.level) === norm(name));
        if (!mine.length) continue;
        levels.set(
            name,
            exits.map((e) => {
                const o = mine.find((x) => x.exitId === e.id);
                return o ? { ...e, dest: o.dest } : e;
            }),
        );
    }
    return { levels };
}

/**
 * The checks of "Make this a campaign" (nothing when it is off). `ctx.exits` should already hold the project's exit
 * edits (`withExitOverrides`); without exits only the settings are checked.
 */
export function validateCampaign(p: ModProject, ctx: ProjectCheckContext): ProjectIssue[] {
    const c = p.campaign;
    if (!c?.enabled) return [];
    const out: ProjectIssue[] = [];
    const add = (severity: ProjectIssue['severity'], code: string, message: string, level?: string): void =>
        void out.push({ severity, code: `campaign-${code}`, message, ...(level ? { level } : {}) });
    const idWhy = campaignIdProblem(c.id);
    if (idWhy) add('error', 'id', idWhy);
    if (c.id === 'ootf')
        add(
            'warning',
            'id',
            '"ootf" is the id of the converted Order of the Flame; a campaign of your own should have its own id.',
        );
    if (!c.title.trim()) add('error', 'title', 'Give the campaign a title: it is what the game card shows.');
    if (c.description.length > CAMPAIGN_DESCRIPTION_MAX)
        add(
            'warning',
            'description',
            `The short description is cut to ${CAMPAIGN_DESCRIPTION_MAX} characters on the game card.`,
        );
    if (!CAMPAIGN_SAVES_PATTERN.test(c.saves))
        add('error', 'saves', 'The save folder name may only hold letters, digits, _ and - (1 to 32).');
    if (!p.levels.some((l) => l.copyOf))
        add('error', 'no-levels', 'A campaign needs levels of its own: add a copy of a level to the project.');
    const start = c.start ? p.levels.find((l) => norm(l.title) === norm(c.start!)) : undefined;
    if (!c.start) add('error', 'start', 'Choose the level New Game starts in.');
    else if (!start) add('error', 'start', `The start level "${c.start}" is not in the project.`);
    else if (!start.copyOf)
        add(
            'error',
            'start',
            `The start level "${start.title}" is a level of the disc; New Game can only start a level of the campaign's own (a copy).`,
            start.title,
        );
    for (const l of p.levels) {
        if (!l.copyOf)
            add(
                'warning',
                'disc-level',
                `"${l.title}" is edited in place: those edits also show in Drakan itself, not only in the campaign. Use a copy of it instead.`,
                l.title,
            );
    }
    if (!c.cover) add('info', 'cover', 'The campaign has no cover picture; the game card shows its title only.');
    if (!ctx.exits || !start) return out;
    // Every level is reached from the start through exits; every exit leads to a level that exists.
    const reached = new Set<string>([start.title]);
    const queue = [start];
    for (let i = 0; i < queue.length; i++) {
        for (const e of levelExits(p, queue[i]!, ctx.exits)) {
            const next = e.dest ? p.levels.find((l) => norm(l.title) === norm(e.dest!)) : undefined;
            if (next && !reached.has(next.title)) {
                reached.add(next.title);
                queue.push(next);
            }
        }
    }
    for (const l of p.levels) {
        if (!reached.has(l.title))
            add(
                'error',
                'unreachable',
                `No exit leads to "${l.title}" from the start level "${start.title}". Point an exit at it (Logic panel, "Only this exit"), or take it out of the campaign.`,
                l.title,
            );
        if (!l.copyOf) continue;
        for (const e of levelExits(p, l, ctx.exits)) {
            if (e.portal) continue;
            const name = e.name || `#${e.id}`;
            if (!e.dest) {
                add(
                    'error',
                    'exit-nowhere',
                    `${l.title}: exit "${name}" has no destination that can be read.`,
                    l.title,
                );
                continue;
            }
            const inCampaign = p.levels.some((x) => norm(x.title) === norm(e.dest!));
            const onDisc = ctx.discLevels.some((d) => norm(d) === norm(e.dest!));
            if (!inCampaign && !onDisc)
                add(
                    'error',
                    'exit-nowhere',
                    `${l.title}: exit "${name}" leads to "${e.dest}", which is neither in the campaign nor on the disc.`,
                    l.title,
                );
            else if (!inCampaign)
                add(
                    'warning',
                    'exit-out',
                    `${l.title}: exit "${name}" leads out of the campaign into Drakan's own "${e.dest}".`,
                    l.title,
                );
        }
    }
    return out;
}

/** A name for the copy Save As makes: `My mod copy`, then `My mod copy 2` ... the first one not in `taken` (case-insensitive). */
export function suggestSaveAsName(name: string, taken: readonly string[] = []): string {
    const base = (name.trim() || 'My mod').replace(/ copy( \d+)?$/i, '');
    const used = new Set(taken.map((t) => t.toLowerCase()));
    let n = 1;
    for (;;) {
        const candidate = n === 1 ? `${base} copy` : `${base} copy ${n}`;
        if (!used.has(candidate.toLowerCase())) return candidate;
        n++;
    }
}
