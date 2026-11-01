import {
    ModError,
    MOD_ID_PATTERN,
    addDiscLevel,
    addLevelCopy,
    levelExits,
    moveLevel,
    projectExitIndex,
    removeLevel,
    routeOrder,
    slugId,
    suggestCopyTitle,
    validateProject,
    type ExitIndex,
    type ModProject,
    type ProjectIssue,
    type ProjectLevel,
} from '@drakan/editor-core';
import type { LevelInfo } from '../../rpc.ts';
import type { ExitIndexData } from '../../logicdata.ts';
import type { Ctx, Panel } from '../ctx.ts';
import { h, icon } from '../dom.ts';
import { createPlayControls } from '../playopts.ts';
import { isDesktop } from '../../platform.ts';
import type { PlayOptions } from '../../launch.ts';
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
}

export interface ProjectPanel extends Panel {
    /** Latest campaign issues (exits, titles) for the Validation tab. */
    issues(): ProjectIssue[];
    onIssues(fn: () => void): void;
    /** Focus the "add a copy" form. */
    addCopy(): void;
    play(): Promise<void>;
    refreshSoon(): void;
    resetExits(): void;
    /** The exits of every level were read, so `issues()` is complete. */
    exitsLoaded(): boolean;
}

const norm = (s: string): string => s.toLowerCase().replace(/[^a-z0-9]/g, '');

export function createProjectPanel(host: ProjectPanelHost): ProjectPanel {
    let exits: ExitIndex | null = null;
    let exitsLoading = false;
    let exitsError = '';
    let issues: ProjectIssue[] = [];
    let counts = new Map<string, number>();
    let openExits = new Set<string>();
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

    // ---- details ----------------------------------------------------------------------------------------------------

    const field = (
        label: string,
        value: string,
        on: (v: string) => void,
        o: { area?: boolean; placeholder?: string; id?: string } = {},
    ): HTMLElement => {
        const el = o.area
            ? h('textarea', { rows: 2, spellcheck: 'false', placeholder: o.placeholder ?? '' })
            : h('input', { type: 'text', spellcheck: 'false', placeholder: o.placeholder ?? '', id: o.id });
        el.value = value;
        el.addEventListener('input', () => on(el.value));
        return h('label', { class: 'pj-f' }, h('span', {}, label), el);
    };

    function detailsCol(p: ModProject): HTMLElement {
        const idInput = field('Id', p.id, (v) => projects.update((q) => ({ ...q, id: v.trim() })), {
            placeholder: 'letters, digits . _ -',
            id: 'pj-id',
        });
        const idEl = idInput.querySelector('input')!;
        idEl.classList.toggle('bad', !MOD_ID_PATTERN.test(p.id));
        const file = h('input', { type: 'file', accept: '.json,.drakan-project.json', hidden: true });
        file.addEventListener('change', () => {
            const f = file.files?.[0];
            file.value = '';
            if (!f) return;
            void f.text().then((t) => host.files.openText(t), fail);
        });
        return h(
            'div',
            { class: 'pj-col pj-details' },
            h('div', { class: 'pj-title' }, 'Mod details'),
            field(
                'Name',
                p.name,
                (v) => {
                    const prevSlug = slugId(p.name);
                    projects.update((q) => ({ ...q, name: v, id: q.id === prevSlug || !q.id ? slugId(v) : q.id }));
                },
                { id: 'pj-name' },
            ),
            idInput,
            h(
                'div',
                { class: 'pj-row2' },
                field('Version', p.version, (v) => projects.update((q) => ({ ...q, version: v.trim() }))),
                field('Author', p.author, (v) => projects.update((q) => ({ ...q, author: v }))),
            ),
            field('Description', p.description, (v) => projects.update((q) => ({ ...q, description: v })), {
                area: true,
                placeholder: 'What does it change?',
            }),
            h(
                'div',
                { class: 'pj-btns' },
                h(
                    'button',
                    {
                        class: 'ui-btn sm',
                        type: 'button',
                        id: 'pj-save',
                        title: isDesktop
                            ? 'Save the project and its level edits into the project folder'
                            : 'Download the project (settings, level list and level edits) as one file',
                        onclick: () => void host.files.save().catch(fail),
                    },
                    icon('save', 13),
                    isDesktop ? 'Save' : 'Save project',
                ),
                h(
                    'button',
                    {
                        class: 'ui-btn sm',
                        type: 'button',
                        id: 'pj-open',
                        title: isDesktop ? 'Open a project folder' : 'Open a project file',
                        onclick: () => (isDesktop ? void host.files.openFolder().catch(fail) : file.click()),
                    },
                    icon('open', 13),
                    isDesktop ? 'Open...' : 'Open project',
                ),
                h(
                    'button',
                    {
                        class: 'ui-btn sm',
                        type: 'button',
                        id: 'pj-new',
                        title: 'Close this project and start another',
                        onclick: () => {
                            if (
                                !projects.folder &&
                                p.levels.length &&
                                !window.confirm('Close this project? Save it first to keep it.')
                            )
                                return;
                            void host.files.close().catch(fail);
                        },
                    },
                    icon('plus', 13),
                    'New...',
                ),
                file,
            ),
            h('p', { class: 'pj-note', id: 'pj-where' }, whereText()),
        );
    }

    function whereText(): string {
        const f = projects.folder;
        if (!isDesktop)
            return 'The project file holds these settings, the level list and your level edits. Save it to keep them; Open restores them.';
        if (!f) return 'Not saved to a folder.';
        const when = f.savedAt ? ` Saved in place at ${new Date(f.savedAt).toLocaleTimeString()}.` : '';
        return f.error
            ? `Folder ${f.dir}. Saving failed: ${f.error}`
            : `Folder ${f.dir}.${when} Level edits and the exported mod live there.`;
    }

    // ---- levels -----------------------------------------------------------------------------------------------------

    const addSel = h('select', { class: 'ui-select', id: 'pj-add-sel', 'aria-label': 'Level to add' });
    const copySel = h('select', { class: 'ui-select', id: 'pj-copy-sel', 'aria-label': 'Level to copy' });
    const copyTitle = h('input', {
        type: 'text',
        id: 'pj-copy-title',
        placeholder: 'Title of the copy',
        spellcheck: 'false',
    });
    copySel.addEventListener('change', () => {
        const p = projects.get();
        if (p) copyTitle.value = suggestCopyTitle(p, copySel.value, discTitles());
    });

    function fillSelects(): void {
        const names = discTitles();
        const keep = (sel: HTMLSelectElement): string => sel.value;
        const a = keep(addSel);
        const c = keep(copySel);
        addSel.replaceChildren(...names.map((n) => new Option(n, n)));
        copySel.replaceChildren(...names.map((n) => new Option(n, n)));
        if (a && names.includes(a)) addSel.value = a;
        if (c && names.includes(c)) copySel.value = c;
        const dis = !names.length;
        addSel.disabled = copySel.disabled = copyTitle.disabled = dis;
    }

    function addCopyNow(): void {
        const p = projects.get();
        if (!p) return;
        try {
            const title = copyTitle.value.trim() || suggestCopyTitle(p, copySel.value, discTitles());
            projects.set(addLevelCopy(p, copySel.value, title));
            copyTitle.value = '';
            host.ctx.toast(`Added a copy: ${title}`, 'ok');
        } catch (e) {
            fail(e);
        }
    }

    function levelRow(p: ModProject, l: ProjectLevel, i: number): HTMLElement {
        const edits = counts.get(l.title) ?? 0;
        const isStart = p.start === l.title;
        const list = exits ? levelExits(p, l, exits) : [];
        const open = openExits.has(l.title);
        const bad = list.filter((e) => e.problem).length;
        const head = h(
            'div',
            { class: 'pj-lhead' },
            h('span', { class: 'pj-idx' }, String(i + 1)),
            h(
                'button',
                {
                    class: 'pj-ltitle',
                    type: 'button',
                    title: 'Open this level in the editor',
                    onclick: () => void host.openLevel(l).catch(fail),
                },
                l.title,
            ),
            l.copyOf ? h('span', { class: 'ui-badge', title: `A new level that starts as ${l.copyOf}` }, 'copy') : null,
            isStart ? h('span', { class: 'ui-badge ok', title: 'The Play button starts here' }, 'play start') : null,
            edits ? h('span', { class: 'ui-badge warn' }, `${edits} change${edits === 1 ? '' : 's'}`) : null,
            h('span', { class: 'spacer' }),
            h(
                'button',
                {
                    class: 'pj-link',
                    type: 'button',
                    'aria-expanded': String(open),
                    onclick: () => {
                        if (open) openExits.delete(l.title);
                        else openExits.add(l.title);
                        render();
                    },
                },
                exits ? `${list.length} exit${list.length === 1 ? '' : 's'}${bad ? `, ${bad} to fix` : ''}` : 'exits',
            ),
            h(
                'button',
                { class: 'ui-btn sm', type: 'button', onclick: () => void host.openLevel(l).catch(fail) },
                'Open',
            ),
            h(
                'button',
                {
                    class: 'ui-tool sm',
                    type: 'button',
                    title: 'Move up',
                    disabled: i === 0,
                    onclick: () => projects.set(moveLevel(p, l.title, -1)),
                },
                '↑',
            ),
            h(
                'button',
                {
                    class: 'ui-tool sm',
                    type: 'button',
                    title: 'Move down',
                    disabled: i === p.levels.length - 1,
                    onclick: () => projects.set(moveLevel(p, l.title, 1)),
                },
                '↓',
            ),
            h(
                'button',
                {
                    class: 'ui-btn sm',
                    type: 'button',
                    title: 'Make the Play button start in this level',
                    disabled: isStart,
                    onclick: () => projects.set({ ...p, start: l.title }),
                },
                'Start here',
            ),
            h(
                'button',
                {
                    class: 'ui-tool sm',
                    type: 'button',
                    title: 'Remove from the project',
                    'aria-label': 'Remove',
                    onclick: () => projects.set(removeLevel(p, l.title)),
                },
                icon('x', 12),
            ),
        );
        const row = h('li', { class: 'pj-level', 'data-level': l.title }, head);
        if (open) {
            row.append(
                h(
                    'ul',
                    { class: 'pj-exits' },
                    ...(list.length
                        ? list.map((e) =>
                              h(
                                  'li',
                                  { class: e.problem ? 'warn' : '' },
                                  h('span', { class: 'arrow' }, '→'),
                                  h('span', { class: 'dest' }, e.dest ?? 'unknown destination'),
                                  e.portal ? h('span', { class: 'ui-badge' }, 'portal') : null,
                                  !e.inProject && e.dest
                                      ? h(
                                            'span',
                                            {
                                                class: 'ui-badge',
                                                title: 'This destination is not one of the project levels',
                                            },
                                            'not in project',
                                        )
                                      : null,
                                  h(
                                      'span',
                                      { class: 'what' },
                                      e.problem ?? `${e.name || `exit #${e.id}`}: arrives correctly`,
                                  ),
                                  h(
                                      'button',
                                      {
                                          class: 'pj-link',
                                          type: 'button',
                                          title: 'Open the level and select this exit; change its destination in the Logic panel',
                                          onclick: () =>
                                              void host
                                                  .openLevel(l)
                                                  .then(() => host.ctx.locate([e.id]))
                                                  .catch(fail),
                                      },
                                      'Show exit',
                                  ),
                              ),
                          )
                        : [
                              h(
                                  'li',
                                  { class: 'dim' },
                                  exits
                                      ? 'This level has no exits.'
                                      : exitsLoading
                                        ? 'Reading the exits of every level...'
                                        : exitsError || 'Exits are read from the disc.',
                              ),
                          ]),
                ),
            );
        }
        return row;
    }

    function levelsCol(p: ModProject): HTMLElement {
        const connected = host.levels().length > 0;
        return h(
            'div',
            { class: 'pj-col pj-levels' },
            h(
                'div',
                { class: 'pj-title' },
                'Levels, in campaign order',
                h('span', { class: 'spacer' }),
                h(
                    'button',
                    {
                        class: 'ui-btn sm',
                        type: 'button',
                        id: 'pj-route',
                        disabled: p.levels.length < 2 || !exits,
                        title: 'Sort the list in the order a player reaches the levels from the start level',
                        onclick: () => exits && projects.set({ ...p, levels: routeOrder(p, exits) }),
                    },
                    'Order by route',
                ),
            ),
            h(
                'div',
                { class: 'pj-add' },
                h('span', { class: 'lbl' }, 'Add level'),
                addSel,
                h(
                    'button',
                    {
                        class: 'ui-btn sm',
                        type: 'button',
                        id: 'pj-add',
                        disabled: !connected,
                        onclick: () => addSel.value && projects.set(addDiscLevel(p, addSel.value)),
                    },
                    icon('plus', 12),
                    'Add',
                ),
                h('span', { class: 'lbl' }, 'or copy'),
                copySel,
                copyTitle,
                h(
                    'button',
                    { class: 'ui-btn sm', type: 'button', id: 'pj-copy', disabled: !connected, onclick: addCopyNow },
                    'Add copy',
                ),
            ),
            !connected ? h('p', { class: 'pj-note' }, 'Open your disc image to add levels.') : null,
            p.levels.length
                ? h('ol', { class: 'pj-list' }, ...p.levels.map((l, i) => levelRow(p, l, i)))
                : h(
                      'p',
                      { class: 'pj-empty' },
                      'No levels yet. A level joins the project when you add it here, or when you edit it with the "Edit a campaign level" task. Levels you do not change are left out of the pack.',
                  ),
            exitsLoading ? h('p', { class: 'pj-note' }, 'Reading the exits of every level (a few seconds)...') : null,
            exitsError ? h('p', { class: 'pj-note bad' }, exitsError) : null,
        );
    }

    // ---- output -----------------------------------------------------------------------------------------------------

    const playControls = createPlayControls(
        () => void play().catch(fail),
        () => true,
    );

    function outputCol(p: ModProject): HTMLElement {
        const errs = issues.filter((i) => i.severity === 'error');
        const warns = issues.filter((i) => i.severity === 'warning');
        const infos = issues.filter((i) => i.severity === 'info');
        const rows = [...errs, ...warns, ...infos];
        return h(
            'div',
            { class: 'pj-col pj-out' },
            h('div', { class: 'pj-title' }, 'Checks'),
            rows.length
                ? h(
                      'ul',
                      { class: 'pj-issues' },
                      ...rows
                          .slice(0, 40)
                          .map((i) =>
                              h(
                                  'li',
                                  { class: i.severity },
                                  h(
                                      'span',
                                      { class: 'sev' },
                                      i.severity === 'error' ? 'Error' : i.severity === 'warning' ? 'Warning' : 'Note',
                                  ),
                                  h('span', {}, i.message),
                              ),
                          ),
                  )
                : h(
                      'p',
                      { class: 'pj-ok' },
                      p.levels.length ? 'No problems found in the project.' : 'Nothing to check yet.',
                  ),
            h('div', { class: 'pj-title' }, 'Export'),
            h(
                'div',
                { class: 'pj-btns' },
                h(
                    'button',
                    {
                        class: 'ui-btn primary sm',
                        type: 'button',
                        id: 'pj-export',
                        disabled: errs.length > 0,
                        title: errs.length ? 'Fix the errors above first' : 'Build the mod pack (Ctrl+E)',
                        onclick: () => host.openExport(),
                    },
                    icon('download', 13),
                    'Export...',
                ),
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
                          'Steps',
                      ),
                  ),
            h(
                'p',
                { class: 'pj-note' },
                'Export builds one mod pack from the project: every level with changes, every copy and the story edits made in the Story tab. Multiplayer maps export from their own tab.',
            ),
            report,
        );
    }

    async function play(): Promise<void> {
        const p = projects.get();
        if (!p) throw new ModError('Create a project first.');
        let pack: Awaited<ReturnType<typeof buildProjectPack>> | null = null;
        try {
            pack = await buildProjectPack(host.exportDeps, p);
        } catch (e) {
            if (!(e instanceof ModError) || !/Nothing to export/.test(e.message)) throw e;
        }
        const level = p.start ?? host.exportDeps.openDoc()?.title ?? null;
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
        const nameIn = h('input', { type: 'text', id: 'pj-new-name', spellcheck: 'false', placeholder: 'My mod' });
        nameIn.value = 'My mod';
        const create = (): void => void host.files.create(nameIn.value).then(() => undefined, fail);
        nameIn.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') create();
        });
        return h(
            'div',
            { class: 'pj-start' },
            h(
                'p',
                {},
                isDesktop
                    ? 'A project is a folder on your disk: the project file, your level edits and the exported mod.'
                    : 'A mod project holds the name, the levels and the settings of what you are making.',
            ),
            h(
                'div',
                { class: 'pj-btns' },
                h('label', { class: 'pj-f' }, h('span', {}, 'Project name'), nameIn),
                h(
                    'button',
                    { class: 'ui-btn primary sm', type: 'button', id: 'pj-create', onclick: create },
                    icon('plus', 13),
                    isDesktop ? 'Create project folder...' : 'Create a project',
                ),
                isDesktop
                    ? h(
                          'button',
                          {
                              class: 'ui-btn sm',
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
                    { class: 'ui-btn sm', type: 'button', onclick: () => host.openGuide('campaign') },
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
        fillSelects();
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
        if (!copyTitle.value && copySel.value) copyTitle.placeholder = suggestCopyTitle(p, copySel.value, discTitles());
        const grid = h('div', { class: 'pj-grid' }, detailsCol(p), levelsCol(p), outputCol(p));
        // Keep focus in the field being typed in across a re-render.
        const active = document.activeElement as HTMLInputElement | null;
        const focusId = active && root.contains(active) ? active.id : '';
        const pos = focusId ? active!.selectionStart : null;
        root.replaceChildren(grid);
        if (focusId) {
            const el = root.querySelector<HTMLInputElement>(`#${focusId}`);
            if (el) {
                el.focus();
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
        try {
            counts =
                cur.levels.length && host.levels().length ? await levelEditCounts(host.exportDeps, cur) : new Map();
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
        const el = root.querySelector('#pj-where');
        if (el) el.textContent = whereText();
        else refreshSoon();
    });
    void host.recentProjects().then((r) => {
        recents = r;
        if (!projects.get()) render();
    });
    void projectExitIndex;
    render();

    return {
        el: root,
        issues: () => issues,
        onIssues: (fn) => void issueListeners.add(fn),
        addCopy: () => {
            copySel.focus();
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
