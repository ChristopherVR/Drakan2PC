/**
 * What the guided tours listen to. The Level Editor reports level loads, selection and document changes through a
 * small panel (`tourBridge`, one line in `levelui.ts`); every registered command reports itself (`onCommandRan` in
 * `registry.ts`). Anything else can call `emitTourEvent`.
 */
import type { Ctx, Panel } from '../ctx.ts';
import { onCommandRan } from '../registry.ts';
import type { TourEvent } from './model.ts';

const listeners = new Set<(ev: TourEvent) => void>();
let ctxRef: Ctx | null = null;
let levelLoaded = false;

export function emitTourEvent(name: string, detail?: unknown): void {
    const ev: TourEvent = { name, detail };
    for (const l of [...listeners]) {
        try {
            l(ev);
        } catch {
            // One listener never stops the others.
        }
    }
}

/** Returns the unsubscribe function. */
export function onTourEvent(fn: (ev: TourEvent) => void): () => void {
    listeners.add(fn);
    return () => listeners.delete(fn);
}

onCommandRan((id) => emitTourEvent('command', id));

/** A panel with no element on screen that forwards the Level Editor's events to the tours. */
export function tourBridge(ctx: Ctx): Panel {
    ctxRef = ctx;
    return {
        el: document.createElement('div'),
        onLoad: () => {
            levelLoaded = true;
            emitTourEvent('level-load');
        },
        onSelection: () => emitTourEvent('selection', selectedCount()),
        onChange: () => emitTourEvent('change'),
    };
}

/** Objects selected in the open level (0 without a level). */
export function selectedCount(): number {
    try {
        return ctxRef?.view().selection.size ?? 0;
    } catch {
        return 0;
    }
}

/** A disc level (or a saved level) is open in the Level Editor. */
export function levelIsOpen(): boolean {
    try {
        if (ctxRef?.src()) return true;
    } catch {
        // no view yet
    }
    return levelLoaded && !!document.querySelector('#ed-outliner .group');
}

/** The disc image has been read (the level list is filled). */
export function discIsOpen(): boolean {
    const s = document.querySelector<HTMLSelectElement>('#ed-level');
    return !!s && [...s.options].some((o) => o.value);
}
