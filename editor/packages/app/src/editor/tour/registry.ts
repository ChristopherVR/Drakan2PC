/**
 * The hook between the Help drawer and the guided tours. Help asks `hasTour(id)` to decide whether a guide shows a
 * "Show me" button and calls `startTour(id)` when it is pressed. The tour system registers itself once with
 * `registerTourStarter`; until then (or when it is not built in) every call is a no-op and Help shows no button.
 *
 * The id is the guide's `tour` field when it has one, else the guide id (see `tourIdOf` in help.ts).
 */
type Start = (id: string) => boolean;
type Has = (id: string) => boolean;

let starter: { start: Start; has: Has } | null = null;
const listeners = new Set<() => void>();

/** Install the tour system. A later call replaces the earlier one. Listeners (the Help drawer) re-render. */
export function registerTourStarter(fn: Start, has: Has): void {
    starter = { start: fn, has };
    for (const l of listeners) l();
}

/** True when a tour with this id can be started now. */
export function hasTour(id: string): boolean {
    try {
        return starter?.has(id) ?? false;
    } catch {
        return false;
    }
}

/** Start the tour; false when there is none (or nothing is registered). */
export function startTour(id: string): boolean {
    if (!starter || !hasTour(id)) return false;
    try {
        return starter.start(id);
    } catch {
        return false;
    }
}

/** Be told when the tour system registers, so a "Show me" button can appear. Returns the unsubscribe function. */
export function onTourRegistry(fn: () => void): () => void {
    listeners.add(fn);
    return () => listeners.delete(fn);
}
