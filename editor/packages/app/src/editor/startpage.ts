import { h, icon, store } from './dom.ts';
import { allCommands, getCommand, onRegistryChange, runCommand } from './registry.ts';
import { projects } from './project/store.ts';
import { installQuickAdd, quickAddItems } from './quickadd.ts';
import { templateArt } from './startart.ts';
import { hasTour, startTour } from './tour/registry.ts';
import { startFlow } from './startflow.ts';
import { isCancelled } from './loadsteps.ts';
import {
    GROUP_TITLES,
    availableTemplates,
    flowSteps,
    needsText,
    primaryLabel,
    primaryNote,
    shortLevelName,
    startPlan,
    suggestedLevels,
    templateById,
    type StartTemplate,
    type TemplateGroup,
    type TemplateId,
} from './starttemplates.ts';
import './shellux.css';
import './project/project.css';
import './startpage.css';

/**
 * First thing a modder sees in the Level tab, laid out like Unreal's Project Browser: template tiles on the left, the
 * selected template's details on the right with one button that does its first real step (asks for the disc image,
 * shows the level picker, opens the editor that makes it), then the checklist takes over. Below: the mod project,
 * recent levels and every level of the disc.
 */
export interface StartPageActions {
    /** Ask the host to open a disc image (file picker or desktop dialog). */
    openIso(): void;
    /** Open a saved .level.json or world dump. */
    openDocument(f: File): void;
    /** Open a level of the disc; resolves once it is shown (rejects when it could not be read). */
    openLevel(dirPath: string): Promise<void> | void;
    /** A level is open in the editor. */
    levelOpen?(): boolean;
    /** Display name of the open level, or null. */
    openLevelName?(): string | null;
    /** Go back to the open level (the start page was opened over it). */
    close?(): void;
    openPalette(): void;
    openHelp(): void;
}

export interface LevelRef {
    name: string;
    dirPath: string;
    /** Exists only in memory (a converted Order of the Flame level). */
    virtual?: boolean;
}

export interface StartPage {
    el: HTMLElement;
    /** Re-render for the current disc (levels = null when no disc is open). */
    refresh(levels: LevelRef[] | null, isoName?: string): void;
    /** Redraw before the page is shown again over an open level (File > Start page, the Home button). */
    show(): void;
}

const RECENT_KEY = 'recent-levels';
const SELECTED_KEY = 'start-selected';
const MAX_RECENT = 4;

export const recentLevels = (): LevelRef[] =>
    store.json<LevelRef[]>(RECENT_KEY, []).filter((l) => l && typeof l.dirPath === 'string');

export function rememberLevel(l: LevelRef): void {
    store.set(
        RECENT_KEY,
        JSON.stringify([l, ...recentLevels().filter((r) => r.dirPath !== l.dirPath)].slice(0, MAX_RECENT)),
    );
}

const splitName = (name: string): [string, string] => {
    const m = /^(Level\s+\S+)\s*[-–]\s*(.*)$/.exec(name);
    return m ? [m[1]!, m[2]!] : [name, ''];
};

export function createStartPage(a: StartPageActions): StartPage {
    installQuickAdd();
    const el = h('div', { class: 'start sp' });
    const docInput = h('input', { type: 'file', accept: '.json', hidden: true });
    docInput.addEventListener('change', () => {
        const f = docInput.files?.[0];
        docInput.value = '';
        if (f) a.openDocument(f);
    });
    let levels: LevelRef[] | null = null;
    let isoName: string | undefined;
    let overlayWasHidden: boolean | null = null;
    /** The tile shown in the details pane. */
    let selected: TemplateId = (templateById(store.get(SELECTED_KEY) ?? '')?.id ?? 'campaign') as TemplateId;
    /** Started without a disc: carries on once the disc is open. */
    let waiting: TemplateId | null = null;
    /** The level picker is shown for this template. */
    let picking: TemplateId | null = null;
    /** The level chosen in the picker; its button opens it. */
    let picked: LevelRef | null = null;

    const levelOpen = (): boolean => {
        try {
            return !!a.levelOpen?.();
        } catch {
            // The host's level state is not set up yet (the first render runs while the host is being built).
            return false;
        }
    };
    const state = () => ({ disc: !!levels, levelOpen: levelOpen() });
    const templates = (): StartTemplate[] => availableTemplates((c) => !!getCommand(c));

    function select(id: TemplateId, focusDetails = false): void {
        selected = id;
        store.set(SELECTED_KEY, id);
        // Update in place (a full redraw would replace the tile under the pointer and swallow a double-click).
        const old = el.querySelector('.sp-details');
        if (!old || picking) return render();
        for (const b of el.querySelectorAll<HTMLElement>('.sp-tile')) {
            const on = b.dataset['template'] === id;
            b.classList.toggle('on', on);
            b.setAttribute('aria-pressed', String(on));
        }
        old.replaceWith(details());
        if (focusDetails) el.querySelector<HTMLElement>('.sp-go')?.focus();
    }

    // ---- starting a template ------------------------------------------------------------------------------------

    function begin(id: TemplateId): void {
        const t = templateById(id);
        if (!t) return;
        selected = id;
        store.set(SELECTED_KEY, id);
        const next = startPlan(t, state())[0];
        if (next === 'disc') {
            waiting = id;
            render();
            a.openIso();
            return;
        }
        waiting = null;
        if (next === 'pick') {
            picking = id;
            picked = null;
            render();
            el.closest('#lv-empty')?.scrollTo({ top: 0 });
            el.querySelector<HTMLElement>('.sp-pick .sp-level')?.focus();
            return;
        }
        run(t);
    }

    /** Put the work in the mod project under the template's task (a new project gets the template's name). */
    function fileUnder(t: StartTemplate): void {
        if (!t.task || t.task === 'save') return;
        const p = projects.ensure(t.projectName ?? 'My mod', t.task);
        if (p.task !== t.task) projects.set({ ...p, task: t.task });
    }

    /** The template's first real step, with everything it needs in place. */
    function run(t: StartTemplate): void {
        picking = null;
        picked = null;
        projects.pickFor = null;
        fileUnder(t);
        // The Save Editor is its own window: its steps would never tick here, so the checklist keeps what it had.
        if (t.id !== 'save') startFlow.set(t.id);
        switch (t.id) {
            case 'build-campaign':
                startFlow.set(null);
                runCommand('campaign.wizard');
                break;
            case 'campaign':
            case 'arena':
            case 'story':
                // The task opens its editor (World Settings for an arena, Story) and the Level Editor.
                if (projects.handlers.start) projects.handlers.start(t.task!);
                else runCommand(`task.${t.task}`);
                break;
            case 'save':
                runCommand('mode.save');
                break;
            case 'ootf':
                runCommand('file.importOotf');
                break;
            default: {
                const qa = quickAddItems().find((i) => i.id === t.id);
                if (qa) qa.run();
                else runCommand(t.steps.find((s) => s.action)?.action?.command ?? '');
            }
        }
        render();
    }

    /**
     * Open the picked level for the template. The Level Editor takes over at once (its loading card shows the
     * steps), so the page does not linger or come back while the level loads; it only returns, on the picker, when
     * the open is cancelled or fails.
     */
    async function pickLevel(t: StartTemplate, l: LevelRef): Promise<void> {
        picking = null;
        picked = null;
        projects.pickFor = null;
        if (t.id === 'newlevel') {
            fileUnder(t);
            startFlow.set(t.id);
            projects.handlers.pick?.('newlevel', { name: l.name, dirPath: l.dirPath });
            render();
            return;
        }
        const opened = a.openLevel(l.dirPath);
        render();
        try {
            await opened;
            run(t);
        } catch (e) {
            // Cancelled: back on the picker with the same level chosen. Failed: the level view said why.
            picking = t.id;
            picked = isCancelled(e) ? l : null;
            render();
        }
    }

    function choose(l: LevelRef): void {
        picked = l;
        for (const b of el.querySelectorAll<HTMLElement>('.sp-pick .sp-level')) {
            const on = b.dataset['dir'] === l.dirPath;
            b.classList.toggle('on', on);
            b.setAttribute('aria-pressed', String(on));
        }
        el.querySelector('.sp-details')?.replaceWith(details());
    }

    function cancel(): void {
        waiting = null;
        picking = null;
        picked = null;
        projects.pickFor = null;
        projects.notify();
        render();
    }

    startFlow.setStarter(begin);

    // ---- pieces -------------------------------------------------------------------------------------------------

    const tile = (t: StartTemplate): HTMLElement => {
        const on = t.id === selected;
        const active = startFlow.active()?.id === t.id;
        return h(
            'button',
            {
                class: `sp-tile${on ? ' on' : ''}`,
                type: 'button',
                'data-template': t.id,
                'aria-pressed': String(on),
                title: on
                    ? 'Selected: press its button on the right, or double-click, to start'
                    : 'Click to select; double-click to start',
                onclick: () => select(t.id),
                ondblclick: () => begin(t.id),
                onkeydown: (e: Event) => {
                    if ((e as KeyboardEvent).key === 'Enter' && on) {
                        e.preventDefault();
                        begin(t.id);
                    }
                },
            },
            h(
                'span',
                { class: 'sp-tile-art' },
                templateArt(t.id),
                active ? h('span', { class: 'sp-badge' }, 'In progress') : null,
                h('span', { class: 'sp-check', 'aria-hidden': 'true' }, icon('check', 12)),
            ),
            h('span', { class: 'sp-tile-t' }, t.title),
            h('span', { class: 'sp-tile-l' }, t.line),
        );
    };

    function grid(): HTMLElement {
        const groups = new Map<TemplateGroup, StartTemplate[]>();
        for (const t of templates()) groups.set(t.group, [...(groups.get(t.group) ?? []), t]);
        return h(
            'div',
            { class: 'sp-grid', role: 'group', 'aria-label': 'Templates' },
            ...[...groups].map(([g, ts]) =>
                h(
                    'section',
                    { class: 'sp-group' },
                    h('h3', {}, GROUP_TITLES[g]),
                    h('div', { class: 'sp-tiles' }, ...ts.map(tile)),
                ),
            ),
        );
    }

    function details(): HTMLElement {
        const t = templateById(selected) ?? templates()[0]!;
        const s = state();
        const primary = (): HTMLElement => {
            if (waiting === t.id)
                return h(
                    'div',
                    { class: 'sp-wait-box' },
                    h(
                        'div',
                        { class: 'sp-wait', role: 'status' },
                        h('span', { class: 'sp-spin' }),
                        'Waiting for your disc image...',
                    ),
                    h(
                        'div',
                        { class: 'sp-row' },
                        h('button', { class: 'ui-btn', type: 'button', onclick: a.openIso }, 'Choose the disc again'),
                        h('button', { class: 'ui-btn', type: 'button', onclick: cancel }, 'Cancel'),
                    ),
                );
            if (picking === t.id && !picked)
                return h(
                    'div',
                    { class: 'sp-wait-box' },
                    h('div', { class: 'sp-wait' }, icon('chevron', 12), 'Choose a level on the left.'),
                    h('p', { class: 'sp-note' }, 'Click a level to choose it; double-click opens it straight away.'),
                );
            if (picking === t.id && picked) {
                const l = picked;
                return h(
                    'div',
                    { class: 'sp-go-box' },
                    h(
                        'button',
                        { class: 'ui-btn primary sp-go', type: 'button', onclick: () => void pickLevel(t, l) },
                        icon(t.id === 'newlevel' ? 'copy' : 'open', 14),
                        primaryLabel(t, s, l),
                    ),
                    h('p', { class: 'sp-note' }, primaryNote(t, s, l)),
                );
            }
            return h(
                'div',
                { class: 'sp-go-box' },
                h(
                    'button',
                    { class: 'ui-btn primary sp-go', type: 'button', onclick: () => begin(t.id) },
                    primaryLabel(t, s),
                ),
                h('p', { class: 'sp-note' }, primaryNote(t, s)),
            );
        };
        return h(
            'aside',
            {
                class: `sp-details${picking === t.id ? ' picking' : ''}`,
                'aria-live': 'polite',
                'aria-label': 'About the selected template',
                'data-template': t.id,
            },
            h('div', { class: 'sp-details-art' }, templateArt(t.id)),
            h('h2', {}, t.title),
            h('p', { class: 'sp-about' }, t.about),
            // The button comes before the details, so it is in view without scrolling on a small window.
            primary(),
            // A guided tour of this template (editor/tour): highlights each control in turn and waits for the step.
            hasTour(t.id)
                ? h(
                      'button',
                      {
                          class: 'ui-btn sm sp-tour',
                          type: 'button',
                          title: 'A guided walkthrough that highlights each thing to click',
                          onclick: () => void startTour(t.id),
                      },
                      icon('play', 12),
                      'Show me how',
                  )
                : null,
            h('h3', {}, "What you'll do"),
            h('ol', { class: 'sp-steps' }, ...t.steps.slice(0, 5).map((st) => h('li', {}, st.title))),
            h(
                'p',
                { class: `sp-needs${t.needs !== 'nothing' && !s.disc ? ' missing' : ''}` },
                icon(t.needs === 'nothing' ? 'save' : 'open', 13),
                needsText(t),
            ),
        );
    }

    /** A level: opens on click, or (picker, with `onOpen`) is chosen on click and opens on double-click or Enter. */
    const levelButton = (
        l: LevelRef,
        cls: string,
        onPick: (l: LevelRef) => void,
        onOpen?: (l: LevelRef) => void,
    ): HTMLElement => {
        const [n, d] = splitName(l.name);
        const short = shortLevelName(l.name);
        const on = !!onOpen && picked?.dirPath === l.dirPath;
        const b = h(
            'button',
            {
                class: `start-level sp-level${cls}${on ? ' on' : ''}`,
                type: 'button',
                'data-dir': l.dirPath,
                title: onOpen
                    ? `Click to choose ${short}; double-click to open it`
                    : `Open ${short} in the Level Editor`,
                onclick: () => onPick(l),
            },
            h('span', { class: 'n' }, n),
            h('span', { class: 'd' }, d),
        );
        if (onOpen) {
            b.setAttribute('aria-pressed', String(on));
            b.addEventListener('dblclick', () => onOpen(l));
            b.addEventListener('keydown', (e) => {
                if (e.key === 'Enter' && picked?.dirPath === l.dirPath) {
                    e.preventDefault();
                    onOpen(l);
                }
            });
        }
        return b;
    };

    /** The level picker, in place of the tiles. */
    function pickView(t: StartTemplate, all: LevelRef[]): HTMLElement {
        const good = suggestedLevels(t, all);
        const rec = recentLevels().filter((r) => all.some((l) => l.dirPath === r.dirPath));
        const open = (l: LevelRef): void => void pickLevel(t, l);
        const pick = (l: LevelRef, cls: string): HTMLElement => levelButton(l, cls, choose, open);
        return h(
            'div',
            { class: 'sp-pick' },
            h(
                'button',
                { class: 'ui-btn sm start-back', type: 'button', onclick: cancel },
                icon('undo', 12),
                'Back to templates',
            ),
            h('h3', { class: 'sp-pick-t' }, t.pickTitle ?? 'Which level?'),
            h(
                'p',
                { class: 'sp-pick-lead' },
                t.id === 'newlevel'
                    ? 'The copy becomes a new level with its own title; the original stays as it is. You can rename the copy in Project Settings.'
                    : 'Your changes are kept for this level and autosaved in this browser. You can open another level later with Ctrl+P.',
            ),
            good.length
                ? h(
                      'div',
                      { class: 'sp-pick-sec' },
                      h('h4', {}, 'Good place to start'),
                      h('div', { class: 'start-levels' }, ...good.map((l) => pick(l, ' good'))),
                  )
                : null,
            rec.length
                ? h(
                      'div',
                      { class: 'sp-pick-sec' },
                      h('h4', {}, 'Opened recently'),
                      h('div', { class: 'start-levels' }, ...rec.map((l) => pick(l, ' recent'))),
                  )
                : null,
            h(
                'div',
                { class: 'sp-pick-sec' },
                h('h4', {}, 'All levels'),
                h('div', { class: 'start-levels' }, ...all.map((l) => pick(l, ''))),
            ),
        );
    }

    /** The project you are working in and the recent ones. */
    function projectBlock(): HTMLElement {
        const p = projects.get();
        const btn = (label: string, id: string, primary = false): HTMLElement | null =>
            getCommand(id)
                ? h(
                      'button',
                      {
                          class: `ui-btn sm${primary ? ' primary' : ''}`,
                          type: 'button',
                          'data-command': id,
                          onclick: () => runCommand(id),
                      },
                      label,
                  )
                : null;
        const recent = allCommands().filter((c) => c.id.startsWith('project.recent.'));
        return h(
            'div',
            { class: 'start-projects sp-projects' },
            h(
                'div',
                { class: 'start-project' },
                icon('layers', 16),
                p ? h('strong', {}, p.name || p.id) : h('strong', { class: 'dim' }, 'No project yet'),
                h(
                    'span',
                    { class: 'dim' },
                    p
                        ? `${p.levels.length} level${p.levels.length === 1 ? '' : 's'} in your project`
                        : 'Starting a template creates one',
                ),
                h('span', { class: 'spacer' }),
                p ? btn('Project Settings', 'panel.project') : null,
                btn('Open project...', 'project.open'),
                btn('New project...', 'project.new'),
            ),
            recent.length
                ? h(
                      'div',
                      { class: 'start-recent-projects' },
                      h('span', { class: 'dim' }, 'Recent projects'),
                      ...recent.map((c) =>
                          h(
                              'button',
                              {
                                  class: 'linkish',
                                  type: 'button',
                                  title: c.hint ?? c.title,
                                  onclick: () => runCommand(c.id),
                              },
                              icon('folder', 13),
                              c.title,
                          ),
                      ),
                  )
                : null,
        );
    }

    function discBar(): HTMLElement {
        if (!levels)
            return h(
                'div',
                { class: 'sp-disc none' },
                h(
                    'div',
                    { class: 'sp-disc-t' },
                    h('strong', {}, 'Start by opening your game disc'),
                    h(
                        'span',
                        {},
                        'The editor reads levels from your own Drakan disc image. Nothing is uploaded and the disc is never changed.',
                    ),
                ),
                h(
                    'button',
                    { class: 'ui-btn primary', type: 'button', onclick: a.openIso },
                    icon('open', 14),
                    'Open disc image (.iso)',
                ),
                h('button', { class: 'ui-btn', type: 'button', onclick: () => docInput.click() }, 'Open a saved level'),
            );
        return h(
            'div',
            { class: 'sp-disc' },
            icon('check-circle', 14),
            h(
                'span',
                {},
                h('strong', {}, isoName ?? 'Your disc'),
                ` is open: ${levels.filter((l) => !l.virtual).length} levels${levels.some((l) => l.virtual) ? ` and ${levels.filter((l) => l.virtual).length} converted` : ''}.`,
            ),
            h('span', { class: 'spacer' }),
            h('button', { class: 'linkish', type: 'button', onclick: a.openIso }, 'Use another disc'),
        );
    }

    function more(): (HTMLElement | null)[] {
        if (!levels) return [];
        const rec = recentLevels().filter((r) => levels!.some((l) => l.dirPath === r.dirPath));
        return [
            rec.length
                ? h(
                      'section',
                      { class: 'sp-more' },
                      h('h3', {}, 'Recent levels'),
                      h(
                          'div',
                          { class: 'start-levels' },
                          ...rec.map((l) => levelButton(l, ' recent', (x) => void a.openLevel(x.dirPath))),
                      ),
                  )
                : null,
            h(
                'details',
                { class: 'start-all sp-more' },
                h('summary', {}, 'Just look around: open any level'),
                h(
                    'div',
                    { class: 'start-levels' },
                    ...levels.map((l) => levelButton(l, '', (x) => void a.openLevel(x.dirPath))),
                ),
            ),
            h(
                'div',
                { class: 'start-actions' },
                h(
                    'button',
                    { class: 'ui-btn', type: 'button', onclick: () => docInput.click() },
                    'Open a saved level (.json)',
                ),
            ),
        ];
    }

    /** Where you are on the way into the editor: the template, the disc, the level, the editor it opens. */
    function stepper(t: StartTemplate, isPicking: boolean): HTMLElement {
        const steps = flowSteps(t, state(), { picking: isPicking });
        return h(
            'ol',
            { class: 'sp-flow', 'aria-label': `Steps for ${t.title}` },
            ...steps.map((st, i) =>
                h(
                    'li',
                    { class: `sp-flow-s ${st.state}`, ...(st.state === 'current' ? { 'aria-current': 'step' } : {}) },
                    h('span', { class: 'sp-flow-n' }, st.state === 'done' ? icon('check', 11) : String(i + 1)),
                    h('span', { class: 'sp-flow-t' }, st.label),
                ),
            ),
        );
    }

    function refresh(lv: LevelRef[] | null, name?: string): void {
        levels = lv;
        isoName = name;
        if (lv && waiting) {
            const id = waiting;
            waiting = null;
            render();
            // After the host has finished with the new disc (it shows the Level Editor), carry on where the user was.
            window.setTimeout(() => begin(id), 0);
            return;
        }
        render();
    }

    function render(): void {
        // The checklist's "Choose level" asks for the picker (projects.pickFor); it shows the running template's
        // picker, or the plain one for a campaign level.
        if (projects.pickFor && !picking) {
            const cur = startFlow.active();
            picking = projects.pickFor === 'newlevel' ? 'newlevel' : cur && cur.needs === 'level' ? cur.id : 'campaign';
            selected = picking;
            projects.pickFor = null;
        }
        // The picker must be visible even over an open level: the overlay this page lives in is hidden while a level
        // is shown, so show it for the pick and hide it again after.
        const overlay = el.parentElement;
        const forced = !!levels && !!picking;
        if (overlay) {
            if (forced) {
                if (overlayWasHidden === null) overlayWasHidden = overlay.hidden !== false;
                overlay.hidden = false;
            } else if (overlayWasHidden !== null) {
                if (overlayWasHidden && levelOpen()) overlay.hidden = true;
                overlayWasHidden = null;
            }
        }
        const pickT = picking && levels ? templateById(picking) : undefined;
        const openName = levelOpen() ? (a.openLevelName?.() ?? null) : null;
        el.replaceChildren(
            ...[
                h(
                    'header',
                    { class: 'sp-head' },
                    h(
                        'div',
                        { class: 'sp-head-t' },
                        h('h2', {}, 'What do you want to make?'),
                        h(
                            'p',
                            { class: 'start-lead' },
                            'Select a template, then press its button on the right. A checklist guides you through the rest. The Home button, top left, brings you back here.',
                        ),
                    ),
                    openName && a.close
                        ? h(
                              'button',
                              {
                                  class: 'ui-btn sp-back',
                                  type: 'button',
                                  title: 'Close the start page (Esc)',
                                  onclick: () => a.close?.(),
                              },
                              icon('undo', 13),
                              `Back to ${shortLevelName(openName)}`,
                          )
                        : null,
                ),
                discBar(),
                stepper(pickT ?? templateById(selected) ?? templates()[0]!, !!pickT),
                h('div', { class: 'sp-browser' }, pickT && levels ? pickView(pickT, levels) : grid(), details()),
                projectBlock(),
                ...more(),
                h(
                    'p',
                    { class: 'start-foot' },
                    h(
                        'button',
                        { class: 'linkish', type: 'button', onclick: () => runCommand('help.guides') },
                        'Help guides',
                    ),
                    h('button', { class: 'linkish', type: 'button', onclick: a.openPalette }, 'Search everything'),
                    h('kbd', {}, 'Ctrl+P'),
                    h('button', { class: 'linkish', type: 'button', onclick: a.openHelp }, 'Keyboard shortcuts'),
                    h('kbd', {}, '?'),
                ),
                docInput,
            ].filter((c): c is HTMLElement => !!c),
        );
    }

    projects.on(render);
    startFlow.on(render);
    // Features register their commands (Vendors, Menus, the importer) after the start page exists.
    let again = false;
    onRegistryChange(() => {
        if (again) return;
        again = true;
        queueMicrotask(() => {
            again = false;
            render();
        });
    });
    // Esc over an open level goes back to it (or back from the level picker to the templates).
    el.addEventListener('keydown', (e) => {
        if (e.key !== 'Escape' || e.defaultPrevented) return;
        if (picking) cancel();
        else if (levelOpen() && a.close) a.close();
        else return;
        e.preventDefault();
    });
    render();
    return { el, refresh, show: render };
}
