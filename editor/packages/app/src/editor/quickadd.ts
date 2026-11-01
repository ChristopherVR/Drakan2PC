import { h, icon } from './dom.ts';
import { openTemplate } from './logicpanel.ts';
import { getCommand, registerCommand, runCommand } from './registry.ts';
import { workspace } from './workspace.ts';

/**
 * Quick Add: the one "+ Add" entry point of the toolbar (the same idea as Unreal's Quick Add). Every row is a thing a
 * modder wants to make; choosing it opens the panel that makes it, already set up. Other features add their own row
 * with `registerQuickAdd` (a Vendor panel, a Menus page) instead of editing the toolbar.
 */
export interface QuickAddItem {
    /** Stable id; also the palette command `add.<id>`. */
    id: string;
    title: string;
    /** One short line under the title. */
    summary: string;
    icon: string;
    /** Opens the panel. Return false when it could not (the toolbar then says so). */
    run: () => boolean | void;
    /** Rows are listed by ascending order. */
    order?: number;
    /** Hide the row until this command exists (a feature another module registers later). */
    whenCommand?: string;
    /** Works without a disc (everything else asks for the disc image first). */
    noDisc?: boolean;
}

const items = new Map<string, QuickAddItem>();
const listeners = new Set<() => void>();

export function registerQuickAdd(item: QuickAddItem): void {
    items.set(item.id, item);
    registerCommand({
        id: `add.${item.id}`,
        title: `Add: ${item.title}`,
        category: 'Add',
        icon: item.icon,
        hint: item.summary,
        keywords: 'create new quick add',
        run: () => void item.run(),
    });
    listeners.forEach((l) => l());
}

export const quickAddItems = (): QuickAddItem[] =>
    [...items.values()]
        .filter((i) => !i.whenCommand || getCommand(i.whenCommand))
        .sort((a, b) => (a.order ?? 100) - (b.order ?? 100));

/** The rows every install has. Each one forwards to a command that already exists (tasks, story editors, Logic). */
function registerBuiltIns(): void {
    const via = (command: string) => (): boolean => runCommand(command);
    /** Story tools that place things in the world open below the viewport (camera and selection stay in view). */
    const besideViewport = (command: string) => (): boolean => {
        workspace()?.openIn('story', 'dock', false);
        return runCommand(command);
    };
    registerQuickAdd({
        id: 'arena',
        title: 'Arena (PvP map)',
        summary: 'Spawn points and pickups on a level of the game.',
        icon: 'rocket',
        order: 10,
        run: via('task.multiplayer'),
    });
    registerQuickAdd({
        id: 'puzzle',
        title: 'Puzzle (plates open a door)',
        summary: 'Pressure plates wired to a door, in the open level.',
        icon: 'link',
        order: 20,
        run: () => openTemplate('pressure-plates') || runCommand('panel.logic'),
    });
    registerQuickAdd({
        id: 'cutscene',
        title: 'Cut scene',
        summary: 'A camera path with a few steps.',
        icon: 'camera',
        order: 30,
        run: besideViewport('story.new-cutscene'),
    });
    registerQuickAdd({
        id: 'dialogue',
        title: 'Dialogue line',
        summary: 'Something a character says, with subtitles.',
        icon: 'edit',
        order: 40,
        run: besideViewport('story.new-dialogue'),
    });
    registerQuickAdd({
        id: 'quest',
        title: 'Quest',
        summary: 'Title, text and a marker in the world.',
        icon: 'check-circle',
        order: 50,
        run: besideViewport('story.quest-marker'),
    });
    // Rows of features that register their own panel: shown once the panel's command exists.
    registerQuickAdd({
        id: 'vendor',
        title: 'Vendor',
        summary: 'What a shopkeeper sells, and for how much.',
        icon: 'item',
        order: 60,
        whenCommand: 'panel.vendors',
        run: via('panel.vendors'),
    });
    registerQuickAdd({
        id: 'menus',
        title: 'Menu page',
        summary: 'Rename, reorder and restyle menu entries.',
        icon: 'list',
        order: 70,
        whenCommand: 'panel.menus',
        run: via('panel.menus'),
    });
    registerQuickAdd({
        id: 'level',
        title: 'Level copy',
        summary: 'A new level that starts as a copy of another.',
        icon: 'copy',
        order: 90,
        run: via('task.newlevel'),
    });
}

let installed = false;
export function installQuickAdd(): void {
    if (installed) return;
    installed = true;
    registerBuiltIns();
}

/** Fill the toolbar popover. `hasDisc` false: rows still work, the editor asks for the disc image first. */
export function buildQuickAddMenu(box: HTMLElement, close: () => void, hasDisc: boolean): void {
    box.classList.add('qa-pop');
    box.append(h('div', { class: 'mh' }, 'Create'));
    for (const it of quickAddItems()) {
        box.append(
            h(
                'button',
                {
                    class: 'qa-item',
                    type: 'button',
                    role: 'menuitem',
                    'data-add': it.id,
                    onclick: () => {
                        close();
                        it.run();
                    },
                },
                h('span', { class: 'qa-ico' }, icon(it.icon, 16)),
                h('span', { class: 'qa-text' }, h('strong', {}, it.title), h('span', {}, it.summary)),
            ),
        );
    }
    box.append(h('div', { class: 'mh' }, 'More'));
    const more = (label: string, ico: string, hint: string, fn: () => void): HTMLElement =>
        h(
            'button',
            {
                class: 'mi',
                type: 'button',
                role: 'menuitem',
                onclick: () => {
                    close();
                    fn();
                },
            },
            icon(ico, 14),
            label,
            h('span', { class: 'hint' }, hint),
        );
    box.append(more('Search or place an object...', 'search', 'Ctrl+P', () => runCommand('help.palette')));
    if (getCommand('file.importOotf'))
        box.append(more('Import an Order of the Flame level', 'download', '', () => runCommand('file.importOotf')));
    box.append(more('All guides', 'help', '', () => runCommand('help.guides')));
    if (!hasDisc) box.append(h('div', { class: 'qa-note' }, 'Your disc image is asked for first.'));
}
