import {
    WORKFLOWS,
    addDiscLevel,
    addLevelCopy,
    suggestCopyTitle,
    validateDocument,
    type LevelDocument,
    type ProjectLevel,
    type StepFacts,
    type TaskId,
} from '@drakan/editor-core';
import type { LevelUi } from '../../levelui.ts';
import type { LevelUiHost } from '../../levelui.ts';
import type { StoryEditors } from '../../story/register.ts';
import { canWriteFolder, playInGame, writePackToModsFolder, type LevelPack } from '../../modio.ts';
import * as platform from '../../platform.ts';
import { setCopyPlayProvider } from '../../playtest.ts';
import { downloadFile, isDesktop } from '../../platform.ts';
import type { ArenaStore } from '../arena/store.ts';
import { addValidationProvider } from '../panels.ts';
import { registerCommand, registerPanel, runCommand } from '../registry.ts';
import { createChecklist } from './checklist.ts';
import { createCampaignPane } from './campaign.ts';
import { buildProjectPack, levelEditCounts, projectLevelDoc, resetExportCache, zipProjectPack } from './export.ts';
import { openExportDialog } from './exportdialog.ts';
import { createProjectFiles, type ProjectFiles } from './folder.ts';
import { createHelp, type Help } from './help.ts';
import { createProjectPanel } from './panel.ts';
import { projects } from './store.ts';

export type Mode = 'inspect' | 'level' | 'save';

export interface ProjectInstallHost {
    ui: LevelUi;
    call: LevelUiHost['call'];
    setStatus(msg: string, error?: boolean): void;
    showTab(tab: Mode): void;
    arena: ArenaStore;
    story: StoryEditors;
}

export interface ProjectInstall {
    help: Help;
    /** Project folder / file operations (the native menu and the Project tab use them). */
    files: ProjectFiles;
    /** Open the one Export dialog. */
    openExport(): void;
    /** A disc was opened or its language changed. */
    onDisc(): void;
    startTask(task: TaskId): void;
}

const norm = (s: string): string => s.toLowerCase().replace(/[^a-z0-9]/g, '');

const DEFAULT_NAME: Record<TaskId, string> = {
    campaign: 'My campaign mod',
    multiplayer: 'My multiplayer map',
    newlevel: 'My new level',
    story: 'My story mod',
    save: 'My mod',
};

/** "?" buttons: where each panel gets one and which guide it opens. Selectors are tried again on every tick. */
const HELP_TARGETS: [string, string][] = [
    ['#ed-left', 'objects'],
    ['#ed-details', 'objects'],
    ['.panel.validation', 'package'],
    ['.panel.mod', 'package'],
    ['.panel.project', 'campaign'],
    ['.panel.logic-panel', 'triggers'],
    ['.panel.mp', 'multiplayer'],
    ['.panel.vendors', 'vendors'],
    ['.panel.menus', 'menus'],
    ['.story-area', 'story'],
    ['#save-main > :first-child', 'save'],
    ['.fl-intro', 'files'],
];

export function installProject(host: ProjectInstallHost): ProjectInstall {
    const { ui } = host;
    const ctx = ui.ctx();
    const help = createHelp();

    const discDir = (title: string): string | null =>
        ui.levels().find((l) => l.present && norm(l.name) === norm(title))?.dirPath ?? null;

    const openDoc = (): { title: string; doc: LevelDocument } | null => {
        if (!ctx.src()) return null;
        const doc = ctx.doc();
        return doc.size ? { title: doc.level, doc } : null;
    };

    const exportDeps = {
        call: host.call,
        levels: () => ui.levels(),
        openDoc,
        target: () => ui.modTarget(),
        story: {
            count: () => host.story.session.editCount(),
            files: async (meta: Parameters<typeof host.story.session.buildFiles>[0]) =>
                (await host.story.session.buildFiles(meta)).files,
        },
    };

    async function openProjectLevel(l: ProjectLevel): Promise<void> {
        const dir = discDir(l.copyOf ?? l.title);
        if (!dir) throw new Error(`"${l.copyOf ?? l.title}" is not on the open disc.`);
        host.showTab('level');
        await ui.openLevel(dir, l.copyOf ? l.title : undefined);
        // The level's autosave banner talks about the disc level; a copy has its own autosave under its own title.
    }

    /** Disc level titles that exist on the open disc. */
    const isDiscLevel = (t: string): boolean => !!discDir(t);

    const files = createProjectFiles({
        flush: () => ui.flushAutosave(),
        dropUnedited: async (p) => {
            if (p.levels.length && ui.levels().length) await levelEditCounts(exportDeps, p, true);
        },
        toast: (m, k) => ctx.toast(m, k),
        setStatus: host.setStatus,
        download: (name, data, type) => downloadFile(name, data, type),
        story: { export: () => host.story.exportProject(), import: (s) => host.story.importProject(s) },
        reloadOpenLevel: (titles) => {
            const open = openDoc();
            const p = projects.get();
            if (!open || !p || ![...titles].some((t) => norm(t) === norm(open.title))) return;
            const l = p.levels.find((x) => norm(x.title) === norm(open.title));
            if (l) void openProjectLevel(l).catch((e: unknown) => ctx.toast(String(e), 'error'));
        },
    });

    // Story edits are part of the project file: a change saves the project soon after, like a level edit.
    let storySig = '';
    host.story.session.subscribe(() => {
        const sig = JSON.stringify(host.story.exportProject());
        if (sig === storySig) return;
        storySig = sig;
        projects.edited();
    });

    // Play test of a level copy: the pack adds the whole level (the disc has no LVL under that title).
    setCopyPlayProvider(async (title, doc, meta) => {
        const p = projects.get();
        const l = p?.levels.find((x) => x.copyOf && norm(x.title) === norm(title));
        if (!p || !l) return null;
        const only = { ...p, id: meta.id, name: meta.name ?? meta.id, levels: [l] };
        const pack = await buildProjectPack(exportDeps, only, new Map([[l.title, doc]]));
        return { meta: { ...pack.meta, description: meta.description, target: meta.target }, files: pack.files };
    });

    // Project Settings > Campaign: "Make this a campaign" (its own entry on the game's title card).
    const campaign = createCampaignPane({
        levels: () => ui.levels(),
        loadExits: () => host.call('exitIndex'),
        levelDoc: (title) => projectLevelDoc(exportDeps, title),
        strings: {
            ensure: () => host.story.session.ensure(),
            text: (file, id) => host.story.session.str(file, id)?.text[0],
            changed: (file, id) => host.story.session.isChanged(file, id),
        },
        toast: (m, k) => ctx.toast(m, k),
    });
    const panel = createProjectPanel({
        sidePane: () => campaign.el(),
        ctx,
        levels: () => ui.levels(),
        loadExits: () => host.call('exitIndex'),
        exportDeps,
        openLevel: openProjectLevel,
        files,
        openExport: () => openExport(),
        recentProjects: async () => (isDesktop ? (await platform.getSettings()).recentProjects : []),
        play: (pack, level, opts) => playInGame(pack as unknown as LevelPack | null, opts, level),
        setStatus: host.setStatus,
        openGuide: (id) => help.open(id),
    });
    /** The one Export: a dialog that builds the mod pack from the project and puts it where the user chooses. */
    function openExport(): void {
        openExportDialog({
            ensureProject: () => {
                const open = openDoc();
                let p = projects.ensure(DEFAULT_NAME.campaign);
                if (open && ui.view().stack.canUndo && !p.levels.some((l) => norm(l.title) === norm(open.title))) {
                    if (isDiscLevel(open.title)) {
                        p = addDiscLevel(p, open.title);
                        projects.set(p);
                    }
                }
                return p;
            },
            build: (p, progress) => buildProjectPack(exportDeps, p, undefined, progress),
            blockers: () => [
                ...panel
                    .issues()
                    .filter((i) => i.severity === 'error')
                    .map((i) => i.message),
                ...campaign.errors().map((m) => `Campaign: ${m}`),
            ],
            zip: zipProjectPack,
            writeToProjectFolder: (rel, bytes) => files.writeFile(rel, bytes),
            saveZip: (name, bytes) => downloadFile(name, bytes as BlobPart, 'application/zip'),
            installAvailable: () => canWriteFolder(),
            install: (pack) => writePackToModsFolder(pack as unknown as LevelPack),
            folderDir: () => projects.folder?.dir ?? null,
            saveProject: () => (projects.folder ? files.save(true) : Promise.resolve()),
            toast: (m, k) => ctx.toast(m, k),
            setStatus: host.setStatus,
            installDefault: () => isDesktop,
        });
    }
    projects.handlers.export = openExport;
    projects.handlers.save = () => void files.save().catch((e: unknown) => ctx.toast(String(e), 'error'));

    registerPanel({
        id: 'project',
        title: 'Project',
        icon: 'layers',
        panel,
        command: {
            title: 'Show the mod project',
            keywords: 'mod project campaign levels order exits export package name author version',
            hint: 'Name your mod, list its levels in campaign order, check the exits and export the pack',
        },
    });
    addValidationProvider({
        issues: () =>
            panel
                .issues()
                .filter((i) => i.severity !== 'info')
                .map((i) => ({
                    severity: i.severity === 'error' ? ('error' as const) : ('warning' as const),
                    code: `project-${i.code}`,
                    message: `Project: ${i.message}`,
                    run: () => runCommand('panel.project'),
                })),
        onChange: (fn) => panel.onIssues(fn),
    });

    // ---- what the checklist can see ---------------------------------------------------------------------------------

    let valKey = '';
    let valErrors = 0;
    function levelErrors(): number {
        const src = ctx.src();
        if (!src) return 0;
        const v = ui.view();
        const key = `${v.stack.top ? 1 : 0}:${v.stack.depth}:${v.doc.size}:${src.file}`;
        if (key === valKey) return valErrors;
        valKey = key;
        const doc = ctx.doc();
        valErrors = validateDocument(doc, {
            sourceLevel: true,
            rawIds: [
                ...src.lvl.list.objects.map((o) => o.id),
                ...[...doc.objects()].filter((o) => o.origin === undefined).map((o) => o.id),
            ],
        }).filter((i) => i.severity === 'error').length;
        return valErrors;
    }

    function facts(): StepFacts {
        const p = projects.get();
        const src = ctx.src();
        const levelOpen = !!src;
        const v = levelOpen ? ui.view() : null;
        const d = host.arena.draft;
        return {
            disc: ui.levels().length > 0,
            levelOpen,
            levelEdits: !!v && v.stack.canUndo,
            errors: levelOpen ? levelErrors() : 0,
            projectLevels: p?.levels.length ?? 0,
            projectCopies: p?.levels.filter((l) => l.copyOf).length ?? 0,
            exitsChecked: panel.exitsLoaded(),
            campaignErrors: panel
                .issues()
                .filter(
                    (i) =>
                        i.severity === 'error' ||
                        (i.code === 'exit' && !!p?.levels.find((l) => l.title === i.level)?.copyOf),
                ).length,
            mpActive: host.arena.active,
            mpNamed: host.arena.active && d.name.trim().length > 0 && d.id.trim().length > 0,
            mpSpawns: host.arena.active ? d.spawns.length : 0,
            mpPickups: host.arena.active ? d.pickups.length : 0,
            mpErrors: host.arena.active && d.spawns.length < 2 ? 1 : 0,
            storyChanges: host.story.session.editCount(),
            saveOpen: !/^\s*(no save open)?\s*$/i.test(document.getElementById('sv-title')?.textContent ?? ''),
            saveEdited: Number(document.getElementById('sv-pending')?.textContent?.match(/\d+/)?.[0] ?? 0) > 0,
            exported: projects.session.exported,
            played: projects.session.played,
        };
    }

    // ---- starting a task --------------------------------------------------------------------------------------------

    function startTask(task: TaskId): void {
        // Saves are edited in the save game editor, an application of its own: no mod project, no tab here.
        if (task === 'save') {
            host.showTab('save');
            return;
        }
        const w = WORKFLOWS[task];
        if (!ui.levels().length) {
            projects.pendingTask = task;
            runCommand('file.openIso');
            return;
        }
        projects.pendingTask = null;
        const p = projects.get();
        if (!p) projects.set({ ...projects.ensure(DEFAULT_NAME[task], task), task });
        else projects.set({ ...p, task });
        projects.pickFor = null;
        host.showTab(w.mode);
        switch (task) {
            case 'campaign':
                if (!ctx.src()) projects.pickFor = 'campaign';
                break;
            case 'newlevel':
                projects.pickFor = 'newlevel';
                break;
            case 'multiplayer':
                runCommand('panel.multiplayer');
                break;
            case 'story':
                runCommand('panel.story');
                break;
        }
        projects.notify();
    }

    projects.handlers.start = startTask;
    projects.handlers.pick = (task, l) => {
        projects.pickFor = null;
        if (task === 'newlevel') {
            const p = projects.ensure(DEFAULT_NAME.newlevel, 'newlevel');
            const title = suggestCopyTitle(
                p,
                l.name,
                ui.levels().map((x) => x.name),
            );
            projects.set({ ...addLevelCopy(p, l.name, title), task: 'newlevel' });
            runCommand('panel.project');
            void openProjectLevel({ title, copyOf: l.name }).catch((e: unknown) => ctx.toast(String(e), 'error'));
        } else {
            void ui.openLevel(l.dirPath).catch((e: unknown) => ctx.toast(String(e), 'error'));
        }
        projects.notify();
    };

    // ---- commands ---------------------------------------------------------------------------------------------------

    for (const w of Object.values(WORKFLOWS)) {
        registerCommand({
            id: `task.${w.id}`,
            title: `Start: ${w.title}`,
            category: 'Tasks',
            icon: 'check-circle',
            keywords: `task workflow checklist ${w.summary}`,
            hint: w.summary,
            run: () => startTask(w.id),
        });
    }
    registerCommand({
        id: 'mode.level',
        title: 'Level Editor',
        category: 'Window',
        icon: 'world',
        keywords: 'go to level tab viewport',
        run: () => host.showTab('level'),
    });
    registerCommand({
        id: 'mode.save',
        title: 'Save Editor',
        category: 'Tools',
        icon: 'save',
        keywords: 'go to save tab editor rsg game',
        hint: 'Opens the save game editor, an application of its own (its own window)',
        run: () => host.showTab('save'),
    });
    registerCommand({
        id: 'mode.files',
        title: 'Disc Files',
        category: 'Tools',
        icon: 'folder',
        keywords: 'go to files tab inspect disc browser raw hex textures advanced',
        hint: 'Advanced: browse the raw files on the disc',
        run: () => host.showTab('inspect'),
    });
    registerCommand({
        id: 'project.save',
        title: 'Save the project',
        category: 'File',
        icon: 'save',
        keywords: 'project file folder level edits',
        hint: isDesktop
            ? 'Save the project and its level edits into the project folder'
            : 'Download the project and its level edits as one file',
        run: () => projects.handlers.save?.(),
    });
    if (isDesktop) {
        registerCommand({
            id: 'project.new',
            title: 'New project folder...',
            category: 'File',
            icon: 'plus',
            keywords: 'create project',
            run: () => {
                runCommand('panel.project');
                void files.close().catch((e: unknown) => ctx.toast(String(e), 'error'));
            },
        });
        registerCommand({
            id: 'project.open',
            title: 'Open a project folder...',
            category: 'File',
            icon: 'open',
            keywords: 'load project',
            run: () => void files.openFolder().catch((e: unknown) => ctx.toast(String(e), 'error')),
        });
    }
    registerCommand({
        id: 'project.play',
        title: 'Play the project',
        category: 'File',
        icon: 'play',
        keywords: 'test launch run',
        run: () => {
            runCommand('panel.project');
            if (isDesktop) {
                void panel.play().catch((e: unknown) => ctx.toast(e instanceof Error ? e.message : String(e), 'error'));
            } else help.open('package');
        },
    });
    registerCommand({
        id: 'project.chooseLevel',
        title: 'Choose a level to edit',
        category: 'File',
        run: () => {
            if (ctx.src()) runCommand('help.palette');
            else {
                host.showTab('level');
                projects.pickFor = 'campaign';
                projects.notify();
            }
        },
    });
    registerCommand({
        id: 'project.addCopy',
        title: 'Add a copy of a level to the project',
        category: 'File',
        run: () => {
            runCommand('panel.project');
            panel.addCopy();
        },
    });

    // ---- upkeep -----------------------------------------------------------------------------------------------------

    const checklist = createChecklist({
        facts,
        run: (c) => {
            if (!runCommand(c)) ctx.toast('That step is not available yet.', 'info');
        },
        openGuide: (id) => help.open(id),
        startTask,
    });

    let lastLevel = '';
    function tick(): void {
        const p = projects.get();
        const src = ctx.src();
        if (src && p && (p.task === 'campaign' || p.task === 'newlevel')) {
            const v = ui.view();
            const title = ctx.doc().level;
            if (v.stack.canUndo && title && !p.levels.some((l) => norm(l.title) === norm(title)) && discDir(title)) {
                projects.set(addDiscLevel(p, title));
            }
        }
        const lv = src ? ctx.doc().level : '';
        if (lv !== lastLevel) {
            lastLevel = lv;
            panel.refreshSoon();
        }
        checklist.refresh();
        for (const [sel, guide] of HELP_TARGETS) {
            const el = document.querySelector(sel);
            if (el) help.attach(el, guide);
        }
    }
    window.setInterval(tick, 700);
    tick();

    return {
        help,
        files,
        openExport,
        startTask,
        onDisc() {
            resetExportCache();
            panel.resetExits();
            if (projects.pendingTask && ui.levels().length) startTask(projects.pendingTask);
            else projects.notify();
        },
    };
}
