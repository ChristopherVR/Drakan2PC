// Cut scene timeline (Sequencer style): a ruler you can scrub, and one lane per actor and kind of action (camera moves,
// animations, sounds, messages). Every key is a marker you can drag sideways to retime it; the playhead follows the
// scrub slider and the play button. Dragging only moves the marker until you let go, then one edit is made.

import { parseActions, type SsdAction, type SsdActor, type SsdSequence } from '@drakan/formats';
import { h } from '../editor/dom.ts';

export const KIND_COLOR: Record<string, string> = {
    transform: '#5c9fff',
    animation: '#4cc38a',
    sound: '#e8b04a',
    message: '#f06a62',
    camera: '#b48cff',
};

export const KIND_TITLE: Record<string, string> = {
    transform: 'Moves',
    animation: 'Animation',
    sound: 'Sound',
    message: 'Message',
    camera: 'Camera',
};

export interface Lane {
    actorIndex: number;
    actor: string;
    kind: string;
    keys: { index: number; action: SsdAction }[];
}

/** The lanes of a sequence: one per (actor, kind) that has keys, in actor order; markers and holds are not shown. */
export function lanesOf(seq: SsdSequence): Lane[] {
    const out: Lane[] = [];
    seq.actors.forEach((a: SsdActor, ai) => {
        const acts = parseActions(a) ?? [];
        const byKind = new Map<string, Lane>();
        acts.forEach((k, index) => {
            if (k.kind === 'marker' || k.kind === 'hold') return;
            let lane = byKind.get(k.kind);
            if (!lane) {
                lane = { actorIndex: ai, actor: a.name, kind: k.kind, keys: [] };
                byKind.set(k.kind, lane);
                out.push(lane);
            }
            lane.keys.push({ index, action: k });
        });
    });
    return out;
}

/** Seconds for a pointer x inside a track, snapped to `step` and kept inside the scene. */
export function timeAt(x: number, width: number, dur: number, step = 0.05): number {
    const raw = (Math.min(Math.max(x, 0), width) / Math.max(width, 1)) * dur;
    return Math.round(raw / step) * step;
}

export interface TimelineOptions {
    seq: SsdSequence;
    dur: number;
    getT(): number;
    /** The playhead was moved by the user. */
    scrub(t: number): void;
    /** A key was dropped at a new time. */
    moveKey(actorIndex: number, keyIndex: number, t: number): void;
    /** A key was clicked (to show its details). */
    pick?(actorIndex: number, keyIndex: number): void;
    describe?(actorIndex: number, key: SsdAction): string;
}

export interface Timeline {
    el: HTMLElement;
    /** Move the playhead to the current time. */
    sync(): void;
}

const LABEL_W = 150;

/** A key moved from the keyboard: the timeline is rebuilt, so the moved key takes the focus again once it exists. */
let refocus: { actor: number; time: number } | null = null;

export function createTimeline(o: TimelineOptions): Timeline {
    const lanes = lanesOf(o.seq);
    const playhead = h('div', { class: 'tl-playhead' });
    const rulerTrack = h('div', { class: 'tl-track tl-ruler-track' });
    const step = o.dur > 30 ? 5 : o.dur > 10 ? 2 : o.dur > 4 ? 1 : 0.5;
    for (let t = 0; t <= o.dur + 1e-6; t += step)
        rulerTrack.append(
            h(
                'div',
                { class: 'tl-tick', style: `left:${(t / o.dur) * 100}%` },
                h('span', {}, `${Number.isInteger(t) ? t : t.toFixed(1)}s`),
            ),
        );
    const rows: HTMLElement[] = [
        h('div', { class: 'tl-row tl-ruler' }, h('div', { class: 'tl-label' }, 'Time'), rulerTrack),
    ];
    const tracks: HTMLElement[] = [rulerTrack];
    for (const lane of lanes) {
        const track = h('div', { class: 'tl-track' });
        tracks.push(track);
        const color = KIND_COLOR[lane.kind] ?? '#a3a3ad';
        for (const k of lane.keys) {
            const key = h('button', {
                class: 'tl-key',
                type: 'button',
                style: `left:${(k.action.time / o.dur) * 100}%;--c:${color}`,
                title: `${lane.actor}: ${o.describe?.(lane.actorIndex, k.action) ?? KIND_TITLE[lane.kind] ?? lane.kind} at ${k.action.time.toFixed(2)} s. Drag to move it.`,
                'aria-label': `${lane.actor}, ${KIND_TITLE[lane.kind] ?? lane.kind} key at ${k.action.time.toFixed(2)} seconds. Left and Right arrows move it by a tenth of a second, with Shift by a second.`,
            });
            if (refocus && refocus.actor === lane.actorIndex && Math.abs(refocus.time - k.action.time) < 1e-3) {
                key.dataset.refocus = '1';
                refocus = null;
            }
            // Keyboard: the same two things the pointer does, pick a key and move it.
            key.addEventListener('keydown', (e) => {
                if (e.key === 'Enter' || e.key === ' ') {
                    e.preventDefault();
                    o.pick?.(lane.actorIndex, k.index);
                } else if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
                    e.preventDefault();
                    const by = (e.shiftKey ? 1 : 0.1) * (e.key === 'ArrowLeft' ? -1 : 1);
                    const t = Math.min(o.dur, Math.max(0, Math.round((k.action.time + by) * 100) / 100));
                    if (Math.abs(t - k.action.time) < 1e-6) return;
                    refocus = { actor: lane.actorIndex, time: t };
                    o.moveKey(lane.actorIndex, k.index, t);
                }
            });
            key.addEventListener('pointerdown', (e) => {
                if (e.button !== 0) return;
                e.preventDefault();
                e.stopPropagation();
                key.setPointerCapture(e.pointerId);
                key.classList.add('drag');
                const r = track.getBoundingClientRect();
                let t = k.action.time;
                let moved = false;
                const move = (m: PointerEvent): void => {
                    if (Math.abs(m.clientX - e.clientX) > 3) moved = true;
                    if (!moved) return;
                    t = timeAt(m.clientX - r.left, r.width, o.dur);
                    key.style.left = `${(t / o.dur) * 100}%`;
                    key.title = `${t.toFixed(2)} s`;
                };
                const up = (): void => {
                    key.removeEventListener('pointermove', move);
                    key.removeEventListener('pointerup', up);
                    key.classList.remove('drag');
                    if (moved && Math.abs(t - k.action.time) > 1e-6) o.moveKey(lane.actorIndex, k.index, t);
                    else o.pick?.(lane.actorIndex, k.index);
                };
                key.addEventListener('pointermove', move);
                key.addEventListener('pointerup', up);
            });
            track.append(key);
        }
        rows.push(
            h(
                'div',
                { class: 'tl-row' },
                h(
                    'div',
                    { class: 'tl-label', title: `${lane.actor}: ${KIND_TITLE[lane.kind] ?? lane.kind}` },
                    h('span', { class: 'tl-dot', style: `background:${color}` }),
                    h('span', { class: 'tl-name' }, `${lane.actor}`),
                    h('span', { class: 'dim' }, KIND_TITLE[lane.kind] ?? lane.kind),
                ),
                track,
            ),
        );
    }
    if (!lanes.length)
        rows.push(
            h(
                'div',
                { class: 'tl-none dim' },
                'This cut scene has nothing on its timeline yet. Add an actor, then a sound, message or animation.',
            ),
        );
    const area = h('div', { class: 'tl-area' }, ...rows, playhead);
    const el = h('div', { class: 'tl', style: `--label:${LABEL_W}px` }, area);
    if (el.querySelector('[data-refocus]')) {
        requestAnimationFrame(() => el.querySelector<HTMLElement>('[data-refocus]')?.focus({ preventScroll: true }));
    }

    const place = (): void => {
        const r = rulerTrack.getBoundingClientRect();
        const a = area.getBoundingClientRect();
        playhead.style.left = `${r.left - a.left + (Math.min(o.getT(), o.dur) / o.dur) * r.width}px`;
    };
    const scrubTo = (e: PointerEvent): void => {
        const r = rulerTrack.getBoundingClientRect();
        o.scrub(timeAt(e.clientX - r.left, r.width, o.dur, 0.01));
    };
    const startScrub = (e: PointerEvent): void => {
        if (e.button !== 0) return;
        const target = e.currentTarget as HTMLElement;
        target.setPointerCapture(e.pointerId);
        scrubTo(e);
        const move = (m: PointerEvent): void => scrubTo(m);
        const up = (): void => {
            target.removeEventListener('pointermove', move);
            target.removeEventListener('pointerup', up);
        };
        target.addEventListener('pointermove', move);
        target.addEventListener('pointerup', up);
    };
    rulerTrack.addEventListener('pointerdown', startScrub);
    for (const t of tracks.slice(1)) t.addEventListener('pointerdown', startScrub);
    return { el, sync: place };
}
