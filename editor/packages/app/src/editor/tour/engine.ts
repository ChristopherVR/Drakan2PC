/**
 * Guided tours, the overlay: a spotlight cut out of a dimmed and blurred shade around the step's target, corner
 * brackets that close in on it, a callout card next to it (title, a sentence or two, step n of m, Back / Next / Skip),
 * and a pointer that shows the click or the drag. The step sequencing is `TourRunner` (model.ts); this file only draws
 * it and feeds it clicks and editor events.
 *
 * The target itself stays usable: the shade has a hole there, so clicks reach it. Open menus, dropdowns and pop-ups
 * get holes too, so a step can say "open the Tools menu, then click Save Editor". The layout is re-measured every
 * frame while a tour runs, so window resizes, splitter drags and scrolling keep the spotlight on its target.
 */
import { h, icon } from '../dom.ts';
import { onTourEvent } from './events.ts';
import {
    TourRunner,
    mergeBoxes,
    padBox,
    placeCard,
    shadePath,
    type Box,
    type RunnerState,
    type Tour,
    type TourStep,
    type TourTarget,
} from './model.ts';
import './tour.css';

/** Pop-ups that open above the editor and must stay reachable during a step. */
const POPUPS =
    '.ui-menu, .qa-pop, .ui-dd-pop, .ed-pop, .pal-card, .lg-add, [role="dialog"]:not(.tour-card):not(.tour-picker)';
/** How long a missing target is waited for before the card says so, ms. */
const MISSING_MS = 6000;

const reducedMotion = (): boolean => {
    try {
        return matchMedia('(prefers-reduced-motion: reduce)').matches;
    } catch {
        return false;
    }
};

function visible(el: Element): boolean {
    const r = el.getBoundingClientRect();
    if (r.width <= 0 || r.height <= 0) return false;
    // Pushed out of the window (a toolbar too narrow for its buttons): treat as not on screen.
    if (r.right <= 0 || r.bottom <= 0 || r.left >= window.innerWidth || r.top >= window.innerHeight) return false;
    const cs = getComputedStyle(el);
    return cs.visibility !== 'hidden' && cs.display !== 'none' && Number(cs.opacity) > 0.05;
}

/** The element a target names now (the first visible match of a selector), or null. */
export function resolveTarget(t: TourTarget | undefined): HTMLElement | null {
    if (!t) return null;
    try {
        if (typeof t === 'function') {
            const el = t();
            return el instanceof HTMLElement && visible(el) ? el : null;
        }
        for (const el of document.querySelectorAll<HTMLElement>(t)) if (visible(el)) return el;
    } catch {
        // a bad selector or a resolver that threw: treat as missing
    }
    return null;
}

const boxOf = (el: Element): Box => {
    const r = el.getBoundingClientRect();
    return { x: r.left, y: r.top, w: r.width, h: r.height };
};

const unionBox = (a: Box, b: Box): Box => {
    const x = Math.min(a.x, b.x);
    const y = Math.min(a.y, b.y);
    return { x, y, w: Math.max(a.x + a.w, b.x + b.w) - x, h: Math.max(a.y + a.h, b.y + b.h) - y };
};

const sameBox = (a: Box | null, b: Box | null): boolean =>
    a === b ||
    (!!a &&
        !!b &&
        Math.abs(a.x - b.x) < 0.5 &&
        Math.abs(a.y - b.y) < 0.5 &&
        Math.abs(a.w - b.w) < 0.5 &&
        Math.abs(a.h - b.h) < 0.5);

export interface TourCallbacks {
    /** Finished every step. */
    onDone(tour: Tour): void;
    /** Closed early on step `index`. */
    onClosed(tour: Tour, index: number): void;
    /** After the last step: open the tour list. */
    onMore?(): void;
}

/** One running tour on screen. `stop()` removes everything it added. */
export class TourOverlay {
    readonly runner: TourRunner;
    private readonly root: HTMLElement;
    private readonly shade: HTMLElement;
    private readonly frame: HTMLElement;
    private readonly pointer: HTMLElement;
    private readonly ghost: HTMLElement;
    private readonly trail: SVGSVGElement;
    private readonly card: HTMLElement;
    private readonly live: HTMLElement;
    private raf = 0;
    private target: HTMLElement | null = null;
    private lastBox: Box | null = null;
    private lastHoles = '!';
    private lastView = '';
    private stepStarted = 0;
    private scrolled = false;
    private demoKey = '';
    private demoAnims: Animation[] = [];
    private readonly offs: (() => void)[] = [];
    private readonly reduced = reducedMotion();
    private finished = false;

    constructor(
        readonly tour: Tour,
        private readonly cb: TourCallbacks,
    ) {
        this.root = h('div', { class: 'tour-root', 'data-tour-root': '' });
        if (this.reduced) this.root.classList.add('reduced');
        this.shade = h('div', { class: 'tour-shade' });
        this.frame = h(
            'div',
            { class: 'tour-frame', 'aria-hidden': 'true' },
            h('i', { class: 'tl' }),
            h('i', { class: 'tr' }),
            h('i', { class: 'bl' }),
            h('i', { class: 'br' }),
        );
        this.trail = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        this.trail.classList.add('tour-trail');
        this.trail.setAttribute('aria-hidden', 'true');
        this.pointer = h('div', { class: 'tour-pointer', 'aria-hidden': 'true' }, h('span', { class: 'tour-pulse' }));
        this.pointer.insertAdjacentHTML(
            'beforeend',
            '<svg class="tour-cursor" viewBox="0 0 24 24" width="26" height="26"><path d="M5 2.5l13.5 11-6.2.6 3.6 6.9-2.6 1.3-3.5-7-4.8 4.1z"/></svg>',
        );
        this.ghost = h('div', { class: 'tour-ghost', 'aria-hidden': 'true' });
        this.pointer.append(this.ghost);
        this.card = h('section', { class: 'tour-card', role: 'dialog', 'aria-modal': 'false', tabindex: '-1' });
        this.live = h('div', { class: 'tour-live', 'aria-live': 'polite' });
        this.root.append(this.shade, this.trail, this.frame, this.pointer, this.card, this.live);

        this.runner = new TourRunner(tour, {
            onChange: (s) => this.render(s),
            onEnd: (how, index) => {
                if (how === 'done') {
                    this.finished = true;
                    this.cb.onDone(tour);
                    this.showFinished();
                } else {
                    this.cb.onClosed(tour, index);
                    this.stop();
                }
            },
        });
    }

    start(index = 0): void {
        document.body.append(this.root);
        this.offs.push(onTourEvent((ev) => this.runner.notify(ev)));
        const onClick = (e: MouseEvent): void => {
            const t = this.target;
            if (t && e.target instanceof Node && t.contains(e.target)) {
                // Let the click do its own work first (open the menu, the tab), then move on.
                setTimeout(() => this.runner.notify({ name: 'click' }), 0);
            }
        };
        document.addEventListener('click', onClick, true);
        this.offs.push(() => document.removeEventListener('click', onClick, true));
        // A value changed inside the target (a field committed with Enter or by leaving it).
        const onChange = (e: Event): void => {
            const t = this.target;
            if (t && e.target instanceof Node && t.contains(e.target))
                setTimeout(() => this.runner.notify({ name: 'target-change' }), 0);
        };
        document.addEventListener('change', onChange, true);
        this.offs.push(() => document.removeEventListener('change', onChange, true));
        const onKey = (e: KeyboardEvent): void => this.key(e);
        window.addEventListener('keydown', onKey, true);
        this.offs.push(() => window.removeEventListener('keydown', onKey, true));
        this.shade.addEventListener('pointerdown', (e) => {
            e.preventDefault();
            e.stopPropagation();
            this.nudge();
        });
        this.shade.addEventListener('wheel', (e) => e.preventDefault(), { passive: false });
        this.runner.start(index);
        const tick = (): void => {
            this.raf = requestAnimationFrame(tick);
            this.layout();
        };
        tick();
    }

    /** Remove the overlay (the runner is left as it is). */
    stop(): void {
        cancelAnimationFrame(this.raf);
        for (const off of this.offs.splice(0)) off();
        this.stopDemo();
        this.root.remove();
    }

    // ---- input --------------------------------------------------------------------------------------------------

    private key(e: KeyboardEvent): void {
        if (!this.root.isConnected) return;
        const popupOpen = !!document.querySelector(POPUPS);
        const a = document.activeElement;
        const inCard = !!a && this.card.contains(a);
        const typing =
            !!a &&
            !inCard &&
            (a instanceof HTMLInputElement ||
                a instanceof HTMLTextAreaElement ||
                a instanceof HTMLSelectElement ||
                (a as HTMLElement).isContentEditable);
        const free = inCard || !a || a === document.body || a === document.documentElement;
        if (e.key === 'Escape') {
            if (popupOpen && !inCard) return; // the menu closes first
            e.preventDefault();
            e.stopPropagation();
            if (this.finished) this.stop();
            else this.runner.close();
            return;
        }
        if (typing || !free) return;
        if (this.finished) {
            if (e.key === 'Enter') {
                e.preventDefault();
                this.stop();
            }
            return;
        }
        if (e.key === 'Enter' || e.key === 'ArrowRight') {
            // Enter on a focused card button is that button's own click.
            if (e.key === 'Enter' && a instanceof HTMLButtonElement && inCard) return;
            // A step that waits for the user is not skipped by a stray key outside the card; its button still works.
            if (this.runner.state.waiting && !inCard) return;
            e.preventDefault();
            e.stopPropagation();
            this.runner.next();
        } else if (e.key === 'ArrowLeft') {
            e.preventDefault();
            e.stopPropagation();
            this.runner.back();
        }
    }

    private nudge(): void {
        this.card.classList.remove('nudge');
        void this.card.offsetWidth;
        this.card.classList.add('nudge');
        const note = this.card.querySelector<HTMLElement>('.tour-outside');
        if (note) note.hidden = false;
    }

    // ---- card ---------------------------------------------------------------------------------------------------

    private render(s: RunnerState): void {
        if (s.phase !== 'running' || !s.step) return;
        const step = s.step;
        const n = this.runner.count;
        const firstRender = this.card.dataset.step !== step.id;
        this.card.dataset.step = step.id;
        if (firstRender) {
            this.stepStarted = performance.now();
            this.scrolled = false;
            this.target = null;
            this.lastBox = null;
            this.demoKey = '';
            this.stopDemo();
            this.root.classList.remove('settled');
        }
        const titleId = `tour-t-${this.tour.id}`;
        this.card.setAttribute('aria-labelledby', titleId);
        const segs = h('div', { class: 'tour-steps', 'aria-hidden': 'true' });
        for (let i = 0; i < n; i++) segs.append(h('i', { class: i < s.index ? 'done' : i === s.index ? 'now' : '' }));
        const last = s.index === n - 1;
        const waiting = s.waiting;
        const next = h(
            'button',
            {
                class: `ui-btn sm ${waiting ? 'tour-later' : 'primary'}`,
                type: 'button',
                onclick: () => this.runner.next(),
            },
            waiting ? 'Skip this step' : (step.nextLabel ?? (last ? 'Finish' : 'Next')),
        );
        const back = h(
            'button',
            {
                class: 'ui-btn sm tour-back',
                type: 'button',
                disabled: s.index === 0,
                onclick: () => this.runner.back(),
            },
            'Back',
        );
        const body: (HTMLElement | null)[] = [
            h('div', { class: 'tour-head' }, h('span', { class: 'tour-name' }, this.tour.title), segs),
            h('h2', { class: 'tour-title', id: titleId }, step.title),
            h('p', { class: 'tour-text' }, step.text),
            waiting
                ? h(
                      'p',
                      { class: 'tour-wait' },
                      h('span', { class: 'tour-dot', 'aria-hidden': 'true' }),
                      waitLine(step),
                  )
                : null,
            waiting && s.hinted && step.hint ? h('p', { class: 'tour-hint' }, icon('info', 14), step.hint) : null,
            h('p', { class: 'tour-missing', hidden: true }),
            h('p', { class: 'tour-outside', hidden: true }, 'Use the lit-up part of the window. Esc leaves the tour.'),
            h(
                'div',
                { class: 'tour-foot' },
                h(
                    'button',
                    { class: 'linkish tour-skip', type: 'button', onclick: () => this.runner.close() },
                    'Leave tour',
                ),
                h('span', { class: 'tour-count' }, `${s.index + 1} of ${n}`),
                back,
                next,
            ),
        ];
        this.card.replaceChildren(...body.filter((x): x is HTMLElement => !!x));
        this.card.classList.toggle('waiting', waiting);
        if (firstRender) {
            this.live.textContent = `Step ${s.index + 1} of ${n}: ${step.title}. ${step.text}`;
            this.card.classList.remove('enter');
            void this.card.offsetWidth;
            this.card.classList.add('enter');
            // Keep keyboard focus where the user is working; take it only when it is nowhere useful.
            const a = document.activeElement;
            if (!a || a === document.body || this.card.contains(a) || !a.isConnected)
                (waiting ? this.card : next).focus({ preventScroll: true });
        }
        this.lastHoles = '!';
        this.layout();
    }

    private showFinished(): void {
        this.target = null;
        this.stopDemo();
        this.root.classList.add('finished');
        const close = h('button', { class: 'ui-btn sm primary', type: 'button', onclick: () => this.stop() }, 'Close');
        this.card.dataset.step = '';
        this.card.replaceChildren(
            h('div', { class: 'tour-head' }, h('span', { class: 'tour-name' }, this.tour.title)),
            h('h2', { class: 'tour-title' }, h('span', { class: 'tour-tick' }, icon('check', 16)), 'Tour finished'),
            h('p', { class: 'tour-text' }, 'You can run it again, or another one, from Help > Start a tutorial.'),
            h(
                'div',
                { class: 'tour-foot' },
                this.cb.onMore
                    ? h(
                          'button',
                          {
                              class: 'linkish tour-skip',
                              type: 'button',
                              onclick: () => {
                                  this.stop();
                                  this.cb.onMore?.();
                              },
                          },
                          'More tutorials',
                      )
                    : null,
                h('span', { class: 'tour-count' }),
                close,
            ),
        );
        this.live.textContent = `${this.tour.title}: tour finished.`;
        close.focus({ preventScroll: true });
        this.lastHoles = '!';
        this.layout();
    }

    // ---- layout (every frame) -----------------------------------------------------------------------------------

    private layout(): void {
        const view = { w: window.innerWidth, h: window.innerHeight };
        const s = this.runner.state;
        const step = !this.finished && s.phase === 'running' ? s.step : null;

        // Target: found again every frame (panels re-render their buttons).
        let target: HTMLElement | null = null;
        if (step?.target) {
            target = this.target && this.target.isConnected && visible(this.target) ? this.target : null;
            if (!target) target = resolveTarget(step.target);
            if (target && !this.scrolled) {
                this.scrolled = true;
                scrollIntoViewIfNeeded(target, this.reduced);
            }
        }
        this.target = target;
        const pad = step?.pad ?? 6;
        const box = target ? padBox(boxOf(target), pad, view) : null;

        // Missing target: a note after a while, card in the middle meanwhile.
        const missing = this.card.querySelector<HTMLElement>('.tour-missing');
        if (missing && step?.target) {
            const late = !target && performance.now() - this.stepStarted > MISSING_MS;
            missing.hidden = !late;
            if (late && !missing.textContent)
                missing.textContent = "This part isn't on screen right now. Next moves on without it.";
        }

        // Holes: the target, the step's extra elements, and any open pop-up.
        const holes: Box[] = [];
        if (box) holes.push(box);
        // The card keeps clear of the extra elements too, unless together they fill most of the window.
        let avoid = box;
        for (const t of step?.also ?? []) {
            const el = resolveTarget(t);
            if (!el) continue;
            const b = padBox(boxOf(el), 4, view);
            holes.push(b);
            if (avoid) {
                const u = unionBox(avoid, b);
                if (u.w * u.h < view.w * view.h * 0.45) avoid = u;
            }
        }
        if (step)
            for (const el of document.querySelectorAll(POPUPS)) {
                if (this.root.contains(el) || !visible(el)) continue;
                const b = padBox(boxOf(el), 2, view);
                holes.push(b);
                // Never cover the menu the step just opened.
                if (avoid) {
                    const u = unionBox(avoid, b);
                    if (u.w * u.h < view.w * view.h * 0.45) avoid = u;
                } else avoid = b;
            }
        const merged = mergeBoxes(holes);
        const key = merged.map((b) => `${b.x | 0},${b.y | 0},${b.w | 0},${b.h | 0}`).join(';');
        const viewKey = `${view.w}x${view.h}`;
        if (key !== this.lastHoles || viewKey !== this.lastView) {
            this.lastHoles = key;
            this.lastView = viewKey;
            const path = shadePath(view, merged, 8);
            this.shade.style.clipPath = `path(evenodd, "${path}")`;
        }

        // Frame (corner brackets) on the target.
        if (box && !sameBox(box, this.lastBox)) {
            Object.assign(this.frame.style, {
                transform: `translate(${box.x}px, ${box.y}px)`,
                width: `${box.w}px`,
                height: `${box.h}px`,
            });
            if (!this.lastBox) {
                this.frame.classList.remove('lock');
                void this.frame.offsetWidth;
                this.frame.classList.add('lock');
            }
        }
        this.frame.hidden = !box;
        this.lastBox = box;
        this.root.classList.toggle('has-target', !!box);

        // Card next to the target.
        const cw = this.card.offsetWidth || 320;
        const ch = this.card.offsetHeight || 180;
        const at = placeCard(avoid, { w: cw, h: ch }, view, step?.side);
        this.card.style.transform = `translate(${Math.round(at.x)}px, ${Math.round(at.y)}px)`;
        this.card.dataset.side = at.side;
        if (box && (at.side === 'top' || at.side === 'bottom')) {
            const ax = Math.max(16, Math.min(cw - 16, box.x + box.w / 2 - at.x));
            this.card.style.setProperty('--tour-arrow', `${ax}px`);
        } else if (box && (at.side === 'left' || at.side === 'right')) {
            const ay = Math.max(16, Math.min(ch - 16, box.y + box.h / 2 - at.y));
            this.card.style.setProperty('--tour-arrow', `${ay}px`);
        }

        this.demo(step, target, box);
    }

    // ---- pointer and demo animation -----------------------------------------------------------------------------

    private demo(step: TourStep | null, target: HTMLElement | null, box: Box | null): void {
        const d = step?.demo;
        const wantsPointer = !!step && !!box && (!!d || (!!step.wait && step.wait.kind !== 'next'));
        if (!wantsPointer || !box) {
            this.pointer.hidden = true;
            this.trail.replaceChildren();
            if (this.demoKey) this.stopDemo();
            this.demoKey = '';
            return;
        }
        this.pointer.hidden = false;
        if (d?.kind === 'drag') {
            const from = resolveTarget(d.from);
            const to = resolveTarget(d.to);
            if (!from || !to) {
                this.pointer.hidden = true;
                return;
            }
            const a = boxOf(from);
            const b = boxOf(to);
            const ax = a.x + Math.min(a.w / 2, 60);
            const ay = a.y + a.h / 2;
            const bx = b.x + b.w / 2;
            const by = b.y + b.h / 2;
            const key = `drag:${ax | 0},${ay | 0},${bx | 0},${by | 0}`;
            if (key === this.demoKey) return;
            this.stopDemo();
            this.demoKey = key;
            this.ghost.textContent = d.label ?? (from.textContent ?? '').trim().slice(0, 28);
            this.ghost.hidden = false;
            this.pointer.style.transform = `translate(${ax}px, ${ay}px)`;
            this.drawTrail(ax, ay, bx, by);
            if (this.reduced) return;
            const dx = bx - ax;
            const dy = by - ay;
            const mx = dx * 0.5;
            const my = dy * 0.5 - Math.min(80, Math.abs(dx) * 0.15);
            const move = this.pointer.querySelector<HTMLElement>('.tour-cursor')!;
            const frames: Keyframe[] = [
                { transform: 'translate(0px, 0px) scale(1)', offset: 0 },
                { transform: 'translate(0px, 0px) scale(0.86)', offset: 0.12 },
                { transform: `translate(${mx}px, ${my}px) scale(0.86)`, offset: 0.45, easing: 'ease-out' },
                { transform: `translate(${dx}px, ${dy}px) scale(0.86)`, offset: 0.72 },
                { transform: `translate(${dx}px, ${dy}px) scale(1)`, offset: 0.8 },
                { transform: `translate(${dx}px, ${dy}px) scale(1)`, offset: 1 },
            ];
            const timing: KeyframeAnimationOptions = { duration: 2600, iterations: Infinity, easing: 'ease-in-out' };
            this.demoAnims.push(move.animate(frames, timing));
            this.demoAnims.push(
                this.ghost.animate(
                    frames.map((f, i) => ({ ...f, opacity: i === 0 || i === frames.length - 1 ? 0 : i > 4 ? 0 : 1 })),
                    timing,
                ),
            );
            return;
        }
        // Click (the default pointer for a step that waits): tap the target's middle.
        const cx = box.x + box.w / 2;
        const cy = box.y + box.h / 2;
        const key = `click:${cx | 0},${cy | 0}`;
        if (key === this.demoKey) return;
        this.stopDemo();
        this.demoKey = key;
        this.ghost.hidden = true;
        this.trail.replaceChildren();
        this.pointer.style.transform = `translate(${cx}px, ${cy}px)`;
        // A big target (the viewport, a panel) gets the pulse only, not a cursor in its middle.
        this.pointer.classList.toggle('pulse-only', !!target && box.w * box.h > 160000);
        if (this.reduced) return;
        const cur = this.pointer.querySelector<HTMLElement>('.tour-cursor')!;
        this.demoAnims.push(
            cur.animate(
                [
                    { transform: 'translate(34px, 30px)', opacity: 0, offset: 0 },
                    { transform: 'translate(34px, 30px)', opacity: 1, offset: 0.15 },
                    { transform: 'translate(0px, 0px) scale(1)', opacity: 1, offset: 0.5, easing: 'ease-out' },
                    { transform: 'translate(0px, 0px) scale(0.82)', opacity: 1, offset: 0.58 },
                    { transform: 'translate(0px, 0px) scale(1)', opacity: 1, offset: 0.68 },
                    { transform: 'translate(0px, 0px) scale(1)', opacity: 0, offset: 1 },
                ],
                { duration: 2400, iterations: Infinity },
            ),
        );
    }

    private drawTrail(ax: number, ay: number, bx: number, by: number): void {
        const ns = 'http://www.w3.org/2000/svg';
        const mx = (ax + bx) / 2;
        const my = (ay + by) / 2 - Math.min(80, Math.abs(bx - ax) * 0.15) * 2;
        const path = document.createElementNS(ns, 'path');
        path.setAttribute('d', `M${ax} ${ay}Q${mx} ${my} ${bx} ${by}`);
        const end = document.createElementNS(ns, 'circle');
        end.setAttribute('cx', String(bx));
        end.setAttribute('cy', String(by));
        end.setAttribute('r', '10');
        this.trail.replaceChildren(path, end);
    }

    private stopDemo(): void {
        for (const a of this.demoAnims.splice(0)) a.cancel();
        this.trail.replaceChildren();
        this.ghost.hidden = true;
        this.pointer.classList.remove('pulse-only');
    }
}

function waitLine(step: TourStep): string {
    const w = step.wait;
    if (!w) return '';
    if (step.doing) return `Waiting for you to ${step.doing}.`;
    if (w.kind === 'click') return 'Click the lit-up part to go on.';
    return 'Waiting for you: the tour moves on when it is done.';
}

/** Scroll the target into its scroll parent's view when it is cut off. */
function scrollIntoViewIfNeeded(el: HTMLElement, reduced: boolean): void {
    const r = el.getBoundingClientRect();
    let p: HTMLElement | null = el.parentElement;
    while (p && p !== document.body) {
        const cs = getComputedStyle(p);
        if (/(auto|scroll)/.test(cs.overflowY + cs.overflowX)) {
            const pr = p.getBoundingClientRect();
            if (r.top < pr.top || r.bottom > pr.bottom || r.left < pr.left || r.right > pr.right) {
                el.scrollIntoView({ block: 'nearest', inline: 'nearest', behavior: reduced ? 'auto' : 'smooth' });
            }
            return;
        }
        p = p.parentElement;
    }
    if (r.bottom > window.innerHeight || r.top < 0) el.scrollIntoView({ block: 'nearest' });
}
