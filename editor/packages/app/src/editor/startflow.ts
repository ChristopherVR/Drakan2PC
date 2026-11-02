import { store } from './dom.ts';
import { templateById, type StartTemplate, type TemplateId } from './starttemplates.ts';

/**
 * The template the user started from the start page: the checklist shows its steps until another one is started.
 * Kept in local storage so a reload keeps the checklist. Ticks of steps the editor cannot see are kept here too.
 */
const KEY = 'start-template';
const TICKS = 'start-ticks';

const listeners = new Set<() => void>();
let starter: ((id: TemplateId) => void) | null = null;
/** Set when a template has just started: the checklist opens and draws attention once. */
let fresh = false;

export const startFlow = {
    active(): StartTemplate | null {
        return templateById(store.get(KEY) ?? '') ?? null;
    },
    /** Mark `id` as the running template (null: none). */
    set(id: TemplateId | null, isNew = true): void {
        store.set(KEY, id ?? '');
        fresh = !!id && isNew;
        listeners.forEach((l) => l());
    },
    /** True once after a template started (the checklist reads it to open itself). */
    takeFresh(): boolean {
        const f = fresh;
        fresh = false;
        return f;
    },
    ticks(id: TemplateId): string[] {
        return store.json<Record<string, string[]>>(TICKS, {})[id] ?? [];
    },
    tick(id: TemplateId, step: string, on: boolean): void {
        const all = store.json<Record<string, string[]>>(TICKS, {});
        const cur = new Set(all[id] ?? []);
        if (on) cur.add(step);
        else cur.delete(step);
        all[id] = [...cur];
        store.set(TICKS, JSON.stringify(all));
        listeners.forEach((l) => l());
    },
    on(fn: () => void): () => void {
        listeners.add(fn);
        return () => listeners.delete(fn);
    },
    /** The start page registers how a template is started (disc, level picker, then the template's first step). */
    setStarter(fn: (id: TemplateId) => void): void {
        starter = fn;
    },
    start(id: TemplateId): void {
        starter?.(id);
    },
};
