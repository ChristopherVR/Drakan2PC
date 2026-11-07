// "Something is loading" for the panels that list what is being loaded (the Outliner, the Content Browser): the disc is
// opening or a level is being read. They show placeholder rows instead of "No level open" while it lasts. Counted, so an
// overlapping disc open and level open end only when both are done.

let depth = 0;
const listeners = new Set<() => void>();

export const isLoading = (): boolean => depth > 0;

/** Run `fn` whenever loading starts or stops. Returns the unsubscribe function. */
export function onLoading(fn: () => void): () => void {
    listeners.add(fn);
    return () => listeners.delete(fn);
}

/** Mark a load as started; call the returned function once when it ends (also on failure). */
export function beginLoading(): () => void {
    depth++;
    listeners.forEach((l) => l());
    let done = false;
    return () => {
        if (done) return;
        done = true;
        depth = Math.max(0, depth - 1);
        listeners.forEach((l) => l());
    };
}

/** Test aid: forget every load in progress. */
export function resetLoading(): void {
    depth = 0;
}
