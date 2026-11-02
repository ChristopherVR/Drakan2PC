/**
 * Project Settings: three docked panes in the editor's (UE5-like) chrome.
 *
 *   Mod details   property grid (name, id, version, author, description), Save / Open / New, the project folder line
 *   Levels        an Outliner-style tree table in campaign order: #, level, type, changes, exits, start, row actions;
 *                 each level expands into its exits (destination, status, "Change destination"); drag rows to reorder;
 *                 "+ Add level" opens a searchable list of the disc levels (add, or add a copy). List | Map toggle.
 *   Checks        a Message Log: grouped by level, severity icon, one short line each (details on expand), counts and
 *                 severity filters in the header, click a message to select the level or exit it is about.
 *   Export and play
 *
 * The wording and grouping of the checks is pure (checks.ts); this file only draws and wires them.
 */
import {
    ModError,
    MOD_ID_PATTERN,
    addDiscLevel,
    addLevelCopy,
    exitDestinationText,
    hasLevel,
    mapStart,
    isPortalWord,
    levelExits,
    moveLevel,
    removeLevel,
    routeOrder,
    suggestCopyTitle,
    validateProject,
    type CampaignExit,
    type ExitIndex,
    type ModProject,
    type ProjectIssue,
    type ProjectLevel,
} from '@drakan/editor-core';
import type { LevelInfo } from '../../rpc.ts';
import type { ExitIndexData } from '../../logicdata.ts';
import type { Ctx, Panel } from '../ctx.ts';
import { h, icon, store } from '../dom.ts';
import { createPlayControls } from '../playopts.ts';
import { openPopover, closePopover } from '../popover.ts';
import { runCommand } from '../registry.ts';
import { contextMenu, searchField, type MenuItem } from '../ui.ts';
import { isDesktop, revealFolder } from '../../platform.ts';
import type { PlayOptions } from '../../launch.ts';
import { createCampaignMap } from './campaignmap.ts';
import {
    SEVERITIES,
    countIssues,
    describeExitProblem,
    exitSummary,
    groupIssues,
    issueKey,
    renameProject,
    reorder,
    worstSeverity,
    type Severity,
    type SeverityFilter,
} from './checks.ts';
import { buildProjectPack, levelEditCounts, type ProjectExportDeps } from './export.ts';
import type { ProjectFiles } from './folder.ts';
import { projects } from './store.ts';

export interface ProjectPanelHost {
    ctx: Ctx;
    levels(): LevelInfo[];
    loadExits(): Promise<ExitIndexData>;
    exportDeps: ProjectExportDeps;
    /** Open a project level in the editor (a copy opens its source under the copy's title). */
    openLevel(l: ProjectLevel): Promise<void>;
    /** Project file operations (the folder on disk on desktop, download/upload in a browser). */
    files: ProjectFiles;
    /** Open the Export dialog. */
    openExport(): void;
    /** Recent project folders (desktop; empty in a browser). */
    recentProjects(): Promise<string[]>;
    /** Desktop: write the pack and start the game; returns a summary or null when cancelled. */
    play(
        pack: { meta: { id: string }; files: Record<string, Uint8Array> } | null,
        level: string | null,
        opts: PlayOptions,
    ): Promise<string | null>;
    setStatus(msg: string, error?: boolean): void;
    openGuide(id: string): void;
    /** Extra pane at the top of the side column (Project Settings > Campaign, campaign.ts). */
    sidePane?(p: ModProject): HTMLElement | null;
}

export interface ProjectPanel extends Panel {
    /** Latest campaign issues (exits, titles) for the Validation tab. */
    issues(): ProjectIssue[];
    onIssues(fn: () => void): void;
    /** Open the "add a copy" list. */
    addCopy(): void;
    /** Play the whole project, starting in `level` (default: the start level). */
    play(level?: string): Promise<void>;
    refreshSoon(): void;
    resetExits(): void;
    /** The exits of every level were read, so `issues()` is complete. */
    exitsLoaded(): boolean;
}

// Icons the shared set does not have; drawn with the same stroke as dom.ts icons.
const EXTRA_ICONS: Record<string, string> = {
    grip: '<path d="M9 6h.01M15 6h.01M9 12h.01M15 12h.01M9 18h.01M15 18h.01" stroke-width="3.2"/>',
    up: '<path d="M12 19V5M6 11l6-6 6 6"/>',
    down: '<path d="M12 5v14M6 13l6 6 6-6"/>',
    flag: '<path d="M6 21V4M6 4h11l-2.5 4.5L17 13H6"/>',
    route: '<circle cx="6" cy="18" r="2"/><circle cx="18" cy="6" r="2"/><path d="M8 18h6a3 3 0 0 0 0-6h-4a3 3 0 0 1 0-6h6"/>',
    'arrow-right': '<path d="M5 12h14M13 6l6 6-6 6"/>',
};
function ico(name: string, size = 14): SVGElement {
    const s = icon(name, size);
    const extra = EXTRA_ICONS[name];
    if (extra) s.innerHTML = extra;
    return s;
}

const SEV_ICON: Record<Severity, string> = { error: 'clear', warning: 'warn', info: 'info' };
const SEV_NAME: Record<Severity, [string, string]> = {
    error: ['error', 'errors'],
    warning: ['warning', 'warnings'],
    info: ['note', 'notes'],
};
const sevIcon = (s: Severity, size = 13): SVGElement => {
    const el = ico(SEV_ICON[s], size);
    el.classList.add('pjx-sev', s);
    return el;
};
const plural = (n: number, [one, many]: [string, string]): string => `${n} ${n === 1 ? one : many}`;

const FILTER_KEY = 'project-checks-filter';
const VIEW_KEY = 'project-levels-view';

export function createProjectPanel(host: ProjectPanelHost): ProjectPanel {
    let exits: ExitIndex | null = null;
    let exitsLoading = false;
    let exitsError = '';
    let issues: ProjectIssue[] = [];
    let counts = new Map<string, number>();
    const openExits = new Set<string>();
    let selected: string | null = null;
    let selectedExit: number | null = null;
    const collapsedGroups = new Set<string>();
    const openMsgs = new Set<string>();
    const saved = store.json<Partial<SeverityFilter>>(FILTER_KEY, {});
    const filter: SeverityFilter = {
        error: saved.error !== false,
        warning: saved.warning !== false,
        info: saved.info !== false,
    };
    let timer = 0;
    const issueListeners = new Set<() => void>();
    const report = h('pre', { class: 'pj-report', hidden: true });

    const root = h('div', { class: 'panel project' });
    const fail = (e: unknown): void => {
        const msg = e instanceof Error ? e.message : String(e);
        report.hidden = false;
        report.textContent = msg;
        host.setStatus(msg, true);
        host.ctx.toast(msg, 'error');
    };

    const discTitles = (): string[] =>
        host
            .levels()
            .filter((l) => l.present)
            .map((l) => l.name);

    const openLevel = (l: ProjectLevel): void => void host.openLevel(l).catch(fail);
    const openExit = (l: ProjectLevel, id: number): void =>
        void host
            .openLevel(l)
            .then(() => {
                host.ctx.locate([id]);
                runCommand('panel.logic');
            })
            .catch(fail);

    /** A docked pane: a header bar (title, count, tools) over its body. */
    const pane = (
        cls: string,
        title: string,
        tools: (Node | null)[],
        body: (Node | null)[],
        extra: { count?: string; id?: string } = {},
    ): HTMLElement =>
        h(
            'section',
            { class: `pjx-pane ${cls}`, id: extra.id },
            h(
                'header',
                { class: 'pjx-pane-head' },
                h('h2', {}, title),
                extra.count ? h('span', { class: 'pjx-count' }, extra.count) : null,
                h('span', { class: 'spacer' }),
                ...tools,
            ),
            h('div', { class: 'pjx-pane-body' }, ...body),
        );

    // ---- details ----------------------------------------------------------------------------------------------------

    const prop = (
        label: string,
        value: string,
        on: (v: string) => void,
        o: { area?: boolean; placeholder?: string; id: string; bad?: string; hint?: string },
    ): HTMLElement => {
        const el = o.area
            ? h('textarea', { rows: 3, spellcheck: 'false', placeholder: o.placeholder ?? '', id: o.id })
            : h('input', { type: 'text', spellcheck: 'false', placeholder: o.placeholder ?? '', id: o.id });
        el.value = value;
        el.classList.add('ui-input');
        if (o.bad) {
            el.classList.add('invalid');
            el.setAttribute('aria-invalid', 'true');
        }
        el.addEventListener('input', () => on(el.value));
        const msg = o.bad ?? o.hint;
        return h(
            'div',
            { class: `pjx-prop${o.area ? ' area' : ''}` },
            h('label', { for: o.id }, label),
            h(
                'div',
                { class: 'pjx-prop-v' },
                el,
                msg ? h('div', { class: `pjx-prop-hint${o.bad ? ' bad' : ''}` }, msg) : null,
            ),
        );
    };

    function folderLine(): HTMLElement {
        const f = projects.folder;
        if (!isDesktop) {
            return h(
                'div',
                { class: 'pjx-folder', id: 'pj-folder' },
                ico('info', 13),
                h(
                    'span',
                    { class: 'pjx-folder-text', id: 'pj-where' },
                    'Kept in one project file: Save downloads it, Open brings it back.',
                ),
            );
        }
        if (!f) {
            return h(
                'div',
                { class: 'pjx-folder', id: 'pj-folder' },
                ico('folder', 13),
                h('span', { class: 'pjx-folder-text dim', id: 'pj-where' }, 'Not saved to a folder yet'),
            );
        }
        const status = f.error
            ? h('span', { class: 'pjx-folder-sub bad' }, `Saving failed: ${f.error}`)
            : f.savedAt
              ? h('span', { class: 'pjx-folder-sub' }, `Saved at ${new Date(f.savedAt).toLocaleTimeString()}`)
              : null;
        return h(
            'div',
            { class: 'pjx-folder', id: 'pj-folder' },
            ico('folder', 13),
            h(
                'div',
                { class: 'pjx-folder-text' },
                h(
                    'span',
                    {
                        class: 'pjx-path',
                        id: 'pj-where',
                        title: `${f.dir}\nYour level edits and the exported mod are kept here.`,
                    },
                    f.dir,
                ),
                status,
                h(
                    'button',
                    {
                        class: 'ui-btn ghost sm pjx-reveal',
                        type: 'button',
                        id: 'pj-reveal',
                        title: 'Open the project folder in Explorer',
                        onclick: () => void revealFolder(f.dir).catch(fail),
                    },
                    ico('folder-open', 13),
                    'Show in folder',
                ),
            ),
            h(
                'button',
                {
                    class: 'ui-tool sm',
                    type: 'button',
                    id: 'pj-copy-path',
                    title: 'Copy the folder path',
                    'aria-label': 'Copy the folder path',
                    onclick: () =>
                        void navigator.clipboard
                            .writeText(f.dir)
                            .then(() => host.ctx.toast('Copied the folder path', 'ok'), fail),
                },
                ico('copy', 13),
            ),
        );
    }

    function detailsPane(p: ModProject): HTMLElement {
        const file = h('input', { type: 'file', accept: '.json,.drakan-project.json', hidden: true });
        file.addEventListener('change', () => {
            const f = file.files?.[0];
            file.value = '';
            if (!f) return;
            void f.text().then((t) => host.files.openText(t), fail);
        });
        const tool = (id: string, ic: string, label: string, title: string, run: () => void): HTMLElement =>
            h('button', { class: 'ui-btn ghost sm', type: 'button', id, title, onclick: run }, ico(ic, 13), label);
        const versionBad = p.version && !/^\d+(\.\d+)*$/.test(p.version) ? 'Use numbers like 1.0.0' : undefined;
        return pane(
            'pjx-details',
            'Mod details',
            [],
            [
                h(
                    'div',
                    { class: 'pjx-toolbar' },
                    tool(
                        'pj-save',
                        'save',
                        'Save',
                        isDesktop
                            ? 'Save the project and its level edits into the project folder'
                            : 'Download the project (settings, level list and level edits) as one file',
                        () => void host.files.save().catch(fail),
                    ),
                    tool(
                        'pj-open',
                        'open',
                        'Open...',
                        isDesktop ? 'Open a project folder' : 'Open a project file',
                        () => (isDesktop ? void host.files.openFolder().catch(fail) : file.click()),
                    ),
                    tool('pj-new', 'plus', 'New...', 'Close this project and start another', () => {
                        if (
                            !projects.folder &&
                            p.levels.length &&
                            !window.confirm('Close this project? Save it first to keep it.')
                        )
                            return;
                        void host.files.close().catch(fail);
                    }),
                    file,
                ),
                h(
                    'div',
                    { class: 'pjx-props' },
                    prop(
                        'Name',
                        p.name,
                        (v) => {
                            projects.update((q) => renameProject(q, v));
                            // Show the followed Id now; the pane itself re-renders a moment later.
                            const id = root.querySelector<HTMLInputElement>('#pj-id');
                            const q = projects.get();
                            if (id && q && document.activeElement !== id) id.value = q.id;
                        },
                        { id: 'pj-name', placeholder: 'My mod' },
                    ),
                    prop('Id', p.id, (v) => projects.update((q) => ({ ...q, id: v.trim() })), {
                        id: 'pj-id',
                        placeholder: 'my-mod',
                        bad: MOD_ID_PATTERN.test(p.id) ? undefined : 'Letters, digits and . _ - only',
                        hint: MOD_ID_PATTERN.test(p.id) ? 'Names the pack and its folder' : undefined,
                    }),
                    prop('Version', p.version, (v) => projects.update((q) => ({ ...q, version: v.trim() })), {
                        id: 'pj-version',
                        placeholder: '1.0.0',
                        bad: versionBad,
                    }),
                    prop('Author', p.author, (v) => projects.update((q) => ({ ...q, author: v })), {
                        id: 'pj-author',
                        placeholder: 'Your name',
                    }),
                    prop('Description', p.description, (v) => projects.update((q) => ({ ...q, description: v })), {
                        id: 'pj-desc',
                        area: true,
                        placeholder: 'What does it change?',
                    }),
                ),
                h('div', { class: 'pjx-sub' }, isDesktop ? 'Project folder' : 'Project file'),
                folderLine(),
            ],
        );
    }

    // ---- levels: add menu ---------------------------------------------------------------------------------------------

    function addCopyNow(p: ModProject, source: string, title: string): boolean {
        try {
            const t = title.trim() || suggestCopyTitle(p, source, discTitles());
            projects.set(addLevelCopy(p, source, t));
            selected = t;
            host.ctx.toast(`Added a copy: ${t}`, 'ok');
            return true;
        } catch (e) {
            fail(e);
            return false;
        }
    }

    /** The searchable list of disc levels: click to add one, the copy button to add a copy under a new title. */
    function openAddMenu(anchor: HTMLElement, mode: 'add' | 'copy' = 'add'): void {
        const pop = openPopover(
            anchor,
            (close) => {
                const body = h('div', { class: 'pjx-addpop' });
                const search = searchField('Find a level');
                search.input.id = 'pj-add-search';
                const list = h('div', { class: 'pjx-add-list', role: 'list' });
                let copyFrom: string | null = null;

                const copyForm = (source: string): HTMLElement => {
                    const p = projects.get();
                    const input = h('input', {
                        type: 'text',
                        class: 'ui-input',
                        id: 'pj-copy-title',
                        spellcheck: 'false',
                        placeholder: p ? suggestCopyTitle(p, source, discTitles()) : '',
                    });
                    const submit = (): void => {
                        const q = projects.get();
                        if (q && addCopyNow(q, source, input.value)) close();
                    };
                    input.addEventListener('keydown', (e) => {
                        if (e.key === 'Enter') submit();
                    });
                    queueMicrotask(() => input.focus());
                    return h(
                        'div',
                        { class: 'pjx-copyform' },
                        h('div', { class: 'pjx-pop-title' }, 'New level from a copy'),
                        h(
                            'p',
                            { class: 'pjx-pop-note' },
                            `Starts as ${source}, with its exits. Edit it into a level of its own.`,
                        ),
                        h('label', { class: 'pjx-pop-label', for: 'pj-copy-title' }, 'Title of the new level'),
                        input,
                        h(
                            'div',
                            { class: 'pjx-pop-btns' },
                            h(
                                'button',
                                {
                                    class: 'ui-btn sm',
                                    type: 'button',
                                    onclick: () => {
                                        copyFrom = null;
                                        draw();
                                    },
                                },
                                'Back',
                            ),
                            h(
                                'button',
                                { class: 'ui-btn primary sm', type: 'button', id: 'pj-copy', onclick: submit },
                                'Add copy',
                            ),
                        ),
                    );
                };

                const draw = (): void => {
                    if (copyFrom) {
                        body.replaceChildren(copyForm(copyFrom));
                        return;
                    }
                    const p = projects.get();
                    const q = search.input.value.trim().toLowerCase();
                    const names = discTitles().filter((n) => !q || n.toLowerCase().includes(q));
                    list.replaceChildren(
                        ...(names.length
                            ? names.map((n) => {
                                  const inProject = !!p && hasLevel(p, n);
                                  return h(
                                      'div',
                                      { class: 'pjx-add-row', role: 'listitem' },
                                      h(
                                          'button',
                                          {
                                              class: 'pjx-add-main',
                                              type: 'button',
                                              disabled: mode === 'add' && inProject,
                                              title:
                                                  mode === 'copy'
                                                      ? `Add a new level that starts as ${n}`
                                                      : inProject
                                                        ? 'Already in the project'
                                                        : `Add ${n} to the project`,
                                              onclick: () => {
                                                  if (mode === 'copy') {
                                                      copyFrom = n;
                                                      draw();
                                                      return;
                                                  }
                                                  const cur = projects.get();
                                                  if (!cur) return;
                                                  projects.set(addDiscLevel(cur, n));
                                                  selected = n;
                                                  host.setStatus(`Added ${n}`);
                                                  draw();
                                              },
                                          },
                                          ico(mode === 'copy' ? 'copy' : 'world', 13),
                                          h('span', { class: 'nm' }, n),
                                          inProject ? h('span', { class: 'tag' }, 'In project') : null,
                                      ),
                                      mode === 'add'
                                          ? h(
                                                'button',
                                                {
                                                    class: 'ui-tool sm pjx-add-copy',
                                                    type: 'button',
                                                    title: 'Add a copy: a new level that starts as this one',
                                                    'aria-label': `Add a copy of ${n}`,
                                                    onclick: () => {
                                                        copyFrom = n;
                                                        draw();
                                                    },
                                                },
                                                ico('copy', 13),
                                            )
                                          : null,
                                  );
                              })
                            : [h('div', { class: 'pjx-add-none' }, 'No level matches.')]),
                    );
                    if (body.firstChild !== search.el) {
                        body.replaceChildren(
                            h(
                                'div',
                                { class: 'pjx-pop-title' },
                                mode === 'copy' ? 'Copy a level' : 'Add a level from the disc',
                            ),
                            search.el,
                            list,
                            h(
                                'p',
                                { class: 'pjx-pop-note' },
                                mode === 'copy'
                                    ? 'Pick the level the new one starts as.'
                                    : 'Click a level to add it. The copy button adds a new level that starts as that one.',
                            ),
                        );
                        queueMicrotask(() => search.input.focus());
                    }
                };
                search.input.addEventListener('input', draw);
                search.input.addEventListener('keydown', (e) => {
                    if (e.key === 'Enter')
                        list.querySelector<HTMLButtonElement>('.pjx-add-main:not(:disabled)')?.click();
                });
                draw();
                return body;
            },
            { className: 'pjx-pop' },
        );
        // Drop down from the button's left edge, like a menu.
        const r = anchor.getBoundingClientRect();
        pop.style.left = `${Math.max(8, Math.min(window.innerWidth - pop.offsetWidth - 8, r.left))}px`;
    }

    function addButton(connected: boolean): HTMLElement {
        const main = h(
            'button',
            {
                class: 'ui-btn primary sm',
                type: 'button',
                id: 'pj-add',
                disabled: !connected,
                title: connected ? 'Add a level from the disc, or a copy of one' : 'Open your disc image first',
                onclick: () => openAddMenu(main),
            },
            ico('plus', 13),
            'Add level',
        );
        const more = h(
            'button',
            {
                class: 'ui-btn primary sm pjx-split-more',
                type: 'button',
                id: 'pj-add-more',
                disabled: !connected,
                title: 'More ways to add',
                'aria-label': 'More ways to add a level',
                onclick: () => {
                    const r = more.getBoundingClientRect();
                    contextMenu(r.left, r.bottom + 2, [
                        { label: 'Add a level from the disc...', icon: 'world', run: () => openAddMenu(main) },
                        { label: 'Add a copy of a level...', icon: 'copy', run: () => openAddMenu(main, 'copy') },
                    ]);
                },
            },
            ico('chevron-down', 12),
        );
        return h('div', { class: 'pjx-split' }, main, more);
    }

    // ---- levels: table ----------------------------------------------------------------------------------------------

    // The levels as a list or as the Campaign map (project/campaignmap.ts); the choice is remembered.
    let view: 'list' | 'map' = store.get(VIEW_KEY) === 'map' ? 'map' : 'list';
    const map = createCampaignMap({
        openLevel,
        openExit,
        play: isDesktop ? (t) => void play(t).catch(fail) : null,
        setLayout: (layout) => projects.update((q) => ({ ...q, mapLayout: layout })),
    });
    const viewToggle = (): HTMLElement =>
        h(
            'div',
            { class: 'ui-seg', role: 'group', 'aria-label': 'Show the levels as' },
            ...(['list', 'map'] as const).map((v) =>
                h(
                    'button',
                    {
                        class: `ui-seg-btn${view === v ? ' active' : ''}`,
                        type: 'button',
                        id: `pj-view-${v}`,
                        'aria-pressed': String(view === v),
                        title: v === 'list' ? 'Show the levels as a list' : 'Show the levels and their exits as a map',
                        onclick: () => {
                            view = v;
                            store.set(VIEW_KEY, v);
                            render();
                        },
                    },
                    icon(v === 'list' ? 'list' : 'link', 12),
                    v === 'list' ? 'List' : 'Map',
                ),
            ),
        );

    const act = (
        name: string,
        tip: string,
        run: () => void,
        o: { disabled?: boolean; cls?: string } = {},
    ): HTMLButtonElement =>
        h(
            'button',
            {
                class: `ui-tool sm pjx-act${o.cls ? ` ${o.cls}` : ''}`,
                type: 'button',
                title: tip,
                'aria-label': tip,
                disabled: o.disabled,
                tabindex: '-1',
                onclick: (e: Event) => {
                    e.stopPropagation();
                    run();
                },
            },
            ico(name, 13),
        );

    function levelMenu(p: ModProject, l: ProjectLevel, i: number): MenuItem[] {
        return [
            { label: 'Open', icon: 'external', run: () => openLevel(l) },
            ...(isDesktop
                ? [{ label: 'Play from here', icon: 'play', run: () => void play(l.title).catch(fail) }]
                : []),
            {
                label: 'Start here',
                icon: 'check',
                disabled: p.start === l.title,
                run: () => projects.set({ ...p, start: l.title }),
            },
            { sep: true },
            { label: 'Move up', hint: 'Alt+Up', disabled: i === 0, run: () => projects.set(moveLevel(p, l.title, -1)) },
            {
                label: 'Move down',
                hint: 'Alt+Down',
                disabled: i === p.levels.length - 1,
                run: () => projects.set(moveLevel(p, l.title, 1)),
            },
            { sep: true },
            { label: 'Remove from the project', icon: 'trash', danger: true, run: () => removeNow(p, l) },
        ];
    }

    function removeNow(p: ModProject, l: ProjectLevel): void {
        projects.set(removeLevel(p, l.title));
        if (selected === l.title) selected = null;
        host.ctx.toast(`Removed ${l.title} from the project. Its edits are kept.`, 'ok');
    }

    function exitRow(l: ProjectLevel, e: CampaignExit): HTMLElement {
        const d = e.problem ? describeExitProblem(l.title, { name: e.name, dest: e.dest, problem: e.problem }) : null;
        const isSel = selected === l.title && selectedExit === e.id;
        const label = e.name || `Exit #${e.id}`;
        const change = e.portal ? 'Show exit' : 'Change destination';
        const row = h(
            'div',
            {
                class: `pjx-row pjx-exit${e.problem ? ' bad' : ''}${isSel ? ' selected' : ''}`,
                role: 'row',
                'aria-level': '2',
                'aria-selected': String(isSel),
                'data-level': l.title,
                'data-exit': String(e.id),
            },
            h('div', { class: 'pjx-c pjx-c-idx' }),
            h(
                'div',
                { class: 'pjx-c pjx-c-name' },
                h('span', { class: 'pjx-tree' }),
                e.problem ? sevIcon('warning', 12) : ico('check-circle', 12),
                h('span', { class: 'pjx-dest' }, exitDestinationText(e)),
                e.portal && !isPortalWord(e.dest) ? h('span', { class: 'pjx-tag' }, 'portal') : null,
                !e.inProject && e.dest
                    ? h(
                          'span',
                          { class: 'pjx-tag dim', title: 'This destination is not one of the project levels' },
                          'outside project',
                      )
                    : null,
                h('span', { class: 'pjx-exit-name', title: label }, label),
            ),
            h(
                'div',
                { class: 'pjx-c pjx-c-status', title: d?.detail ?? 'The player arrives at the way back.' },
                d ? d.short : 'Arrives correctly',
            ),
            h(
                'div',
                { class: 'pjx-c pjx-c-act' },
                h(
                    'button',
                    {
                        class: 'ui-btn ghost sm pjx-change',
                        type: 'button',
                        tabindex: '-1',
                        title: 'Open the level, select this exit and show the Logic panel with its destination picker',
                        onclick: (ev: Event) => {
                            ev.stopPropagation();
                            openExit(l, e.id);
                        },
                    },
                    change,
                ),
            ),
        );
        row.addEventListener('click', () => select(l.title, e.id));
        row.addEventListener('dblclick', () => openExit(l, e.id));
        return row;
    }

    function levelRows(p: ModProject, l: ProjectLevel, i: number): HTMLElement[] {
        const edits = counts.get(l.title) ?? 0;
        const isStart = p.start === l.title;
        // Like the map: with no start picked, the first level is shown as the start.
        const shownStart = mapStart(p) === l.title;
        const list = exits ? levelExits(p, l, exits) : [];
        const sum = exitSummary(list);
        const open = openExits.has(l.title);
        const isSel = selected === l.title && selectedExit === null;
        const canExpand = !exits || list.length > 0;
        const toggle = (): void => {
            if (openExits.has(l.title)) openExits.delete(l.title);
            else openExits.add(l.title);
            render();
        };
        const row = h(
            'div',
            {
                class: `pjx-row pjx-level${isSel ? ' selected' : ''}`,
                role: 'row',
                'aria-level': '1',
                'aria-expanded': canExpand ? String(open) : undefined,
                'aria-selected': String(isSel),
                'data-level': l.title,
                'data-index': String(i),
                draggable: 'true',
            },
            h(
                'div',
                { class: 'pjx-c pjx-c-idx', title: 'Drag to reorder' },
                h('span', { class: 'num' }, String(i + 1)),
                h('span', { class: 'grip' }, ico('grip', 13)),
            ),
            h(
                'div',
                { class: 'pjx-c pjx-c-name' },
                h(
                    'button',
                    {
                        class: `pjx-twist${open ? ' open' : ''}`,
                        type: 'button',
                        tabindex: '-1',
                        hidden: !canExpand,
                        'aria-label': open ? 'Hide the exits' : 'Show the exits',
                        onclick: (e: Event) => {
                            e.stopPropagation();
                            toggle();
                        },
                    },
                    ico('chevron', 12),
                ),
                h('span', { class: `pjx-kind${l.copyOf ? ' copy' : ''}` }, ico(l.copyOf ? 'copy' : 'world', 13)),
                h('span', { class: 'pjx-name', title: l.title }, l.title),
            ),
            h(
                'div',
                {
                    class: 'pjx-c pjx-c-type',
                    title: l.copyOf ? `A new level that starts as ${l.copyOf}` : 'A level of the disc',
                },
                l.copyOf ? 'Copy' : 'Disc',
            ),
            h(
                'div',
                {
                    class: `pjx-c pjx-c-num${edits ? ' has' : ''}`,
                    title: edits ? `${edits} change${edits === 1 ? '' : 's'} to this level` : 'Not changed yet',
                },
                edits ? String(edits) : '0',
            ),
            h(
                'div',
                { class: 'pjx-c pjx-c-exits', title: exits ? sum.text : 'The exits are read from the disc' },
                exits
                    ? h(
                          'span',
                          { class: 'pjx-exits-v' },
                          h('span', { class: 'n' }, String(sum.count)),
                          sum.problems
                              ? h('span', { class: 'fix' }, sevIcon('warning', 12), `${sum.problems} to fix`)
                              : sum.count
                                ? h('span', { class: 'ok' }, ico('check', 12))
                                : null,
                      )
                    : h('span', { class: 'dim' }, exitsLoading ? '...' : '-'),
            ),
            h(
                'div',
                { class: 'pjx-c pjx-c-start' },
                h(
                    'button',
                    {
                        class: `pjx-start${shownStart ? ' on' : ''}${isStart ? '' : ' implied'}`,
                        type: 'button',
                        tabindex: '-1',
                        'aria-pressed': String(isStart),
                        title: isStart
                            ? 'Play starts in this level'
                            : shownStart
                              ? 'The first level is the start until you pick one. Click to make it the start.'
                              : 'Start here: make Play start in this level',
                        'aria-label': shownStart ? `Play starts in ${l.title}` : `Start in ${l.title}`,
                        onclick: (e: Event) => {
                            e.stopPropagation();
                            if (!isStart) projects.set({ ...p, start: l.title });
                        },
                    },
                    ico('flag', 13),
                ),
            ),
            h(
                'div',
                { class: 'pjx-c pjx-c-act' },
                act('external', 'Open in the editor', () => openLevel(l)),
                isDesktop
                    ? act('play', 'Play the whole project from this level', () => void play(l.title).catch(fail), {
                          cls: 'play',
                      })
                    : null,
                act('up', 'Move up', () => projects.set(moveLevel(p, l.title, -1)), { disabled: i === 0 }),
                act('down', 'Move down', () => projects.set(moveLevel(p, l.title, 1)), {
                    disabled: i === p.levels.length - 1,
                }),
                act('trash', 'Remove from the project (its edits are kept)', () => removeNow(p, l), { cls: 'danger' }),
            ),
        );
        row.addEventListener('click', () => select(l.title, null));
        row.addEventListener('dblclick', (e) => {
            if ((e.target as HTMLElement).closest('button')) return;
            openLevel(l);
        });
        row.addEventListener('contextmenu', (e) => {
            e.preventDefault();
            select(l.title, null);
            contextMenu(e.clientX, e.clientY, levelMenu(p, l, i));
        });
        const out: HTMLElement[] = [row];
        if (open) {
            if (list.length) out.push(...list.map((e) => exitRow(l, e)));
            else
                out.push(
                    h(
                        'div',
                        { class: 'pjx-row pjx-exit pjx-exit-note', role: 'row', 'aria-level': '2' },
                        h('div', { class: 'pjx-c pjx-c-idx' }),
                        h(
                            'div',
                            { class: 'pjx-c pjx-c-wide' },
                            h('span', { class: 'pjx-tree' }),
                            exits
                                ? 'This level has no exits.'
                                : exitsLoading
                                  ? 'Reading the exits of every level...'
                                  : exitsError || 'The exits are read from the disc.',
                        ),
                    ),
                );
        }
        return out;
    }

    /** Select a level (exitId null) or one of its exits without rebuilding the table (keeps double-click working). */
    function select(level: string | null, exitId: number | null): void {
        selected = level;
        selectedExit = exitId;
        for (const r of root.querySelectorAll<HTMLElement>('.pjx-row[data-level]')) {
            const on =
                r.dataset.level === level &&
                (exitId === null ? r.dataset.exit === undefined : r.dataset.exit === String(exitId));
            r.classList.toggle('selected', on);
            r.setAttribute('aria-selected', String(on));
        }
    }

    function levelsTable(p: ModProject, connected: boolean): HTMLElement {
        if (!p.levels.length) {
            const add = h(
                'button',
                {
                    class: 'ui-btn primary',
                    type: 'button',
                    id: 'pj-add-empty',
                    disabled: !connected,
                    onclick: () => openAddMenu(add),
                },
                ico('plus', 14),
                'Add a level',
            );
            return h(
                'div',
                { class: 'pjx-empty' },
                ico('layers', 28),
                h('strong', {}, 'No levels yet'),
                h(
                    'span',
                    {},
                    connected
                        ? 'Add the levels your mod changes, in the order a player reaches them. A level you edit with "Edit a campaign level" joins on its own.'
                        : 'Open your disc image to add its levels.',
                ),
                connected
                    ? add
                    : h(
                          'button',
                          { class: 'ui-btn primary', type: 'button', onclick: () => runCommand('file.openIso') },
                          ico('open', 14),
                          'Open disc image...',
                      ),
            );
        }
        const body = h('div', { class: 'pjx-tbody', role: 'rowgroup' });
        p.levels.forEach((l, i) => body.append(...levelRows(p, l, i)));
        const table = h(
            'div',
            {
                class: 'pjx-table',
                id: 'pj-levels',
                role: 'treegrid',
                'aria-label': 'Levels in campaign order',
                tabindex: '0',
            },
            h(
                'div',
                { class: 'pjx-row pjx-thead', role: 'row' },
                h('div', { class: 'pjx-c pjx-c-idx', role: 'columnheader' }, '#'),
                h('div', { class: 'pjx-c pjx-c-name', role: 'columnheader' }, 'Level'),
                h('div', { class: 'pjx-c pjx-c-type', role: 'columnheader' }, 'Type'),
                h('div', { class: 'pjx-c pjx-c-num', role: 'columnheader' }, 'Changes'),
                h('div', { class: 'pjx-c pjx-c-exits', role: 'columnheader' }, 'Exits'),
                h('div', { class: 'pjx-c pjx-c-start', role: 'columnheader', title: 'Where Play starts' }, 'Start'),
                h('div', { class: 'pjx-c pjx-c-act', role: 'columnheader' }),
            ),
            body,
        );
        wireDrag(body, p);
        wireKeys(table, p);
        return table;
    }

    function wireDrag(body: HTMLElement, p: ModProject): void {
        let from = -1;
        let mark: HTMLElement | null = null;
        const clear = (): void => {
            mark?.classList.remove('drop-before', 'drop-after');
            mark = null;
        };
        const target = (e: DragEvent): { row: HTMLElement; to: number; after: boolean } | null => {
            const el = (e.target as HTMLElement).closest<HTMLElement>('.pjx-row[data-level]');
            if (!el) return null;
            const row = el.classList.contains('pjx-level')
                ? el
                : body.querySelector<HTMLElement>(`.pjx-level[data-level="${CSS.escape(el.dataset.level!)}"]`);
            if (!row) return null;
            const i = Number(row.dataset.index);
            const r = el.getBoundingClientRect();
            const after = el !== row || e.clientY > r.top + r.height / 2;
            return { row, to: after ? i + 1 : i, after };
        };
        body.addEventListener('dragstart', (e) => {
            const row = (e.target as HTMLElement).closest<HTMLElement>('.pjx-level');
            if (!row) return;
            from = Number(row.dataset.index);
            row.classList.add('dragging');
            e.dataTransfer?.setData('text/plain', row.dataset.level ?? '');
            if (e.dataTransfer) e.dataTransfer.effectAllowed = 'move';
        });
        body.addEventListener('dragover', (e) => {
            if (from < 0) return;
            const t = target(e);
            if (!t) return;
            e.preventDefault();
            if (e.dataTransfer) e.dataTransfer.dropEffect = 'move';
            if (mark !== t.row) clear();
            mark = t.row;
            t.row.classList.toggle('drop-after', t.after);
            t.row.classList.toggle('drop-before', !t.after);
        });
        body.addEventListener('dragleave', (e) => {
            if (!body.contains(e.relatedTarget as Node | null)) clear();
        });
        body.addEventListener('drop', (e) => {
            const t = target(e);
            clear();
            if (from < 0 || !t) return;
            e.preventDefault();
            const levels = reorder(p.levels, from, t.to);
            from = -1;
            if (levels.some((l, n) => l !== p.levels[n])) projects.set({ ...p, levels });
        });
        body.addEventListener('dragend', () => {
            from = -1;
            clear();
            body.querySelector('.dragging')?.classList.remove('dragging');
        });
    }

    function wireKeys(table: HTMLElement, p: ModProject): void {
        table.addEventListener('keydown', (e) => {
            const rows = [...table.querySelectorAll<HTMLElement>('.pjx-row[data-level]')];
            if (!rows.length) return;
            let at = rows.findIndex((r) => r.classList.contains('selected'));
            const cur = at >= 0 ? rows[at]! : null;
            const level = cur ? p.levels.find((l) => l.title === cur.dataset.level) : undefined;
            const exitId = cur?.dataset.exit !== undefined ? Number(cur!.dataset.exit) : null;
            const go = (n: number): void => {
                const r = rows[Math.max(0, Math.min(rows.length - 1, n))]!;
                select(r.dataset.level!, r.dataset.exit !== undefined ? Number(r.dataset.exit) : null);
                r.scrollIntoView({ block: 'nearest' });
            };
            if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
                e.preventDefault();
                const d = e.key === 'ArrowDown' ? 1 : -1;
                if (e.altKey && level && exitId === null) {
                    projects.set(moveLevel(p, level.title, d as 1 | -1));
                    return;
                }
                if (at < 0) at = d > 0 ? -1 : rows.length;
                go(at + d);
            } else if (e.key === 'Home' || e.key === 'End') {
                e.preventDefault();
                go(e.key === 'Home' ? 0 : rows.length - 1);
            } else if (e.key === 'Enter' && level) {
                e.preventDefault();
                if (exitId !== null) openExit(level, exitId);
                else openLevel(level);
            } else if ((e.key === 'ArrowRight' || e.key === 'ArrowLeft') && level) {
                e.preventDefault();
                const want = e.key === 'ArrowRight';
                if (exitId !== null && !want) {
                    selectedExit = null;
                } else if (openExits.has(level.title) !== want) {
                    if (want) openExits.add(level.title);
                    else openExits.delete(level.title);
                } else return;
                render();
            }
        });
    }

    function levelsPane(p: ModProject): HTMLElement {
        const connected = host.levels().length > 0;
        const note = exitsError
            ? h('div', { class: 'pjx-banner bad' }, ico('alert-circle', 13), exitsError)
            : exitsLoading
              ? h(
                    'div',
                    { class: 'pjx-banner' },
                    h('span', { class: 'pjx-spin' }),
                    'Reading the exits of every level...',
                )
              : null;
        return pane(
            'pjx-levels',
            'Levels',
            [viewToggle()],
            [
                h(
                    'div',
                    { class: 'pjx-toolbar' },
                    addButton(connected),
                    h(
                        'button',
                        {
                            class: 'ui-btn ghost sm',
                            type: 'button',
                            id: 'pj-route',
                            disabled: p.levels.length < 2 || !exits,
                            title: 'Sort the levels in the order a player reaches them from the start level',
                            onclick: () => exits && projects.set({ ...p, levels: routeOrder(p, exits) }),
                        },
                        ico('route', 13),
                        'Order by route',
                    ),
                    h('span', { class: 'spacer' }),
                    h(
                        'span',
                        { class: 'pjx-hint' },
                        view === 'list' && p.levels.length ? 'Campaign order. Drag to reorder.' : '',
                    ),
                ),
                note,
                view === 'map' && p.levels.length
                    ? map.update({
                          project: p,
                          exits,
                          issues,
                          counts,
                          note: exits
                              ? ''
                              : exitsLoading
                                ? 'Reading the exits of every level...'
                                : exitsError || (connected ? '' : 'Open your disc image to see the exits.'),
                      })
                    : levelsTable(p, connected),
            ],
            { count: p.levels.length ? String(p.levels.length) : undefined },
        );
    }

    // ---- checks (message log) -----------------------------------------------------------------------------------------

    function jump(i: ProjectIssue): void {
        if (!i.level) {
            const id = ({ id: 'pj-id', name: 'pj-name', version: 'pj-version' } as Record<string, string>)[i.code];
            const el = id ? root.querySelector<HTMLInputElement>(`#${id}`) : null;
            el?.focus();
            el?.select();
            return;
        }
        selected = i.level;
        selectedExit = i.exitId ?? null;
        if (i.exitId !== undefined) openExits.add(i.level);
        if (view === 'map') view = 'list';
        render();
        const sel =
            selectedExit !== null
                ? `.pjx-exit[data-level="${CSS.escape(i.level)}"][data-exit="${selectedExit}"]`
                : `.pjx-level[data-level="${CSS.escape(i.level)}"]`;
        const row = root.querySelector<HTMLElement>(sel);
        if (row) {
            row.scrollIntoView({ block: 'nearest' });
            row.classList.remove('flash');
            void row.offsetWidth;
            row.classList.add('flash');
        }
        root.querySelector<HTMLElement>('#pj-levels')?.focus({ preventScroll: true });
    }

    function checksPane(p: ModProject): HTMLElement {
        const all = countIssues(issues);
        const order = p.levels.map((l) => l.title);
        const exitOf = (i: ProjectIssue): CampaignExit | undefined => {
            if (!exits || i.exitId === undefined || !i.level) return undefined;
            const l = p.levels.find((x) => x.title === i.level);
            return l ? levelExits(p, l, exits).find((e) => e.id === i.exitId) : undefined;
        };
        const groups = groupIssues(issues, order, filter, exitOf);
        const filters = SEVERITIES.map((s) =>
            h(
                'button',
                {
                    class: `pjx-filt ${s}${filter[s] ? ' on' : ''}`,
                    type: 'button',
                    id: `pj-filter-${s}`,
                    'aria-pressed': String(filter[s]),
                    title: `${filter[s] ? 'Hide' : 'Show'} ${SEV_NAME[s][1]}`,
                    onclick: () => {
                        filter[s] = !filter[s];
                        store.set(FILTER_KEY, JSON.stringify(filter));
                        render();
                    },
                },
                sevIcon(s, 12),
                String(all[s]),
            ),
        );
        const total = all.error + all.warning + all.info;
        const msgRow = (m: (typeof groups)[number]['items'][number], level: ProjectLevel | undefined): HTMLElement => {
            const s = m.issue.severity;
            const open = openMsgs.has(m.key);
            const toggle = (e: Event): void => {
                e.stopPropagation();
                if (open) openMsgs.delete(m.key);
                else openMsgs.add(m.key);
                render();
            };
            const exitId = m.issue.exitId;
            return h(
                'div',
                { class: `pjx-msg ${s}${open ? ' open' : ''}` },
                h(
                    'div',
                    {
                        class: 'pjx-msg-line',
                        role: 'button',
                        tabindex: '0',
                        title: m.detail,
                        onclick: () => jump(m.issue),
                        onkeydown: (e: Event) => {
                            const k = (e as KeyboardEvent).key;
                            if (k === 'Enter' || k === ' ') {
                                e.preventDefault();
                                jump(m.issue);
                            } else if (k === 'ArrowRight' || k === 'ArrowLeft') {
                                if (open !== (k === 'ArrowRight')) toggle(e);
                            }
                        },
                    },
                    sevIcon(s, 13),
                    h('span', { class: 'pjx-msg-text' }, m.short),
                    h(
                        'button',
                        {
                            class: 'ui-tool sm pjx-msg-more',
                            type: 'button',
                            tabindex: '-1',
                            'aria-expanded': String(open),
                            title: open ? 'Hide the details' : 'Show the details',
                            'aria-label': open ? 'Hide the details' : 'Show the details',
                            onclick: toggle,
                        },
                        ico('chevron', 11),
                    ),
                ),
                open
                    ? h(
                          'div',
                          { class: 'pjx-msg-detail' },
                          h('p', {}, m.detail),
                          h(
                              'div',
                              { class: 'pjx-msg-acts' },
                              m.issue.level
                                  ? h(
                                        'button',
                                        { class: 'pj-link', type: 'button', onclick: () => jump(m.issue) },
                                        exitId !== undefined ? 'Show the exit in the list' : 'Show in the list',
                                    )
                                  : null,
                              level && exitId !== undefined
                                  ? h(
                                        'button',
                                        { class: 'pj-link', type: 'button', onclick: () => openExit(level, exitId) },
                                        'Change destination',
                                    )
                                  : null,
                              level && exitId === undefined
                                  ? h(
                                        'button',
                                        { class: 'pj-link', type: 'button', onclick: () => openLevel(level) },
                                        'Open the level',
                                    )
                                  : null,
                          ),
                      )
                    : null,
            );
        };
        const body: (Node | null)[] = [];
        if (!exits && p.levels.length && host.levels().length) {
            body.push(
                h(
                    'div',
                    { class: 'pjx-banner' },
                    exitsLoading ? h('span', { class: 'pjx-spin' }) : ico('info', 13),
                    exitsLoading ? 'Checking the exits...' : 'The exits are checked once they are read.',
                ),
            );
        }
        if (!total && exitsLoading) {
            // Nothing to say until the exits are checked.
        } else if (!total) {
            body.push(
                h(
                    'div',
                    { class: 'pjx-allgood' },
                    ico('check-circle', 16),
                    p.levels.length ? 'No problems found' : 'Nothing to check yet',
                ),
            );
        } else if (!groups.length) {
            body.push(h('div', { class: 'pjx-allgood dim' }, ico('filter', 14), 'All messages are filtered out'));
        } else {
            for (const g of groups) {
                const collapsed = collapsedGroups.has(g.key);
                const level = g.level ? p.levels.find((l) => l.title === g.level) : undefined;
                const worst = worstSeverity(g.counts);
                body.push(
                    h(
                        'div',
                        { class: `pjx-grp${collapsed ? ' collapsed' : ''}`, 'data-group': g.key },
                        h(
                            'button',
                            {
                                class: 'pjx-grp-head',
                                type: 'button',
                                'aria-expanded': String(!collapsed),
                                onclick: () => {
                                    if (collapsed) collapsedGroups.delete(g.key);
                                    else collapsedGroups.add(g.key);
                                    render();
                                },
                            },
                            ico('chevron', 11),
                            g.level ? ico('world', 12) : ico('sliders', 12),
                            h('span', { class: 'pjx-grp-name', title: g.label }, g.label),
                            h(
                                'span',
                                {
                                    class: `pjx-grp-n ${worst ?? ''}`,
                                    title: SEVERITIES.filter((s) => g.counts[s])
                                        .map((s) => plural(g.counts[s], SEV_NAME[s]))
                                        .join(', '),
                                },
                                String(g.items.length),
                            ),
                        ),
                        collapsed ? null : h('div', { class: 'pjx-grp-body' }, ...g.items.map((m) => msgRow(m, level))),
                    ),
                );
            }
        }
        const summary = total
            ? SEVERITIES.filter((s) => all[s])
                  .map((s) => plural(all[s], SEV_NAME[s]))
                  .join(', ')
            : '';
        return pane(
            'pjx-checks',
            'Checks',
            [h('div', { class: 'pjx-filters', role: 'group', 'aria-label': summary || 'No problems' }, ...filters)],
            body,
            { id: 'pj-checks' },
        );
    }

    // ---- export and play --------------------------------------------------------------------------------------------

    const playControls = createPlayControls(
        () => void play().catch(fail),
        () => true,
    );

    function exportPane(): HTMLElement {
        const errs = issues.filter((i) => i.severity === 'error').length;
        return pane(
            'pjx-export',
            isDesktop ? 'Export and play' : 'Export',
            [],
            [
                h(
                    'div',
                    { class: 'pjx-export-row' },
                    h(
                        'button',
                        {
                            class: 'ui-btn primary',
                            type: 'button',
                            id: 'pj-export',
                            disabled: errs > 0,
                            title: errs ? 'Fix the errors in Checks first' : 'Build the mod pack (Ctrl+E)',
                            onclick: () => host.openExport(),
                        },
                        icon('download', 13),
                        'Export...',
                    ),
                    errs
                        ? h(
                              'span',
                              { class: 'pjx-export-why bad' },
                              sevIcon('error', 12),
                              `Fix ${plural(errs, SEV_NAME.error)} first`,
                          )
                        : h('span', { class: 'pjx-export-why' }, 'Changed levels, copies and story edits in one pack'),
                ),
                isDesktop
                    ? h('div', { class: 'pj-play' }, playControls.el)
                    : h(
                          'p',
                          { class: 'pj-note' },
                          "To play: put the zip in the game's mods folder, turn it on in the game's Mods settings, restart the game. ",
                          h(
                              'button',
                              { class: 'pj-link', type: 'button', onclick: () => host.openGuide('package') },
                              'Show the steps',
                          ),
                      ),
                report,
            ],
        );
    }

    async function play(from?: string): Promise<void> {
        const p = projects.get();
        if (!p) throw new ModError('Create a project first.');
        let pack: Awaited<ReturnType<typeof buildProjectPack>> | null = null;
        try {
            pack = await buildProjectPack(host.exportDeps, p);
        } catch (e) {
            if (!(e instanceof ModError) || !/Nothing to export/.test(e.message)) throw e;
        }
        const level = from ?? p.start ?? host.exportDeps.openDoc()?.title ?? null;
        const msg = await host.play(pack ? { meta: pack.meta, files: pack.files } : null, level, playControls.read());
        if (msg === null) return;
        projects.session.played = true;
        projects.notify();
        report.hidden = false;
        report.textContent = [...(pack?.warnings ?? []), msg].join('\n');
        host.setStatus(msg);
    }

    // ---- render + data ----------------------------------------------------------------------------------------------

    let recents: string[] = [];
    function noProject(): HTMLElement {
        const nameIn = h('input', {
            type: 'text',
            class: 'ui-input',
            id: 'pj-new-name',
            spellcheck: 'false',
            placeholder: 'My mod',
        });
        nameIn.value = 'My mod';
        const create = (): void => void host.files.create(nameIn.value).then(() => undefined, fail);
        nameIn.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') create();
        });
        return h(
            'div',
            { class: 'pj-start' },
            ico('layers', 28),
            h('h2', {}, 'Start a mod project'),
            h(
                'p',
                {},
                isDesktop
                    ? 'A project is a folder on your disk: the project file, your level edits and the exported mod.'
                    : 'A mod project holds the name, the levels and the settings of what you are making.',
            ),
            h(
                'div',
                { class: 'pj-start-form' },
                h('label', { for: 'pj-new-name' }, 'Project name'),
                nameIn,
                h(
                    'button',
                    { class: 'ui-btn primary', type: 'button', id: 'pj-create', onclick: create },
                    icon('plus', 13),
                    isDesktop ? 'Create project folder...' : 'Create a project',
                ),
            ),
            h(
                'div',
                { class: 'pj-start-more' },
                isDesktop
                    ? h(
                          'button',
                          {
                              class: 'ui-btn ghost sm',
                              type: 'button',
                              id: 'pj-open-start',
                              onclick: () => void host.files.openFolder().then(() => undefined, fail),
                          },
                          icon('open', 13),
                          'Open project...',
                      )
                    : null,
                h(
                    'button',
                    { class: 'ui-btn ghost sm', type: 'button', onclick: () => host.openGuide('campaign') },
                    icon('help', 13),
                    'How projects work',
                ),
            ),
            recents.length
                ? h(
                      'div',
                      { class: 'pj-recent' },
                      h('div', { class: 'pj-title' }, 'Recent projects'),
                      ...recents.map((r) =>
                          h(
                              'button',
                              {
                                  class: 'pj-link',
                                  type: 'button',
                                  title: r,
                                  onclick: () => void host.files.openRecent(r).then(() => undefined, fail),
                              },
                              r,
                          ),
                      ),
                  )
                : null,
        );
    }

    function render(): void {
        const p = projects.get();
        if (!p) {
            void host.recentProjects().then((r) => {
                if (r.join() !== recents.join()) {
                    recents = r;
                    if (!projects.get()) render();
                }
            });
            root.replaceChildren(noProject());
            return;
        }
        if (selected && !p.levels.some((l) => l.title === selected)) selected = null;
        // Keep the scroll of each pane and the focus of the field being typed in across a re-render.
        const scrolls = [...root.querySelectorAll<HTMLElement>('.pjx-pane-body')].map((b) => b.scrollTop);
        const active = document.activeElement as HTMLInputElement | null;
        const focusId = active && root.contains(active) ? active.id : '';
        const pos = focusId && 'selectionStart' in active! ? active!.selectionStart : null;
        const grid = h(
            'div',
            { class: `pj-grid pjx-grid${view === 'map' ? ' pj-map' : ''}` },
            detailsPane(p),
            levelsPane(p),
            h('div', { class: 'pjx-side' }, host.sidePane?.(p) ?? null, checksPane(p), exportPane()),
        );
        root.replaceChildren(grid);
        root.querySelectorAll<HTMLElement>('.pjx-pane-body').forEach((b, n) => {
            if (scrolls[n]) b.scrollTop = scrolls[n]!;
        });
        if (focusId) {
            const el = root.querySelector<HTMLInputElement>(`#${focusId}`);
            if (el) {
                el.focus({ preventScroll: true });
                if (pos !== null && pos !== undefined && typeof el.setSelectionRange === 'function') {
                    el.setSelectionRange(pos, pos);
                }
            }
        }
    }

    async function recompute(): Promise<void> {
        const p = projects.get();
        if (!p) {
            issues = [];
            render();
            return;
        }
        if (!exits && !exitsLoading && host.levels().length && p.levels.length) {
            exitsLoading = true;
            exitsError = '';
            render();
            try {
                const d = await host.loadExits();
                exits = { levels: new Map(d.levels.map((l) => [l.name, l.exits])) };
            } catch (e) {
                exitsError = `Could not read the exits: ${e instanceof Error ? e.message : String(e)}`;
            }
            exitsLoading = false;
        }
        const cur = projects.get() ?? p;
        issues = validateProject(cur, { discLevels: discTitles(), exits });
        // Messages that went away do not keep their expanded state.
        const live = new Set(issues.map(issueKey));
        for (const k of openMsgs) if (!live.has(k)) openMsgs.delete(k);
        try {
            counts =
                cur.levels.length && host.levels().length
                    ? await levelEditCounts(host.exportDeps, cur, true)
                    : new Map();
        } catch {
            counts = new Map();
        }
        render();
        for (const l of issueListeners) l();
    }

    const refreshSoon = (): void => {
        window.clearTimeout(timer);
        timer = window.setTimeout(() => void recompute(), 300);
    };
    projects.on(refreshSoon);
    projects.onFolder(() => {
        const el = root.querySelector('#pj-folder');
        if (el) el.replaceWith(folderLine());
        else refreshSoon();
    });
    void host.recentProjects().then((r) => {
        recents = r;
        if (!projects.get()) render();
    });
    render();

    return {
        el: root,
        issues: () => issues,
        onIssues: (fn) => void issueListeners.add(fn),
        addCopy: () => {
            closePopover();
            requestAnimationFrame(() => {
                const anchor = root.querySelector<HTMLElement>('#pj-add, #pj-add-empty');
                if (anchor && !(anchor as HTMLButtonElement).disabled) openAddMenu(anchor, 'copy');
            });
        },
        play,
        refreshSoon,
        exitsLoaded: () => exits !== null,
        resetExits() {
            exits = null;
            exitsError = '';
            refreshSoon();
        },
        onLoad: refreshSoon,
        onChange: () => {
            window.clearTimeout(timer);
            timer = window.setTimeout(() => void recompute(), 1500);
        },
    };
}
