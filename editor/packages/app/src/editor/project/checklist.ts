import { WORKFLOWS, nextStep, type StepFacts, type TaskId, type WorkflowStep } from '@drakan/editor-core';
import { getCommand } from '../registry.ts';
import { h, icon, store } from '../dom.ts';
import { startFlow } from '../startflow.ts';
import { LOAD_CARD_EVENT, loadCardCovering } from '../loadprogress.ts';
import { availableTemplates, templateStepStates, type TemplateId } from '../starttemplates.ts';
import { projects } from './store.ts';
import './project.css';
import '../startpage.css';

export interface ChecklistHost {
    facts(): StepFacts;
    run(command: string): void;
    openGuide(id: string): void;
    startTask(task: TaskId): void;
}

/** What the card lists: the template started on the start page, else the task of the mod project. */
interface Flow {
    key: string;
    title: string;
    guide: string;
    steps: WorkflowStep[];
    done: boolean[];
    tick(step: string, on: boolean): void;
    /** Template id for the "Making" picker (task flows map to their template). */
    template: TemplateId | null;
}

const TASK_TEMPLATE: Record<TaskId, TemplateId> = {
    campaign: 'campaign',
    multiplayer: 'arena',
    newlevel: 'newlevel',
    story: 'story',
    save: 'save',
};

/**
 * The "next steps" card in the corner: the steps of what the user chose to make, ticked off as the editor sees them
 * done (or by hand for steps it cannot see). The next step is highlighted with its button; collapsed, the card still
 * names the next step. It opens by itself when a template starts, and can be dismissed until the next one starts.
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
    let pulse = 0;

    function flow(): Flow | null {
        const t = startFlow.active();
        if (t) {
            const facts = host.facts();
            return {
                key: `t:${t.id}`,
                title: t.title,
                guide: t.steps.find((s) => s.guide)?.guide ?? 'start',
                steps: t.steps,
                done: templateStepStates(t, facts, startFlow.ticks(t.id)),
                tick: (s, on) => startFlow.tick(t.id, s, on),
                template: t.id,
            };
        }
        const p = projects.get();
        const w = p?.task ? WORKFLOWS[p.task] : null;
        if (!p || !w) return null;
        const facts = host.facts();
        const ticks = p.ticks[w.id] ?? [];
        return {
            key: `p:${p.id}:${w.id}`,
            title: w.title,
            guide: w.guide,
            steps: w.steps,
            done: w.steps.map((s) => (s.auto ? s.auto(facts) : false) || ticks.includes(s.id)),
            tick: (s, on) => projects.tick(w.id, s, on),
            template: TASK_TEMPLATE[w.id],
        };
    }

    /**
     * Over an open level the card sits in the viewport's bottom-left corner, so it never covers the panels the steps
     * point at (World Settings, Details); otherwise (start page, small windows) it keeps its corner of the window.
     */
    function place(): void {
        const vp = document.getElementById('lv-viewport');
        const empty = document.getElementById('lv-empty');
        const r = vp && vp.offsetParent && empty?.hidden !== false ? vp.getBoundingClientRect() : null;
        const fits = !!r && r.width > 420 && r.height > 260 && !isSmall();
        el.classList.toggle('in-view', fits);
        el.style.left = fits ? `${Math.round(r!.left + 12)}px` : '';
        el.style.right = fits ? 'auto' : '';
        el.style.bottom = fits ? `${Math.round(window.innerHeight - r!.bottom + 12)}px` : '';
        el.style.maxHeight = fits ? `${Math.round(r!.height - 70)}px` : '';
        // A viewport too small for the card (a narrow window) still gets the pill, in its top-right corner: the window's
        // bottom-right corner is where the bottom dock's content (buttons, lists) ends.
        const vr = vp && vp.offsetParent ? vp.getBoundingClientRect() : null;
        const corner = !fits && !!vr && vr.width > 200;
        el.classList.toggle('in-corner', corner);
        el.style.top = corner ? `${Math.round(vr!.top + 12)}px` : '';
        if (corner) {
            el.style.left = 'auto';
            el.style.right = `${Math.round(window.innerWidth - vr!.right + 12)}px`;
            el.style.bottom = 'auto';
        }
    }

    /** The start page is open over a level (Home): the card would cover its button, and the page shows the same. */
    const startOverLevel = (): boolean => {
        const empty = document.getElementById('lv-empty');
        return !!empty && !empty.hidden && !!empty.offsetParent && host.facts().levelOpen;
    };

    function refresh(): void {
        const f = flow();
        // While a level is being read the loading card has the middle of the viewport; the card comes back after.
        if (!f || dismissedFor === f.key || startOverLevel() || loadCardCovering()) {
            el.hidden = true;
            last = '';
            return;
        }
        if (startFlow.takeFresh()) {
            // Just started: open the card and make it noticeable once.
            openedHere = true;
            if (pref) {
                pref = false;
                store.set('task-card-collapsed', '0');
            }
            pulse = Date.now();
            last = '';
        }
        collapsed = isSmall() ? !openedHere : pref;
        el.classList.toggle('small', isSmall());
        place();
        const next = nextStep(f.done);
        const key = JSON.stringify([f.key, f.done, collapsed]);
        el.hidden = false;
        if (key === last) return;
        last = key;
        const count = f.done.filter(Boolean).length;
        const toggle = (): void => {
            if (isSmall()) openedHere = collapsed;
            else {
                pref = !pref;
                store.set('task-card-collapsed', pref ? '1' : '0');
            }
            last = '';
            refresh();
        };
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
                    onclick: toggle,
                },
                icon('chevron', 12),
                h('strong', {}, f.title),
            ),
            h('span', { class: 'pj-prog' }, next < 0 ? 'All done' : `Step ${next + 1} of ${f.steps.length}`),
            h(
                'button',
                {
                    class: 'pj-q-inline',
                    type: 'button',
                    title: 'Guide for this',
                    'aria-label': 'Guide for this',
                    onclick: () => host.openGuide(f.guide),
                },
                '?',
            ),
            h(
                'button',
                {
                    class: 'pj-card-x',
                    type: 'button',
                    title: 'Hide the checklist (start something on the start page to bring it back)',
                    'aria-label': 'Hide the checklist',
                    onclick: () => {
                        dismissedFor = f.key;
                        refresh();
                    },
                },
                icon('x', 12),
            ),
        );
        const bar = h(
            'div',
            { class: 'pj-bar' },
            h('i', { style: `width:${Math.round((count / f.steps.length) * 100)}%` }),
        );
        // Collapsed: one line with the next step, so the card always says what to do now.
        const peek =
            collapsed && next >= 0
                ? h(
                      'button',
                      { class: 'pj-peek', type: 'button', title: 'Show the steps', onclick: toggle },
                      h('span', { class: 'pj-peek-n' }, String(next + 1)),
                      h('span', { class: 'pj-peek-t' }, f.steps[next]!.title),
                  )
                : null;
        const list = h(
            'ol',
            { class: 'pj-steps' },
            ...f.steps.map((s, i) => {
                const isDone = f.done[i]!;
                const auto = !!s.auto;
                const box = h('input', {
                    type: 'checkbox',
                    class: 'pj-tick',
                    'aria-label': `${s.title}: done`,
                    title: auto ? 'Ticks itself when the editor sees it done' : 'Tick it when you have done it',
                    ...(auto ? { disabled: true } : {}),
                });
                box.checked = isDone;
                if (!auto) box.addEventListener('change', () => f.tick(s.id, box.checked));
                const action = s.action && (!getCommand(s.action.command) ? null : s.action);
                return h(
                    'li',
                    { class: `${isDone ? 'done' : ''}${i === next ? ' next' : ''}`, 'data-step': s.id },
                    box,
                    h(
                        'div',
                        { class: 'pj-step' },
                        h('div', { class: 'pj-step-t' }, s.title),
                        i === next ? h('div', { class: 'pj-step-x' }, s.text) : null,
                        i === next && (action || s.guide)
                            ? h(
                                  'div',
                                  { class: 'pj-step-a' },
                                  action
                                      ? h(
                                            'button',
                                            {
                                                class: 'ui-btn sm primary',
                                                type: 'button',
                                                onclick: () => host.run(action.command),
                                            },
                                            action.label,
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
                        // A step the editor cannot see: say how to move on.
                        i === next && !auto
                            ? h(
                                  'button',
                                  { class: 'pj-link pj-skip', type: 'button', onclick: () => f.tick(s.id, true) },
                                  'Done, next step',
                              )
                            : null,
                    ),
                );
            }),
        );
        const sel = h('select', { class: 'ui-select', 'aria-label': 'What you are making' });
        for (const t of availableTemplates((c) => !!getCommand(c)))
            sel.append(new Option(t.title, t.id, false, t.id === f.template));
        sel.addEventListener('change', () => startFlow.start(sel.value as TemplateId));
        el.replaceChildren(
            head,
            bar,
            ...(collapsed
                ? peek
                    ? [peek]
                    : []
                : [list, h('div', { class: 'pj-card-foot' }, h('span', {}, 'Making'), sel)]),
        );
        el.classList.toggle('collapsed', collapsed);
        if (pulse && Date.now() - pulse < 1500) {
            el.classList.remove('fresh');
            void el.offsetWidth;
            el.classList.add('fresh');
            pulse = 0;
        }
    }

    window.addEventListener('resize', () => {
        last = '';
        refresh();
    });
    const changed = (): void => {
        // Something new was started: show the card again.
        const f = flow();
        if (dismissedFor && f?.key !== dismissedFor) dismissedFor = null;
        last = '';
        refresh();
    };
    projects.on(changed);
    document.addEventListener(LOAD_CARD_EVENT, () => {
        last = '';
        refresh();
    });
    const empty = document.getElementById('lv-empty');
    if (empty)
        new MutationObserver(() => {
            last = '';
            refresh();
        }).observe(empty, { attributes: true, attributeFilter: ['hidden'] });
    startFlow.on(() => {
        dismissedFor = null;
        last = '';
        refresh();
    });
    return { el, refresh };
}
