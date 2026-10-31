/**
 * Extension points for the editor shell. Panels and tools written elsewhere (multiplayer maps, logic graph, story
 * content, desktop shell) plug in here instead of editing `levelui.ts`:
 *
 *   registerCommand({ id, title, run, ... })   adds an entry to the command palette (Ctrl+P) and, with `keys`, to the
 *                                              shortcut sheet and the keyboard handler.
 *   registerSource({ id, label, search })      adds a family of dynamic palette results (objects, levels, ...).
 *   registerPanel({ id, title, icon, panel })  adds a tab to the bottom dock (and a "Show <title>" command); the
 *                                              panel receives selection / change / load events like the built-in ones.
 *
 * Registrations may happen before or after the editor starts; the shell reads the registry when it needs it.
 */
import type { Panel } from './ctx.ts';

export interface Command {
    /** Stable id, `area.verb` (for example `file.export`). */
    id: string;
    /** Sentence-case label shown in the palette: "Export mod". */
    title: string;
    /** Palette group heading: File, Edit, Selection, Camera, View, Help, ... */
    category?: string;
    /** Display only unless `bindKey` is set, e.g. `Ctrl+D`. Shown as key caps. */
    keys?: string;
    /** One plain sentence on what it does; searched too. */
    hint?: string;
    /** Extra search words ("remove" for Delete). */
    keywords?: string;
    icon?: string;
    /** Return false to grey the command out (and hide it from the empty-query list). */
    enabled?: () => boolean;
    run: () => void;
}

/** A palette result produced for a query (an object, a level, a class...). */
export interface PaletteItem {
    id: string;
    title: string;
    /** Right-aligned secondary text. */
    detail?: string;
    icon?: string;
    /** Higher sorts first within the source. */
    score?: number;
    run: () => void;
}

export interface PaletteSource {
    id: string;
    /** Group heading and prefix label ("Objects"). */
    label: string;
    /** Typing this character first limits the palette to this source ("#" objects, "@" classes, ":" levels). */
    prefix?: string;
    /** Show results even for an empty query. */
    showEmpty?: boolean;
    search(query: string, limit: number): PaletteItem[];
}

export interface PanelSpec {
    id: string;
    title: string;
    icon: string;
    panel: Panel;
    /** Command that shows it; defaults to "Show <title>". */
    command?: Partial<Pick<Command, 'title' | 'keys' | 'keywords' | 'hint'>>;
}

interface PanelHost {
    add(spec: PanelSpec): void;
    show(id: string): void;
}

const commands = new Map<string, Command>();
const sources = new Map<string, PaletteSource>();
const pendingPanels: PanelSpec[] = [];
let panelHost: PanelHost | null = null;
const listeners = new Set<() => void>();

const changed = (): void => listeners.forEach((l) => l());

/** Add or replace a command. Returns a function that removes it. */
export function registerCommand(cmd: Command): () => void {
    commands.set(cmd.id, cmd);
    changed();
    return () => {
        if (commands.get(cmd.id) === cmd) commands.delete(cmd.id);
        changed();
    };
}

export function registerSource(src: PaletteSource): () => void {
    sources.set(src.id, src);
    changed();
    return () => {
        if (sources.get(src.id) === src) sources.delete(src.id);
        changed();
    };
}

/** Add a bottom-dock tab. Safe to call before the editor exists: it is attached when the shell binds. */
export function registerPanel(spec: PanelSpec): void {
    if (panelHost) panelHost.add(spec);
    else pendingPanels.push(spec);
    registerCommand({
        id: `panel.${spec.id}`,
        title: spec.command?.title ?? `Show ${spec.title}`,
        category: 'Panels',
        icon: spec.icon,
        ...(spec.command?.keys ? { keys: spec.command.keys } : {}),
        ...(spec.command?.keywords ? { keywords: spec.command.keywords } : {}),
        ...(spec.command?.hint ? { hint: spec.command.hint } : {}),
        run: () => panelHost?.show(spec.id),
    });
}

/** Shell side of `registerPanel`: called once by `levelui.ts` after the dock exists. */
export function bindPanelHost(host: PanelHost): void {
    panelHost = host;
    for (const p of pendingPanels.splice(0)) host.add(p);
}

export const allCommands = (): Command[] => [...commands.values()];
export const allSources = (): PaletteSource[] => [...sources.values()];
export const getCommand = (id: string): Command | undefined => commands.get(id);
export function runCommand(id: string): boolean {
    const c = commands.get(id);
    if (!c || (c.enabled && !c.enabled())) return false;
    c.run();
    return true;
}
/** Be told when commands or sources change (the palette re-reads on open anyway). */
export function onRegistryChange(fn: () => void): () => void {
    listeners.add(fn);
    return () => listeners.delete(fn);
}

// ---- fuzzy matching (shared by the palette) -------------------------------------------------------------------------

/**
 * Score `text` against `query` (both lower-cased by the caller or not): 0 = no match. Word-prefix and substring
 * matches beat loose subsequences, shorter strings beat longer ones.
 */
export function fuzzyScore(text: string, query: string): number {
    const t = text.toLowerCase();
    const q = query.trim().toLowerCase();
    if (!q) return 1;
    if (t === q) return 1000;
    if (t.startsWith(q)) return 800 - t.length;
    const at = t.indexOf(q);
    if (at >= 0) return (/[\s._-]/.test(t[at - 1] ?? ' ') ? 600 : 400) - at - t.length / 4;
    // every query word as a substring
    const words = q.split(/\s+/).filter(Boolean);
    if (words.length > 1 && words.every((w) => t.includes(w))) return 300 - t.length / 4;
    // loose subsequence
    let i = 0;
    let gaps = 0;
    let last = -1;
    for (const ch of q) {
        const j = t.indexOf(ch, i);
        if (j < 0) return 0;
        if (last >= 0 && j > last + 1) gaps++;
        last = j;
        i = j + 1;
    }
    return Math.max(1, 120 - gaps * 12 - t.length / 4);
}

// ---- context menu items -----------------------------------------------------------------------------------------

export interface ContextTarget {
    /** Selected objects the menu was opened on (empty for empty space in the viewport). */
    ids: number[];
    /** Ground point under the pointer in game units (viewport menus only). */
    ground: [number, number, number] | null;
}

export interface ContextItem {
    id: string;
    label: string;
    icon?: string;
    /** `object` entries need objects; `world` entries need a ground point; default: both. */
    where?: 'object' | 'world' | 'any';
    /** Return false to leave it out for this target. */
    when?: (t: ContextTarget) => boolean;
    /** Reason to show the entry greyed out (menu hint) instead of runnable, e.g. "Available in the desktop app". */
    unavailable?: (t: ContextTarget) => string | null;
    run: (t: ContextTarget) => void;
}

const contextEntries = new Map<string, ContextItem>();

/**
 * Add an entry to the right-click menus of the viewport and the Outliner (for example "Play test from here", which
 * needs `where: 'world'` and uses `target.ground`). Returns a function that removes it.
 */
export function registerContextItem(item: ContextItem): () => void {
    contextEntries.set(item.id, item);
    return () => {
        if (contextEntries.get(item.id) === item) contextEntries.delete(item.id);
    };
}

/** The registered items that apply to `t`. */
export function contextItems(t: ContextTarget): ContextItem[] {
    return [...contextEntries.values()].filter((x) => {
        const w = x.where ?? 'any';
        if (w === 'object' && !t.ids.length) return false;
        if (w === 'world' && !t.ground) return false;
        return !x.when || x.when(t);
    });
}
