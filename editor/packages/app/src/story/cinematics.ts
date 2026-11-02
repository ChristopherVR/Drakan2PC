// Cinematics panel: the cut scene sequences (.SSD 0x311) of one level folder. A sequence is a list of actors (the camera, Rynn,
// NPCs, props), each with timed actions: transform keyframes (the camera path), animations, sounds (speech with its
// subtitle), messages and a few unnamed types. The panel shows the timeline, previews the camera path and the subtitle
// timing (with the real recordings when the host can play them) and edits times, sound / message / animation references and
// positions. Format and the proven / inferred split: docs/reference/level-format.md "Cut scenes, conversations and sequences".

import {
    actionField,
    actorPath,
    cameraPath,
    describeMessage,
    insertAction,
    isCameraActor,
    newActionBytes,
    parseActions,
    refParts,
    removeAction,
    samplePath,
    sequenceDuration,
    setActionField,
    setActionTime,
    splitSubtitlePages,
    type SsdActor,
    type SsdAction,
    type SsdSequence,
} from '@drakan/formats';
import { quatYawPitch, yawPitchQuat, type ModMeta } from '@drakan/editor-core';
import type { Panel } from '../editor/ctx.ts';
import { fill, h, icon } from '../editor/dom.ts';
import type { ContentSession } from './content.ts';
import { createTimeline } from './timeline.ts';
import './cinematics.css';

const KIND_COLOR: Record<string, string> = {
    transform: '#5c9fff',
    animation: '#4cc38a',
    sound: '#e8b04a',
    message: '#f06a62',
    camera: '#b48cff',
    marker: '#6f6f7a',
    hold: '#6f6f7a',
};
const kindColor = (k: string): string => KIND_COLOR[k] ?? '#a3a3ad';

const num = (v: number, d = 2): string => (Number.isInteger(v) ? String(v) : v.toFixed(d));

export interface CinematicsPanel extends Panel {
    /** Show one sequence's timeline. */
    focus(id: number): void;
}

/**
 * `onNewCutscene` adds the "New cut scene" button: the host makes one at the camera target (player, trigger box and a
 * starter camera move), selects it in the level and focuses it here.
 */
export function createCinematicsPanel(
    session: ContentSession,
    meta: () => ModMeta | null,
    onNewCutscene?: () => void,
): CinematicsPanel {
    const state = { q: '', sel: 0, t: 0, playing: false, raf: 0, t0: 0, cue: 0 };
    const stemSel = h('select', { class: 'story-sel', 'aria-label': 'Level folder' });
    const search = h('input', {
        type: 'search',
        class: 'story-search',
        placeholder: 'Search sequences and actors',
        spellcheck: 'false',
        'aria-label': 'Search sequences',
    });
    const info = h('div', { class: 'story-info' });
    const newName = h('input', {
        type: 'text',
        class: 'story-search',
        placeholder: 'New sequence name',
        maxlength: 80,
        'aria-label': 'New sequence name',
    });
    const newFolder = h('select', { class: 'story-sel', 'aria-label': 'Folder of the new sequence' });
    const newBar = h(
        'div',
        { class: 'cin-add' },
        newName,
        newFolder,
        h(
            'button',
            {
                class: 'btn',
                type: 'button',
                title: 'Add an empty sequence, then add actors and actions',
                onclick: () => {
                    try {
                        const id = session.addSequence(newName.value, Number(newFolder.value));
                        newName.value = '';
                        state.sel = id;
                        state.t = 0;
                        renderAll();
                        session.host.status(
                            `New sequence ${id}. Reference it from a cut scene player's Sequence List.`,
                        );
                    } catch (e) {
                        session.host.status(e instanceof Error ? e.message : String(e), true);
                    }
                },
            },
            'New sequence',
        ),
    );
    const list = h('div', { class: 'story-list', role: 'listbox', 'aria-label': 'Sequences' });
    const detail = h('div', { class: 'story-detail' });

    const loadBtn = h(
        'button',
        { class: 'btn', type: 'button', title: 'Read the sequences of the open disc', onclick: () => void open() },
        'Load',
    );
    const exportBtn = h(
        'button',
        {
            class: 'btn',
            type: 'button',
            title: 'Download the edited sequences, subtitles and dialogue as a mod pack (.zip)',
            onclick: async () => {
                const m = meta();
                if (!m) return session.host.status('Open a disc first: the pack needs its profile.', true);
                try {
                    const ch = await session.exportZip(m);
                    session.host.status(
                        `Exported ${ch.length} story entr${ch.length === 1 ? 'y' : 'ies'} to ${m.id}.zip`,
                    );
                } catch (e) {
                    session.host.status(e instanceof Error ? e.message : String(e), true);
                }
            },
        },
        'Export pack',
    );
    const root = h(
        'div',
        { class: 'panel story cin' },
        h(
            'div',
            { class: 'story-bar' },
            onNewCutscene
                ? h(
                      'button',
                      {
                          class: 'btn primary',
                          type: 'button',
                          title: 'Place a new cut scene where the camera looks: a player, a trigger box and a starter camera move',
                          onclick: () => onNewCutscene(),
                      },
                      icon('plus', 13),
                      'New cut scene',
                  )
                : null,
            loadBtn,
            stemSel,
            search,
            exportBtn,
            info,
        ),
        newBar,
        h('div', { class: 'story-body' }, list, detail),
    );
    stemSel.addEventListener('change', () => {
        stop();
        void session.use(stemSel.value);
    });
    search.addEventListener('input', () => {
        state.q = search.value.trim().toLowerCase();
        renderList();
    });

    const soundName = (ref: number): { name: string; text: string; stem: string | null; id: number } => {
        const { id, db } = refParts(ref);
        const story = session.story!;
        if (db === 0) {
            const s = story.sounds.get(id);
            return { name: s?.name ?? `sound ${id}`, text: s ? session.text(id) : '', stem: story.stem, id };
        }
        const stem = story.deps[db] ?? null;
        return { name: `${stem ?? `db ${db}`}:${id}`, text: '', stem, id };
    };

    // ---- list ---------------------------------------------------------------------------------------------------------

    function renderList(): void {
        const story = session.story;
        if (!story) return fill(list, h('div', { class: 'story-empty' }, 'Open a disc to edit its cut scenes.'));
        const rows: HTMLElement[] = [];
        const folders = new Map(story.folders.map((f) => [f.id, f.name]));
        let lastFolder = -1;
        const seqs = [...session.sequences()].sort(
            (a, b) => a.folder - b.folder || (a.seq?.name ?? '').localeCompare(b.seq?.name ?? ''),
        );
        for (const r of seqs) {
            const seq = session.sequence(r.id);
            const name = seq?.name ?? `Sequence ${r.id}`;
            const hay = `${name} ${seq?.actors.map((a) => a.name).join(' ') ?? ''}`.toLowerCase();
            if (state.q && !hay.includes(state.q)) continue;
            if (r.folder !== lastFolder) {
                lastFolder = r.folder;
                rows.push(h('div', { class: 'story-group' }, folders.get(r.folder) ?? `Folder ${r.folder}`));
            }
            rows.push(
                h(
                    'button',
                    {
                        class: `story-row${r.id === state.sel ? ' sel' : ''}`,
                        type: 'button',
                        role: 'option',
                        onclick: () => {
                            stop();
                            state.sel = r.id;
                            state.t = 0;
                            renderList();
                            renderDetail();
                        },
                    },
                    h('span', { class: 'story-id' }, String(r.id)),
                    h('span', { class: 'story-title' }, name),
                    h(
                        'span',
                        { class: 'story-text' },
                        seq ? `${seq.actors.length} actors, ${sequenceDuration(seq).toFixed(1)} s` : 'not decoded',
                    ),
                    session.sequenceIsNew(r.id)
                        ? h('span', { class: 'story-tag warn' }, 'new')
                        : session.sequenceEdited(r.id)
                          ? h('span', { class: 'story-tag warn' }, 'edited')
                          : null,
                ),
            );
        }
        fill(list, ...(rows.length ? rows : [h('div', { class: 'story-empty' }, 'No sequences match.')]));
    }

    // ---- drawing --------------------------------------------------------------------------------------------------------

    function drawTimeline(cv: HTMLCanvasElement, seq: SsdSequence, dur: number): void {
        const g = cv.getContext('2d');
        if (!g) return;
        const w = cv.width;
        const rowH = 14;
        g.clearRect(0, 0, w, cv.height);
        g.font = '10px sans-serif';
        const x = (t: number): number => 70 + (t / Math.max(dur, 0.001)) * (w - 78);
        seq.actors.forEach((a, i) => {
            const y = 6 + i * rowH;
            g.fillStyle = '#a3a3ad';
            g.fillText(a.name.slice(0, 11), 2, y + 9);
            g.fillStyle = 'rgba(255,255,255,0.05)';
            g.fillRect(70, y, w - 78, rowH - 3);
            for (const k of parseActions(a) ?? []) {
                if (k.kind === 'hold' || k.kind === 'marker') continue;
                g.fillStyle = kindColor(k.kind);
                g.fillRect(x(k.time) - 1, y, k.kind === 'transform' ? 2 : 3, rowH - 3);
            }
        });
        g.fillStyle = '#fff';
        g.fillRect(x(state.t) - 1, 0, 2, cv.height);
    }

    function drawPath(cv: HTMLCanvasElement, seq: SsdSequence): void {
        const g = cv.getContext('2d');
        if (!g) return;
        g.clearRect(0, 0, cv.width, cv.height);
        const { keys } = cameraPath(seq);
        const others = seq.actors.filter((a) => !isCameraActor(a)).map((a) => ({ a, k: actorPath(a) }));
        const pts = [...keys, ...others.flatMap((o) => o.k)];
        if (!pts.length) {
            g.fillStyle = '#6f6f7a';
            g.fillText('No transform keyframes', 8, 16);
            return;
        }
        let x0 = Infinity,
            x1 = -Infinity,
            z0 = Infinity,
            z1 = -Infinity;
        for (const p of pts) {
            x0 = Math.min(x0, p.pos[0]);
            x1 = Math.max(x1, p.pos[0]);
            z0 = Math.min(z0, p.pos[2]);
            z1 = Math.max(z1, p.pos[2]);
        }
        const span = Math.max(x1 - x0, z1 - z0, 1);
        const sx = (v: number): number => 10 + ((v - x0) / span) * (cv.width - 20);
        const sz = (v: number): number => 10 + ((v - z0) / span) * (cv.height - 20);
        const line = (ks: typeof keys, color: string): void => {
            g.strokeStyle = color;
            g.fillStyle = color;
            g.beginPath();
            ks.forEach((p, i) => (i ? g.lineTo(sx(p.pos[0]), sz(p.pos[2])) : g.moveTo(sx(p.pos[0]), sz(p.pos[2]))));
            g.stroke();
            for (const p of ks) g.fillRect(sx(p.pos[0]) - 1.5, sz(p.pos[2]) - 1.5, 3, 3);
        };
        for (const o of others) line(o.k, 'rgba(76,195,138,0.55)');
        line(keys, '#5c9fff');
        const cur = samplePath(keys, state.t);
        if (cur) {
            g.fillStyle = '#fff';
            g.beginPath();
            g.arc(sx(cur.pos[0]), sz(cur.pos[2]), 4, 0, Math.PI * 2);
            g.fill();
        }
    }

    // ---- playback ---------------------------------------------------------------------------------------------------

    let subtitleEl: HTMLElement | null = null;
    let redraw: (() => void) | null = null;
    function stop(): void {
        state.playing = false;
        if (state.raf) cancelAnimationFrame(state.raf);
        state.raf = 0;
        session.host.stop?.();
    }
    function subtitleAt(seq: SsdSequence, t: number): string {
        let best = '';
        for (const a of seq.actors)
            for (const k of parseActions(a) ?? []) {
                if (k.kind !== 'sound' || k.time > t) continue;
                const s = soundName(actionField(k, 'sound'));
                if (!s.text) continue;
                const pages = splitSubtitlePages(s.text);
                let at = k.time;
                let shown = pages[0]!.text;
                for (let i = 1; i < pages.length; i++) {
                    at += pages[i - 1]!.delay;
                    if (t >= at) shown = pages[i]!.text;
                }
                const sec = session.story!.sounds.get(s.id)?.seconds ?? 4;
                if (t <= k.time + Math.max(sec, 1.5)) best = `${a.name}: ${shown}`;
            }
        return best;
    }
    function play(seq: SsdSequence, dur: number): void {
        stop();
        state.playing = true;
        state.t0 = performance.now() - state.t * 1000;
        const cues = seq.actors
            .flatMap((a) =>
                (parseActions(a) ?? [])
                    .filter((k) => k.kind === 'sound')
                    .map((k) => ({ t: k.time, ref: actionField(k, 'sound') })),
            )
            .sort((a, b) => a.t - b.t);
        let next = cues.findIndex((c) => c.t >= state.t);
        if (next < 0) next = cues.length;
        const tick = (): void => {
            if (!state.playing) return;
            state.t = (performance.now() - state.t0) / 1000;
            while (next < cues.length && cues[next]!.t <= state.t) {
                const s = soundName(cues[next]!.ref);
                if (s.stem && s.id) void session.host.play?.(s.stem, s.id);
                next++;
            }
            if (state.t >= dur + 1) {
                state.t = dur;
                state.playing = false;
            }
            redraw?.();
            if (state.playing) state.raf = requestAnimationFrame(tick);
        };
        state.raf = requestAnimationFrame(tick);
    }

    // ---- detail ---------------------------------------------------------------------------------------------------------

    const numInput = (
        value: number,
        onChange: (v: number) => void,
        label: string,
        step = 'any',
        w = '',
    ): HTMLInputElement =>
        h('input', {
            type: 'number',
            class: `dlg-num ${w}`,
            value: Number.isFinite(value) ? Number(value.toPrecision(7)) : 0,
            step,
            'aria-label': label,
            onchange: (e: Event) => {
                const v = Number((e.target as HTMLInputElement).value);
                if (Number.isFinite(v)) onChange(v);
            },
        });

    function actionRow(seq: SsdSequence, ai: number, a: SsdActor, k: SsdAction, ki: number): HTMLElement {
        const edit = (fn: (actor: SsdActor, act: SsdAction) => Uint8Array): void =>
            session.editSequence(seq.id, (s) => {
                const actor = s.actors[ai]!;
                const act = parseActions(actor)![ki]!;
                actor.actions = fn(actor, act);
            });
        const fld = (name: string, label: string, step = 'any', cls = ''): HTMLElement =>
            numInput(
                actionField(k, name),
                (v) => edit((actor, act) => setActionField(actor, act, name, v)),
                label,
                step,
                cls,
            );
        const cells: (HTMLElement | string)[] = [];
        if (k.kind === 'sound') {
            const ref = actionField(k, 'sound');
            const s = soundName(ref);
            const { db } = refParts(ref);
            cells.push(
                numInput(
                    ref & 0xffff,
                    (v) => edit((actor, act) => setActionField(actor, act, 'sound', ((db << 16) | v) >>> 0)),
                    'Sound id',
                    '1',
                ),
                h('span', { class: 'story-ref' }, s.name),
                h('span', { class: 'dim' }, s.text ? `"${s.text.slice(0, 60)}${s.text.length > 60 ? '...' : ''}"` : ''),
                h(
                    'button',
                    {
                        class: 'btn',
                        type: 'button',
                        disabled: !s.stem || !session.host.play,
                        onclick: () => void session.host.play?.(s.stem!, s.id),
                    },
                    'Play',
                ),
            );
        } else if (k.kind === 'message') {
            cells.push(
                fld('message', 'Message number', '1'),
                h('span', { class: 'story-ref' }, describeMessage(actionField(k, 'message'))),
                'param',
                fld('param', 'Message parameter', '1'),
            );
        } else if (k.kind === 'animation') {
            cells.push(
                'anim',
                fld('animation', 'Animation id', '1'),
                'speed',
                fld('speed', 'Speed'),
                'mode',
                fld('mode', 'Mode', '1'),
            );
        } else if (k.kind === 'transform') {
            // Orientation as yaw / pitch (yaw 0 looks along -Z, 90 along +X, pitch > 0 down; the key quaternion is a row-vector
            // rotation looking along its -Z, proven in game): editing either one rewrites the four quaternion fields, roll dropped.
            const look = quatYawPitch([
                actionField(k, 'qx'),
                actionField(k, 'qy'),
                actionField(k, 'qz'),
                actionField(k, 'qw'),
            ]);
            const aim = (yaw: number, pitch: number): void =>
                edit((actor, act) => {
                    const [qx, qy, qz, qw] = yawPitchQuat(yaw, pitch);
                    let bytes = actor.actions;
                    for (const [f, v] of Object.entries({ qx, qy, qz, qw }))
                        bytes = setActionField({ ...actor, actions: bytes }, act, f, v);
                    return bytes;
                });
            cells.push(
                'x',
                fld('x', 'X'),
                'y',
                fld('y', 'Y'),
                'z',
                fld('z', 'Z'),
                'yaw',
                numInput(
                    Math.round(look.yaw * 100) / 100,
                    (v) => aim(v, look.pitch),
                    'Yaw (degrees, 0 looks along -Z, 90 along +X)',
                    '1',
                ),
                'pitch',
                numInput(
                    Math.round(look.pitch * 100) / 100,
                    (v) => aim(look.yaw, v),
                    'Pitch (degrees, positive looks down)',
                    '1',
                ),
            );
        } else {
            cells.push(h('span', { class: 'dim' }, k.fields.map((f) => `${f.name} ${num(f.value)}`).join('  ')));
        }
        return h(
            'div',
            { class: 'cin-act' },
            h(
                'span',
                { class: 'cin-kind', style: `border-color:${kindColor(k.kind)}` },
                k.kind === 'hold' ? 'hold' : k.kind,
            ),
            numInput(
                k.time,
                (v) => edit((actor, act) => setActionTime(actor, act, Math.max(0, v))),
                'Time (seconds)',
                '0.01',
                'cin-time',
            ),
            ...cells,
            h(
                'button',
                {
                    class: 'btn',
                    type: 'button',
                    title: 'Delete this action',
                    'aria-label': 'Delete action',
                    onclick: () =>
                        session.editSequence(seq.id, (s) => {
                            const actor = s.actors[ai]!;
                            Object.assign(actor, removeAction(actor, ki));
                        }),
                },
                'x',
            ),
        );
    }

    function renderDetail(): void {
        const story = session.story;
        stop();
        redraw = null;
        subtitleEl = null;
        if (!story) return fill(detail);
        const all = session.sequences();
        const rec = all.find((s) => s.id === state.sel) ?? all[0];
        if (!rec) return fill(detail, h('div', { class: 'story-empty' }, 'This level folder has no sequences.'));
        state.sel = rec.id;
        const seq = session.sequence(rec.id);
        if (!seq) {
            return fill(
                detail,
                h(
                    'div',
                    { class: 'story-warn' },
                    `Sequence ${rec.id} does not follow the known layout, so it is kept as it is (${rec.raw.length} bytes).`,
                ),
            );
        }
        const dur = Math.max(sequenceDuration(seq), 0.5);
        const timeline = createTimeline({
            seq,
            dur,
            getT: () => state.t,
            scrub: (t) => {
                stop();
                state.t = Math.min(dur, Math.max(0, t));
                redraw?.();
            },
            moveKey: (ai, ki, t) =>
                session.editSequence(seq.id, (s2) => {
                    const actor = s2.actors[ai]!;
                    const act = parseActions(actor)![ki]!;
                    actor.actions = setActionTime(actor, act, Math.max(0, t));
                }),
        });
        const pathCv = h('canvas', { class: 'cin-path', width: 220, height: 220 });
        const scrub = h('input', {
            type: 'range',
            min: 0,
            max: dur,
            step: 0.01,
            value: state.t,
            class: 'cin-scrub',
            'aria-label': 'Playhead',
        });
        const clock = h('span', { class: 'dim cin-clock' });
        subtitleEl = h('div', { class: 'cin-sub' });
        redraw = (): void => {
            timeline.sync();
            drawPath(pathCv, seq);
            scrub.value = String(state.t);
            clock.textContent = `${state.t.toFixed(2)} / ${dur.toFixed(2)} s`;
            subtitleEl!.textContent = subtitleAt(seq, state.t) || ' ';
            const cam = cameraPath(seq).keys;
            session.host.showPath?.(cam.length ? cam.map((p) => p.pos) : null);
        };
        scrub.addEventListener('input', () => {
            stop();
            state.t = Number(scrub.value);
            redraw?.();
        });
        const edited = session.sequenceEdited(rec.id);
        const actorName = h('input', {
            type: 'text',
            class: 'story-search',
            placeholder: 'Actor name (Camera, Rynn, ...)',
            'aria-label': 'New actor name',
        });
        const actorRef = h('input', {
            type: 'number',
            class: 'dlg-num',
            min: 0,
            value: 0,
            'aria-label': 'Actor object id',
            title: 'Object id of the prop or character',
        });
        const addActorRow = h(
            'div',
            { class: 'cin-add' },
            actorName,
            actorRef,
            h(
                'button',
                {
                    class: 'btn',
                    type: 'button',
                    onclick: () => session.addSequenceActor(seq.id, actorName.value, Number(actorRef.value) || 0),
                },
                'Add actor',
            ),
            session.sequenceIsNew(rec.id)
                ? h(
                      'button',
                      {
                          class: 'btn',
                          type: 'button',
                          onclick: () => {
                              session.revertSequence(rec.id);
                              state.sel = 0;
                              renderAll();
                          },
                      },
                      'Remove new sequence',
                  )
                : null,
        );
        const nameIn = h('input', {
            type: 'text',
            class: 'story-search',
            value: seq.name,
            maxlength: 90,
            'aria-label': 'Sequence name',
            onchange: (e: Event) =>
                session.editSequence(seq.id, (s) => (s.name = (e.target as HTMLInputElement).value.trim() || s.name)),
        });
        const actors = seq.actors.map((a, ai) => {
            const acts = parseActions(a) ?? [];
            const add = (label: string, type: number, vals: Record<string, number>): HTMLElement =>
                h(
                    'button',
                    {
                        class: 'btn',
                        type: 'button',
                        onclick: () =>
                            session.editSequence(seq.id, (s) => {
                                const actor = s.actors[ai]!;
                                Object.assign(actor, insertAction(actor, newActionBytes(type, state.t, vals)));
                            }),
                    },
                    label,
                );
            return h(
                'details',
                { class: 'cin-actor', open: ai < 3 },
                h(
                    'summary',
                    {},
                    h('span', { class: 'story-title' }, a.name),
                    h('span', { class: 'dim' }, `slot ${a.slot}, ref ${a.ref}, ${acts.length} actions`),
                ),
                h(
                    'div',
                    { class: 'cin-add' },
                    add('+ Sound', 3, { params: 0x382710 }),
                    add('+ Message', 7, { message: 4097 }),
                    add('+ Animation', 1, {}),
                ),
                ...acts.filter((k) => k.kind !== 'marker').map((k) => actionRow(seq, ai, a, k, acts.indexOf(k))),
            );
        });
        fill(
            detail,
            h(
                'div',
                { class: 'cin-head' },
                nameIn,
                h('span', { class: 'dim' }, `${seq.actors.length} actors`),
                edited
                    ? h(
                          'button',
                          { class: 'btn', type: 'button', onclick: () => session.revertSequence(seq.id) },
                          'Revert',
                      )
                    : null,
            ),
            h(
                'div',
                { class: 'cin-tools' },
                h(
                    'button',
                    { class: 'btn', type: 'button', onclick: () => (state.playing ? stop() : play(seq, dur)) },
                    'Play / stop',
                ),
                scrub,
                clock,
                session.host.framePath
                    ? h(
                          'button',
                          {
                              class: 'btn',
                              type: 'button',
                              title: 'Point the level view at this sequence camera path (pink line, green start, red end)',
                              onclick: () => session.host.framePath?.(),
                          },
                          'Show camera path in 3D',
                      )
                    : null,
            ),
            addActorRow,
            subtitleEl,
            h('div', { class: 'cin-viz' }, timeline.el, pathCv),
            h(
                'div',
                { class: 'dim' },
                'Blue keyframes, green animations, amber sounds, red messages. Camera path seen from above.',
            ),
            ...actors,
        );
        redraw();
    }

    function renderAll(): void {
        if (stemSel.options.length !== session.stems.length)
            fill(stemSel, ...session.stems.map((t) => h('option', { value: t }, t)));
        if (session.stem) stemSel.value = session.stem;
        {
            const keep = newFolder.value;
            fill(newFolder, ...(session.story?.folders ?? []).map((f) => h('option', { value: String(f.id) }, f.name)));
            if (keep && [...newFolder.options].some((o) => o.value === keep)) newFolder.value = keep;
            else newFolder.selectedIndex = 0;
        }
        info.textContent = session.story
            ? `${session.sequences().length} sequences, ${session.count()} edit${session.count() === 1 ? '' : 's'}`
            : '';
        renderList();
        renderDetail();
    }
    session.subscribe(() => {
        const keepT = state.t;
        renderAll();
        state.t = keepT;
        redraw?.();
    });
    const open = (): Promise<void> =>
        session.ensure().then(renderAll, (e) => session.host.status(e instanceof Error ? e.message : String(e), true));
    renderAll();
    return {
        el: root,
        onLoad: renderAll,
        focus(id) {
            stop();
            state.sel = id;
            state.t = 0;
            renderAll();
            list.querySelector('.story-row.sel')?.scrollIntoView({ block: 'nearest' });
        },
    };
}
