import {
    addDiscLevel,
    newProject,
    parseProjectFile,
    serializeProjectFile,
    slugId,
    suggestSaveAsName,
    type ModProject,
    type ProjectEdits,
    type ProjectStory,
    type TaskId,
} from '@drakan/editor-core';
import * as platform from '../../platform.ts';
import { askText } from '../asktext.ts';
import { autosaveKey, clearAutosave, loadAutosave, saveAutosave } from '../autosave.ts';
import { projects } from './store.ts';

export interface FolderHost {
    /** Write the open level's edits to the autosave now. */
    flush(): Promise<void>;
    /** Drop the stored edits of project levels that no longer differ from the disc (optional). */
    dropUnedited?(p: ModProject): Promise<void>;
    toast(msg: string, kind?: 'info' | 'error' | 'ok' | 'warn'): void;
    setStatus(msg: string, error?: boolean): void;
    /** A project was opened: re-open the level in the editor when it has restored edits. */
    reloadOpenLevel(titles: ReadonlySet<string>): void;
    download(name: string, data: BlobPart, type: string): Promise<unknown> | void;
    /** The story session's edits (strings, quests, dialogue, cut scenes) as they go into the project file. */
    story: { export(): ProjectStory | null; import(s: ProjectStory | null): void };
}

export interface ProjectFiles {
    /** Save the project in place (folder) or as a download (browser). `quiet`: no toast (the timed save). */
    save(quiet?: boolean): Promise<void>;
    /** Create a project: a folder on disk (desktop, asks where) or a browser-held project. */
    create(name: string, task?: TaskId | null): Promise<boolean>;
    /** Desktop: choose a project folder and open it. */
    openFolder(): Promise<boolean>;
    /** Desktop: open one of the recent project folders. */
    openRecent(dir: string): Promise<boolean>;
    /** Browser (or any): open a project file's text. */
    openText(text: string): Promise<void>;
    /** Desktop: write the open project into a new folder (asks for its name and where) and continue in that copy. */
    saveAs(): Promise<boolean>;
    /** Close the project so the start card shows (the folder is saved first). */
    close(): Promise<void>;
    /** Re-open the folder the last session ended with (desktop). */
    reopenLast(): Promise<void>;
    /** Write a file into the project folder (`mod/<id>.zip`); null when the project has no folder. */
    writeFile(rel: string, bytes: Uint8Array): Promise<string | null>;
}

/** The edits of every project level, from the autosave records (the editor keeps those current). */
export async function collectEdits(p: ModProject): Promise<ProjectEdits> {
    const out: ProjectEdits = {};
    for (const l of p.levels) {
        const rec = await loadAutosave(autosaveKey(l.title));
        if (rec) out[l.title] = { savedAt: rec.savedAt, doc: rec.doc, bookmarks: rec.bookmarks ?? [] };
    }
    return out;
}

/** Make the project's edits the editor's autosave records, so opening the level restores them. */
async function applyEdits(p: ModProject, edits: ProjectEdits): Promise<Set<string>> {
    const titles = new Set<string>();
    for (const l of p.levels) {
        const e = edits[l.title];
        if (e) {
            await saveAutosave({
                key: autosaveKey(l.title),
                level: l.title,
                savedAt: e.savedAt,
                doc: e.doc,
                bookmarks: e.bookmarks as never,
            });
            titles.add(l.title);
        } else {
            // The autosave belongs to whichever project was open before; this project has no edits for the level.
            await clearAutosave(autosaveKey(l.title));
        }
    }
    return titles;
}

export function createProjectFiles(host: FolderHost): ProjectFiles {
    let timer = 0;
    let saving: Promise<void> = Promise.resolve();

    async function writeNow(quiet: boolean): Promise<void> {
        const p = projects.get();
        if (!p) return;
        // Desktop: a project that is not in a folder yet gets one the first time it is saved.
        if (!projects.folder && platform.isDesktop && !quiet) {
            const info = await platform.projectNew(p.name || 'My mod');
            if (!info) return;
            projects.setFolder({ dir: info.dir, savedAt: null, error: null });
        }
        const f = projects.folder;
        await host.flush();
        await host.dropUnedited?.(p).catch(() => undefined);
        const text = serializeProjectFile(p, await collectEdits(p), host.story.export());
        if (!f) {
            const name = `${p.id || 'project'}.drakan-project.json`;
            await host.download(name, text, 'application/json');
            if (!quiet) host.toast(`Project file ${name} saved (with its level edits)`, 'ok');
            return;
        }
        try {
            await platform.projectWrite(f.dir, text);
            f.savedAt = Date.now();
            f.error = null;
            if (!quiet) host.toast('Project saved', 'ok');
        } catch (e) {
            f.error = e instanceof Error ? e.message : String(e);
            host.setStatus(`Saving the project failed: ${f.error}`, true);
            if (!quiet) host.toast(f.error, 'error');
        }
        projects.folderChanged();
    }

    const save = (quiet = false): Promise<void> => (saving = saving.then(() => writeNow(quiet)));

    const saveSoon = (): void => {
        if (!projects.folder) return;
        window.clearTimeout(timer);
        timer = window.setTimeout(() => void save(true), 1500);
    };
    projects.on(saveSoon);
    projects.onEdit(saveSoon);

    async function adopt(
        project: ModProject,
        edits: ProjectEdits,
        story: ProjectStory | null,
        folder: { dir: string } | null,
    ): Promise<void> {
        // Leaving a folder project: its edits are written to its file first.
        if (projects.folder) {
            window.clearTimeout(timer);
            await save(true);
        }
        const titles = await applyEdits(project, edits);
        host.story.import(story);
        projects.autoRestore.clear();
        for (const t of titles) projects.autoRestore.add(t);
        projects.setFolder(folder ? { dir: folder.dir, savedAt: Date.now(), error: null } : null);
        projects.set(project);
        const n = titles.size;
        host.setStatus(`Opened project "${project.name}"${n ? ` with edits to ${n} level${n === 1 ? '' : 's'}` : ''}.`);
        host.toast(
            `Opened project "${project.name}"${n ? ` (${n} level${n === 1 ? '' : 's'} with edits restored)` : ''}`,
            'ok',
        );
        host.reloadOpenLevel(titles);
    }

    async function openInfo(info: platform.ProjectFolderInfo): Promise<boolean> {
        if (info.text === null) return false;
        const { project, edits, story } = parseProjectFile(info.text);
        await adopt(project, edits, story, { dir: info.dir });
        return true;
    }

    return {
        save,
        async create(name, task = null) {
            const nm = name.trim() || 'My mod';
            if (!platform.isDesktop) {
                if (projects.folder) await save(true);
                projects.setFolder(null);
                host.story.import(null);
                projects.set(newProject(nm, task));
                return true;
            }
            const info = await platform.projectNew(nm);
            if (!info) return false;
            if (projects.folder) {
                window.clearTimeout(timer);
                await save(true);
            }
            const p = newProject(nm, task);
            await applyEdits(p, {});
            host.story.import(null);
            projects.autoRestore.clear();
            projects.setFolder({ dir: info.dir, savedAt: null, error: null });
            projects.set(p);
            await save(true);
            host.toast(`Created project folder ${info.dir}`, 'ok');
            return true;
        },
        async openFolder() {
            const info = await platform.projectOpen();
            return info ? openInfo(info) : false;
        },
        async openRecent(dir) {
            try {
                return await openInfo(await platform.projectOpenPath(dir));
            } catch (e) {
                // A folder that is gone (moved, deleted, a drive that is not plugged in) stays in the list forever otherwise.
                if (/not a project folder|cannot find|not found|no such|os error 3|os error 2/i.test(String((e as Error)?.message ?? e))) {
                    await platform.forgetRecentProject(dir).catch(() => undefined);
                    throw new Error(`${dir} is no longer a project folder; it was removed from the recent projects.`);
                }
                throw e;
            }
        },
        async openText(text) {
            const { project, edits, story } = parseProjectFile(text);
            await adopt(project, edits, story, null);
        },
        async saveAs() {
            const p = projects.get();
            if (!p) {
                host.toast('There is no project to save yet. Start one first.', 'warn');
                return false;
            }
            if (!platform.isDesktop) {
                await save();
                return true;
            }
            const recent = (await platform.getSettings()).recentProjects.map((r) => r.split(/[\\/]/).filter(Boolean).pop() ?? r);
            const name = await askText('Save project as', 'Name of the new project', suggestSaveAsName(p.name, recent), 'Choose folder...');
            if (!name) return false;
            // the open project is written first, so nothing of it is lost if the copy fails
            if (projects.folder) await save(true);
            const info = await platform.projectNew(name);
            if (!info) return false;
            projects.update((q) => ({ ...q, name, id: slugId(name) }));
            projects.setFolder({ dir: info.dir, savedAt: null, error: null });
            await save(true);
            host.toast(`Saved a copy as "${name}" in ${info.dir}. You are now working in the copy.`, 'ok');
            return true;
        },
        async close() {
            if (projects.folder) {
                window.clearTimeout(timer);
                await save(true);
            }
            projects.setFolder(null);
            host.story.import(null);
            projects.set(null);
        },
        async reopenLast() {
            if (!platform.isDesktop) return;
            const dir = projects.lastFolder();
            if (!dir) return;
            const recent = (await platform.getSettings()).recentProjects;
            if (!recent.some((r) => r.toLowerCase() === dir.toLowerCase())) return;
            try {
                await openInfo(await platform.projectOpenPath(dir));
            } catch (e) {
                host.setStatus(
                    `The last project folder could not be opened: ${e instanceof Error ? e.message : e}`,
                    true,
                );
                projects.setFolder(null);
            }
        },
        async writeFile(rel, bytes) {
            const f = projects.folder;
            if (!f) return null;
            return platform.projectFileWrite(f.dir, rel, bytes);
        },
    };
}

/** The open edited level joins the project the first time it is exported (one level, no setup). */
export function withOpenLevel(
    p: ModProject,
    open: { title: string } | null,
    isDiscLevel: (t: string) => boolean,
): ModProject {
    if (!open || !isDiscLevel(open.title)) return p;
    return addDiscLevel(p, open.title);
}
