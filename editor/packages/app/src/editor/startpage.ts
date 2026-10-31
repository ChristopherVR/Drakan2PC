import { WORKFLOWS, type TaskId } from '@drakan/editor-core';
import { h, icon, store } from './dom.ts';
import { getCommand, runCommand } from './registry.ts';
import { projects } from './project/store.ts';
import './shellux.css';
import './project/project.css';

/**
 * First thing a modder sees in the Level tab: what they can make (task cards), the current mod project, recent
 * levels. A task card starts the task (project, panels, checklist; see `project/install.ts`).
 */
export interface StartPageActions {
    /** Ask the host to open a disc image (file picker or desktop dialog). */
    openIso(): void;
    /** Open a saved .level.json or world dump. */
    openDocument(f: File): void;
    openLevel(dirPath: string): void;
    openPalette(): void;
    openHelp(): void;
}

export interface LevelRef {
    name: string;
    dirPath: string;
}

export interface StartPage {
    el: HTMLElement;
    /** Re-render for the current disc (levels = null when no disc is open). */
    refresh(levels: LevelRef[] | null, isoName?: string): void;
}

const RECENT_KEY = 'recent-levels';
const MAX_RECENT = 4;

export const recentLevels = (): LevelRef[] =>
    store.json<LevelRef[]>(RECENT_KEY, []).filter((l) => l && typeof l.dirPath === 'string');

export function rememberLevel(l: LevelRef): void {
    store.set(
        RECENT_KEY,
        JSON.stringify([l, ...recentLevels().filter((r) => r.dirPath !== l.dirPath)].slice(0, MAX_RECENT)),
    );
}

const TASK_ICON: Record<TaskId, string> = {
    campaign: 'layers',
    multiplayer: 'rocket',
    newlevel: 'copy',
    story: 'edit',
    save: 'save',
};

/** Tasks in the order they are offered. */
const TASK_ORDER: TaskId[] = ['campaign', 'multiplayer', 'newlevel', 'story', 'save'];

export function createStartPage(a: StartPageActions): StartPage {
    const el = h('div', { class: 'start' });
    const docInput = h('input', { type: 'file', accept: '.json', hidden: true });
    docInput.addEventListener('change', () => {
        const f = docInput.files?.[0];
        docInput.value = '';
        if (f) a.openDocument(f);
    });
    let levels: LevelRef[] | null = null;
    let isoName: string | undefined;

    const taskCard = (id: TaskId, discOpen: boolean): HTMLElement => {
        const w = WORKFLOWS[id];
        const needs = id !== 'save' && !discOpen;
        return h(
            'button',
            {
                class: 'start-task',
                type: 'button',
                'data-task': id,
                onclick: () => {
                    if (projects.handlers.start) projects.handlers.start(id);
                    else runCommand(`task.${id}`);
                },
            },
            icon(TASK_ICON[id], 20),
            h('strong', {}, w.title),
            h('span', {}, w.summary),
            h('em', {}, needs ? 'Asks for your disc image first' : 'Start'),
        );
    };

    const ootfCard = (): HTMLElement | null => {
        if (!getCommand('file.importOotf')) return null;
        return h(
            'button',
            { class: 'start-task', type: 'button', 'data-task': 'ootf', onclick: () => runCommand('file.importOotf') },
            icon('download', 20),
            h('strong', {}, 'Import an Order of the Flame level'),
            h('span', {}, 'Convert a level of the older game that you own and play it here.'),
            h('em', {}, 'Open the importer'),
        );
    };

    const tasks = (discOpen: boolean): HTMLElement =>
        h('div', { class: 'start-tasks' }, ...TASK_ORDER.map((t) => taskCard(t, discOpen)), ootfCard());

    const levelButton = (l: LevelRef, recent: boolean, onPick: (l: LevelRef) => void): HTMLElement => {
        const m = /^(Level\s+\S+)\s*[-–]\s*(.*)$/.exec(l.name);
        return h(
            'button',
            { class: `start-level${recent ? ' recent' : ''}`, type: 'button', onclick: () => onPick(l) },
            h('span', { class: 'n' }, m ? m[1]! : l.name),
            h('span', { class: 'd' }, m ? m[2]! : ''),
        );
    };

    function pickView(task: TaskId, all: LevelRef[]): Node[] {
        const copy = task === 'newlevel';
        return [
            h(
                'button',
                {
                    class: 'ui-btn sm start-back',
                    type: 'button',
                    onclick: () => {
                        projects.pickFor = null;
                        projects.notify();
                    },
                },
                'Back to tasks',
            ),
            h('h2', {}, copy ? 'Which level do you want to copy?' : 'Which level do you want to edit?'),
            h(
                'p',
                { class: 'start-lead' },
                copy
                    ? 'The copy becomes a new level with its own title; the original stays as it is. You can rename the copy in the Project panel.'
                    : 'You can open any other level later with Ctrl+P. Your edits are autosaved in this browser.',
            ),
            h(
                'div',
                { class: 'start-levels' },
                ...all.map((l) =>
                    levelButton(l, false, (x) => projects.handlers.pick?.(task, { name: x.name, dirPath: x.dirPath })),
                ),
            ),
        ];
    }

    function refresh(lv: LevelRef[] | null, name?: string): void {
        levels = lv;
        isoName = name;
        render();
    }

    function render(): void {
        const children: (Node | null)[] = [];
        if (levels && projects.pickFor) {
            el.replaceChildren(...pickView(projects.pickFor, levels), docInput);
            return;
        }
        const p = projects.get();
        if (!levels) {
            children.push(
                h('h2', {}, 'Open your game disc to start'),
                h(
                    'p',
                    { class: 'start-lead' },
                    'The editor reads levels straight from your own Drakan disc image. Nothing is uploaded and the disc is never modified.',
                ),
                h(
                    'div',
                    { class: 'start-actions' },
                    h(
                        'button',
                        { class: 'ui-btn primary lg', type: 'button', onclick: a.openIso },
                        icon('open', 15),
                        'Open disc image (.iso)',
                    ),
                    h(
                        'button',
                        { class: 'ui-btn lg', type: 'button', onclick: () => docInput.click() },
                        'Open a saved level (.json)',
                    ),
                ),
                h('h3', {}, 'Or choose what to make first'),
                tasks(false),
            );
        } else {
            const rec = recentLevels().filter((r) => levels!.some((l) => l.dirPath === r.dirPath));
            children.push(
                h('h2', {}, 'What do you want to make?'),
                h(
                    'p',
                    { class: 'start-lead' },
                    `${isoName ?? 'Your disc'} is open. Pick a task: the editor opens the right panels and lists the next steps.`,
                ),
                tasks(true),
            );
            if (p) {
                children.push(
                    h(
                        'div',
                        { class: 'start-project' },
                        icon('layers', 16),
                        h('strong', {}, p.name || p.id),
                        h(
                            'span',
                            { class: 'dim' },
                            `${p.levels.length} level${p.levels.length === 1 ? '' : 's'} in your project`,
                        ),
                        h('span', { class: 'spacer' }),
                        h(
                            'button',
                            { class: 'ui-btn sm', type: 'button', onclick: () => runCommand('panel.project') },
                            'Open the project',
                        ),
                    ),
                );
            }
            if (rec.length) {
                children.push(
                    h('h3', {}, 'Recent levels'),
                    h(
                        'div',
                        { class: 'start-levels' },
                        ...rec.map((l) => levelButton(l, true, (x) => a.openLevel(x.dirPath))),
                    ),
                );
            }
            children.push(
                h(
                    'details',
                    { class: 'start-all' },
                    h('summary', {}, 'Just look around: open any level'),
                    h(
                        'div',
                        { class: 'start-levels' },
                        ...levels.map((l) => levelButton(l, false, (x) => a.openLevel(x.dirPath))),
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
            );
        }
        children.push(
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
        );
        el.replaceChildren(...children.filter((c): c is Node => !!c));
    }

    projects.on(render);
    render();
    return { el, refresh };
}
