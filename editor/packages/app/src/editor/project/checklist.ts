import { WORKFLOWS, nextStep, stepStates, type StepFacts, type TaskId } from '@drakan/editor-core';
import { h, icon, store } from '../dom.ts';
import { projects } from './store.ts';
import './project.css';

export interface ChecklistHost {
    facts(): StepFacts;
    run(command: string): void;
    openGuide(id: string): void;
    startTask(task: TaskId): void;
}

/**
 * The "next steps" card in the corner: the steps of the task the user chose, ticked off as the editor sees them done
 * (or by hand for steps it cannot see). Collapsible, and it can be dismissed until the next task is chosen.
 */
export function createChecklist(host: ChecklistHost): { el: HTMLElement; refresh(): void } {
    const el = h('aside', { class: 'pj-card', id: 'task-card', 'aria-label': 'Next steps', hidden: true });
    document.body.append(el);
    /** Below this the card would sit on top of the panels, so it starts as a small pill and opens only on request. */
    const isSmall = (): boolean => window.innerWidth < 1100 || window.innerHeight < 760;
    let pref = store.get('task-card-collapsed') === '1';
    let openedHere = false;
    let collapsed = pref;
    let dismissedFor: string | null = null;
    let last = '';

    function refresh(): void {
        const p = projects.get();
        const task = p?.task ?? null;
        const w = task ? WORKFLOWS[task] : null;
        if (!p || !w || dismissedFor === `${p.id}:${task}`) {
            el.hidden = true;
            last = '';
            return;
        }
        collapsed = isSmall() ? !openedHere : pref;
        el.classList.toggle('small', isSmall());
        const facts = host.facts();
        const ticks = p.ticks[w.id] ?? [];
        const done = stepStates(w, facts, ticks);
        const next = nextStep(done);
        const key = JSON.stringify([w.id, done, collapsed, p.id]);
        el.hidden = false;
        if (key === last) return;
        last = key;
        const count = done.filter(Boolean).length;
        const head = h(
            'div',
            { class: 'pj-card-head' },
            h(
                'button',
                {
                    class: 'pj-card-toggle',
                    type: 'button',
                    'aria-expanded': String(!collapsed),
                    title: collapsed ? 'Show the steps' : 'Hide the steps',
                    onclick: () => {
                        if (isSmall()) openedHere = collapsed;
                        else {
                            pref = !pref;
                            store.set('task-card-collapsed', pref ? '1' : '0');
                        }
                        last = '';
                        refresh();
                    },
                },
                icon('chevron', 12),
                h('strong', {}, w.title),
            ),
            h('span', { class: 'pj-prog' }, next < 0 ? 'All done' : `${count} of ${w.steps.length}`),
            h(
                'button',
                {
                    class: 'pj-q-inline',
                    type: 'button',
                    title: 'Guide for this task',
                    'aria-label': 'Guide for this task',
                    onclick: () => host.openGuide(w.guide),
                },
                '?',
            ),
            h(
                'button',
                {
                    class: 'pj-card-x',
                    type: 'button',
                    title: 'Hide the checklist (choose a task again to bring it back)',
                    'aria-label': 'Hide the checklist',
                    onclick: () => {
                        dismissedFor = `${p.id}:${task}`;
                        refresh();
                    },
                },
                icon('x', 12),
            ),
        );
        const bar = h(
            'div',
            { class: 'pj-bar' },
            h('i', { style: `width:${Math.round((count / w.steps.length) * 100)}%` }),
        );
        const list = h(
            'ol',
            { class: 'pj-steps' },
            ...w.steps.map((s, i) => {
                const isDone = done[i]!;
                const auto = !!s.auto;
                const box = h('input', {
                    type: 'checkbox',
                    class: 'pj-tick',
                    'aria-label': `${s.title}: done`,
                    ...(auto ? { disabled: true } : {}),
                });
                box.checked = isDone;
                if (!auto) box.addEventListener('change', () => projects.tick(w.id, s.id, box.checked));
                const li = h(
                    'li',
                    { class: `${isDone ? 'done' : ''}${i === next ? ' next' : ''}`, 'data-step': s.id },
                    box,
                    h(
                        'div',
                        { class: 'pj-step' },
                        h('div', { class: 'pj-step-t' }, s.title),
                        i === next ? h('div', { class: 'pj-step-x' }, s.text) : null,
                        i === next && (s.action || s.guide)
                            ? h(
                                  'div',
                                  { class: 'pj-step-a' },
                                  s.action
                                      ? h(
                                            'button',
                                            {
                                                class: 'ui-btn sm primary',
                                                type: 'button',
                                                onclick: () => host.run(s.action!.command),
                                            },
                                            s.action.label,
                                        )
                                      : null,
                                  s.guide
                                      ? h(
                                            'button',
                                            {
                                                class: 'pj-link',
                                                type: 'button',
                                                onclick: () => host.openGuide(s.guide!),
                                            },
                                            'How',
                                        )
                                      : null,
                              )
                            : null,
                    ),
                );
                return li;
            }),
        );
        const sel = h('select', { class: 'ui-select', 'aria-label': 'Change task' });
        for (const t of Object.values(WORKFLOWS)) sel.append(new Option(t.title, t.id, false, t.id === w.id));
        sel.addEventListener('change', () => host.startTask(sel.value as TaskId));
        el.replaceChildren(
            head,
            bar,
            ...(collapsed ? [] : [list, h('div', { class: 'pj-card-foot' }, h('span', {}, 'Task'), sel)]),
        );
        el.classList.toggle('collapsed', collapsed);
    }

    window.addEventListener('resize', () => {
        last = '';
        refresh();
    });
    projects.on(() => {
        // A newly chosen task shows the card again.
        dismissedFor =
            dismissedFor && projects.get() && dismissedFor === `${projects.get()!.id}:${projects.get()!.task}`
                ? dismissedFor
                : null;
        last = '';
        refresh();
    });
    return { el, refresh };
}
