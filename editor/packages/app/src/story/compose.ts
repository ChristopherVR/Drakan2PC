// Authoring panels for NEW story content (the other Story pages edit what the disc already has):
//   New dialogue   a line (speaker, sound, subtitle) added to a conversation phase
//   New cut scene  camera points and ordered steps as a new sequence, optionally with a trigger volume and player
//   Quest marker   a quest (journal text, map dot) placed in the level, started / completed by volumes or a quest giver
// Entry layout and what is proven: docs/reference/level-format.md sections 13 and 14, editor-guide.md "Create story content".

import {
    ApplyTemplateCommand,
    MAX_LINE_NAME,
    TEMPLATES,
    checkCutscene,
    cutsceneBytes,
    dialogLineName,
    isError,
    planDialogueLine,
    setObjectField,
    lookAtAngles,
    wireQuestGiver,
    type CameraPoint,
    type CutsceneStep,
    type ParamValues,
} from '@drakan/editor-core';
import { HANDLER_PHASES, MESSAGE_NAMES, refParts, type HandlerPhaseKey } from '@drakan/formats';
import type { Ctx, Panel } from '../editor/ctx.ts';
import { fill, h } from '../editor/dom.ts';
import { selectField } from '../editor/dropdown.ts';
import { objectLabel } from '../editor/linking.ts';
import type { ContentSession } from './content.ts';
import { placedStoryObjects } from './placed.ts';
import type { StorySession } from './session.ts';
import './compose.css';

type Opt = string | [string, string];

const err = (e: unknown): string => (e instanceof Error ? e.message : String(e));

function field(label: string, control: HTMLElement, hint?: string): HTMLElement {
    return h('label', { class: 'cmp-field', title: hint ?? '' }, h('span', { class: 'cmp-label' }, label), control);
}

function num(value: number, label: string, step = 1, min?: number): HTMLInputElement {
    return h('input', {
        type: 'number',
        class: 'cmp-num',
        value,
        step,
        ...(min !== undefined ? { min } : {}),
        'aria-label': label,
    }) as HTMLInputElement;
}

const stemField = (s: ContentSession, onPick: () => void): HTMLSelectElement => {
    const sel = selectField(s.stems.length ? s.stems : [''], s.stem ?? '', (v) => void s.use(v).then(onPick), {
        'aria-label': 'Level folder',
    });
    return sel;
};

/** Selected object's position, else the viewport centre ground point (where a template places its setup). */
function placeAt(ctx: Ctx): [number, number, number] {
    const v = ctx.view();
    const sel = [...v.selection];
    const p = (sel.length ? ctx.doc().get(sel[0]!)?.transform.pos : null) ?? v.groundAtCentre() ?? v.cameraGamePos();
    return [p[0], p[1], p[2]];
}

// ---- new dialogue -------------------------------------------------------------------------------------------------

export function createNewDialoguePanel(session: ContentSession, ctx?: () => Ctx): Panel {
    const st = { target: 'class', handler: 0, phase: 'beginning' as HandlerPhaseKey, sound: 0, q: '', pos: -1 };
    const bar = h('div', { class: 'story-bar' });
    const form = h('div', { class: 'story-detail cmp-form' });
    const side = h('div', { class: 'story-list cmp-side' });
    const root = h('div', { class: 'panel story cmp' }, bar, h('div', { class: 'story-body' }, side, form));
    const speaker = h('input', { type: 'text', class: 'story-search', placeholder: 'Farmer', 'aria-label': 'Speaker' });
    const text = h('textarea', {
        class: 'story-ta',
        rows: 4,
        spellcheck: 'true',
        'aria-label': 'Subtitle text',
    }) as HTMLTextAreaElement;
    const search = h('input', {
        type: 'search',
        class: 'story-search',
        placeholder: 'Search sounds by name or subtitle',
        'aria-label': 'Search sounds',
    }) as HTMLInputElement;
    const soundList = h('div', { class: 'cmp-sounds', role: 'listbox', 'aria-label': 'Sounds' });
    const preview = h('div', { class: 'cmp-preview' });
    const status = h('div', { class: 'story-info' });

    const stor = (): NonNullable<ContentSession['story']> | null => session.story;
    const phaseField = (): string => HANDLER_PHASES.find((p) => p.key === st.phase)!.field;
    /** Handlers placed in the open level (their phase arrays override the class's). */
    const placedHandlers = (): ReturnType<NonNullable<ContentSession['host']['placed']>> =>
        (session.host.placed?.() ?? []).filter((o) => o.fields.has('Beginning Dialog Exchange'));
    const placedTarget = (): number => (st.target.startsWith('obj:') ? Number(st.target.slice(4)) : 0);
    /** Phase list of the chosen target: the placed object's own array, else the conversation class's. */
    function curLines(): number[] {
        const o = placedHandlers().find((x) => x.id === placedTarget());
        const v = o?.fields.get(phaseField());
        return o ? (Array.isArray(v) ? [...v] : []) : session.phaseLines(st.handler, st.phase);
    }
    const handlers = (): NonNullable<ContentSession['story']>['handlers'] => stor()?.handlers ?? [];

    function pickSound(id: number): void {
        st.sound = id;
        const s = stor()?.sounds.get(id);
        text.value = s ? session.text(id) : '';
        renderSounds();
        renderPreview();
    }

    function renderSounds(): void {
        const story = stor();
        if (!story) return fill(soundList, h('div', { class: 'story-empty' }, 'Open a disc first.'));
        const q = st.q;
        const rows: HTMLElement[] = [];
        let total = 0;
        for (const s of story.sounds.values()) {
            const hay = `${s.name} ${session.text(s.id)}`.toLowerCase();
            if (q && !hay.includes(q)) continue;
            total++;
            if (rows.length >= 120) continue;
            rows.push(
                h(
                    'div',
                    {
                        class: `story-row${s.id === st.sound ? ' sel' : ''}`,
                        role: 'option',
                        'aria-selected': s.id === st.sound,
                    },
                    h(
                        'button',
                        { class: 'cmp-pick', type: 'button', onclick: () => pickSound(s.id) },
                        h('span', { class: 'story-id' }, String(s.id)),
                        h('span', { class: 'story-title cmp-name' }, s.name),
                        h('span', { class: 'story-text' }, session.text(s.id) || '(no subtitle)'),
                        h('span', { class: 'story-ref' }, s.seconds ? `${s.seconds.toFixed(1)} s` : ''),
                    ),
                    session.host.play
                        ? h(
                              'button',
                              {
                                  class: 'btn sm',
                                  type: 'button',
                                  'aria-label': `Play ${s.name}`,
                                  onclick: () => void session.host.play!(story.stem, s.id),
                              },
                              'Play',
                          )
                        : null,
                ),
            );
        }
        fill(
            soundList,
            ...rows,
            total > rows.length
                ? h('div', { class: 'story-ref' }, `${total - rows.length} more: narrow the search.`)
                : null,
            !total ? h('div', { class: 'story-empty' }, 'No sound matches.') : null,
        );
    }

    function draft(): Parameters<ContentSession['composeLine']>[0] {
        const story = stor();
        const snd = story?.sounds.get(st.sound);
        return {
            speaker: (speaker as HTMLInputElement).value,
            text: text.value,
            soundRef: st.sound,
            soundName: snd?.name ?? '',
            handlerId: st.handler,
            phase: st.phase,
            ...(st.pos >= 0 ? { position: st.pos } : {}),
        };
    }

    function renderPreview(): void {
        const story = stor();
        if (!story) return fill(preview);
        const d = draft();
        const lines: HTMLElement[] = [];
        let name = '';
        try {
            name = dialogLineName(d.speaker, d.soundName);
        } catch (e) {
            lines.push(h('div', { class: 'story-warn' }, err(e)));
        }
        if (name) lines.push(h('div', {}, 'New class: ', h('code', {}, name), ` (${name.length}/${MAX_LINE_NAME})`));
        const cur = curLines();
        const label = HANDLER_PHASES.find((p) => p.key === st.phase)?.label ?? st.phase;
        const at = st.pos < 0 ? cur.length : Math.min(st.pos, cur.length);
        lines.push(
            h('div', { class: 'cmp-lead' }, `${label} phase after the insert`),
            h(
                'ol',
                { class: 'cmp-seq' },
                ...[
                    ...cur.slice(0, at).map((r) => ({ r, nw: false })),
                    { r: -1, nw: true },
                    ...cur.slice(at).map((r) => ({ r, nw: false })),
                ].map((x) => {
                    const ln = session.lines().find((l) => l.classId === (x.r & 0xffff));
                    const li = h(
                        'li',
                        { class: x.nw ? 'new' : '' },
                        x.nw ? `${name || 'new line'} (new)` : (ln?.name ?? `class ${x.r & 0xffff}`),
                    );
                    return li;
                }),
            ),
        );
        if (st.sound && refParts(st.sound).db === 0 && text.value.trim() !== session.text(st.sound))
            lines.push(
                h(
                    'div',
                    { class: 'story-ref' },
                    'The subtitle is stored on the sound, so every line that plays this sound shows the new text.',
                ),
            );
        fill(preview, ...lines);
    }

    /** Put the new line class into the phase array of a placed handler object (one undo step naming the object). */
    function addToPlaced(objId: number, classId: number): void {
        const c = ctx?.();
        const env = c?.schemas();
        const o = c?.doc().get(objId);
        const f = env && o ? env.schemaFor(o.resource)?.fields.find((x) => x.name === phaseField()) : undefined;
        if (!c || !o || !f) throw new Error('The placed handler is gone or has no such phase.');
        const next = planDialogueLine({ ...draft(), soundRef: draft().soundRef }, curLines(), classId).phaseLines;
        const name = session.lines().find((l) => l.classId === classId)?.name ?? `class ${classId}`;
        c.edit(`Add dialogue line "${name}" to ${objectLabel(o)}`, [objId], (x) => setObjectField(x, f, next));
    }

    function renderForm(): void {
        const story = stor();
        if (!story) {
            fill(form, h('div', { class: 'story-empty' }, 'Open a disc to write new dialogue.'));
            fill(side);
            return;
        }
        if (!handlers().some((x) => x.classId === st.handler)) st.handler = handlers()[0]?.classId ?? 0;
        const handlerSel = selectField(
            handlers().map((x): Opt => [String(x.classId), `${x.name} (${x.classId})`]),
            String(st.handler),
            (v) => {
                st.handler = Number(v);
                st.pos = -1;
                renderForm();
            },
            { 'aria-label': 'Conversation' },
        );
        const targets = placedHandlers();
        if (st.target !== 'class' && !targets.some((x) => `obj:${x.id}` === st.target)) st.target = 'class';
        const targetSel = selectField(
            [
                ['class', 'All conversations of this class'],
                ...targets.map((o): Opt => [`obj:${o.id}`, `Placed ${o.name} #${o.id} in the open level`]),
            ],
            st.target,
            (v) => {
                st.target = v;
                st.pos = -1;
                renderForm();
            },
            { 'aria-label': 'Add to', 'data-search': 'on' },
        );
        const phaseSel = selectField(
            HANDLER_PHASES.map((p): Opt => [p.key, p.label]),
            st.phase,
            (v) => {
                st.phase = v as HandlerPhaseKey;
                st.pos = -1;
                renderForm();
            },
            { 'aria-label': 'Phase' },
        );
        const cur = curLines();
        const posSel = selectField(
            [
                ['-1', 'At the end'],
                ...cur.map((r, i): Opt => [
                    String(i),
                    `Before ${session.lines().find((l) => l.classId === (r & 0xffff))?.name ?? `class ${r & 0xffff}`}`,
                ]),
            ],
            String(Math.min(st.pos, cur.length - 1)),
            (v) => {
                st.pos = Number(v);
                renderPreview();
            },
            { 'aria-label': 'Position' },
        );
        const add = h(
            'button',
            {
                class: 'btn primary',
                type: 'button',
                onclick: () => {
                    try {
                        const obj = placedTarget();
                        const id = session.composeLine(draft(), { classOnly: obj > 0 });
                        if (obj > 0) addToPlaced(obj, id);
                        status.textContent = `Added line class ${id}.`;
                        session.host.status(`Added dialogue line (class ${id}) to the ${st.phase} phase.`);
                        st.pos = -1;
                        renderForm();
                    } catch (e) {
                        status.textContent = err(e);
                        session.host.status(err(e), true);
                    }
                },
            },
            'Add line to conversation',
        );
        speaker.oninput = renderPreview;
        text.oninput = renderPreview;
        fill(
            form,
            h('div', { class: 'story-title' }, 'New dialogue line'),
            field(
                'Add to',
                targetSel,
                'A placed handler keeps its own line lists, so a line added to the class does not reach it',
            ),
            placedTarget()
                ? null
                : field('Conversation', handlerSel, 'The handler class whose phase list gets the line'),
            h('div', { class: 'cmp-row' }, field('Phase', phaseSel), field('Position', posSel)),
            field(
                'Speaker',
                speaker,
                'Written into the class name; which character speaks is decided by the conversation',
            ),
            field('Subtitle', text, 'Shown while the sound plays'),
            preview,
            h('div', { class: 'btnrow' }, add, status),
        );
        const added = session.lines().filter((l) => session.lineIsNew(l.classId));
        fill(
            side,
            h('div', { class: 'cmp-lead' }, 'Sound'),
            search,
            soundList,
            added.length ? h('div', { class: 'cmp-lead' }, 'Lines added this session') : null,
            ...added.map((l) =>
                h(
                    'div',
                    { class: 'story-row' },
                    h('span', { class: 'story-id' }, String(l.classId)),
                    h('span', { class: 'story-text' }, l.name),
                    h(
                        'button',
                        {
                            class: 'btn sm',
                            type: 'button',
                            'aria-label': `Remove ${l.name}`,
                            onclick: () => session.removeNewLine(l.classId),
                        },
                        'Remove',
                    ),
                ),
            ),
        );
        renderSounds();
        renderPreview();
    }

    search.addEventListener('input', () => {
        st.q = search.value.trim().toLowerCase();
        renderSounds();
    });

    function renderBar(): void {
        const stems = stemField(session, () => renderAll());
        fill(bar, stems);
    }
    function renderAll(): void {
        renderBar();
        renderForm();
    }
    session.subscribe(() => {
        if (root.isConnected) renderAll();
    });
    renderAll();
    return {
        el: root,
        onLoad: () => void session.ensure().then(renderAll, (e) => session.host.status(err(e), true)),
    };
}

// ---- new cut scene ------------------------------------------------------------------------------------------------

type StepKind = CutsceneStep['kind'];

export function createNewCutscenePanel(session: ContentSession, ctx?: () => Ctx): Panel {
    const st = {
        name: 'My cut scene',
        folder: 0,
        cameraRef: 0,
        cameraStem: null as string | null,
        duration: 0,
        camera: [] as CameraPoint[],
        steps: [] as CutsceneStep[],
        place: true,
        who: '2',
    };
    const bar = h('div', { class: 'story-bar' });
    const form = h('div', { class: 'story-detail cmp-form' });
    const side = h('div', { class: 'story-list cmp-side' });
    const root = h('div', { class: 'panel story cmp' }, bar, h('div', { class: 'story-body' }, side, form));
    const status = h('div', { class: 'story-info' });

    const story = (): NonNullable<ContentSession['story']> | null => session.story;
    const spec = (): Parameters<ContentSession['addCutscene']>[0] => ({
        folder: st.folder,
        name: st.name,
        cameraRef: st.cameraRef,
        camera: st.camera,
        steps: st.steps,
        ...(st.duration > 0 ? { duration: st.duration } : {}),
    });

    const lastTime = (): number => Math.max(0, ...st.camera.map((c) => c.time), ...st.steps.map((s) => s.time));

    function addPoint(pos: [number, number, number], aim?: [number, number, number]): void {
        const prev = st.camera.at(-1);
        const a = aim ? lookAtAngles(pos, aim) : { yaw: prev?.yaw ?? 0, pitch: prev?.pitch ?? 0 };
        st.camera.push({
            time: st.camera.length ? Math.round((lastTime() + 2) * 10) / 10 : 0,
            pos,
            yaw: a.yaw,
            pitch: a.pitch,
        });
        renderForm();
    }

    function pointRow(p: CameraPoint, i: number): HTMLElement {
        const bind = (get: () => number, set: (v: number) => void, label: string, step: number): HTMLInputElement => {
            const el = num(Math.round(get() * 100) / 100, label, step);
            el.addEventListener('change', () => {
                set(Number(el.value));
                renderPreview();
            });
            return el;
        };
        return h(
            'div',
            { class: 'cmp-grid cmp-point' },
            h('span', { class: 'story-id' }, `#${i + 1}`),
            bind(
                () => p.time,
                (v) => (p.time = v),
                `Point ${i + 1} time (s)`,
                0.5,
            ),
            bind(
                () => p.pos[0],
                (v) => (p.pos[0] = v),
                `Point ${i + 1} X`,
                100,
            ),
            bind(
                () => p.pos[1],
                (v) => (p.pos[1] = v),
                `Point ${i + 1} Y`,
                100,
            ),
            bind(
                () => p.pos[2],
                (v) => (p.pos[2] = v),
                `Point ${i + 1} Z`,
                100,
            ),
            bind(
                () => p.yaw,
                (v) => (p.yaw = v),
                `Point ${i + 1} yaw (deg)`,
                5,
            ),
            bind(
                () => p.pitch,
                (v) => (p.pitch = v),
                `Point ${i + 1} pitch (deg)`,
                5,
            ),
            h(
                'button',
                {
                    class: 'btn sm',
                    type: 'button',
                    'aria-label': `Remove camera point ${i + 1}`,
                    onclick: () => {
                        st.camera.splice(i, 1);
                        renderForm();
                    },
                },
                'Remove',
            ),
        );
    }

    function stepRow(s: CutsceneStep, i: number): HTMLElement {
        const kind = selectField(
            [
                ['sound', 'Sound'],
                ['animation', 'Animation'],
                ['message', 'Message'],
            ],
            s.kind,
            (v) => {
                const t = s.time;
                st.steps[i] =
                    v === 'sound'
                        ? { kind: 'sound', time: t, soundRef: 0 }
                        : v === 'animation'
                          ? { kind: 'animation', time: t, actorName: '', actorRef: 0, animation: 0 }
                          : { kind: 'message', time: t, actorName: '', actorRef: 0, message: 0x1001, param: 0 };
                renderForm();
            },
            { 'aria-label': `Step ${i + 1} kind` },
        );
        const time = num(s.time, `Step ${i + 1} time (s)`, 0.5, 0);
        time.addEventListener('change', () => {
            s.time = Number(time.value);
            renderPreview();
        });
        const details: HTMLElement[] = [];
        if (s.kind === 'sound') {
            const sounds = [...(story()?.sounds.values() ?? [])];
            const sel = selectField(
                [
                    ['0', 'Pick a sound'],
                    ...sounds.map((x): Opt => [String(x.id), `${x.name}  ${session.text(x.id).slice(0, 40)}`]),
                ],
                String(s.soundRef & 0xffff),
                (v) => {
                    s.soundRef = Number(v);
                    renderPreview();
                },
                { 'aria-label': `Step ${i + 1} sound`, 'data-search': 'on' },
            );
            details.push(sel);
        } else {
            const obj = num(s.actorRef, `Step ${i + 1} object id`, 1, 0);
            obj.title = 'Level object the step acts on (an NPC or prop id)';
            obj.addEventListener('change', () => {
                s.actorRef = Number(obj.value);
                const o = ctx?.().doc().get(s.actorRef);
                if (o) s.actorName = o.class;
                renderPreview();
            });
            details.push(obj);
            if (s.kind === 'animation') {
                const an = num(s.animation, `Step ${i + 1} animation id`, 1, 0);
                an.title = 'Animation id (ADB)';
                an.addEventListener('change', () => {
                    (s as Extract<CutsceneStep, { kind: 'animation' }>).animation = Number(an.value);
                    renderPreview();
                });
                details.push(an);
            } else {
                const m = s as Extract<CutsceneStep, { kind: 'message' }>;
                details.push(
                    selectField(
                        MESSAGE_NAMES.slice(0, 9).map((n, k): Opt => [String(0x1000 + k), n]),
                        String(m.message),
                        (v) => {
                            m.message = Number(v);
                            renderPreview();
                        },
                        { 'aria-label': `Step ${i + 1} message` },
                    ),
                );
            }
        }
        return h(
            'div',
            { class: 'cmp-grid cmp-step' },
            h('span', { class: 'story-id' }, `#${i + 1}`),
            kind,
            time,
            h('div', { class: 'cmp-detail' }, ...details),
            h(
                'button',
                {
                    class: 'btn sm',
                    type: 'button',
                    'aria-label': `Remove step ${i + 1}`,
                    onclick: () => {
                        st.steps.splice(i, 1);
                        renderForm();
                    },
                },
                'Remove',
            ),
        );
    }

    const preview = h('div', { class: 'cmp-preview' });
    function renderPreview(): void {
        const issues = checkCutscene({ ...spec(), id: 1 });
        const lines: HTMLElement[] = issues.map((x) =>
            h('div', { class: x.level === 'error' ? 'story-warn' : 'story-ref' }, x.message),
        );
        if (!issues.some((x) => x.level === 'error')) {
            try {
                const bytes = cutsceneBytes({ ...spec(), id: 1 });
                const actors = new Set(
                    st.steps.filter((x) => x.kind !== 'sound').map((x) => (x as { actorRef: number }).actorRef),
                ).size;
                lines.push(
                    h(
                        'div',
                        {},
                        `${st.camera.length} camera point${st.camera.length === 1 ? '' : 's'}, ${st.steps.length} step${st.steps.length === 1 ? '' : 's'}, ${1 + actors} actors, ${Math.max(0, lastTime()).toFixed(1)} s, entry of ${bytes.length} bytes`,
                    ),
                );
            } catch (e) {
                lines.push(h('div', { class: 'story-warn' }, err(e)));
            }
        }
        fill(preview, ...lines);
    }

    function renderForm(): void {
        const sty = story();
        if (!sty) {
            fill(form, h('div', { class: 'story-empty' }, 'Open a disc to build a cut scene.'));
            fill(side);
            return;
        }
        if (st.cameraStem !== session.stem) {
            st.cameraStem = session.stem;
            st.cameraRef = session.cameraRef();
        }
        const folders = sty.folders.length ? sty.folders : [{ id: 0, parent: 0, name: '(root)' }];
        if (!folders.some((f) => f.id === st.folder)) st.folder = folders[0]!.id;
        const name = h('input', {
            type: 'text',
            class: 'story-search',
            value: st.name,
            'aria-label': 'Cut scene name',
        }) as HTMLInputElement;
        name.addEventListener('input', () => {
            st.name = name.value;
            renderPreview();
        });
        const folderSel = selectField(
            folders.map((f): Opt => [String(f.id), f.name || `folder ${f.id}`]),
            String(st.folder),
            (v) => (st.folder = Number(v)),
            { 'aria-label': 'Folder' },
        );
        const cam = num(st.cameraRef, 'Camera object id', 1, 0);
        cam.title = 'Level object id of the Camera used by this level’s other cut scenes';
        cam.addEventListener('change', () => {
            st.cameraRef = Number(cam.value);
            renderPreview();
        });
        const dur = num(st.duration, 'Minimum duration (s)', 1, 0);
        dur.addEventListener('change', () => {
            st.duration = Number(dur.value);
            renderPreview();
        });
        const c = ctx?.();
        const sel = c ? [...c.view().selection] : [];
        const fromCamera = h(
            'button',
            {
                class: 'btn',
                type: 'button',
                disabled: !c,
                title: 'Add a point at the viewport camera, looking at the selected object when one is selected',
                onclick: () => {
                    const cc = ctx!();
                    const o = [...cc.view().selection].map((id) => cc.doc().get(id)).find(Boolean);
                    addPoint(cc.view().cameraGamePos(), o?.transform.pos);
                },
            },
            'Add point at viewport camera',
        );
        const fromSel = h(
            'button',
            {
                class: 'btn',
                type: 'button',
                disabled: !c || !sel.length,
                title: 'Add a point at the selected object',
                onclick: () => {
                    const cc = ctx!();
                    const o = cc.doc().get([...cc.view().selection][0] ?? -1);
                    if (o) addPoint([...o.transform.pos] as [number, number, number]);
                },
            },
            'Add point at selection',
        );
        const addStep = h(
            'button',
            {
                class: 'btn',
                type: 'button',
                onclick: () => {
                    st.steps.push({ kind: 'sound', time: Math.round(lastTime() * 10) / 10, soundRef: 0 });
                    renderForm();
                },
            },
            'Add step',
        );
        const place = h('input', {
            type: 'checkbox',
            checked: st.place,
            'aria-label': 'Place a trigger volume',
        }) as HTMLInputElement;
        place.checked = st.place;
        place.addEventListener('change', () => (st.place = place.checked));
        const create = h(
            'button',
            {
                class: 'btn primary',
                type: 'button',
                onclick: () => {
                    try {
                        const id = session.addCutscene(spec());
                        let note = `Cut scene ${id} "${st.name.trim()}" added.`;
                        if (st.place && c) note += ' ' + placeTrigger(c, id, st.name.trim());
                        status.textContent = note;
                        session.host.status(note);
                        st.camera = [];
                        st.steps = [];
                        renderForm();
                    } catch (e) {
                        status.textContent = err(e);
                        session.host.status(err(e), true);
                    }
                },
            },
            'Create cut scene',
        );
        fill(
            form,
            h('div', { class: 'story-title' }, 'New cut scene'),
            h('div', { class: 'cmp-row' }, field('Name', name), field('Folder', folderSel)),
            h('div', { class: 'cmp-row' }, field('Camera object id', cam), field('Minimum duration (s)', dur)),
            h('div', { class: 'cmp-lead' }, 'Camera points'),
            h(
                'div',
                { class: 'cmp-grid cmp-head' },
                ...['', 'Time', 'X', 'Y', 'Z', 'Yaw', 'Pitch', ''].map((t) => h('span', {}, t)),
            ),
            ...st.camera.map(pointRow),
            h('div', { class: 'btnrow' }, fromCamera, fromSel),
            h('div', { class: 'cmp-lead' }, 'Steps in order'),
            ...st.steps.map(stepRow),
            h('div', { class: 'btnrow' }, addStep),
            preview,
            h(
                'label',
                { class: 'story-chk' },
                place,
                'Also place a trigger volume and cut scene player in the level (at the selection or viewport centre)',
            ),
            h('div', { class: 'btnrow' }, create, status),
        );
        fill(
            side,
            h('div', { class: 'cmp-lead' }, 'Cut scenes added this session'),
            ...session
                .sequences()
                .filter((x) => session.sequenceIsNew(x.id))
                .map((x) =>
                    h(
                        'div',
                        { class: 'story-row' },
                        h('span', { class: 'story-id' }, String(x.id)),
                        h('span', { class: 'story-text' }, x.seq?.name ?? ''),
                        h(
                            'button',
                            {
                                class: 'btn sm',
                                type: 'button',
                                'aria-label': `Remove ${x.seq?.name ?? x.id}`,
                                onclick: () => session.revertSequence(x.id),
                            },
                            'Remove',
                        ),
                    ),
                ),
            h('div', { class: 'cmp-note' }, 'Places a trigger and a cut scene player, already linked.'),
        );
        renderPreview();
    }

    function placeTrigger(c: Ctx, sequence: number, name: string): string {
        const t = TEMPLATES.find((x) => x.id === 'cutscene-trigger')!;
        const env = c.schemas();
        if (!env || !c.doc().size) return 'No level is open, so nothing was placed.';
        const v: ParamValues = { sequence, w: 3, h: 2, d: 1, once: true, who: st.who };
        const r = t.build(
            { doc: c.doc(), env, catalog: c.catalog(), at: placeAt(c), selection: [...c.view().selection] },
            v,
        );
        if (isError(r)) return r.error;
        const cmd = new ApplyTemplateCommand(r, `Place trigger for cut scene "${name}"`);
        c.view().run(cmd);
        c.view().select(cmd.ids);
        return r.notes.join('. ') + '.';
    }

    function renderBar(): void {
        fill(
            bar,
            stemField(session, () => renderAll()),
        );
    }
    function renderAll(): void {
        renderBar();
        renderForm();
    }
    session.subscribe(() => {
        if (root.isConnected) renderForm();
    });
    renderAll();
    return {
        el: root,
        onSelection: () => root.isConnected && renderForm(),
        onLoad: () => void session.ensure().then(renderAll, (e) => session.host.status(err(e), true)),
    };
}

// ---- quest marker -------------------------------------------------------------------------------------------------

export function createQuestPlacePanel(story: StorySession, ctx?: () => Ctx): Panel {
    const st = {
        quest: 0,
        start: true,
        complete: true,
        giver: 0,
        w: 3,
        h: 2,
        d: 3,
        title: '',
        text: '',
        mapX: 0,
        mapY: 0,
    };
    const bar = h('div', { class: 'story-bar' });
    const form = h('div', { class: 'story-detail cmp-form' });
    const side = h('div', { class: 'story-list cmp-side' });
    const root = h('div', { class: 'panel story cmp' }, bar, h('div', { class: 'story-body' }, side, form));
    const status = h('div', { class: 'story-info' });

    const rows = (): ReturnType<StorySession['questRows']> => story.questRows();

    function journalOf(classId: number): string {
        const r = rows().find((x) => x.classId === classId);
        const id = r?.draft.journalId ?? 0;
        return id ? story.text('SYSTEM.XDB', id, 0).split('\n')[0]!.slice(0, 80) : '';
    }

    function create(): void {
        try {
            if (!story.data) throw new Error('Open a disc first.');
            const base = st.quest || story.data.quests[0]!.cls.classId;
            const title = st.title.trim();
            if (!title) throw new Error('Write the journal text of the quest first.');
            const id = story.addQuest(base, title);
            const row = rows().find((x) => x.classId === id)!;
            story.setText('SYSTEM.XDB', row.draft.journalId, 0, st.text.trim() || title);
            story.setDraft(id, { kind: st.mapX || st.mapY ? 'both' : 'journal', mapX: st.mapX, mapY: st.mapY });
            st.quest = id;
            st.title = '';
            st.text = '';
            status.textContent = `Quest class ${id} created.`;
            renderForm();
        } catch (e) {
            status.textContent = err(e);
            story.host.status(err(e), true);
        }
    }

    function questLabel(classId: number): string {
        const r = rows().find((x) => x.classId === classId);
        return r ? `${journalOf(classId) || r.name}` : `class ${classId}`;
    }

    function placeQuest(c: Ctx): void {
        const t = TEMPLATES.find((x) => x.id === 'quest-object')!;
        const env = c.schemas();
        if (!env || !c.doc().size) return void c.toast('Open a level from the disc first.', 'warn');
        if (!st.quest) return void c.toast('Pick or create a quest first.', 'warn');
        const v: ParamValues = {
            classId: st.quest,
            start: st.start,
            complete: st.complete,
            w: st.w,
            h: st.h,
            d: st.d,
            who: '2',
        };
        const r = t.build(
            { doc: c.doc(), env, catalog: c.catalog(), at: placeAt(c), selection: [...c.view().selection] },
            v,
        );
        if (isError(r)) return void c.toast(r.error, 'error');
        const label = questLabel(st.quest);
        const cmd = new ApplyTemplateCommand(r, `Place quest "${label}"`);
        c.view().run(cmd);
        c.view().select(cmd.ids);
        const questObj = r.objects[0]!;
        let note = r.notes.join('. ') + '.';
        if (st.giver) {
            const giver = c.doc().get(st.giver);
            if (giver) {
                const missing: string[] = [];
                c.edit(`Wire ${objectLabel(giver)} to quest "${label}"`, [giver.id], (o) => {
                    missing.push(...wireQuestGiver(o, env, questObj.id));
                });
                note += ` ${objectLabel(giver)} sends Quest Initiate / Complete to the quest object.`;
                if (missing.length) note += ` Its class has no field ${missing.join(', ')}.`;
            }
        }
        for (const w of r.warnings) c.toast(w, 'warn');
        c.toast(note, 'ok');
        status.textContent = note;
    }

    function renderForm(): void {
        if (!story.data) {
            fill(form, h('div', { class: 'story-empty' }, 'Open a disc to place quests.'));
            fill(side);
            return;
        }
        const qrows = rows();
        if (!st.quest) st.quest = qrows.find((r) => r.isNew)?.classId ?? 0;
        const title = h('input', {
            type: 'text',
            class: 'story-search',
            value: st.title,
            placeholder: 'Short title of the objective',
            'aria-label': 'Quest title',
        }) as HTMLInputElement;
        title.addEventListener('input', () => (st.title = title.value));
        const text = h('textarea', { class: 'story-ta', rows: 3, 'aria-label': 'Journal text' }) as HTMLTextAreaElement;
        text.value = st.text;
        text.addEventListener('input', () => (st.text = text.value));
        const mx = num(st.mapX, 'Map X', 5, 0);
        const my = num(st.mapY, 'Map Y', 5, 0);
        mx.addEventListener('change', () => (st.mapX = Number(mx.value)));
        my.addEventListener('change', () => (st.mapY = Number(my.value)));
        const c = ctx?.();
        const handlers = c
            ? placedStoryObjects(c.doc(), c.schemas()).filter((p) => p.fields.has('Beginning Dialog Exchange'))
            : [];
        const questSel = selectField(
            [
                ['0', 'Pick a quest'],
                ...qrows.map((r): Opt => [
                    String(r.classId),
                    `${r.isNew ? 'New: ' : ''}${journalOf(r.classId) || r.name} (${r.classId})`,
                ]),
            ],
            String(st.quest),
            (v) => {
                st.quest = Number(v);
            },
            { 'aria-label': 'Quest', 'data-search': 'on' },
        );
        const giverSel = selectField(
            [['0', 'None (volumes only)'], ...handlers.map((p): Opt => [String(p.id), `${p.name} #${p.id}`])],
            String(st.giver),
            (v) => (st.giver = Number(v)),
            { 'aria-label': 'Quest giver' },
        );
        const chk = (label: string, key: 'start' | 'complete'): HTMLElement => {
            const i = h('input', { type: 'checkbox', 'aria-label': label }) as HTMLInputElement;
            i.checked = st[key];
            i.addEventListener('change', () => (st[key] = i.checked));
            return h('label', { class: 'story-chk' }, i, label);
        };
        const dims = (key: 'w' | 'h' | 'd', label: string): HTMLElement => {
            const i = num(st[key], label, 0.25, 0.25);
            i.addEventListener('change', () => (st[key] = Number(i.value)));
            return field(label, i);
        };
        fill(
            form,
            h('div', { class: 'story-title' }, 'Quest marker'),
            h('div', { class: 'cmp-lead' }, '1. Quest'),
            field('Quest', questSel, 'An existing quest, or one you create below'),
            h(
                'div',
                { class: 'cmp-sub' },
                field('New quest title', title),
                field('Journal text', text),
                h(
                    'div',
                    { class: 'cmp-row' },
                    field('Map X (0 to 260, 0 = no dot)', mx),
                    field('Map Y (0 to 190)', my),
                ),
                h(
                    'div',
                    { class: 'btnrow' },
                    h('button', { class: 'btn', type: 'button', onclick: create }, 'Create quest'),
                ),
            ),
            h('div', { class: 'cmp-lead' }, '2. Place it in the level'),
            h(
                'div',
                { class: 'cmp-row' },
                chk('Volume starts the quest (On)', 'start'),
                chk('Second volume completes it', 'complete'),
            ),
            h('div', { class: 'cmp-row' }, dims('w', 'Volume width (cells)'), dims('h', 'Height'), dims('d', 'Depth')),
            field(
                'Quest giver',
                giverSel,
                'A conversation handler placed in this level: it sends Quest Initiate after its Beginning dialog and Quest Complete after its Complete dialog',
            ),
            h(
                'div',
                { class: 'btnrow' },
                h(
                    'button',
                    { class: 'btn primary', type: 'button', disabled: !c, onclick: () => c && placeQuest(ctx!()) },
                    'Place quest marker',
                ),
                status,
            ),
            h(
                'div',
                { class: 'cmp-note' },
                'The marker goes at the selected object, else the viewport centre. Level objects use the same wiring the game does: a volume sends message 1 (On) to start the quest and 18 (Quest Complete) to finish it.',
            ),
        );
        fill(
            side,
            h('div', { class: 'cmp-lead' }, 'Quests added this session'),
            ...qrows
                .filter((r) => r.isNew)
                .map((r) =>
                    h(
                        'div',
                        { class: `story-row${r.classId === st.quest ? ' sel' : ''}` },
                        h(
                            'button',
                            {
                                class: 'cmp-pick',
                                type: 'button',
                                onclick: () => {
                                    st.quest = r.classId;
                                    renderForm();
                                },
                            },
                            h('span', { class: 'story-id' }, String(r.classId)),
                            h('span', { class: 'story-text' }, journalOf(r.classId) || r.name),
                        ),
                        h(
                            'button',
                            {
                                class: 'btn sm',
                                type: 'button',
                                'aria-label': `Remove quest ${r.classId}`,
                                onclick: () => story.removeNewQuest(r.classId),
                            },
                            'Remove',
                        ),
                    ),
                ),
        );
    }
    story.subscribe(() => {
        if (root.isConnected) renderForm();
    });
    renderForm();
    return {
        el: root,
        onSelection: () => root.isConnected && renderForm(),
        onLoad: () => void story.ensure().then(renderForm, (e) => story.host.status(err(e), true)),
    };
}
