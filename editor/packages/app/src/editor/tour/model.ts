/**
 * Guided tours, the pure part: what a tour is, the step sequencer (`TourRunner`) and the progress store. No DOM here,
 * so the sequencing, the waits and the persistence are tested without a browser (`test/tours.test.ts`). The overlay
 * that draws the spotlight and the callout card is `engine.ts`; the tours themselves are in `tours.ts`.
 */

/** Where a step points: a CSS selector (the first visible match wins) or a function that finds the element. */
export type TourTarget = string | (() => Element | null | undefined);

/** Something the editor reports to the tour system (`events.ts`). */
export interface TourEvent {
    /**
     * `command` (a registered command ran), `level-load`, `selection`, `change` (the level document), `click` (on the
     * step target), `target-change` (a field inside the step target changed).
     */
    name: string;
    /** Command id for `command`, number of selected objects for `selection`. */
    detail?: unknown;
}

/** What moves a step on. `next` (the default) waits for the Next button. */
export type TourWait =
    | { kind: 'next' }
    /** A click on the step's target (the target still gets the click). */
    | { kind: 'click' }
    /** One of these commands ran (from a menu, the palette, a button or a shortcut). */
    | { kind: 'command'; ids: string[] }
    /** An editor event, optionally filtered. */
    | { kind: 'event'; name: string; test?: (detail: unknown) => boolean }
    /** A condition that becomes true (checked a few times a second). */
    | { kind: 'until'; test: () => boolean };

/** A short animation that shows what to do. */
export type TourDemo =
    /** A pointer that taps the target. */
    | { kind: 'click' }
    /** A pointer that drags from one element to another (a Place row into the viewport). */
    | { kind: 'drag'; from: TourTarget; to: TourTarget; label?: string };

export type TourSide = 'top' | 'bottom' | 'left' | 'right';

export interface TourStep {
    id: string;
    title: string;
    /** One or two plain sentences. */
    text: string;
    /** The element to light up. None: the card sits in the middle of the window. */
    target?: TourTarget;
    /** More elements that stay usable and unblurred (a menu the target opens, a second panel). */
    also?: TourTarget[];
    /** Preferred side of the card; it moves to the side with room when this one has none. */
    side?: TourSide;
    /** Space around the target in the spotlight, px (default 6). */
    pad?: number;
    wait?: TourWait;
    /** Shown on the card when the wait has not finished after `hintAfter` ms. */
    hint?: string;
    hintAfter?: number;
    /** What a waiting step waits for, as the end of "Waiting for you to ...": "open a level". */
    doing?: string;
    demo?: TourDemo;
    /** Run when the step starts (open the panel it talks about, so the target exists). */
    enter?: () => void;
    /** Skip the step when this is already true (the disc is already open). */
    skipIf?: () => boolean;
    /** Wording of the Next button on this step ("Finish" on the last one by default). */
    nextLabel?: string;
}

export type TourGroup = 'basics' | 'templates';

export interface Tour {
    /** Stable id: the start template id where there is one (`campaign`, `puzzle`, ...). */
    id: string;
    title: string;
    /** One line in the tour list. */
    line: string;
    group: TourGroup;
    /** About how long it takes, in minutes. */
    minutes: number;
    steps: TourStep[];
    /** Run when the tour starts (before the first step). */
    begin?: () => void;
}

// ---- sequencer ------------------------------------------------------------------------------------------------------

export type RunnerPhase = 'idle' | 'running' | 'done' | 'closed';

export interface RunnerState {
    phase: RunnerPhase;
    index: number;
    step: TourStep | null;
    /** The step waits for the user to do something (Next still moves on). */
    waiting: boolean;
    /** The wait has taken long enough to show the step's hint. */
    hinted: boolean;
}

export interface Clock {
    set(fn: () => void, ms: number): unknown;
    clear(handle: unknown): void;
}

export const realClock: Clock = {
    set: (fn, ms) => setTimeout(fn, ms),
    clear: (h) => clearTimeout(h as ReturnType<typeof setTimeout>),
};

export interface RunnerOptions {
    clock?: Clock;
    /** How often `until` waits are checked, ms. */
    pollMs?: number;
    /** Default delay before a waiting step shows its hint, ms. */
    hintMs?: number;
    /** Called on every state change. */
    onChange?: (s: RunnerState) => void;
    /** Called when the tour reaches its end (`done`) or is closed early (`closed`, with the step index). */
    onEnd?: (how: 'done' | 'closed', index: number) => void;
}

/** True when a step needs the user to do something before Next is the obvious move. */
export const stepWaits = (s: TourStep): boolean => !!s.wait && s.wait.kind !== 'next';

/** Whether an event finishes a step's wait. */
export function eventMatches(wait: TourWait | undefined, ev: TourEvent): boolean {
    if (!wait) return false;
    switch (wait.kind) {
        case 'click':
            return ev.name === 'click';
        case 'command':
            return ev.name === 'command' && typeof ev.detail === 'string' && wait.ids.includes(ev.detail);
        case 'event':
            return ev.name === wait.name && (!wait.test || wait.test(ev.detail));
        default:
            return false;
    }
}

/**
 * Walks a tour's steps. `next` / `back` / `goTo` move; `notify(event)` finishes a waiting step when the event matches;
 * `until` waits are polled on the clock. Steps whose `skipIf` holds are passed over in the direction of travel.
 */
export class TourRunner {
    private s: RunnerState = { phase: 'idle', index: -1, step: null, waiting: false, hinted: false };
    private poll: unknown = null;
    private hintTimer: unknown = null;
    private readonly clock: Clock;
    private readonly pollMs: number;
    private readonly hintMs: number;

    constructor(
        readonly tour: Tour,
        private readonly opts: RunnerOptions = {},
    ) {
        this.clock = opts.clock ?? realClock;
        this.pollMs = opts.pollMs ?? 250;
        this.hintMs = opts.hintMs ?? 12000;
    }

    get state(): RunnerState {
        return { ...this.s };
    }

    get count(): number {
        return this.tour.steps.length;
    }

    /** Start at `index` (0, or a remembered step to resume). */
    start(index = 0): void {
        this.s.phase = 'running';
        this.tour.begin?.();
        this.enter(Math.max(0, Math.min(index, this.count - 1)), 1);
    }

    next(): void {
        if (this.s.phase !== 'running') return;
        if (this.s.index >= this.count - 1) this.finish();
        else this.enter(this.s.index + 1, 1);
    }

    back(): void {
        if (this.s.phase !== 'running' || this.s.index <= 0) return;
        this.enter(this.s.index - 1, -1);
    }

    goTo(index: number): void {
        if (this.s.phase !== 'running') return;
        this.enter(Math.max(0, Math.min(index, this.count - 1)), index >= this.s.index ? 1 : -1);
    }

    /** Close before the end (Skip tour, Esc). */
    close(): void {
        if (this.s.phase !== 'running') return;
        const at = this.s.index;
        this.stopTimers();
        this.s = { ...this.s, phase: 'closed', waiting: false };
        this.emit();
        this.opts.onEnd?.('closed', at);
    }

    /** An editor event: moves on when it finishes the current step's wait. */
    notify(ev: TourEvent): void {
        if (this.s.phase !== 'running' || !this.s.step || !this.s.waiting) return;
        if (eventMatches(this.s.step.wait, ev)) this.next();
    }

    private finish(): void {
        const at = this.s.index;
        this.stopTimers();
        this.s = { ...this.s, phase: 'done', waiting: false };
        this.emit();
        this.opts.onEnd?.('done', at);
    }

    private enter(index: number, dir: 1 | -1): void {
        this.stopTimers();
        let i = index;
        // Pass over steps that are already done, in the direction of travel; never past the ends.
        while (i >= 0 && i < this.count && safe(this.tour.steps[i]!.skipIf)) {
            if (dir > 0 && i === this.count - 1) return this.finish();
            i += dir;
        }
        if (i < 0) i = index;
        const step = this.tour.steps[i]!;
        this.s = { phase: 'running', index: i, step, waiting: stepWaits(step), hinted: false };
        try {
            step.enter?.();
        } catch {
            // A step that cannot set itself up still shows its card.
        }
        if (this.s.waiting) {
            this.hintTimer = this.clock.set(() => {
                if (this.s.step !== step) return;
                this.s.hinted = true;
                this.emit();
            }, step.hintAfter ?? this.hintMs);
            if (step.wait?.kind === 'until') this.schedulePoll(step);
        }
        this.emit();
    }

    private schedulePoll(step: TourStep): void {
        const w = step.wait;
        if (w?.kind !== 'until') return;
        this.poll = this.clock.set(() => {
            if (this.s.step !== step || this.s.phase !== 'running') return;
            if (safe(w.test)) this.next();
            else this.schedulePoll(step);
        }, this.pollMs);
    }

    private stopTimers(): void {
        if (this.poll !== null) this.clock.clear(this.poll);
        if (this.hintTimer !== null) this.clock.clear(this.hintTimer);
        this.poll = this.hintTimer = null;
    }

    private emit(): void {
        this.opts.onChange?.(this.state);
    }
}

function safe(fn: (() => boolean) | undefined): boolean {
    if (!fn) return false;
    try {
        return !!fn();
    } catch {
        return false;
    }
}

// ---- progress -------------------------------------------------------------------------------------------------------

/** The part of `Storage` the progress store uses (localStorage, or a map in tests). */
export interface KeyValue {
    getItem(key: string): string | null;
    setItem(key: string, value: string): void;
}

export interface TourProgressData {
    /** Tour id -> when it was finished (ms since 1970). */
    done: Record<string, number>;
    /** Tour id -> the step it was closed on, to resume there. */
    left: Record<string, number>;
}

export const PROGRESS_KEY = 'drakan-editor:tours';

/** Read stored progress, ignoring anything malformed. */
export function parseProgress(raw: string | null): TourProgressData {
    const out: TourProgressData = { done: {}, left: {} };
    if (!raw) return out;
    let v: unknown;
    try {
        v = JSON.parse(raw);
    } catch {
        return out;
    }
    if (!v || typeof v !== 'object') return out;
    const pick = (o: unknown, into: Record<string, number>): void => {
        if (!o || typeof o !== 'object') return;
        for (const [k, n] of Object.entries(o as Record<string, unknown>))
            if (typeof n === 'number' && Number.isFinite(n) && n >= 0) into[k] = n;
    };
    pick((v as { done?: unknown }).done, out.done);
    pick((v as { left?: unknown }).left, out.left);
    return out;
}

/** Completed tours and where unfinished ones stopped. Storage errors (private mode, blocked) are ignored. */
export class TourProgress {
    private data: TourProgressData;

    constructor(
        private readonly kv: KeyValue | null,
        private readonly now: () => number = Date.now,
    ) {
        let raw: string | null = null;
        try {
            raw = kv?.getItem(PROGRESS_KEY) ?? null;
        } catch {
            raw = null;
        }
        this.data = parseProgress(raw);
    }

    isDone(id: string): boolean {
        return id in this.data.done;
    }

    /** Step to resume at, or null when the tour was finished or never started. */
    resumeAt(id: string): number | null {
        return id in this.data.left ? this.data.left[id]! : null;
    }

    markDone(id: string): void {
        this.data.done[id] = this.now();
        delete this.data.left[id];
        this.save();
    }

    /** Remember the step a tour was left on (index 0 is not worth resuming). */
    markLeft(id: string, index: number): void {
        if (index > 0) this.data.left[id] = index;
        else delete this.data.left[id];
        this.save();
    }

    forget(id: string): void {
        delete this.data.done[id];
        delete this.data.left[id];
        this.save();
    }

    private save(): void {
        try {
            this.kv?.setItem(PROGRESS_KEY, JSON.stringify(this.data));
        } catch {
            // Storage full or blocked: progress just is not remembered.
        }
    }
}

// ---- card placement -------------------------------------------------------------------------------------------------

export interface Box {
    x: number;
    y: number;
    w: number;
    h: number;
}

/**
 * Where the callout card goes next to `target` (already padded): the preferred side when the card fits there, else
 * the side with the most room; clamped inside the window with `margin`. No target: the middle of the window. When no
 * side has room (a target that fills the window) the card sits inside the target near its bottom edge.
 */
export function placeCard(
    target: Box | null,
    card: { w: number; h: number },
    view: { w: number; h: number },
    prefer?: TourSide,
    gap = 14,
    margin = 12,
): { x: number; y: number; side: TourSide | 'center' | 'inside' } {
    const clampX = (x: number): number => Math.max(margin, Math.min(x, view.w - card.w - margin));
    const clampY = (y: number): number => Math.max(margin, Math.min(y, view.h - card.h - margin));
    if (!target) return { x: clampX((view.w - card.w) / 2), y: clampY((view.h - card.h) / 2), side: 'center' };
    const room: Record<TourSide, number> = {
        top: target.y - gap - margin,
        bottom: view.h - (target.y + target.h) - gap - margin,
        left: target.x - gap - margin,
        right: view.w - (target.x + target.w) - gap - margin,
    };
    const fits = (s: TourSide): boolean => (s === 'top' || s === 'bottom' ? room[s] >= card.h : room[s] >= card.w);
    const order: TourSide[] = ['bottom', 'right', 'left', 'top'];
    const side: TourSide | undefined = prefer && fits(prefer) ? prefer : order.find(fits);
    const cx = target.x + target.w / 2;
    const cy = target.y + target.h / 2;
    switch (side) {
        case 'bottom':
            return { x: clampX(cx - card.w / 2), y: target.y + target.h + gap, side };
        case 'top':
            return { x: clampX(cx - card.w / 2), y: target.y - gap - card.h, side };
        case 'right':
            return { x: target.x + target.w + gap, y: clampY(cy - card.h / 2), side };
        case 'left':
            return { x: target.x - gap - card.w, y: clampY(cy - card.h / 2), side };
        default:
            return {
                x: clampX(cx - card.w / 2),
                y: clampY(Math.min(target.y + target.h, view.h) - card.h - gap * 2),
                side: 'inside',
            };
    }
}

/** SVG path (even-odd) of the whole window with rounded holes, for the dimmed and blurred shade. */
export function shadePath(view: { w: number; h: number }, holes: readonly Box[], radius = 6): string {
    const f = (n: number): string => String(Math.round(n * 10) / 10);
    let d = `M0 0H${f(view.w)}V${f(view.h)}H0Z`;
    for (const b of holes) {
        if (b.w <= 0 || b.h <= 0) continue;
        const r = Math.max(0, Math.min(radius, b.w / 2, b.h / 2));
        const x2 = b.x + b.w;
        const y2 = b.y + b.h;
        d +=
            `M${f(b.x + r)} ${f(b.y)}H${f(x2 - r)}A${f(r)} ${f(r)} 0 0 1 ${f(x2)} ${f(b.y + r)}` +
            `V${f(y2 - r)}A${f(r)} ${f(r)} 0 0 1 ${f(x2 - r)} ${f(y2)}H${f(b.x + r)}` +
            `A${f(r)} ${f(r)} 0 0 1 ${f(b.x)} ${f(y2 - r)}V${f(b.y + r)}A${f(r)} ${f(r)} 0 0 1 ${f(b.x + r)} ${f(b.y)}Z`;
    }
    return d;
}

/**
 * Make holes that never overlap, so even-odd holes cannot cancel each other out. A thin overlap (two padded panels
 * side by side) is trimmed off the second box; a large one joins both into their bounding box.
 */
export function mergeBoxes(boxes: readonly Box[]): Box[] {
    const out = boxes.filter((b) => b.w > 0 && b.h > 0).map((b) => ({ ...b }));
    for (let again = true; again;) {
        again = false;
        for (let i = 0; i < out.length && !again; i++)
            for (let j = i + 1; j < out.length; j++) {
                const a = out[i]!;
                const b = out[j]!;
                const ox = Math.min(a.x + a.w, b.x + b.w) - Math.max(a.x, b.x);
                const oy = Math.min(a.y + a.h, b.y + b.h) - Math.max(a.y, b.y);
                if (ox <= 0 || oy <= 0) continue;
                if (ox <= oy && ox < Math.min(a.w, b.w) / 2) {
                    if (b.x < a.x) b.w -= ox;
                    else {
                        b.x += ox;
                        b.w -= ox;
                    }
                } else if (oy < ox && oy < Math.min(a.h, b.h) / 2) {
                    if (b.y < a.y) b.h -= oy;
                    else {
                        b.y += oy;
                        b.h -= oy;
                    }
                } else {
                    const x = Math.min(a.x, b.x);
                    const y = Math.min(a.y, b.y);
                    out[i] = { x, y, w: Math.max(a.x + a.w, b.x + b.w) - x, h: Math.max(a.y + a.h, b.y + b.h) - y };
                    out.splice(j, 1);
                }
                again = true;
                break;
            }
    }
    return out;
}

/** Pad a box and keep it inside the window. */
export function padBox(b: Box, pad: number, view: { w: number; h: number }): Box {
    const x = Math.max(0, b.x - pad);
    const y = Math.max(0, b.y - pad);
    const x2 = Math.min(view.w, b.x + b.w + pad);
    const y2 = Math.min(view.h, b.y + b.h + pad);
    return { x, y, w: Math.max(0, x2 - x), h: Math.max(0, y2 - y) };
}
