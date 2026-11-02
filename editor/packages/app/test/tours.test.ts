import { describe, expect, it } from 'vitest';
import {
    PROGRESS_KEY,
    TourProgress,
    TourRunner,
    eventMatches,
    mergeBoxes,
    padBox,
    parseProgress,
    placeCard,
    shadePath,
    type Clock,
    type KeyValue,
    type RunnerState,
    type Tour,
    type TourStep,
} from '../src/editor/tour/model.ts';
import { EDITOR_TOURS, TOUR_ALIASES, saveWindowTour } from '../src/editor/tour/tours.ts';

/** A clock the test drives by hand. */
function fakeClock(): Clock & { advance(ms: number): void; pending(): number } {
    let now = 0;
    let next = 1;
    const timers = new Map<number, { at: number; fn: () => void }>();
    return {
        set(fn, ms) {
            const id = next++;
            timers.set(id, { at: now + ms, fn });
            return id;
        },
        clear(h) {
            timers.delete(h as number);
        },
        advance(ms) {
            const end = now + ms;
            for (;;) {
                const due = [...timers.entries()].filter(([, t]) => t.at <= end).sort((a, b) => a[1].at - b[1].at)[0];
                if (!due) break;
                timers.delete(due[0]);
                now = due[1].at;
                due[1].fn();
            }
            now = end;
        },
        pending: () => timers.size,
    };
}

const step = (id: string, over: Partial<TourStep> = {}): TourStep => ({ id, title: id, text: id, ...over });
const tour = (steps: TourStep[]): Tour => ({ id: 't', title: 'T', line: '', group: 'basics', minutes: 1, steps });

function run(t: Tour, clock = fakeClock()) {
    const states: RunnerState[] = [];
    const ends: [string, number][] = [];
    const r = new TourRunner(t, {
        clock,
        pollMs: 100,
        hintMs: 1000,
        onChange: (s) => states.push(s),
        onEnd: (how, i) => ends.push([how, i]),
    });
    return { r, states, ends, clock };
}

describe('TourRunner sequencing', () => {
    it('walks forward and back and finishes after the last step', () => {
        const { r, ends } = run(tour([step('a'), step('b'), step('c')]));
        r.start();
        expect(r.state.step?.id).toBe('a');
        r.back();
        expect(r.state.index).toBe(0);
        r.next();
        r.next();
        expect(r.state.step?.id).toBe('c');
        r.back();
        expect(r.state.step?.id).toBe('b');
        r.next();
        r.next();
        expect(r.state.phase).toBe('done');
        expect(ends).toEqual([['done', 2]]);
        r.next();
        expect(ends.length).toBe(1);
    });

    it('closes early with the step index', () => {
        const { r, ends } = run(tour([step('a'), step('b')]));
        r.start();
        r.next();
        r.close();
        expect(r.state.phase).toBe('closed');
        expect(ends).toEqual([['closed', 1]]);
    });

    it('resumes at a given step and clamps it', () => {
        const { r } = run(tour([step('a'), step('b'), step('c')]));
        r.start(7);
        expect(r.state.step?.id).toBe('c');
    });

    it('skips steps that are already done, in the direction of travel', () => {
        let discOpen = true;
        const t = tour([step('a'), step('disc', { skipIf: () => discOpen }), step('c')]);
        const { r } = run(t);
        r.start();
        r.next();
        expect(r.state.step?.id).toBe('c');
        r.back();
        expect(r.state.step?.id).toBe('a');
        discOpen = false;
        r.next();
        expect(r.state.step?.id).toBe('disc');
    });

    it('finishes when every remaining step is skipped', () => {
        const { r, ends } = run(tour([step('a'), step('b', { skipIf: () => true })]));
        r.start();
        r.next();
        expect(ends).toEqual([['done', 0]]);
    });

    it('runs enter and begin, and survives an enter that throws', () => {
        const calls: string[] = [];
        const t: Tour = {
            ...tour([
                step('a', { enter: () => calls.push('a') }),
                step('b', {
                    enter: () => {
                        throw new Error('no panel');
                    },
                }),
            ]),
            begin: () => calls.push('begin'),
        };
        const { r } = run(t);
        r.start();
        r.next();
        expect(calls).toEqual(['begin', 'a']);
        expect(r.state.step?.id).toBe('b');
    });
});

describe('TourRunner waits', () => {
    it('waits for a command and ignores other commands', () => {
        const { r } = run(tour([step('a', { wait: { kind: 'command', ids: ['add.puzzle'] } }), step('b')]));
        r.start();
        expect(r.state.waiting).toBe(true);
        r.notify({ name: 'command', detail: 'edit.undo' });
        expect(r.state.step?.id).toBe('a');
        r.notify({ name: 'command', detail: 'add.puzzle' });
        expect(r.state.step?.id).toBe('b');
        expect(r.state.waiting).toBe(false);
    });

    it('waits for a filtered event and a click', () => {
        const t = tour([
            step('sel', { wait: { kind: 'event', name: 'selection', test: (n) => (n as number) > 0 } }),
            step('click', { wait: { kind: 'click' } }),
            step('end'),
        ]);
        const { r } = run(t);
        r.start();
        r.notify({ name: 'selection', detail: 0 });
        expect(r.state.step?.id).toBe('sel');
        r.notify({ name: 'selection', detail: 2 });
        expect(r.state.step?.id).toBe('click');
        r.notify({ name: 'change' });
        expect(r.state.step?.id).toBe('click');
        r.notify({ name: 'click' });
        expect(r.state.step?.id).toBe('end');
    });

    it('polls until-waits and stops polling when the step changes', () => {
        let ready = false;
        const { r, clock } = run(tour([step('a', { wait: { kind: 'until', test: () => ready } }), step('b')]));
        r.start();
        clock.advance(350);
        expect(r.state.step?.id).toBe('a');
        ready = true;
        clock.advance(100);
        expect(r.state.step?.id).toBe('b');
        expect(clock.pending()).toBe(0);
    });

    it('an until-test that throws counts as not yet', () => {
        const { r, clock } = run(
            tour([
                step('a', {
                    wait: {
                        kind: 'until',
                        test: () => {
                            throw new Error('x');
                        },
                    },
                }),
            ]),
        );
        r.start();
        clock.advance(500);
        expect(r.state.step?.id).toBe('a');
    });

    it('shows the hint after the timeout, and Next still moves on', () => {
        const { r, clock } = run(
            tour([
                step('a', { wait: { kind: 'event', name: 'change' }, hint: 'try this', hintAfter: 3000 }),
                step('b'),
            ]),
        );
        r.start();
        clock.advance(2999);
        expect(r.state.hinted).toBe(false);
        clock.advance(1);
        expect(r.state.hinted).toBe(true);
        r.next();
        expect(r.state.step?.id).toBe('b');
        expect(r.state.hinted).toBe(false);
    });

    it('ignores events after closing and clears its timers', () => {
        const { r, clock } = run(tour([step('a', { wait: { kind: 'until', test: () => false } }), step('b')]));
        r.start();
        r.close();
        expect(clock.pending()).toBe(0);
        r.notify({ name: 'click' });
        expect(r.state.phase).toBe('closed');
    });

    it('eventMatches covers each wait kind', () => {
        expect(eventMatches({ kind: 'next' }, { name: 'click' })).toBe(false);
        expect(eventMatches(undefined, { name: 'click' })).toBe(false);
        expect(eventMatches({ kind: 'command', ids: ['a'] }, { name: 'command', detail: 'a' })).toBe(true);
        expect(eventMatches({ kind: 'command', ids: ['a'] }, { name: 'event', detail: 'a' })).toBe(false);
        expect(eventMatches({ kind: 'event', name: 'level-load' }, { name: 'level-load' })).toBe(true);
    });
});

describe('TourProgress', () => {
    const mem = (init: Record<string, string> = {}): KeyValue & { data: Record<string, string> } => {
        const data = { ...init };
        return {
            data,
            getItem: (k) => data[k] ?? null,
            setItem: (k, v) => {
                data[k] = v;
            },
        };
    };

    it('remembers finished tours and where unfinished ones stopped', () => {
        const kv = mem();
        const p = new TourProgress(kv, () => 42);
        p.markLeft('puzzle', 3);
        expect(p.resumeAt('puzzle')).toBe(3);
        p.markDone('puzzle');
        expect(p.isDone('puzzle')).toBe(true);
        expect(p.resumeAt('puzzle')).toBe(null);
        const again = new TourProgress(kv);
        expect(again.isDone('puzzle')).toBe(true);
        expect(JSON.parse(kv.data[PROGRESS_KEY]!)).toEqual({ done: { puzzle: 42 }, left: {} });
    });

    it('does not keep step 0 as a resume point, and forgets', () => {
        const p = new TourProgress(mem());
        p.markLeft('a', 0);
        expect(p.resumeAt('a')).toBe(null);
        p.markLeft('a', 2);
        p.forget('a');
        expect(p.resumeAt('a')).toBe(null);
    });

    it('works when storage throws or is missing', () => {
        const broken: KeyValue = {
            getItem: () => {
                throw new Error('blocked');
            },
            setItem: () => {
                throw new Error('full');
            },
        };
        const p = new TourProgress(broken);
        expect(() => p.markDone('x')).not.toThrow();
        expect(p.isDone('x')).toBe(true);
        expect(new TourProgress(null).isDone('x')).toBe(false);
    });

    it('ignores malformed stored data', () => {
        expect(parseProgress('not json')).toEqual({ done: {}, left: {} });
        expect(parseProgress('[1,2]')).toEqual({ done: {}, left: {} });
        expect(parseProgress(JSON.stringify({ done: { a: 'x', b: 5 }, left: { c: -1, d: 2 } }))).toEqual({
            done: { b: 5 },
            left: { d: 2 },
        });
    });
});

describe('geometry', () => {
    const view = { w: 1000, h: 800 };
    const card = { w: 300, h: 150 };

    it('centres the card without a target', () => {
        expect(placeCard(null, card, view)).toEqual({ x: 350, y: 325, side: 'center' });
    });

    it('uses the preferred side when it fits and falls back when it does not', () => {
        const t = { x: 400, y: 100, w: 200, h: 40 };
        expect(placeCard(t, card, view, 'bottom').side).toBe('bottom');
        expect(placeCard(t, card, view, 'top').side).toBe('bottom');
        const right = placeCard({ x: 0, y: 0, w: 300, h: 800 }, card, view, 'left');
        expect(right.side).toBe('right');
        expect(right.x).toBe(314);
    });

    it('keeps the card inside the window', () => {
        const at = placeCard({ x: 980, y: 10, w: 20, h: 20 }, card, view);
        expect(at.x + card.w).toBeLessThanOrEqual(view.w - 12);
        expect(at.x).toBeGreaterThanOrEqual(12);
    });

    it('goes inside a target that fills the window', () => {
        const at = placeCard({ x: 0, y: 0, w: 1000, h: 800 }, card, view);
        expect(at.side).toBe('inside');
        expect(at.y + card.h).toBeLessThanOrEqual(view.h);
    });

    it('pads and clamps boxes', () => {
        expect(padBox({ x: 2, y: 2, w: 10, h: 10 }, 6, view)).toEqual({ x: 0, y: 0, w: 18, h: 18 });
    });

    it('trims a thin overlap between side-by-side panels instead of joining them', () => {
        const m = mergeBoxes([
            { x: 0, y: 0, w: 106, h: 800 },
            { x: 100, y: 0, w: 400, h: 300 },
        ]);
        expect(m).toEqual([
            { x: 0, y: 0, w: 106, h: 800 },
            { x: 106, y: 0, w: 394, h: 300 },
        ]);
    });

    it('merges overlapping holes so they do not cancel out', () => {
        const m = mergeBoxes([
            { x: 0, y: 0, w: 10, h: 10 },
            { x: 5, y: 5, w: 10, h: 10 },
            { x: 100, y: 100, w: 5, h: 5 },
            { x: 0, y: 0, w: 0, h: 5 },
        ]);
        expect(m).toEqual([
            { x: 0, y: 0, w: 15, h: 15 },
            { x: 100, y: 100, w: 5, h: 5 },
        ]);
    });

    it('draws one sub-path per hole', () => {
        const d = shadePath(view, [{ x: 10, y: 10, w: 100, h: 50 }], 6);
        expect(d.startsWith('M0 0H1000V800H0Z')).toBe(true);
        expect(d.match(/M/g)?.length).toBe(2);
        expect(d).not.toContain('\n');
    });
});

describe('tour content', () => {
    const all = [...EDITOR_TOURS, saveWindowTour];

    it('has the required tours with unique ids and steps', () => {
        const ids = all.map((t) => t.id);
        expect(new Set(ids).size).toBe(ids.length);
        for (const id of ['editor', 'campaign', 'newlevel', 'puzzle', 'cutscene', 'vendor', 'build-campaign', 'arena'])
            expect(ids).toContain(id);
        expect(ids).toContain('save');
        for (const t of all) {
            expect(t.steps.length).toBeGreaterThan(1);
            const sids = t.steps.map((s) => s.id);
            expect(new Set(sids).size).toBe(sids.length);
            for (const s of t.steps) {
                expect(s.title.length).toBeGreaterThan(0);
                expect(s.text.length).toBeLessThan(260);
            }
        }
    });

    it('aliases point at tours', () => {
        const ids = new Set(all.map((t) => t.id));
        for (const target of Object.values(TOUR_ALIASES)) expect(ids.has(target)).toBe(true);
    });
});
