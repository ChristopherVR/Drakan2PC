// Quest steps: for one quest, what starts it and what completes it in the open level, picked from the level instead of
// typed as message numbers. "Starts when" / "Completes when" list the objects wired to the quest object and let you add
// one: the player entering an area, or any object in the level that fires (a lever, a plate, a counter, a talk).

import {
    graphProblems,
    linkWithMessage,
    logicOf,
    nodeName,
    outgoingMessages,
    plainRoleName,
    resourceParts,
    buildLogicGraph,
    type LevelObject,
} from '@drakan/editor-core';
import { MESSAGE_QUEST_COMPLETE, MESSAGE_QUEST_INITIATE } from '@drakan/formats';
import type { Ctx } from '../editor/ctx.ts';
import { fill, h, icon } from '../editor/dom.ts';
import { openTemplate } from '../editor/logicpanel.ts';

export interface QuestWiring {
    quest: LevelObject;
    starts: LevelObject[];
    completes: LevelObject[];
}

/** The quest objects of one quest class placed in the document, with what is wired to start and complete each. */
export function questWiring(ctx: Ctx, classId: number): QuestWiring[] {
    const env = ctx.schemas();
    const doc = ctx.doc();
    if (!env || doc.size === 0) return [];
    const out: QuestWiring[] = [];
    for (const q of doc.objects()) {
        const spec = logicOf(q, env);
        if (!spec || spec.role !== 'quest' || resourceParts(q.resource).classId !== classId) continue;
        const w: QuestWiring = { quest: q, starts: [], completes: [] };
        for (const s of doc.objects()) {
            if (!s.links?.includes(q.id)) continue;
            const sp = logicOf(s, env);
            const m = sp ? outgoingMessages(s, env, sp)[0] : undefined;
            if (m?.value === MESSAGE_QUEST_COMPLETE) w.completes.push(s);
            else w.starts.push(s);
        }
        out.push(w);
    }
    return out;
}

export function questStepsBlock(ctx: Ctx, classId: number): HTMLElement {
    const host = h('div', { class: 'qs' });
    const env = ctx.schemas();
    const doc = ctx.doc();

    const pickAndLink = (q: LevelObject, message: number): void => {
        const v = ctx.view();
        if (!v.policy.structural) return;
        ctx.toast('Click the object that should do it, in the level or the Outliner.', 'info');
        v.pickObject((id) => {
            if (id === null) return ctx.toast('Nothing there.', 'warn');
            if (id === q.id) return ctx.toast('Pick something other than the quest itself.', 'warn');
            if (!env) return;
            ctx.edit(
                `Wire ${nodeName(doc.get(id) ?? q)} to the quest`,
                [id],
                (o) => void linkWithMessage(o, env, q.id, message),
            );
            render();
        });
    };

    const areaFor = (q: LevelObject, message: 'Quest Initiate' | 'Quest Complete'): void => {
        ctx.view().select([q.id]);
        openTemplate('volume-message', { message });
    };

    function listFor(q: LevelObject, items: LevelObject[], message: number): HTMLElement[] {
        const rows = items.map((s) => {
            const sp = env ? logicOf(s, env) : undefined;
            return h(
                'div',
                { class: 'qs-row' },
                h(
                    'button',
                    {
                        class: 'qs-name',
                        type: 'button',
                        title: 'Find it in the level',
                        onclick: () => ctx.locate([s.id]),
                    },
                    nodeName(s, sp),
                ),
                h('span', { class: 'dim' }, sp ? plainRoleName(sp) : ''),
                h(
                    'button',
                    {
                        class: 'btn sm',
                        type: 'button',
                        title: 'Stop this from doing it',
                        'aria-label': 'Remove',
                        onclick: () => {
                            ctx.edit(
                                'Unwire quest step',
                                [s.id],
                                (o) => void (o.links = (o.links ?? []).filter((x) => x !== q.id)),
                            );
                            render();
                        },
                    },
                    icon('x', 11),
                ),
            );
        });
        if (!rows.length) rows.push(h('div', { class: 'dim' }, 'Nothing yet.'));
        return [
            ...rows,
            h(
                'div',
                { class: 'qs-add' },
                h(
                    'button',
                    {
                        class: 'btn sm',
                        type: 'button',
                        onclick: () =>
                            areaFor(q, message === MESSAGE_QUEST_INITIATE ? 'Quest Initiate' : 'Quest Complete'),
                    },
                    icon('plus', 12),
                    'The player enters an area',
                ),
                h(
                    'button',
                    { class: 'btn sm', type: 'button', onclick: () => pickAndLink(q, message) },
                    icon('link', 12),
                    'Something in the level fires',
                ),
            ),
        ];
    }

    function render(): void {
        const wirings = questWiring(ctx, classId);
        if (!env || doc.size === 0)
            return fill(
                host,
                h('div', { class: 'lbl' }, 'Steps in the open level'),
                h('div', { class: 'dim small' }, 'Open a level to wire this quest.'),
            );
        const kids: (Node | null)[] = [h('div', { class: 'lbl' }, 'Steps in the open level')];
        if (!wirings.length)
            kids.push(
                h('div', { class: 'dim small' }, 'This quest is not placed in the open level yet.'),
                h(
                    'button',
                    { class: 'btn', type: 'button', onclick: () => openTemplate('quest-object', { classId }) },
                    icon('plus', 12),
                    'Place it and start it when the player enters an area',
                ),
            );
        const g = buildLogicGraph(doc, env, { focus: wirings.map((x) => x.quest.id), depth: 1 });
        const probs = graphProblems(doc, env, g);
        for (const w of wirings) {
            kids.push(
                h(
                    'div',
                    { class: 'qs-card' },
                    h(
                        'div',
                        { class: 'qs-head' },
                        h('strong', {}, `${nodeName(w.quest)} (object ${w.quest.id})`),
                        h(
                            'button',
                            { class: 'btn sm', type: 'button', onclick: () => ctx.locate([w.quest.id]) },
                            'Find it',
                        ),
                    ),
                    h('div', { class: 'qs-sect' }, 'Starts when'),
                    ...listFor(w.quest, w.starts, MESSAGE_QUEST_INITIATE),
                    h('div', { class: 'qs-sect' }, 'Completes when'),
                    ...listFor(w.quest, w.completes, MESSAGE_QUEST_COMPLETE),
                    h(
                        'div',
                        { class: 'dim small' },
                        'To finish a quest when something is picked up, put an area around the item: items do not send messages themselves.',
                    ),
                    ...(probs.get(w.quest.id) ?? []).map((p) => h('div', { class: 'story-warn' }, p.text)),
                ),
            );
        }
        fill(host, ...kids);
    }
    render();
    return host;
}
