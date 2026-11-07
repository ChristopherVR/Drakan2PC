/**
 * Pure helpers behind the Project Settings panel (panel.ts): the Checks message log (short one-line messages with the
 * long explanation kept for the details, grouped by level, counted and filtered) and the level table (reordering, exit
 * summaries). No DOM here, so the tests run in node.
 */
import { slugId, type CampaignExit, type ProjectIssue } from '@drakan/editor-core';

export type Severity = ProjectIssue['severity'];
export const SEVERITIES: readonly Severity[] = ['error', 'warning', 'info'];

export type SeverityCounts = Record<Severity, number>;
export type SeverityFilter = Record<Severity, boolean>;

export interface CheckMessage {
    issue: ProjectIssue;
    /** One line, plain language; the group already names the level. */
    short: string;
    /** The full explanation and what to do about it. */
    detail: string;
    /** Stable key for the expanded state across re-renders. */
    key: string;
}

export interface CheckGroup {
    /** `project` for the mod settings, else `level:<title>`. */
    key: string;
    /** The project level the group is about; null for the mod settings. */
    level: string | null;
    label: string;
    items: CheckMessage[];
    /** Counts of every message of the group, before the filter. */
    counts: SeverityCounts;
}

const RANK: Record<Severity, number> = { error: 0, warning: 1, info: 2 };

/** "Level 1B - Kragmor" reads as "Kragmor" in a short message; other titles stay as they are. */
export function shortLevelName(title: string): string {
    const m = /^level\s+\w+\s+-\s+(.+)$/i.exec(title.trim());
    return m ? m[1]!.trim() : title.trim();
}

const sentence = (s: string): string => {
    const t = s.trim().replace(/\.$/, '');
    return t ? t.charAt(0).toUpperCase() + t.slice(1) : t;
};

interface ExitProblem {
    name: string;
    dest: string | null;
    problem: string;
}

/** One exit's problem as a short line and a full explanation. `level` is the level the exit is in. */
export function describeExitProblem(level: string, e: ExitProblem): { short: string; detail: string } {
    const name = e.name ? `"${e.name}"` : 'This exit';
    const dest = e.dest ?? '';
    if (/has no exit leading back/i.test(e.problem)) {
        return {
            short: `No way back from ${shortLevelName(dest)}`,
            detail: `Exit ${name} leads to ${dest}, but ${dest} has no exit leading back to ${level}. The game puts the player at the way back, so with none the player arrives with no marker and falls. Point the exit somewhere else, or give ${dest} an exit back.`,
        };
    }
    if (/no object named "portal"/i.test(e.problem)) {
        return {
            short: `No portal to arrive at in ${shortLevelName(dest)}`,
            detail: `Exit ${name} is a portal exit, but ${dest} has no object named "portal" for the player to arrive at.`,
        };
    }
    if (/is not on this disc/i.test(e.problem)) {
        return {
            short: `${shortLevelName(dest)} is not on the disc`,
            detail: `Exit ${name} leads to "${dest}", which is not a level on the open disc.`,
        };
    }
    if (/could not be read/i.test(e.problem)) {
        return {
            short: e.name ? `Exit "${e.name}" has no readable destination` : 'An exit has no readable destination',
            detail: `The destination of exit ${name} could not be read. Open the level and pick a destination in the Logic panel.`,
        };
    }
    return { short: sentence(e.problem), detail: `Exit ${name} to ${dest || 'an unknown level'}: ${e.problem}.` };
}

/** Pull the exit name, destination and problem back out of an `exit` issue message (validateProject's wording). */
export function parseExitIssue(issue: ProjectIssue): ExitProblem | null {
    if (issue.code !== 'exit') return null;
    let rest = issue.message;
    if (issue.level && rest.startsWith(`${issue.level}: `)) rest = rest.slice(issue.level.length + 2);
    const m = /^exit "(.*?)" to (.*?): (.*?)\.?$/s.exec(rest);
    if (!m) return null;
    const name = /^#\d+$/.test(m[1]!) ? '' : m[1]!;
    return { name, dest: m[2] === '?' ? null : m[2]!, problem: m[3]! };
}

const SHORT: Record<string, string> = {
    id: 'The mod id has characters that are not allowed',
    name: 'The project has no name',
    version: 'The version is not numbers like 1.0.0',
    'title-clash': 'Same file name as another level',
    'copy-source': 'The level it copies is not on the disc',
    'copy-title': 'Its title is taken by an original Drakan level',
    'missing-level': 'Not a level of the open disc',
    start: 'The start level is not in the project',
};

/** The short line and the full text of one issue. Pass the exit when it is known; it is parsed from the text if not. */
export function describeIssue(
    issue: ProjectIssue,
    exit?: Pick<CampaignExit, 'name' | 'dest' | 'problem'>,
): {
    short: string;
    detail: string;
} {
    if (issue.code === 'exit') {
        const e = exit?.problem ? { name: exit.name, dest: exit.dest, problem: exit.problem } : parseExitIssue(issue);
        if (e) return describeExitProblem(issue.level ?? 'this level', e);
    }
    if (issue.code === 'unreachable') {
        return { short: 'No exit leads here yet', detail: issue.message };
    }
    const short = SHORT[issue.code];
    return short ? { short, detail: issue.message } : { short: sentence(issue.message), detail: issue.message };
}

export const issueKey = (i: ProjectIssue): string => `${i.code}|${i.level ?? ''}|${i.exitId ?? ''}|${i.message}`;

export function countIssues(issues: readonly ProjectIssue[]): SeverityCounts {
    const c: SeverityCounts = { error: 0, warning: 0, info: 0 };
    for (const i of issues) c[i.severity]++;
    return c;
}

/**
 * The messages grouped for the log: the mod settings first, then one group per level in `levelOrder` (the campaign
 * order), then levels not in that order. Messages run error, warning, note inside a group; groups whose messages are
 * all filtered out are left out. `exitOf` finds the exit an issue is about, for the wording.
 */
export function groupIssues(
    issues: readonly ProjectIssue[],
    levelOrder: readonly string[],
    filter: SeverityFilter = { error: true, warning: true, info: true },
    exitOf?: (i: ProjectIssue) => Pick<CampaignExit, 'name' | 'dest' | 'problem'> | undefined,
): CheckGroup[] {
    const groups = new Map<string, CheckGroup>();
    const keyOf = (i: ProjectIssue): string => (i.level ? `level:${i.level}` : 'project');
    for (const i of issues) {
        const key = keyOf(i);
        let g = groups.get(key);
        if (!g) {
            g = {
                key,
                level: i.level ?? null,
                label: i.level ?? 'Mod settings',
                items: [],
                counts: { error: 0, warning: 0, info: 0 },
            };
            groups.set(key, g);
        }
        g.counts[i.severity]++;
        if (!filter[i.severity]) continue;
        g.items.push({ issue: i, ...describeIssue(i, exitOf?.(i)), key: issueKey(i) });
    }
    const order = new Map(levelOrder.map((t, n) => [`level:${t}`, n]));
    const rank = (g: CheckGroup): number => (g.key === 'project' ? -1 : (order.get(g.key) ?? levelOrder.length));
    const out = [...groups.values()].filter((g) => g.items.length);
    for (const g of out) g.items.sort((a, b) => RANK[a.issue.severity] - RANK[b.issue.severity]);
    return out.sort((a, b) => rank(a) - rank(b));
}

/** The worst severity of a set of counts, or null when there is nothing. */
export function worstSeverity(c: SeverityCounts): Severity | null {
    return c.error ? 'error' : c.warning ? 'warning' : c.info ? 'info' : null;
}

/** Move the item at `from` so it ends up at index `to` (an index into the list before the move). */
export function reorder<T>(list: readonly T[], from: number, to: number): T[] {
    const out = list.slice();
    if (from < 0 || from >= out.length) return out;
    const [item] = out.splice(from, 1);
    const at = Math.max(0, Math.min(out.length, to > from ? to - 1 : to));
    out.splice(at, 0, item!);
    return out;
}

/** "3 exits", "3 exits, 1 to fix", "No exits". */
export function exitSummary(exits: readonly Pick<CampaignExit, 'problem'>[]): {
    count: number;
    problems: number;
    text: string;
} {
    const problems = exits.filter((e) => e.problem).length;
    const n = exits.length;
    const text = n ? `${n} exit${n === 1 ? '' : 's'}${problems ? `, ${problems} to fix` : ''}` : 'No exits';
    return { count: n, problems, text };
}

/**
 * A new project name, with the Id following it while the Id is still the one derived from the current name (or empty);
 * once the user typed their own Id it stays. Works on the latest project state, never a copy captured at render time
 * (the panel re-renders after a delay, so a captured name went stale after one keystroke).
 */
export function renameProject<P extends { name: string; id: string }>(p: P, name: string): P {
    const follows = !p.id || p.id === slugId(p.name);
    return { ...p, name, id: follows ? slugId(name) : p.id };
}

/**
 * What an export of edited converted Order of the Flame levels needs: those levels exist only in the converted pack, so
 * the project pack holds just the edits and the game must load both. One sentence per export, empty when none is used.
 */
export function convertedPackRequirement(
    projectTitles: readonly string[],
    convertedNames: readonly string[],
): string[] {
    const key = (s: string): string => s.trim().toLowerCase();
    const set = new Set(convertedNames.map(key));
    const used = projectTitles.filter((t) => set.has(key(t)));
    if (!used.length) return [];
    const who = used.length === 1 ? `"${used[0]}" is` : `${used.length} levels are`;
    const them = used.length === 1 ? 'it' : 'them';
    return [
        `${who} part of your converted Order of the Flame pack. This pack only holds your changes to ${them}, so the game needs the "Order of the Flame levels" pack (Tools > Import Order of the Flame levels, then Download mod) installed and turned on as well.`,
    ];
}
