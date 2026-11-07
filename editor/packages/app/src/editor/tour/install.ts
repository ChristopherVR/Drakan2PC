/**
 * Guided tours, wiring: one tour runs at a time, finished tours and the step an unfinished one was left on are
 * remembered (localStorage, `drakan-editor:tours`), and the tours are offered through
 *   - `startTour(id)` / `hasTour(id)` (tour/registry.ts; the start page and the Help drawer call these),
 *   - the command "Start a tutorial..." (`help.tours`, Help menu and Ctrl+P), which lists every tour,
 *   - a palette source: typing "tutorial" or a tour's name in Ctrl+P starts it.
 */
import { h, icon } from '../dom.ts';
import { trapFocus } from '../focustrap.ts';
import { fuzzyScore, registerCommand, registerSource } from '../registry.ts';
import { TourOverlay } from './engine.ts';
import { TourProgress, type KeyValue, type Tour } from './model.ts';
import { registerTourStarter } from './registry.ts';

const tours = new Map<string, Tour>();
/** Other names a tour answers to (Help guide ids). */
const aliases = new Map<string, string>();
let active: TourOverlay | null = null;
let progressStore: TourProgress | null = null;

function storage(): KeyValue | null {
    try {
        return window.localStorage;
    } catch {
        return null;
    }
}

const progress = (): TourProgress => (progressStore ??= new TourProgress(storage()));

export function addTours(list: readonly Tour[], alias: Record<string, string> = {}): void {
    for (const t of list) tours.set(t.id, t);
    for (const [k, v] of Object.entries(alias)) aliases.set(k, v);
}

const tourOf = (id: string): Tour | undefined => tours.get(id) ?? tours.get(aliases.get(id) ?? '');

export const listTours = (): Tour[] => [...tours.values()];

/** Start (or restart, `fresh`) a tour; an unfinished one resumes where it was left unless `fresh`. */
export function runTour(id: string, fresh = false): boolean {
    const tour = tourOf(id);
    if (!tour) return false;
    closePicker();
    if (active) {
        const prev = active;
        const st = prev.runner.state;
        if (st.phase === 'running') progress().markLeft(prev.tour.id, st.index);
        prev.stop();
        active = null;
    }
    const at = fresh ? 0 : (progress().resumeAt(tour.id) ?? 0);
    const overlay = new TourOverlay(tour, {
        onDone: (t) => progress().markDone(t.id),
        onClosed: (t, index) => {
            progress().markLeft(t.id, index);
            if (active === overlay) active = null;
        },
        onMore: () => openPicker(),
    });
    active = overlay;
    overlay.start(at >= tour.steps.length ? 0 : at);
    return true;
}

export const tourRunning = (): boolean => !!active && active.runner.state.phase === 'running';

// ---- tour list ------------------------------------------------------------------------------------------------------

let pickerEl: HTMLElement | null = null;
let pickerOff: (() => void) | null = null;

function closePicker(): void {
    pickerEl?.remove();
    pickerEl = null;
    pickerOff?.();
    pickerOff = null;
}

const GROUP_TITLE: Record<Tour['group'], string> = {
    basics: 'Get to know the editor',
    templates: 'Make something',
};

export function openPicker(): void {
    closePicker();
    const prev = document.activeElement as HTMLElement | null;
    const p = progress();
    const list = h('div', { class: 'tour-list' });
    for (const group of ['basics', 'templates'] as const) {
        const items = listTours().filter((t) => t.group === group);
        if (!items.length) continue;
        list.append(h('h3', {}, GROUP_TITLE[group]));
        for (const t of items) {
            const resume = p.resumeAt(t.id);
            const done = p.isDone(t.id);
            const state = done
                ? 'Done'
                : resume !== null
                  ? `Left at step ${resume + 1} of ${t.steps.length}`
                  : `${t.steps.length} steps, about ${t.minutes} min`;
            const btns = h('div', { class: 'tour-row-btns' });
            if (resume !== null && !done) {
                btns.append(
                    h('button', { class: 'ui-btn sm', type: 'button', onclick: () => runTour(t.id, true) }, 'Restart'),
                    h('button', { class: 'ui-btn sm primary', type: 'button', onclick: () => runTour(t.id) }, 'Resume'),
                );
            } else {
                btns.append(
                    h(
                        'button',
                        {
                            class: 'ui-btn sm',
                            type: 'button',
                            onclick: () => runTour(t.id, true),
                            'aria-label': `${done ? 'Run again' : 'Start'}: ${t.title}`,
                        },
                        done ? 'Run again' : 'Start',
                    ),
                );
            }
            list.append(
                h(
                    'div',
                    { class: 'tour-row', 'data-tour-id': t.id },
                    h(
                        'div',
                        {},
                        h('b', {}, t.title, done ? icon('check', 14) : null),
                        h('small', {}, t.line),
                        h('small', {}, state),
                    ),
                    btns,
                ),
            );
        }
    }
    const close = h(
        'button',
        { class: 'tool', type: 'button', 'aria-label': 'Close', title: 'Close', onclick: () => closePicker() },
        icon('x'),
    );
    const card = h(
        'section',
        { class: 'tour-picker', role: 'dialog', 'aria-modal': 'true', 'aria-labelledby': 'tour-picker-title' },
        h(
            'header',
            {},
            h(
                'div',
                {},
                h('h2', { id: 'tour-picker-title' }, 'Start a tutorial'),
                h('p', {}, 'Each tutorial lights up the part of the editor to use and waits while you try it.'),
            ),
            close,
        ),
        list,
    );
    const back = h('div', { class: 'tour-picker-back' }, card);
    back.addEventListener('pointerdown', (e) => {
        if (e.target === back) closePicker();
    });
    const onKey = (e: KeyboardEvent): void => {
        if (e.key === 'Escape') {
            e.preventDefault();
            e.stopPropagation();
            closePicker();
            prev?.focus?.();
        }
    };
    window.addEventListener('keydown', onKey, true);
    const untrap = trapFocus(back, () => true);
    pickerOff = () => {
        window.removeEventListener('keydown', onKey, true);
        untrap();
    };
    document.body.append(back);
    pickerEl = back;
    (list.querySelector<HTMLElement>('button.primary') ?? list.querySelector<HTMLElement>('button'))?.focus();
}

// ---- install --------------------------------------------------------------------------------------------------------

let installed = false;

/** Register the tour system (once): the starter for tour/registry.ts, the command and the palette source. */
export function installTourSystem(): void {
    if (installed) return;
    installed = true;
    registerTourStarter(
        (id) => runTour(id),
        (id) => !!tourOf(id),
    );
    registerCommand({
        id: 'help.tours',
        title: 'Start a tutorial...',
        category: 'Help',
        icon: 'rocket',
        keywords: 'tour tutorial guide walkthrough learn show me interactive',
        hint: 'Step-by-step tours that light up what to click',
        run: () => openPicker(),
    });
    registerSource({
        id: 'tours',
        label: 'Tutorials',
        search(query, limit) {
            const q = query.trim();
            if (!q) return [];
            const wantsAll = /^(tour|tuto|walk|learn|show me)/i.test(q);
            return listTours()
                .map((t) => ({
                    t,
                    score: wantsAll ? 50 : Math.max(fuzzyScore(t.title, q), fuzzyScore(`tutorial ${t.title}`, q)),
                }))
                .filter((x) => x.score >= 300)
                .sort((a, b) => b.score - a.score)
                .slice(0, limit)
                .map(({ t, score }) => ({
                    id: `tour.${t.id}`,
                    title: `Tutorial: ${t.title}`,
                    detail: progress().isDone(t.id) ? 'Done' : `${t.minutes} min`,
                    icon: 'rocket',
                    score,
                    run: () => runTour(t.id, true),
                }));
        },
    });
}
