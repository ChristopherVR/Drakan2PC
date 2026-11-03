// The campaign wizard's plan, the pure part (no DOM): its steps, default names for level copies, which exits link the
// campaign's levels in order, and the one-click fixes the Check step offers for the campaign checks.
// The wizard itself is campaignwizard.ts; the checks are editor-core `validateCampaign`.
import {
    CAMPAIGN_SAVES_PATTERN,
    campaignIdProblem,
    campaignSlug,
    type ExitIndex,
    type ModProject,
    type ProjectIssue,
    type ProjectLevel,
} from '@drakan/editor-core';

export type WizardStepId = 'about' | 'levels' | 'exits' | 'check' | 'play' | 'ship';

export interface WizardStep {
    id: WizardStepId;
    /** Name in the step rail. */
    rail: string;
    /** Heading of the step. */
    title: string;
    /** One or two plain sentences under the heading. */
    intro: string;
}

export const WIZARD_STEPS: readonly WizardStep[] = [
    {
        id: 'about',
        rail: 'Name and cover',
        title: 'Name your campaign',
        intro: 'Players see the title, the short text and the picture on the "Choose your game" screen, next to Drakan.',
    },
    {
        id: 'levels',
        rail: 'Levels',
        title: 'Choose the levels',
        intro: 'Pick the levels in the order a player meets them. New Game opens the first one.',
    },
    {
        id: 'exits',
        rail: 'Link the exits',
        title: 'Link the exits',
        intro: 'Each level ends where the player walks through an exit. Point those exits at the next level of your campaign.',
    },
    {
        id: 'check',
        rail: 'Check',
        title: 'Check the campaign',
        intro: 'The editor walks every exit from the first level and lists what would stop a player. Most problems fix with one click.',
    },
    {
        id: 'play',
        rail: 'Play test',
        title: 'Play it',
        intro: 'Try one level on its own, or start from the title screen and choose your campaign like a player would.',
    },
    {
        id: 'ship',
        rail: 'Export and install',
        title: 'Export and install',
        intro: 'The campaign leaves the editor as one mod pack. Install it into the game, or save the pack to share it.',
    },
];

export const wizardStep = (id: string): WizardStep | undefined => WIZARD_STEPS.find((s) => s.id === id);

const norm = (s: string): string => s.toLowerCase().replace(/[^a-z0-9]/g, '');

/** "Level 1A - Surdana" -> "Surdana". */
export function placeName(title: string): string {
    const m = /^Level\s+\S+\s*[-–]\s*(.+)$/.exec(title);
    return m ? m[1]!.trim() : title;
}

/** Characters a level title cannot hold: it becomes a folder and a file name in the pack. */
const BAD_TITLE = /[\\/:*?"<>|\r\n\t]/;

/** What is wrong with a title for a new level copy, or null. */
export function copyTitleProblem(title: string, taken: readonly string[]): string | null {
    const t = title.trim();
    if (!t) return 'Give the level a name.';
    if (t.length > 48) return 'Keep the name under 48 characters.';
    if (!norm(t) || /[. ]$/.test(t) || /^(con|prn|aux|nul|com[1-9]|lpt[1-9])(\.|$)/i.test(t))
        return 'Choose a name that can be used as a folder on Windows.';
    if (BAD_TITLE.test(t)) return 'Letters, digits, spaces and - _ ( ) only: the name becomes a folder name.';
    if (taken.some((x) => norm(x) === norm(t))) return `"${t}" is already taken.`;
    return null;
}

/** A free title for a copy of `source`: "Surdana Copy", then "Surdana Copy 2", ... */
export function defaultCopyTitle(source: string, taken: readonly string[]): string {
    const base = `${placeName(source).slice(0, 36)} Copy`;
    const free = (t: string): boolean => !taken.some((x) => norm(x) === norm(t));
    if (free(base)) return base;
    for (let n = 2; ; n++) if (free(`${base} ${n}`)) return `${base} ${n}`;
}

/** Replace a disc level with a copy without losing its start flags or map position. Does not mutate the project. */
export function copyProjectLevel(p: ModProject, title: string, copy: string): ModProject {
    const level = p.levels.find((l) => l.title === title);
    if (!level || level.copyOf) return p;
    const mapLayout = p.mapLayout ? { ...p.mapLayout } : undefined;
    if (mapLayout && title in mapLayout) {
        mapLayout[copy] = mapLayout[title]!;
        delete mapLayout[title];
    }
    return {
        ...p,
        levels: p.levels.map((l) => (l.title === title ? { ...l, title: copy, copyOf: title } : l)),
        start: p.start === title ? copy : p.start,
        ...(mapLayout ? { mapLayout } : {}),
        ...(p.campaign
            ? { campaign: { ...p.campaign, start: p.campaign.start === title ? copy : p.campaign.start } }
            : {}),
    };
}

// ---- links ----------------------------------------------------------------------------------------------------------

/** One exit as the wizard shows it. */
export interface WizardExit {
    id: number;
    name: string;
    /** Where it leads now (with the project's own exit edits). */
    dest: string | null;
    /** Where it leads on the disc. */
    discDest: string | null;
}

export interface LinkPlan {
    from: ProjectLevel;
    to: ProjectLevel;
    /**
     * linked: an exit already leads there. suggested: exits of the source level lead to the source of the next level,
     * so pointing them at the copy keeps the game's own route. manual: no exit leads there on the disc; choose one.
     * disc: the first level is a disc level (edit it and Drakan changes too): make it a copy first.
     */
    state: 'linked' | 'suggested' | 'manual' | 'disc' | 'unknown';
    /** The exits to point at `to` (the linked ones for `linked`, the suggested ones for `suggested`). */
    forward: WizardExit[];
    /** Exits of `to` that lead back to the source of `from`: pointing them at `from` keeps the way back. */
    back: WizardExit[];
    /** Every exit of `from` (for a manual choice). */
    all: WizardExit[];
}

/** The exits of a level as the disc has them, and as the project changed them. */
export interface LinkData {
    /** Disc level name -> its exits. */
    disc: ExitIndex;
    /** The same with the project's copies and exit edits (`withExitOverrides`). */
    current: ExitIndex;
}

const exitsOf = (index: ExitIndex, name: string) =>
    index.levels.get(name) ?? [...index.levels.entries()].find(([n]) => norm(n) === norm(name))?.[1] ?? [];

/** The exits of a project level: disc destination and current destination side by side (portals left out). */
export function wizardExits(level: ProjectLevel, data: LinkData): WizardExit[] {
    const disc = exitsOf(data.disc, level.copyOf ?? level.title);
    const cur = exitsOf(data.current, level.title);
    return disc
        .filter((e) => !e.portal && norm(e.dest ?? '') !== 'portal')
        .map((e) => ({
            id: e.id,
            name: e.name,
            discDest: e.dest,
            dest: cur.find((c) => c.id === e.id)?.dest !== undefined ? cur.find((c) => c.id === e.id)!.dest : e.dest,
        }));
}

/** How each level of the project leads to the next one in the list. */
export function linkPlans(p: ModProject, data: LinkData | null): LinkPlan[] {
    const out: LinkPlan[] = [];
    for (let i = 0; i + 1 < p.levels.length; i++) {
        const from = p.levels[i]!;
        const to = p.levels[i + 1]!;
        if (!data) {
            out.push({ from, to, state: 'unknown', forward: [], back: [], all: [] });
            continue;
        }
        const all = wizardExits(from, data);
        const linked = all.filter((e) => e.dest && norm(e.dest) === norm(to.title));
        const toSrc = to.copyOf ?? to.title;
        const fromSrc = from.copyOf ?? from.title;
        const back = to.copyOf
            ? wizardExits(to, data).filter(
                  (e) => e.discDest && norm(e.discDest) === norm(fromSrc) && norm(e.dest ?? '') !== norm(from.title),
              )
            : [];
        if (!from.copyOf) {
            out.push({ from, to, state: 'disc', forward: [], back, all });
            continue;
        }
        if (linked.length) {
            out.push({ from, to, state: 'linked', forward: linked, back, all });
            continue;
        }
        const suggested = all.filter((e) => e.discDest && norm(e.discDest) === norm(toSrc));
        out.push({ from, to, state: suggested.length ? 'suggested' : 'manual', forward: suggested, back, all });
    }
    return out;
}

/** The plans "Link levels in order" can do by itself (no choice needed). */
export const autoLinkable = (plans: readonly LinkPlan[]): LinkPlan[] =>
    plans.filter((l) => l.state === 'suggested' || (l.state === 'linked' && l.back.length > 0));

// ---- check step: one-click fixes ------------------------------------------------------------------------------------

export type WizardFix =
    | { kind: 'title'; label: string }
    | { kind: 'id'; label: string; value: string }
    | { kind: 'saves'; label: string; value: string }
    | { kind: 'start'; label: string; level: string }
    | { kind: 'copy-instead'; label: string; level: string }
    | { kind: 'link'; label: string; from: string; to: string }
    | { kind: 'retarget'; label: string; level: string; exitId: number; to: string }
    | { kind: 'cover'; label: string }
    | { kind: 'goto'; label: string; step: WizardStepId };

/** The exit an "exit-out" or "exit-nowhere" message is about (validateCampaign's wording). */
export function issueExitName(i: ProjectIssue): string | null {
    const m = /: exit "(.+?)" leads /.exec(i.message) ?? /: exit "(.+?)" has no destination/.exec(i.message);
    return m ? m[1]! : null;
}

/** The fix the Check step offers for a campaign issue (null: nothing to do with one click, or nothing to fix). */
export function fixFor(p: ModProject, i: ProjectIssue, data: LinkData | null): WizardFix | null {
    const c = p.campaign;
    const code = i.code.replace(/^campaign-/, '');
    switch (code) {
        case 'title':
            return { kind: 'title', label: 'Name it' };
        case 'id': {
            const value = campaignSlug(c?.title || p.name);
            return c && value !== c.id && !campaignIdProblem(value)
                ? { kind: 'id', label: `Use "${value}"`, value }
                : null;
        }
        case 'saves': {
            const value = c?.id && CAMPAIGN_SAVES_PATTERN.test(c.id) ? c.id : campaignSlug(c?.title || p.name);
            return { kind: 'saves', label: `Use "${value}"`, value };
        }
        case 'no-levels':
            return { kind: 'goto', label: 'Choose levels', step: 'levels' };
        case 'start': {
            const first = p.levels.find((l) => l.copyOf);
            return first
                ? { kind: 'start', label: `Start in ${first.title}`, level: first.title }
                : { kind: 'goto', label: 'Choose levels', step: 'levels' };
        }
        case 'disc-level':
            return i.level ? { kind: 'copy-instead', label: 'Use a copy instead', level: i.level } : null;
        case 'cover':
            return { kind: 'cover', label: 'Make a picture' };
        case 'unreachable': {
            const at = p.levels.findIndex((l) => l.title === i.level);
            const prev = at > 0 ? p.levels[at - 1] : undefined;
            if (prev && data) {
                const plan = linkPlans(p, data).find((l) => l.from.title === prev.title && l.to.title === i.level);
                if (plan?.state === 'suggested')
                    return { kind: 'link', label: `Link from ${prev.title}`, from: prev.title, to: i.level! };
            }
            return { kind: 'goto', label: 'Link it on the map', step: 'exits' };
        }
        case 'exit-out':
        case 'exit-nowhere': {
            const name = issueExitName(i);
            const level = p.levels.find((l) => l.title === i.level);
            if (!name || !level || !data) return null;
            const e = wizardExits(level, data).find((x) => x.name === name);
            if (!e) return null;
            // The campaign has a copy of where the exit leads on the disc: send it there.
            const copy = p.levels.find(
                (l) => l.copyOf && e.discDest && norm(l.copyOf) === norm(e.discDest) && l.title !== level.title,
            );
            if (copy)
                return {
                    kind: 'retarget',
                    label: `Send it to ${copy.title}`,
                    level: level.title,
                    exitId: e.id,
                    to: copy.title,
                };
            return code === 'exit-nowhere' ? { kind: 'goto', label: 'Fix it on the map', step: 'exits' } : null;
        }
        default:
            return null;
    }
}

/** Plain short wording for the Check step (validateCampaign's messages are long). */
export function issueLine(i: ProjectIssue): string {
    const code = i.code.replace(/^campaign-/, '');
    if (code === 'exit-out') {
        const name = issueExitName(i);
        const dest = /into Drakan's own "(.+)"/.exec(i.message)?.[1];
        if (name && dest) return `${i.level}: "${name}" leaves the campaign for Drakan's ${placeName(dest)}.`;
    }
    if (code === 'disc-level' && i.level)
        return `${i.level} is a level of the disc: changing it would change Drakan too.`;
    return i.message;
}

/** Which steps look finished, for the ticks in the step rail. */
export function stepsDone(
    p: ModProject | null,
    facts: { issues: readonly ProjectIssue[] | null; played: boolean; exported: boolean },
    plans: readonly LinkPlan[],
): Record<WizardStepId, boolean> {
    const c = p?.campaign;
    const copies = p?.levels.filter((l) => l.copyOf).length ?? 0;
    return {
        about: !!c?.enabled && !!c.title.trim(),
        levels: copies >= 1 && !!p && p.levels.every((l) => l.copyOf),
        exits: copies > 0 && !!facts.issues && plans.every((l) => l.state === 'linked'),
        check: !!c?.enabled && copies > 0 && !!facts.issues && !facts.issues.some((i) => i.severity === 'error'),
        play: facts.played,
        ship: facts.exported,
    };
}
