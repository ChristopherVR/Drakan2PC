import { newProject, parseProject, serializeProject, type ModProject, type TaskId } from '@drakan/editor-core';
import { store } from '../dom.ts';

/**
 * The open mod project. One per browser tab, kept in local storage so a reload keeps it; "Save project" writes it to a
 * file and "Open project" reads one. Changes notify listeners (the Project panel, the checklist, the start page).
 */
const KEY = 'project';

class ProjectStore {
    private project: ModProject | null = null;
    private readonly listeners = new Set<() => void>();
    /** Things only the session knows: reset when the project changes. */
    readonly session = { exported: false, played: false };
    /** A task chosen before a disc was open; started once it is. */
    pendingTask: TaskId | null = null;
    /** The start page shows a level picker for this task (campaign: the level to edit, newlevel: the level to copy). */
    pickFor: TaskId | null = null;
    /** Set by the installer: what picking a level on the start page does. */
    handlers: {
        pick?: (task: TaskId, level: { name: string; dirPath: string }) => void;
        start?: (task: TaskId) => void;
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

    notify(): void {
        for (const l of this.listeners) l();
    }

    on(fn: () => void): () => void {
        this.listeners.add(fn);
        return () => this.listeners.delete(fn);
    }
}

export const projects = new ProjectStore();
