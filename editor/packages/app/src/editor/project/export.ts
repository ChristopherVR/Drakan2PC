import {
    LevelDocument,
    ModError,
    applyDocumentToLvl,
    buildModFiles,
    checkEntrySize,
    projectMeta,
    zipModFiles,
    type ChangedEntry,
    type ModProject,
    type ModTarget,
} from '@drakan/editor-core';
import { SrscReader, mangleComponent, writeObjectList, writeSrsc } from '@drakan/formats';
import type { LevelInfo, LevelLoadResult } from '../../rpc.ts';
import type { LevelUiHost } from '../../levelui.ts';
import { autosaveKey, clearAutosave, loadAutosave } from '../autosave.ts';
import { changeCount } from '../../modio.ts';
import { getMenusProvider } from '../menus/bridge.ts';

export interface ProjectExportDeps {
    call: LevelUiHost['call'];
    levels(): LevelInfo[];
    /** The level open in the editor right now (its live document beats the autosave). */
    openDoc(): { title: string; doc: LevelDocument } | null;
    target(): ModTarget | null;
    /** Story edits (strings, quests, dialogue, cut scenes), when any. */
    story?: { count(): number; files(meta: ReturnType<typeof projectMeta>): Promise<Record<string, Uint8Array>> };
}

export interface ProjectPack {
    files: Record<string, Uint8Array>;
    meta: ReturnType<typeof projectMeta>;
    /** One line per thing in the pack. */
    lines: string[];
    warnings: string[];
}

const norm = (s: string): string => s.toLowerCase().replace(/[^a-z0-9]/g, '');

/** The edited document of a project level: the open one, else the autosave, else null (nothing changed). */
async function editedDoc(deps: ProjectExportDeps, title: string): Promise<LevelDocument | null> {
    const open = deps.openDoc();
    if (open && norm(open.title) === norm(title)) return open.doc;
    const rec = await loadAutosave(autosaveKey(title));
    return rec ? LevelDocument.fromJSON(rec.doc) : null;
}

/** The titles whose stored edits changed nothing (diff of zero against the disc level): they count as unedited. */
export function uneditedTitles(counts: ReadonlyMap<string, number>, hadDoc: ReadonlySet<string>): string[] {
    return [...counts].filter(([t, n]) => n === 0 && hadDoc.has(t)).map(([t]) => t);
}

/**
 * Which of the project's levels have edits, without building anything (for the Project panel). With `dropUnedited`,
 * a level whose stored edits turn out to change nothing (a revert made it equal to the disc again) loses its autosave,
 * so the project file stops carrying it and reopening does not report it as edited.
 */
export async function levelEditCounts(
    deps: ProjectExportDeps,
    p: ModProject,
    dropUnedited = false,
): Promise<Map<string, number>> {
    const out = new Map<string, number>();
    const hadDoc = new Set<string>();
    for (const l of p.levels) {
        const doc = await editedDoc(deps, l.title);
        if (!doc) {
            out.set(l.title, 0);
            continue;
        }
        const dir = sourceDir(deps, l.copyOf ?? l.title);
        if (!dir) {
            out.set(l.title, 0);
            continue;
        }
        const src = await loadSrc(deps, dir);
        hadDoc.add(l.title);
        out.set(l.title, changeCount(applyDocumentToLvl(src.lvl.list, doc, { onReferenced: 'unlink' }).report.diff));
    }
    if (dropUnedited) for (const t of uneditedTitles(out, hadDoc)) await clearAutosave(autosaveKey(t));
    return out;
}

const srcCache = new Map<string, Promise<LevelLoadResult>>();
/** Forget loaded levels (a different disc or language was opened). */
export const resetExportCache = (): void => srcCache.clear();
/** The editor already read this level from the disc to open it: the export reuses that read instead of asking the worker. */
export const rememberLoadedLevel = (dir: string, r: LevelLoadResult): void => {
    srcCache.set(dir, Promise.resolve(r));
};
function loadSrc(deps: ProjectExportDeps, dir: string): Promise<LevelLoadResult> {
    let p = srcCache.get(dir);
    if (!p) {
        p = deps.call('loadLevel', dir);
        srcCache.set(dir, p);
        p.catch(() => srcCache.delete(dir));
    }
    return p;
}

function sourceDir(deps: ProjectExportDeps, title: string): string | null {
    return deps.levels().find((x) => x.present && norm(x.name) === norm(title))?.dirPath ?? null;
}

/** Build one pack from every level the project lists, plus story edits. Throws ModError with a plain reason. */
export async function buildProjectPack(
    deps: ProjectExportDeps,
    p: ModProject,
    /** Documents to use instead of the open one or the autosave, by level title (the play test's edited copies). */
    docs?: ReadonlyMap<string, LevelDocument>,
    /** Called with what the build is doing now (the Export dialog shows it). */
    progress?: (msg: string) => void,
): Promise<ProjectPack> {
    const t0 = performance.now();
    const say = (msg: string): Promise<void> => {
        progress?.(msg);
        console.debug(`[export] +${Math.round(performance.now() - t0)} ms ${msg}`);
        // Let the dialog paint before the next synchronous stretch (diffing thousands of objects).
        return new Promise((r) => setTimeout(r, 0));
    };
    const target = deps.target();
    if (!target) throw new ModError('The disc profile is unknown; open your disc image first.');
    const warnings: string[] = [];
    const lines: string[] = [];
    const entries: ChangedEntry[] = [];
    const extra: Record<string, Uint8Array> = {};
    const copies: string[] = [];
    const copyOf: string[] = [];

    let n = 0;
    for (const l of p.levels) {
        n++;
        const where = p.levels.length > 1 ? ` (${n} of ${p.levels.length})` : '';
        const srcTitle = l.copyOf ?? l.title;
        const dir = sourceDir(deps, srcTitle);
        if (!dir) throw new ModError(`"${srcTitle}" is not on the open disc.`);
        // Cheap first: a level without edits needs no read of its object list for a plain level.
        const doc = docs?.get(l.title) ?? (await editedDoc(deps, l.title));
        if (!doc && !l.copyOf) {
            lines.push(`${l.title}: no changes, left out`);
            continue;
        }
        await say(`Reading ${srcTitle} from the disc${where}...`);
        const src = await loadSrc(deps, dir);
        let payload: Uint8Array | null = null;
        let report: ReturnType<typeof applyDocumentToLvl>['report'] | null = null;
        if (doc) {
            await say(`Comparing ${l.title} with the disc${where}...`);
            const r = applyDocumentToLvl(src.lvl.list, doc, { onReferenced: 'unlink' });
            if (changeCount(r.report.diff) > 0) {
                payload = writeObjectList(r.list);
                report = r.report;
                warnings.push(...r.report.warnings.map((w) => `${l.title}: ${w}`));
                if (r.report.unlinked.length) {
                    warnings.push(`${l.title}: ${r.report.unlinked.length} link(s) to deleted objects were removed.`);
                }
            }
        }
        if (!l.copyOf) {
            if (!payload || !report) {
                lines.push(`${l.title}: no changes, left out`);
                continue;
            }
            const size = checkEntrySize(src.file, src.objects.size, payload.length);
            if (size) throw new ModError(`${l.title}: ${size}`);
            entries.push({
                file: src.file,
                entry: { type: src.objects.type, id: src.objects.id, sub: src.objects.sub },
                data: payload,
            });
            lines.push(`${l.title}: ${changeCount(report.diff)} change(s) in ${src.file}`);
            continue;
        }
        // A copy ships as a whole new LVL under its own name, with the splat files the game opens for every level.
        await say(`Packing the copy ${l.title}${where}...`);
        const bytes = await deps.call('readLevelFile', dir);
        let out: Uint8Array = bytes;
        if (payload) {
            const reader = await SrscReader.fromBytes(bytes);
            const idx = reader.entries.findIndex(
                (e) => e.type === src.objects.type && e.id === src.objects.id && e.sub === src.objects.sub,
            );
            if (idx < 0) throw new ModError(`${l.title}: the object list entry was not found in ${src.file}.`);
            out = await writeSrsc(reader, new Map([[idx, payload]]));
        }
        const mangled = mangleComponent(l.title);
        extra[`files/ENGLISH/LEVELS/${mangled}/${mangled}.LVL`] = out;
        const srcStem = dir.split('\\').pop() ?? '';
        const from = `${srcStem.slice(0, 6)}TS`;
        const to = `${mangled.slice(0, 6)}TS`;
        let splats = 0;
        for (const ext of ['DB', 'TXD', 'ODB', 'ADB', 'MOD', 'SDB']) {
            const data = await deps.call('readStoryFile', `LEVELS\\${srcStem}\\${from}.${ext}`);
            if (data) {
                // The LVL's dependency table still names `<source level>_splats` (opened when the copy is entered
                // through an exit); a play test that starts the copy in the intro's slot asks for `<copy>_splats`. Both names ship: a missing splat file crashes the level load.
                extra[`files/ENGLISH/LEVELS/${mangled}/${from}.${ext}`] = data;
                extra[`files/ENGLISH/LEVELS/${mangled}/${to}.${ext}`] = data;
                splats++;
            }
        }
        if (!splats)
            warnings.push(`${l.title}: no splat files found next to ${srcStem}; the game may fail to load it.`);
        copies.push(l.title);
        copyOf.push(`${l.title} = ${srcTitle}`);
        lines.push(
            `${l.title}: new level (copy of ${srcTitle})${payload ? `, ${changeCount(report!.diff)} change(s)` : ''}`,
        );
    }

    const meta = { ...projectMeta(p, target), ...(copies.length ? { levels: copies, levelCopies: copyOf } : {}) };
    const files = buildModFiles(meta, entries);
    Object.assign(files, extra);

    if (deps.story && deps.story.count() > 0) {
        await say('Building the story edits...');
        const sf = await deps.story.files(meta);
        let n = 0;
        for (const [k, v] of Object.entries(sf)) {
            if (k === 'mod.toml' || k === 'mod.json') continue;
            if (k in files) warnings.push(`story file ${k} also comes from a level; the story one is used.`);
            files[k] = v;
            n++;
        }
        lines.push(`Story: ${deps.story.count()} edit(s), ${n} file(s)`);
    }
    // Menu and HUD skin of the Menus panel (ui/menus.ini), when it changes anything.
    const menus = getMenusProvider();
    if (menus?.dirty()) {
        files['ui/menus.ini'] = menus.packFiles(meta)['ui/menus.ini']!;
        lines.push('Menus: ui/menus.ini');
    }
    if (Object.keys(files).length <= 1) {
        throw new ModError('Nothing to export yet: no level in the project has changes and there are no story edits.');
    }
    return { files, meta, lines, warnings };
}

export const zipProjectPack = (pack: ProjectPack): Uint8Array => zipModFiles(pack.files);
