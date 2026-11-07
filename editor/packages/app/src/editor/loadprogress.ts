import { h, icon } from './dom.ts';
import {
    LOAD_STEPS,
    estimate,
    headline,
    levelShown,
    loadFinished,
    loadFraction,
    newLoad,
    readyLine,
    stepDetail,
    type LoadState,
    type LoadStepId,
    type LoadStepState,
} from './loadsteps.ts';
import './loadprogress.css';

/**
 * The loading card of the viewport. It appears the moment a level is chosen (on the start page, in the toolbar, with
 * Ctrl+P): first over the viewport while the level file is read, then, once the objects are placed, as a small strip
 * in the viewport's corner while the terrain and the models fill in, so the level can be used straight away.
 */
export interface LoadProgress {
    /** Start showing the load of `name`. `covering`: no level is open, so the card hides the empty viewport. */
    begin(name: string, o: { covering: boolean; converted?: boolean; onCancel(): void; onStop(): void }): void;
    step(id: LoadStepId, state: LoadStepState): void;
    set(patch: Partial<Pick<LoadState, 'objects' | 'layers' | 'models' | 'error'>>): void;
    /** Everything is in: say so briefly, then go. */
    finish(): void;
    hide(): void;
    /** A load is being shown. */
    active(): boolean;
    /** The load state (tests, harnesses). */
    state(): LoadState | null;
}

/** Fired on `document` when the card starts or stops covering the viewport (the checklist steps aside meanwhile). */
export const LOAD_CARD_EVENT = 'level-load-card';

/** The card covers the viewport (a level is being read; nothing to work on yet). */
export const loadCardCovering = (): boolean =>
    !!document.querySelector('#lv-viewport .lp:not([hidden]):not(.strip):not(.ready)');

/** Usual time of the steps that report no count of their own (the bar estimates their share from elapsed time). */
const TYPICAL_MS: Partial<Record<LoadStepId, number>> = { read: 700, objects: 150, terrain: 900 };
const READY_MS = 2600;

function splitTitle(name: string): [string, string] {
    const m = /^(Level\s+\S+)\s*[-–]\s*(.+)$/.exec(name);
    return m ? [m[2]!.trim(), m[1]!] : [name, ''];
}

export function createLoadProgress(host: HTMLElement): LoadProgress {
    let s: LoadState | null = null;
    let actions: { onCancel(): void; onStop(): void } | null = null;
    let since = 0;
    let ticker = 0;
    let readyTimer = 0;
    let title = '';

    const fill = h('div', { class: 'lp-fill' });
    const bar = h('div', { class: 'lp-bar', role: 'progressbar', 'aria-valuemin': '0', 'aria-valuemax': '100' }, fill);
    const pct = h('span', { class: 'lp-pct' });
    const heading = h('h2', { class: 'lp-title' });
    const sub = h('p', { class: 'lp-sub' });
    const now = h('span', { class: 'lp-now' });
    const steps = h('ol', { class: 'lp-steps' });
    const note = h('p', { class: 'lp-note' });
    const btn = h('button', { class: 'ui-btn sm lp-btn', type: 'button' });
    const close = h(
        'button',
        { class: 'tool sm lp-close', type: 'button', 'aria-label': 'Close', title: 'Close' },
        icon('x', 12),
    );
    const card = h(
        'div',
        { class: 'lp-card' },
        h('div', { class: 'lp-head' }, h('div', { class: 'lp-names' }, heading, sub), close),
        h('div', { class: 'lp-line' }, now, pct),
        bar,
        steps,
        h('div', { class: 'lp-foot' }, note, btn),
    );
    const el = h(
        'div',
        { class: 'lp', role: 'status', 'aria-live': 'polite', hidden: true },
        h('div', { class: 'lp-backdrop' }),
        card,
    );
    host.append(el);

    btn.addEventListener('click', () => {
        if (!s || !actions) return;
        if (levelShown(s)) actions.onStop();
        else actions.onCancel();
    });
    close.addEventListener('click', () => hide());

    function tick(): void {
        if (!s) return;
        const active = LOAD_STEPS.find((st) => s!.steps[st.id] === 'active');
        const typ = active ? TYPICAL_MS[active.id] : undefined;
        s.partial = typ ? estimate(performance.now() - since, typ) : 0;
        draw();
    }

    let covering = false;
    function announce(): void {
        const now = !el.hidden && !el.classList.contains('strip') && !el.classList.contains('ready');
        if (now === covering) return;
        covering = now;
        document.dispatchEvent(new CustomEvent(LOAD_CARD_EVENT));
    }

    function draw(): void {
        if (!s) return;
        const shown = levelShown(s);
        const done = loadFinished(s);
        const f = done ? 1 : loadFraction(s);
        el.classList.toggle('strip', shown);
        // The strip sits just below the viewport's display and transform bars at the top (on a narrow viewport the
        // transform bar moves to the bottom and is left out).
        if (shown || done) {
            const box = host.getBoundingClientRect();
            let below = 0;
            for (const b of host.querySelectorAll<HTMLElement>('.vp-bar')) {
                const r = b.getBoundingClientRect();
                if (b.offsetParent && r.top - box.top < box.height / 3) below = Math.max(below, r.bottom - box.top);
            }
            el.style.paddingTop = `${Math.round(Math.min(below, box.height / 3) + 10)}px`;
        } else el.style.paddingTop = '';
        el.classList.toggle('ready', done);
        el.classList.toggle('failed', !!s.error && !shown);
        fill.style.transform = `scaleX(${f.toFixed(4)})`;
        bar.setAttribute('aria-valuenow', String(Math.round(f * 100)));
        pct.textContent = done ? '' : `${Math.round(f * 100)}%`;
        heading.textContent = done ? title : shown ? title : `Opening ${title}`;
        now.textContent = done ? readyLine(s, title) : headline(s);
        steps.replaceChildren(
            ...LOAD_STEPS.map((st) => {
                const state = s!.steps[st.id];
                const mark =
                    state === 'active'
                        ? h('span', { class: 'lp-spin' })
                        : state === 'done'
                          ? icon('check', 12)
                          : state === 'failed'
                            ? icon('warn', 12)
                            : state === 'stopped'
                              ? icon('x', 12)
                              : h('span', { class: 'lp-dot' });
                return h(
                    'li',
                    { class: `lp-step ${state}` },
                    h('span', { class: 'lp-mark' }, mark),
                    h('span', { class: 'lp-step-t' }, st.title),
                    h('span', { class: 'lp-step-d' }, stepDetail(s!, st.id)),
                );
            }),
        );
        note.textContent = done
            ? ''
            : shown
              ? 'You can look around and edit while the rest loads.'
              : 'The level appears as soon as its objects are placed.';
        // Inline display: the editor's `.tool` / `.ui-btn` rules outrank a plain [hidden].
        btn.style.display = done || s.error ? 'none' : '';
        btn.textContent = shown ? 'Stop loading' : 'Cancel';
        btn.title = shown
            ? 'Keep what has loaded; objects without a model yet show as markers'
            : 'Go back to where you were';
        close.style.display = done || s.error ? '' : 'none';
        announce();
    }

    function stopTicker(): void {
        if (ticker) window.clearInterval(ticker);
        ticker = 0;
    }

    function hide(): void {
        stopTicker();
        window.clearTimeout(readyTimer);
        el.hidden = true;
        el.classList.remove('leaving');
        s = null;
        actions = null;
        announce();
    }

    return {
        begin(name, o) {
            hide();
            s = newLoad(name);
            actions = o;
            const [t, st] = splitTitle(name);
            title = t;
            const origin = o.converted ? 'converted from Order of the Flame' : 'from your disc';
            sub.textContent = st ? `${st}, ${origin}` : origin.charAt(0).toUpperCase() + origin.slice(1);
            el.classList.toggle('covering', o.covering);
            since = performance.now();
            el.hidden = false;
            draw();
            ticker = window.setInterval(tick, 120);
        },
        step(id, state) {
            if (!s) return;
            if (s.steps[id] !== state && state === 'active') since = performance.now();
            s.steps[id] = state;
            s.partial = 0;
            draw();
        },
        set(patch) {
            if (!s) return;
            Object.assign(s, patch);
            draw();
        },
        finish() {
            if (!s) return;
            for (const st of LOAD_STEPS)
                if (s.steps[st.id] === 'active' || s.steps[st.id] === 'waiting') s.steps[st.id] = 'done';
            stopTicker();
            draw();
            window.clearTimeout(readyTimer);
            readyTimer = window.setTimeout(() => {
                el.classList.add('leaving');
                readyTimer = window.setTimeout(hide, 300);
            }, READY_MS);
        },
        hide,
        active: () => !!s,
        state: () => s,
    };
}
