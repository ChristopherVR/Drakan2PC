// Dialogue panel: the conversation lines (dialog line classes, runtime class 0x100ba) and conversation handlers (0x100b4) of
// one level folder. A line has a speaker, one or more sounds and an animation; its subtitle is the text stored in the sound's
// SDB descriptor, so editing it edits that descriptor (one entry override of `<LEVEL>.SDB`); changing which sound a line plays
// patches the class record (`<LEVEL>.ODB`). Formats and the proven / inferred split: docs/reference/level-format.md.

import {
    HANDLER_PHASES,
    parseConversationHandler,
    refParts,
    splitSubtitlePages,
    type ConversationHandler,
    type DialogLine,
} from '@drakan/formats';
import type { ModMeta } from '@drakan/editor-core';
import type { Panel } from '../editor/ctx.ts';
import { fill, h } from '../editor/dom.ts';
import type { ContentSession } from './content.ts';
import './cinematics.css';

type Mode = 'lines' | 'handlers';

const soundStem = (s: ContentSession, db: number): string | null => (db === 0 ? s.stem : (s.story?.deps[db] ?? null));

/** `Name (id)` of a sound ref, or `db:id` when it is in another database. */
export function soundLabel(s: ContentSession, ref: number): string {
    const { id, db } = refParts(ref);
    if (id === 0) return 'none';
    if (db === 0) return s.story?.sounds.get(id)?.name ?? `sound ${id}`;
    return `${s.story?.deps[db] ?? `db ${db}`}:${id}`;
}

export function createDialoguePanel(session: ContentSession, meta: () => ModMeta | null): Panel {
    const state = { q: '', mode: 'lines' as Mode, line: 0, handler: 0, error: '' };
    const stemSel = h('select', { class: 'story-sel', 'aria-label': 'Level folder' });
    const search = h('input', {
        type: 'search',
        class: 'story-search',
        placeholder: 'Search speaker, sound or text',
        spellcheck: 'false',
        'aria-label': 'Search dialogue',
    });
    const modeSel = h(
        'select',
        { class: 'story-sel', 'aria-label': 'List' },
        h('option', { value: 'lines' }, 'Lines'),
        h('option', { value: 'handlers' }, 'Conversations'),
    );
    const info = h('div', { class: 'story-info' });
    const list = h('div', { class: 'story-list', role: 'listbox', 'aria-label': 'Dialogue' });
    const detail = h('div', { class: 'story-detail' });
    const exportBtn = h(
        'button',
        {
            class: 'btn',
            type: 'button',
            title: 'Download the edited subtitles, sounds and sequences as a mod pack (.zip)',
            onclick: () => void doExport(),
        },
        'Export pack',
    );

    async function doExport(): Promise<void> {
        const m = meta();
        if (!m) return session.host.status('Open a disc first: the pack needs its profile.', true);
        try {
            const ch = await session.exportZip(m);
            session.host.status(`Exported ${ch.length} story entr${ch.length === 1 ? 'y' : 'ies'} to ${m.id}.zip`);
        } catch (e) {
            session.host.status(e instanceof Error ? e.message : String(e), true);
        }
    }

    const loadBtn = h(
        'button',
        {
            class: 'btn',
            type: 'button',
            title: 'Read the dialogue of the open disc',
            onclick: () => void open(),
        },
        'Load',
    );
    const root = h(
        'div',
        { class: 'panel story dlg' },
        h('div', { class: 'story-bar' }, loadBtn, stemSel, modeSel, search, exportBtn, info),
        h('div', { class: 'story-body' }, list, detail),
    );

    stemSel.addEventListener('change', () => void session.use(stemSel.value));
    modeSel.addEventListener('change', () => {
        state.mode = modeSel.value as Mode;
        renderAll();
    });
    search.addEventListener('input', () => {
        state.q = search.value.trim().toLowerCase();
        renderList();
    });

    const lineText = (l: DialogLine): string => {
        const r = refParts(session.lineSound(l.classId, 0));
        return r.db === 0 && r.id ? session.text(r.id) : '';
    };

    function visibleLines(): DialogLine[] {
        const all = session.lines();
        if (!state.q) return all;
        return all.filter((l) => {
            const r = refParts(session.lineSound(l.classId, 0));
            const hay = `${l.name} ${soundLabel(session, session.lineSound(l.classId, 0))} ${r.db === 0 ? session.text(r.id) : ''}`;
            return hay.toLowerCase().includes(state.q);
        });
    }

    function renderList(): void {
        const story = session.story;
        if (!story) return fill(list, h('div', { class: 'story-empty' }, 'Open a disc to edit its dialogue.'));
        if (state.mode === 'lines') {
            const rows = visibleLines().map((l) => {
                const edited =
                    session.lineSound(l.classId, 0) !== (l.sounds[0] ?? 0) ||
                    l.sounds.some(
                        (r, i) => session.lineSound(l.classId, i) !== r || session.textEdited(refParts(r).id),
                    );
                return h(
                    'button',
                    {
                        class: `story-row${l.classId === state.line ? ' sel' : ''}`,
                        type: 'button',
                        role: 'option',
                        onclick: () => {
                            state.line = l.classId;
                            renderList();
                            renderDetail();
                        },
                    },
                    h('span', { class: 'story-id' }, String(l.classId)),
                    h('span', { class: 'story-title' }, l.speaker),
                    h('span', { class: 'story-text' }, lineText(l) || soundLabel(session, l.sounds[0] ?? 0)),
                    session.lineIsNew(l.classId)
                        ? h('span', { class: 'story-tag warn' }, 'new')
                        : edited
                          ? h('span', { class: 'story-tag warn' }, 'edited')
                          : null,
                );
            });
            fill(list, ...(rows.length ? rows : [h('div', { class: 'story-empty' }, 'No dialogue lines match.')]));
        } else {
            const hs = story.handlers.filter((x) => !state.q || x.name.toLowerCase().includes(state.q));
            fill(
                list,
                ...(hs.length
                    ? hs.map((x) =>
                          h(
                              'button',
                              {
                                  class: `story-row${x.classId === state.handler ? ' sel' : ''}`,
                                  type: 'button',
                                  role: 'option',
                                  onclick: () => {
                                      state.handler = x.classId;
                                      renderList();
                                      renderDetail();
                                  },
                              },
                              h('span', { class: 'story-id' }, String(x.classId)),
                              h('span', { class: 'story-title' }, x.name),
                              h(
                                  'span',
                                  { class: 'story-text' },
                                  `${HANDLER_PHASES.reduce((n, p) => n + session.phaseLines(x.classId, p.key).length, 0)} lines`,
                              ),
                              session.phaseEdited(x.classId) ? h('span', { class: 'story-tag warn' }, 'edited') : null,
                          ),
                      )
                    : [h('div', { class: 'story-empty' }, 'No conversation handler classes in this level folder.')]),
            );
        }
    }

    function play(ref: number): HTMLElement {
        const { id, db } = refParts(ref);
        const stem = soundStem(session, db);
        const ok = !!session.host.play && !!stem && id !== 0;
        return h(
            'button',
            {
                class: 'btn',
                type: 'button',
                disabled: !ok,
                title: ok ? 'Play the recording' : 'No recording to play',
                onclick: () => void session.host.play?.(stem!, id),
            },
            'Play',
        );
    }

    function lineDetail(l: DialogLine): HTMLElement[] {
        const story = session.story!;
        const out: HTMLElement[] = [
            h('div', { class: 'story-title' }, l.name),
            h(
                'div',
                { class: 'dim' },
                `Class ${l.classId} - plays ${l.sounds.length} sound${l.sounds.length === 1 ? '' : 's'}`,
            ),
        ];
        l.sounds.forEach((_, i) => {
            const ref = session.lineSound(l.classId, i);
            const { id, db } = refParts(ref);
            const snd = db === 0 ? story.sounds.get(id) : undefined;
            const idIn = h('input', {
                type: 'number',
                class: 'dlg-num',
                value: id,
                min: 0,
                max: 65535,
                'aria-label': `Sound id ${i + 1}`,
                ...(session.lineIsNew(l.classId) && i > 0 ? { disabled: true } : {}),
                onchange: (e: Event) =>
                    session.setLineSound(
                        l.classId,
                        i,
                        ((db << 16) | Number((e.target as HTMLInputElement).value)) >>> 0,
                    ),
            });
            out.push(
                h(
                    'div',
                    { class: 'dlg-sound' },
                    h('span', { class: 'dim' }, `Sound ${i + 1}`),
                    idIn,
                    h('span', { class: 'story-ref' }, soundLabel(session, ref)),
                    snd ? h('span', { class: 'dim' }, `${snd.seconds.toFixed(1)} s`) : null,
                    play(ref),
                ),
            );
            if (snd) {
                const ta = h('textarea', {
                    class: 'story-ta',
                    rows: 3,
                    spellcheck: 'true',
                    'aria-label': `Subtitle of ${snd.name}`,
                    oninput: (e: Event) => {
                        session.setText(id, (e.target as HTMLTextAreaElement).value);
                        pages.textContent = pageSummary(id);
                    },
                });
                ta.value = session.text(id);
                const pages = h('div', { class: 'dim dlg-pages' }, pageSummary(id));
                out.push(ta, pages);
            } else if (id !== 0 && db === 0) {
                out.push(h('div', { class: 'story-warn' }, `Sound ${id} is not in ${story.files.sdb}.`));
            }
        });
        out.push(newLineForm(l));
        out.push(
            h(
                'div',
                { class: 'dim' },
                `Animation ${l.animations.join(', ') || 'none'} - sound mode ${l.playMode} - camera position ${l.camera.position}, transition ${l.camera.transition}`,
            ),
        );
        const users = story.handlers.filter((x) =>
            x.phases.some((p) => p.lines.some((r) => (r & 0xffff) === l.classId)),
        );
        const placed = (session.host.placed?.() ?? []).filter((o) =>
            [...o.fields.values()].some((v) => Array.isArray(v) && v.some((r) => (r & 0xffff) === l.classId)),
        );
        out.push(
            h(
                'div',
                { class: 'dim' },
                users.length || placed.length
                    ? `Used by ${[...users.map((x) => `class ${x.name}`), ...placed.map((o) => `object ${o.id} ${o.name}`)].join(', ')}`
                    : 'Not referenced by a handler class or a placed handler of the open level.',
            ),
        );
        return out;
    }

    /** "New line from this one": copies the class (animation, camera, play mode) under a new name with another sound. */
    function newLineForm(l: DialogLine): HTMLElement {
        const name = h('input', {
            type: 'text',
            class: 'story-search',
            value: `${l.speaker} - NEW${String((session.story?.lines.length ?? 0) + 1).padStart(3, '0')}`,
            'aria-label': 'Name of the new line',
            title: 'Class name, Speaker - SOUNDNAME',
        });
        const snd = h('input', {
            type: 'number',
            class: 'dlg-num',
            min: 1,
            max: 65535,
            value: refParts(session.lineSound(l.classId, 0)).id,
            'aria-label': 'Sound id of the new line',
            title: 'An existing sound of this level',
        });
        const add = h(
            'button',
            {
                class: 'btn',
                type: 'button',
                title: 'Add a new line that copies this one',
                onclick: () => {
                    try {
                        const id = session.addLine(l.classId, name.value, Number(snd.value) >>> 0);
                        state.line = id;
                        renderAll();
                        session.host.status(`New dialog line class ${id}. Add it to a conversation phase to use it.`);
                    } catch (e) {
                        session.host.status(e instanceof Error ? e.message : String(e), true);
                    }
                },
            },
            'New line from this one',
        );
        const rm = session.lineIsNew(l.classId)
            ? h(
                  'button',
                  {
                      class: 'btn',
                      type: 'button',
                      onclick: () => {
                          session.removeNewLine(l.classId);
                          state.line = 0;
                          renderAll();
                      },
                  },
                  'Remove this new line',
              )
            : null;
        return h('div', { class: 'dlg-new' }, name, snd, add, rm);
    }

    /** One editable phase: lines in play order with move / remove, and an add row. */
    function phaseEditor(x: ConversationHandler, i: number): HTMLElement[] {
        const story = session.story!;
        const key = HANDLER_PHASES[i]!.key;
        const refs = session.phaseLines(x.classId, key);
        const set = (next: number[]): void => session.setPhaseLines(x.classId, key, next);
        const p = x.phases[i]!;
        const out: HTMLElement[] = [
            h(
                'div',
                { class: 'story-group' },
                `${HANDLER_PHASES[i]!.label}${p.message ? ` - sends message ${p.message}` : ''}`,
                session.phaseEdited(x.classId, key) ? h('span', { class: 'story-tag warn' }, 'edited') : null,
            ),
        ];
        const all = session.lines();
        refs.forEach((ref, k) => {
            const cid = ref & 0xffff;
            const l = all.find((y) => y.classId === cid);
            const btn = (label: string, title: string, fn: () => void, disabled = false): HTMLElement =>
                h('button', { class: 'btn', type: 'button', title, disabled, 'aria-label': title, onclick: fn }, label);
            out.push(
                h(
                    'div',
                    { class: 'dlg-phase-row' },
                    h(
                        'button',
                        {
                            class: 'story-row',
                            type: 'button',
                            onclick: () => {
                                state.mode = 'lines';
                                modeSel.value = 'lines';
                                state.line = cid;
                                renderAll();
                            },
                        },
                        h('span', { class: 'story-id' }, String(cid)),
                        h('span', { class: 'story-title' }, l?.speaker ?? '?'),
                        h(
                            'span',
                            { class: 'story-text' },
                            l ? lineText(l) || soundLabel(session, l.sounds[0] ?? 0) : 'missing class',
                        ),
                        session.lineIsNew(cid) ? h('span', { class: 'story-tag warn' }, 'new') : null,
                    ),
                    btn(
                        'Up',
                        'Move up',
                        () => {
                            const n = [...refs];
                            [n[k - 1], n[k]] = [n[k]!, n[k - 1]!];
                            set(n);
                        },
                        k === 0,
                    ),
                    btn(
                        'Down',
                        'Move down',
                        () => {
                            const n = [...refs];
                            [n[k + 1], n[k]] = [n[k]!, n[k + 1]!];
                            set(n);
                        },
                        k === refs.length - 1,
                    ),
                    btn('Remove', 'Remove from this phase', () => set(refs.filter((_, j) => j !== k))),
                ),
            );
        });
        if (!refs.length) out.push(h('div', { class: 'dim' }, 'No lines in this phase.'));
        const pick = h(
            'select',
            { class: 'story-sel', 'aria-label': `Line to add to ${HANDLER_PHASES[i]!.label}` },
            ...all.map((y) => h('option', { value: String(y.classId) }, `${y.classId} ${y.name}`.slice(0, 80))),
        );
        out.push(
            h(
                'div',
                { class: 'dlg-new' },
                pick,
                h(
                    'button',
                    {
                        class: 'btn',
                        type: 'button',
                        disabled: !all.length,
                        onclick: () => {
                            const id = Number(pick.value);
                            if (id) set([...refs, id]);
                        },
                    },
                    'Add line',
                ),
            ),
        );
        void story;
        return out;
    }

    function pageSummary(id: number): string {
        const pg = splitSubtitlePages(session.text(id));
        if (pg.length < 2) return '';
        return `${pg.length} pages: ${pg.map((p, i) => (i < pg.length - 1 ? `${p.delay}s` : 'end')).join(' / ')}  (a backslash and seconds, \\.9, starts the next page)`;
    }

    function handlerDetail(x: ConversationHandler): HTMLElement[] {
        const story = session.story!;
        const out: HTMLElement[] = [
            h('div', { class: 'story-title' }, x.name),
            h(
                'div',
                { class: 'dim' },
                `Class ${x.classId} - starts ${x.initiallyActive ? 'active' : 'inactive'} - talks within ${x.distance.toFixed(1)} - ${x.hasQuest ? 'has a quest' : 'no quest'}${x.goldOnComplete ? ` - ${x.goldOnComplete} gold on completion` : ''}`,
            ),
        ];
        for (let i = 0; i < HANDLER_PHASES.length; i++) out.push(...phaseEditor(x, i));
        if (session.phaseEdited(x.classId))
            out.push(
                h(
                    'button',
                    { class: 'btn', type: 'button', onclick: () => session.revertPhases(x.classId) },
                    'Revert phase edits',
                ),
            );
        out.push(h('div', { class: 'dim' }, 'The quest state decides which phase plays.'));
        const placed = (session.host.placed?.() ?? []).filter((o) => o.fields.has('Beginning Dialog Exchange'));
        if (placed.length) {
            out.push(h('div', { class: 'story-group' }, 'Placed in the open level'));
            for (const o of placed) {
                const ph = parseConversationHandler(story.classes.get(x.classId) ?? ({} as never), o.fields);
                out.push(
                    h(
                        'div',
                        { class: 'dim' },
                        `Object ${o.id} ${o.name}: ${ph ? ph.phases.map((p) => p.lines.length).join(' / ') : '?'} lines per phase`,
                    ),
                );
            }
        }
        return out;
    }

    function renderDetail(): void {
        const story = session.story;
        if (!story) return fill(detail);
        if (state.mode === 'lines') {
            const all = session.lines();
            const l = all.find((x) => x.classId === state.line) ?? all[0];
            if (!l) return fill(detail, h('div', { class: 'story-empty' }, 'This level folder has no dialogue lines.'));
            state.line = l.classId;
            return fill(detail, ...lineDetail(l));
        }
        const x = story.handlers.find((y) => y.classId === state.handler) ?? story.handlers[0];
        if (!x) return fill(detail, h('div', { class: 'story-empty' }, 'No conversation handlers here.'));
        state.handler = x.classId;
        fill(detail, ...handlerDetail(x));
    }

    function renderAll(): void {
        const s = session.story;
        if (stemSel.options.length !== session.stems.length) {
            fill(stemSel, ...session.stems.map((t) => h('option', { value: t }, t)));
        }
        if (session.stem) stemSel.value = session.stem;
        info.textContent = s
            ? `${session.lines().length} lines, ${s.handlers.length} handler classes, ${session.count()} edit${session.count() === 1 ? '' : 's'}`
            : '';
        renderList();
        renderDetail();
    }

    session.subscribe(() => {
        // typing in the subtitle box must not rebuild it
        if (detail.contains(document.activeElement) && document.activeElement instanceof HTMLTextAreaElement) {
            info.textContent = `${session.count()} edit${session.count() === 1 ? '' : 's'}`;
            renderList();
            return;
        }
        renderAll();
    });

    const open = (): Promise<void> =>
        session.ensure().then(renderAll, (e) => {
            session.host.status(e instanceof Error ? e.message : String(e), true);
        });
    renderAll();
    return { el: root, onLoad: renderAll };
}
