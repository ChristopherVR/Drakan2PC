/**
 * The top bar's menu bar (File, Edit, Window, Tools, Help) and the project chip. Every entry is a registered command
 * (registry.ts), so the menus, the command palette and the desktop app's native menu (menu.rs, which forwards
 * `cmd:<id>` items to `runCommand`) stay one list. The browser shows this bar; the desktop app hides it and uses its
 * native menu bar built from the same structure (`MENUS`).
 */
import { h, icon } from './dom.ts';
import { allCommands, getCommand, runCommand } from './registry.ts';
import { projects } from './project/store.ts';
import { contextMenu, closeContextMenu, type MenuItem } from './ui.ts';
import { workspace } from './workspace.ts';

/** `-` is a separator; `*window` expands to the registered editors. Missing commands are left out. */
export const MENUS: { id: string; label: string; items: string[] }[] = [
    {
        id: 'file',
        label: 'File',
        items: [
            'project.new',
            'project.open',
            'project.save',
            '-',
            'file.openIso',
            'file.openDocument',
            'file.save',
            '-',
            'file.export',
            'project.play',
            '-',
            'desktop.gameFolder',
            'desktop.modsFolder',
        ],
    },
    {
        id: 'edit',
        label: 'Edit',
        items: [
            'edit.undo',
            'edit.redo',
            'edit.history',
            '-',
            'edit.copy',
            'edit.paste',
            'edit.duplicate',
            'edit.delete',
            '-',
            'sel.all',
            'sel.invert',
            '-',
            'panel.project',
        ],
    },
    {
        id: 'window',
        label: 'Window',
        items: [
            'mode.level',
            '-',
            '*window',
            '-',
            'panel.show.assets',
            'panel.show.output',
            'panel.show.validation',
            '-',
            'view.right',
            'view.bottom',
            'view.reset',
        ],
    },
    {
        id: 'tools',
        label: 'Tools',
        items: [
            'mode.save',
            'mode.files',
            '-',
            'file.importOotf',
            'file.exportContent',
            'file.exportUnreal',
            '-',
            'view.theme',
        ],
    },
    { id: 'help', label: 'Help', items: ['help.guides', 'help.shortcuts', 'help.palette'] },
];

/** Window-menu order of the registered editors. */
const WINDOW_ORDER = ['project', 'mod', 'story', 'logic', 'multiplayer', 'vendors', 'menus'];

function menuItems(ids: string[]): MenuItem[] {
    const out: MenuItem[] = [];
    const push = (id: string): void => {
        const c = getCommand(id);
        if (!c) return;
        const enabled = !c.enabled || c.enabled();
        out.push({
            label: c.title.replace(/^Export > /, 'Export '),
            run: () => runCommand(id),
            ...(c.icon ? { icon: c.icon } : {}),
            ...(c.keys ? { hint: c.keys } : {}),
            disabled: !enabled,
        });
    };
    for (const id of ids) {
        if (id === '-') {
            if (out.length && !('sep' in out[out.length - 1]!)) out.push({ sep: true });
        } else if (id === '*window') {
            const eds = workspace()?.editors() ?? [];
            const rank = (x: string): number => {
                const i = WINDOW_ORDER.indexOf(x);
                return i < 0 ? 99 : i;
            };
            for (const e of [...eds].sort((a, b) => rank(a.id) - rank(b.id))) push(`panel.${e.id}`);
        } else push(id);
    }
    while (out.length && 'sep' in out[out.length - 1]!) out.pop();
    return out;
}

/** A button that drops a menu below itself; a second click closes it. */
function dropButton(label: HTMLElement | string, cls: string, build: () => MenuItem[], id?: string): HTMLElement {
    let openedAt = 0;
    let isOpen = false;
    const b = h(
        'button',
        { class: cls, type: 'button', 'aria-haspopup': 'menu', ...(id ? { id } : {}) },
        label,
    ) as HTMLButtonElement;
    b.addEventListener('pointerdown', () => {
        // The menu's outside-click handler closes it on this press; remember so the click does not reopen it.
        if (isOpen) openedAt = -1;
    });
    b.addEventListener('click', () => {
        if (openedAt === -1) {
            openedAt = 0;
            isOpen = false;
            return;
        }
        const r = b.getBoundingClientRect();
        closeContextMenu();
        contextMenu(r.left, r.bottom + 2, build());
        isOpen = true;
        openedAt = Date.now();
        const menu = document.querySelector<HTMLElement>('body > .ui-menu:last-of-type');
        menu?.classList.add('menubar-menu');
        const watch = new MutationObserver(() => {
            if (!menu?.isConnected) {
                isOpen = false;
                watch.disconnect();
            }
        });
        watch.observe(document.body, { childList: true });
    });
    // Moving across the bar with a menu open switches menus (like a desktop menu bar).
    b.addEventListener('pointerenter', () => {
        const other = document.querySelector('body > .ui-menu.menubar-menu');
        if (other && !isOpen) b.click();
    });
    return b;
}

export interface MenuBarOptions {
    /** The desktop app has a native menu bar: only the project chip is drawn. */
    desktop: boolean;
}

export function installMenuBar(o: MenuBarOptions): void {
    const bar = document.getElementById('menubar');
    if (bar) {
        if (o.desktop) bar.hidden = true;
        else
            bar.replaceChildren(
                ...MENUS.map((m) => dropButton(m.label, 'mb-item', () => menuItems(m.items), `menu-${m.id}`)),
            );
    }

    // ---- the project chip: the project is the global context ------------------------------------------------------
    const host = document.getElementById('project-chip-host');
    if (!host) return;
    const name = h('span', { class: 'pc-name' });
    const chip = dropButton(
        h('span', { class: 'pc-inner' }, icon('layers', 13), name, icon('chevron-down', 10)),
        'project-chip',
        () => {
            const p = projects.get();
            const items: MenuItem[] = [];
            items.push({ heading: p ? p.name || p.id : 'No project yet' });
            items.push(
                ...menuItems(['panel.project', 'project.save', '-', 'file.export', 'project.play', 'panel.mod']),
            );
            const recent = allCommands().filter((c) => c.id.startsWith('project.recent.'));
            const files = menuItems(['-', 'project.new', 'project.open']);
            if (files.length > 1) items.push(...files);
            if (recent.length) {
                items.push({ sep: true }, { heading: 'Recent projects' });
                for (const c of recent) items.push({ label: c.title, icon: 'folder', run: () => runCommand(c.id) });
            }
            return items;
        },
        'project-chip',
    );
    host.replaceWith(chip);
    const render = (): void => {
        const p = projects.get();
        name.textContent = p ? p.name || p.id : 'No project';
        chip.classList.toggle('none', !p);
        const dir = projects.folder?.dir;
        chip.title = p
            ? `Project "${p.name || p.id}"${dir ? ` in ${dir}` : ''}: ${p.levels.length} level${p.levels.length === 1 ? '' : 's'}. Click for Project Settings, Save, Export and Play.`
            : 'No mod project yet: it is created when you start a task or open Project Settings.';
    };
    projects.on(render);
    projects.onFolder(render);
    render();
}
