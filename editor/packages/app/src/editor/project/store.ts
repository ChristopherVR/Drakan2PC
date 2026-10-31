import { newProject, parseProject, serializeProject, type ModProject, type TaskId } from '@drakan/editor-core';
import { store } from '../dom.ts';

/**
 * The open mod project. One per browser tab, kept in local storage so a reload keeps it; "Save project" writes it to a
 * file and "Open project" reads one. Changes notify listeners (the Project panel, the checklist, the start page).
 */
const KEY = 'project';
const FOLDER_KEY = 'project-dir';

export interface ProjectFolder {
    dir: string;
    /** Last successful in-place save (ms), null until the first. */
    savedAt: number | null;
    error: string | null;
}

class ProjectStore {
    private project: ModProject | null = null;
    private readonly listeners = new Set<() => void>();
    /** Things only the session knows: reset when the project changes. */
    readonly session: {
        exported: boolean;
        played: boolean;
        /** The last export: where it went and when (the Mod tab shows it). */
        lastExport: { where: string; at: number } | null;
    } = { exported: false, played: false, lastExport: null };
    /** Desktop: the project folder on disk the project is saved into (null = not backed by a folder). */
    folder: ProjectFolder | null = null;
    /** Level titles whose restored edits open without asking (set when a project is opened). */
    readonly autoRestore = new Set<string>();
    private readonly editListeners = new Set<() => void>();
    private readonly folderListeners = new Set<() => void>();
    /** A task chosen before a disc was open; started once it is. */
    pendingTask: TaskId | null = null;
    /** The start page shows a level picker for this task (campaign: the level to edit, newlevel: the level to copy). */
    pickFor: TaskId | null = null;
    /** Set by the installer: what picking a level on the start page does. */
    handlers: {
        pick?: (task: TaskId, level: { name: string; dirPath: string }) => void;
        start?: (task: TaskId) => void;
        /** Save the project (in place when it has a folder, else a download). */
        save?: () => void;
        /** Open the Export dialog. */
        export?: () => void;
    } = {};

    constructor() {
        const raw = store.get(KEY);
        if (raw) {
            try {
                this.project = parseProject(raw);
            } catch {
                this.project = null;
            }
        }
    }

    get(): ModProject | null {
        return this.project;
    }

    /** The project, created on first use. */
    ensure(name = 'My mod', task: TaskId | null = null): ModProject {
        if (!this.project) this.set(newProject(name, task));
        return this.project!;
    }

    set(p: ModProject | null): void {
        const changedId = this.project?.id !== p?.id;
        this.project = p;
        if (changedId) {
            this.session.exported = false;
            this.session.played = false;
            this.session.lastExport = null;
        }
        if (p) store.set(KEY, serializeProject(p));
        else store.set(KEY, '');
        for (const l of this.listeners) l();
    }

    update(fn: (p: ModProject) => ModProject): void {
        if (this.project) this.set(fn(this.project));
    }

    tick(task: TaskId, step: string, on: boolean): void {
        this.update((p) => {
            const cur = new Set(p.ticks[task] ?? []);
            if (on) cur.add(step);
            else cur.delete(step);
            return { ...p, ticks: { ...p.ticks, [task]: [...cur] } };
        });
    }

    /** A level's autosave changed (the project folder is saved soon after). */
    edited(): void {
        for (const l of this.editListeners) l();
    }

    onEdit(fn: () => void): () => void {
        this.editListeners.add(fn);
        return () => this.editListeners.delete(fn);
    }

    setFolder(f: ProjectFolder | null): void {
        this.folder = f;
        store.set(FOLDER_KEY, f?.dir ?? '');
        this.folderChanged();
    }

    /** The folder link or its save state changed (does not count as a project change). */
    folderChanged(): void {
        for (const l of this.folderListeners) l();
    }

    onFolder(fn: () => void): () => void {
        this.folderListeners.add(fn);
        return () => this.folderListeners.delete(fn);
    }

    /** The folder the last session had open (desktop; reopened at start-up). */
    lastFolder(): string {
        return store.get(FOLDER_KEY) ?? '';
    }

    notify(): void {
        for (const l of this.listeners) l();
    }

    on(fn: () => void): () => void {
        this.listeners.add(fn);
        return () => this.listeners.delete(fn);
    }
}

export const projects = new ProjectStore();
