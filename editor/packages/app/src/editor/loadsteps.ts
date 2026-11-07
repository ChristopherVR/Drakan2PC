/**
 * The steps of opening a disc level, as the viewport's loading card shows them: reading the level file, placing its
 * objects, building the terrain, loading the models. Pure state and wording: no DOM here (loadprogress.ts draws it).
 */
export type LoadStepId = 'read' | 'objects' | 'terrain' | 'models';
export type LoadStepState = 'waiting' | 'active' | 'done' | 'failed' | 'stopped';

/**
 * Share of the bar per step. Models take most of the time (hundreds of classes in batches), but the level is usable
 * from the moment the objects are placed, so the bar reaches a third by then.
 */
export const LOAD_STEPS: readonly { id: LoadStepId; title: string; weight: number }[] = [
    { id: 'read', title: 'Reading the level', weight: 20 },
    { id: 'objects', title: 'Placing objects', weight: 5 },
    { id: 'terrain', title: 'Building the terrain', weight: 15 },
    { id: 'models', title: 'Loading models', weight: 60 },
];

export interface LoadState {
    /** Display name of the level ("Level 1A - Surdana"). */
    name: string;
    steps: Record<LoadStepId, LoadStepState>;
    objects: number;
    layers: number;
    /**
     * `missing`: classes whose model could not be loaded. Classes without a model of their own (detectors, timers, level
     * logic) are not counted: they show as icons or markers by design.
     */
    models: { done: number; total: number; missing: number };
    /** Progress of the active step that has no count of its own (0..1, an estimate from elapsed time). */
    partial: number;
    error: string;
}

export const newLoad = (name: string): LoadState => ({
    name,
    steps: { read: 'active', objects: 'waiting', terrain: 'waiting', models: 'waiting' },
    objects: 0,
    layers: 0,
    models: { done: 0, total: 0, missing: 0 },
    partial: 0,
    error: '',
});

/** Cancelling an open: callers go back to where they were and report nothing. */
export class LoadCancelled extends Error {
    constructor() {
        super('Opening the level was cancelled.');
        this.name = 'LoadCancelled';
    }
}

export const isCancelled = (e: unknown): boolean => e instanceof LoadCancelled;

/** Fraction of an active step from elapsed time when it reports nothing itself: quick at first, never quite 1. */
export const estimate = (elapsedMs: number, typicalMs: number): number =>
    Math.min(0.92, 1 - Math.exp(-Math.max(0, elapsedMs) / Math.max(1, typicalMs)));

function stepPart(s: LoadState, id: LoadStepId): number {
    const st = s.steps[id];
    if (st === 'done' || st === 'failed' || st === 'stopped') return 1;
    if (st === 'waiting') return 0;
    if (id === 'models' && s.models.total > 0) return s.models.done / s.models.total;
    return s.partial;
}

/** Overall progress, 0..1. */
export function loadFraction(s: LoadState): number {
    let sum = 0;
    let all = 0;
    for (const st of LOAD_STEPS) {
        all += st.weight;
        sum += st.weight * Math.min(1, Math.max(0, stepPart(s, st.id)));
    }
    return all ? sum / all : 0;
}

/** The level is in the viewport (objects placed): the card can step aside and let the user work. */
export const levelShown = (s: LoadState): boolean => s.steps.objects === 'done';

export const loadFinished = (s: LoadState): boolean =>
    LOAD_STEPS.every((st) => s.steps[st.id] !== 'active' && s.steps[st.id] !== 'waiting');

const n = (v: number): string => v.toLocaleString('en-US');

/** What a step shows on its right: a count, or nothing yet. */
export function stepDetail(s: LoadState, id: LoadStepId): string {
    const st = s.steps[id];
    if (st === 'failed') return 'Failed';
    switch (id) {
        case 'read':
            return st === 'done' ? 'Done' : '';
        case 'objects':
            return st === 'done' ? `${n(s.objects)} placed` : '';
        case 'terrain':
            return st === 'done' ? `${n(s.layers)} layer${s.layers === 1 ? '' : 's'}` : '';
        case 'models': {
            const m = s.models;
            if (!m.total) return st === 'done' ? 'None needed' : '';
            if (st === 'stopped') return `Stopped at ${n(m.done)} of ${n(m.total)}`;
            return `${n(m.done)} of ${n(m.total)}`;
        }
    }
}

/** One line for the compact strip: the step running now, or how it ended. */
export function headline(s: LoadState): string {
    const active = LOAD_STEPS.find((st) => s.steps[st.id] === 'active');
    if (active) {
        if (active.id === 'models' && s.models.total)
            return `Loading models: ${n(s.models.done)} of ${n(s.models.total)}`;
        return `${active.title}...`;
    }
    if (s.error) return s.error;
    if (s.steps.models === 'stopped') return 'Stopped loading models. The rest show as markers.';
    return 'Ready';
}

/** What the card says when everything has loaded. */
export function readyLine(s: LoadState, title: string): string {
    const miss = s.models.missing;
    if (s.steps.models === 'stopped')
        return `${title} is open. Models that were not loaded show as markers; reopen the level to load them.`;
    if (miss > 0)
        return `${title} is ready. ${n(miss)} object type${miss === 1 ? '' : 's'} could not load a model and show${miss === 1 ? 's' : ''} as markers.`;
    return `${title} is ready.`;
}
