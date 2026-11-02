// Conversation editor: one conversation at a time as a tree. Each stage of the talk (first meeting, quest still open, quest
// finished ...) is a branch of line cards: who speaks, what the subtitle says (edit it right there), which recording plays
// (with a play button), and move / remove. Lines are added to a stage from the level's lines or made new in one step.
// It reads and writes through the shared ContentSession, so it exports with the same story pack as the other editors.

import { HANDLER_PHASES, refParts, type HandlerPhaseKey } from '@drakan/formats';
import type { Panel } from '../editor/ctx.ts';
import { fill, h, icon } from '../editor/dom.ts';
import type { ContentSession } from './content.ts';
import { soundLabel } from './dialogue.ts';
import './conversation.css';

/** Stage names in the words of the game: when in the quest each branch plays. */
export const STAGE_TITLES: Record<HandlerPhaseKey, { title: string; hint: string }> = {
    beginning: { title: 'First meeting', hint: 'Plays when the player talks to them for the first time.' },
    incomplete: { title: 'Quest still open', hint: 'Plays when the player comes back before finishing the quest.' },
    complete: { title: 'Quest finished', hint: 'Plays when the player comes back with the quest done.' },
    completeNoBegin: {
        title: 'Quest finished without a first meeting',
        hint: 'Plays when the quest was done before they ever spoke.',
    },
    done: { title: 'After everything', hint: 'Plays every time the player talks to them once it is all over.' },
};

const soundStem = (s: ContentSession, db: number): string | null => (db === 0 ? s.stem : (s.story?.deps[db] ?? null));

export interface ConversationPanel extends Panel {
    /** Show the conversation of one handler class. */
    focus(classId: number): void;
}

export function createConversationPanel(session: ContentSession, onNewLine?: () => void): ConversationPanel {
    const state = { handler: 0 };
    const stemSel = h('select', { class: 'story-sel', 'aria-label': 'Level folder' });
    const convSel = h('select', { class: 'story-sel', 'aria-label': 'Conversation' });
    const info = h('div', { class: 'story-info' });
    const body = h('div', { class: 'cv-body' });
    const loadBtn = h(
        'button',
        { class: 'btn primary', type: 'button', onclick: () => void session.ensure().then(renderAll, fail) },
        'Load the conversations',
    );
    const newBtn = h(
        'button',
        {
            class: 'btn',
            type: 'button',
            title: 'Write a new line with its own subtitle and recording',
            onclick: () => onNewLine?.(),
        },
        icon('plus', 13),
        'Write a new line',
    );
    const root = h(
        'div',
        { class: 'panel story cv' },
        h('div', { class: 'story-bar' }, stemSel, convSel, onNewLine ? newBtn : null, info),
        body,
    );
    const fail = (e: unknown): void => session.host.status(e instanceof Error ? e.message : String(e), true);

    stemSel.addEventListener('change', () => void session.use(stemSel.value));
    convSel.addEventListener('change', () => {
        state.handler = Number(convSel.value);
        renderBody();
    });

    function play(ref: number): HTMLElement {
        const { id, db } = refParts(ref);
        const stem = soundStem(session, db);
        const ok = !!session.host.play && !!stem && id !== 0;
        return h(
            'button',
            {
                class: 'btn sm cv-play',
                type: 'button',
                disabled: !ok,
                title: ok ? 'Play the recording' : 'No recording to play',
                'aria-label': 'Play the recording',
                onclick: () => void session.host.play?.(stem!, id),
            },
            icon('play', 12),
        );
    }

    function lineCard(refs: number[], k: number, key: HandlerPhaseKey): HTMLElement {
        const set = (next: number[]): void => session.setPhaseLines(state.handler, key, next);
        const cid = refs[k]! & 0xffff;
        const l = session.lines().find((y) => y.classId === cid);
        const soundRef = l ? session.lineSound(l.classId, 0) : 0;
        const { id, db } = refParts(soundRef);
        const snd = l && db === 0 ? session.story?.sounds.get(id) : undefined;
        const move = (to: number): void => {
            const n = [...refs];
            [n[to], n[k]] = [n[k]!, n[to]!];
            set(n);
        };
        const btn = (iconName: string, title: string, fn: () => void, disabled = false): HTMLElement =>
            h(
                'button',
                {
                    class: `btn sm${title === 'Move up' ? ' flip' : ''}`,
                    type: 'button',
                    title,
                    'aria-label': title,
                    disabled,
                    onclick: fn,
                },
                icon(iconName, 12),
            );
        const text = h('textarea', {
            class: 'story-ta cv-text',
            rows: 2,
            spellcheck: 'true',
            'aria-label': 'Subtitle',
            placeholder: snd ? 'Type the subtitle' : 'This line has no subtitle to edit',
            disabled: !snd,
        });
        if (snd) {
            text.value = session.text(id);
            text.addEventListener('input', () => session.setText(id, text.value));
        }
        return h(
            'div',
            { class: `cv-line${session.lineIsNew(cid) ? ' new' : ''}` },
            h('div', { class: 'cv-n' }, String(k + 1)),
            h(
                'div',
                { class: 'cv-main' },
                h(
                    'div',
                    { class: 'cv-head' },
                    h('strong', { class: 'cv-who' }, l?.speaker ?? 'Missing line'),
                    h('span', { class: 'dim cv-snd' }, soundRef ? soundLabel(session, soundRef) : 'no recording'),
                    snd ? h('span', { class: 'dim' }, `${snd.seconds.toFixed(1)} s`) : null,
                    session.lineIsNew(cid) ? h('span', { class: 'story-tag warn' }, 'new') : null,
                ),
                text,
            ),
            h(
                'div',
                { class: 'cv-acts' },
                play(soundRef),
                btn('chevron-down', 'Move up', () => move(k - 1), k === 0),
                btn('chevron-down', 'Move down', () => move(k + 1), k === refs.length - 1),
                btn('x', 'Take it out of this stage', () => set(refs.filter((_, j) => j !== k))),
            ),
        );
    }

    function stage(i: number): HTMLElement {
        const key = HANDLER_PHASES[i]!.key;
        const refs = session.phaseLines(state.handler, key);
        const all = session.lines();
        const pick = h(
            'select',
            { class: 'story-sel', 'aria-label': `Line to add to ${STAGE_TITLES[key].title}` },
            ...all.map((y) => h('option', { value: String(y.classId) }, `${y.speaker}: ${y.name.slice(0, 50)}`)),
        );
        const edited = session.phaseEdited(state.handler, key);
        return h(
            'section',
            { class: 'cv-stage' },
            h(
                'header',
                {},
                h('strong', {}, STAGE_TITLES[key].title),
                edited ? h('span', { class: 'story-tag warn' }, 'edited') : null,
                h('span', { class: 'dim' }, STAGE_TITLES[key].hint),
            ),
            h(
                'div',
                { class: 'cv-lines' },
                ...(refs.length
                    ? refs.map((_, k) => lineCard(refs, k, key))
                    : [h('div', { class: 'cv-none dim' }, 'Nobody speaks in this stage.')]),
            ),
            h(
                'div',
                { class: 'cv-add' },
                pick,
                h(
                    'button',
                    {
                        class: 'btn sm',
                        type: 'button',
                        disabled: !all.length,
                        onclick: () => {
                            const id = Number(pick.value);
                            if (id) session.setPhaseLines(state.handler, key, [...refs, id]);
                        },
                    },
                    icon('plus', 12),
                    'Add this line',
                ),
            ),
        );
    }

    function renderBody(): void {
        const story = session.story;
        if (!story)
            return fill(body, h('div', { class: 'story-empty' }, 'Open a disc to edit its conversations.', loadBtn));
        const x = story.handlers.find((y) => y.classId === state.handler) ?? story.handlers[0];
        if (!x) return fill(body, h('div', { class: 'story-empty' }, 'This level has no conversations.'));
        state.handler = x.classId;
        convSel.value = String(x.classId);
        fill(
            body,
            h(
                'div',
                { class: 'cv-root' },
                h('div', { class: 'cv-title' }, icon('edit', 15), h('strong', {}, x.name)),
                h(
                    'div',
                    { class: 'dim' },
                    `${x.hasQuest ? 'Gives a quest. ' : ''}${x.initiallyActive ? 'Ready to talk from the start. ' : 'Starts silent until something wakes it. '}Talks to the player within ${x.distance.toFixed(1)} cells.${x.goldOnComplete ? ` Pays ${x.goldOnComplete} gold when the quest is finished.` : ''}`,
                ),
            ),
            ...HANDLER_PHASES.map((_, i) => stage(i)),
            session.phaseEdited(x.classId)
                ? h(
                      'button',
                      { class: 'btn', type: 'button', onclick: () => session.revertPhases(x.classId) },
                      'Undo my changes to this conversation',
                  )
                : null,
        );
    }

    function renderAll(): void {
        const s = session.story;
        if (stemSel.options.length !== session.stems.length)
            fill(stemSel, ...session.stems.map((t) => h('option', { value: t }, t)));
        if (session.stem) stemSel.value = session.stem;
        fill(convSel, ...(s?.handlers ?? []).map((x) => h('option', { value: String(x.classId) }, x.name)));
        info.textContent = s
            ? `${s.handlers.length} conversation${s.handlers.length === 1 ? '' : 's'}, ${session.count()} change${session.count() === 1 ? '' : 's'}`
            : '';
        renderBody();
    }

    session.subscribe(() => {
        // typing a subtitle must not rebuild the box under the cursor
        if (body.contains(document.activeElement) && document.activeElement instanceof HTMLTextAreaElement) {
            info.textContent = `${session.count()} change${session.count() === 1 ? '' : 's'}`;
            return;
        }
        renderAll();
    });
    renderAll();
    return {
        el: root,
        onLoad: renderAll,
        focus(classId) {
            state.handler = classId;
            renderAll();
        },
    };
}
