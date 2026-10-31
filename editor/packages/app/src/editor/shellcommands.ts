import { ANGLE_STEPS, GRID_SIZES, type AssetClass } from '@drakan/editor-core';
import type { LevelView } from '../level.ts';
import type { Ctx } from './ctx.ts';
import { fuzzyScore, registerCommand, registerSource } from './registry.ts';
import type { ToolbarActions } from './toolbar.ts';

/** What the built-in commands need beyond the toolbar actions. */
export interface ShellHost {
    actions: ToolbarActions;
    ctx: Ctx;
    view(): LevelView;
    levels(): { name: string; dirPath: string }[];
    openIso(): void;
    openPanel(tab: string): void;
    hasLevel(): boolean;
    selCount(): number;
    canUndo(): boolean;
    canRedo(): boolean;
    canExport(): boolean;
    catalog(): AssetClass[];
    clearSelection(): void;
    openPalette(initial?: string): void;
}

/** Register the editor's own commands and palette sources (levels, classes, objects, saved views). */
export function registerShellCommands(s: ShellHost): void {
    const a = s.actions;
    const lvl = (): boolean => s.hasLevel();
    const sel = (): boolean => s.selCount() > 0;
    const sel3 = (): boolean => s.selCount() >= 3;
    const cmd = (
        id: string,
        title: string,
        category: string,
        run: () => void,
        o: { keys?: string; icon?: string; enabled?: () => boolean; hint?: string; keywords?: string } = {},
    ): void => {
        registerCommand({ id, title, category, run, ...o });
    };

    // File
    cmd('file.openIso', 'Open disc image...', 'File', () => s.openIso(), {
        icon: 'open',
        hint: 'Choose your own Drakan disc image to read the levels from',
        keywords: 'iso load game',
    });
    cmd('file.save', 'Save document', 'File', () => a.save(), {
        keys: 'Ctrl+S',
        icon: 'save',
        enabled: lvl,
        hint: 'Download the level as a .level.json file you can reopen later',
    });
    cmd('file.export', 'Export mod...', 'File', () => a.exportMod(), {
        keys: 'Ctrl+E',
        icon: 'download',
        enabled: s.canExport,
        hint: 'Build the mod pack from the project and save it (zip, project folder, or the game)',
        keywords: 'publish build pack zip',
    });
    cmd('file.play', 'How to play your mod', 'File', () => a.play(), {
        icon: 'play',
        enabled: lvl,
        hint: 'Steps for loading the exported mod in the game',
        keywords: 'launch run test enable',
    });

    // Edit
    cmd('edit.undo', 'Undo', 'Edit', () => a.undo(), { keys: 'Ctrl+Z', icon: 'undo', enabled: s.canUndo });
    cmd('edit.redo', 'Redo', 'Edit', () => a.redo(), { keys: 'Ctrl+Y', icon: 'redo', enabled: s.canRedo });
    cmd('edit.history', 'Show undo history', 'Edit', () => s.openPanel('history'), {
        icon: 'history',
        hint: 'Every step you made, with labels; click one to jump back',
    });
    cmd('edit.duplicate', 'Duplicate selection', 'Edit', () => a.duplicate(), {
        keys: 'Ctrl+D',
        icon: 'copy',
        enabled: sel,
        keywords: 'clone copy',
    });
    cmd('edit.delete', 'Delete selection', 'Edit', () => a.remove(), {
        keys: 'Delete',
        icon: 'trash',
        enabled: sel,
        keywords: 'remove',
    });
    cmd('edit.copy', 'Copy', 'Edit', () => a.copy(), { keys: 'Ctrl+C', icon: 'copy', enabled: sel });
    cmd('edit.paste', 'Paste at cursor', 'Edit', () => a.paste(), { keys: 'Ctrl+V', enabled: lvl });
    cmd('edit.ground', 'Drop selection to the ground', 'Edit', () => a.toSnapGround(), {
        keys: 'End',
        icon: 'magnet',
        enabled: sel,
        keywords: 'snap terrain floor',
    });

    // Tools
    cmd('tool.select', 'Select tool', 'Tools', () => a.tool('select'), { keys: 'Q', icon: 'select' });
    cmd('tool.move', 'Move tool', 'Tools', () => a.tool('translate'), { keys: 'W', icon: 'move' });
    cmd('tool.rotate', 'Rotate tool', 'Tools', () => a.tool('rotate'), { keys: 'E', icon: 'rotate' });
    cmd('tool.scale', 'Scale tool', 'Tools', () => a.tool('scale'), { keys: 'R', icon: 'scale' });
    cmd('tool.space', 'Switch between world and local axes', 'Tools', () => a.toggleSpace(), { keys: 'X' });
    cmd('tool.grid', 'Toggle grid snap', 'Tools', () => a.toggleGridSnap(), { keys: 'G', icon: 'magnet' });
    cmd('tool.angle', 'Toggle angle snap', 'Tools', () => a.toggleAngleSnap(), { keys: 'Shift+G' });
    for (const n of GRID_SIZES) {
        cmd(`tool.gridSize.${n}`, `Grid size ${n}`, 'Tools', () => a.setGridSize(n), { keywords: 'snap step' });
    }
    for (const n of ANGLE_STEPS) {
        cmd(`tool.angleStep.${n}`, `Angle snap ${n} degrees`, 'Tools', () => a.setAngleStep(n), {
            keywords: 'rotation step',
        });
    }

    // Selection
    cmd('sel.all', 'Select all visible', 'Selection', () => a.selectAll(), { keys: 'Ctrl+A', enabled: lvl });
    cmd('sel.invert', 'Invert selection', 'Selection', () => a.invert(), { keys: 'Ctrl+I', enabled: lvl });
    cmd('sel.sameClass', 'Select everything of the same class', 'Selection', () => a.sameClass(), {
        keys: 'Ctrl+Shift+A',
        enabled: sel,
    });
    cmd('sel.clear', 'Clear selection', 'Selection', () => s.clearSelection(), { keys: 'Esc', enabled: sel });
    cmd('sel.hide', 'Hide selection', 'Selection', () => a.hide(), { keys: 'H', icon: 'eye', enabled: sel });
    cmd('sel.showAll', 'Show all hidden objects', 'Selection', () => a.showAll(), { keys: 'Alt+H', enabled: lvl });
    cmd('sel.lock', 'Lock or unlock selection', 'Selection', () => a.lock(), {
        keys: 'L',
        icon: 'lock',
        enabled: sel,
        hint: 'Locked objects cannot be moved or edited by accident',
    });
    const axes = ['X', 'Y', 'Z'] as const;
    const modes = { min: 'to the lowest', center: 'to the middle', max: 'to the highest' } as const;
    axes.forEach((ax, i) => {
        for (const m of ['min', 'center', 'max'] as const) {
            cmd(
                `arrange.align.${ax}.${m}`,
                `Align selection on ${ax} ${modes[m]}`,
                'Arrange',
                () => a.align(i as 0 | 1 | 2, m),
                {
                    enabled: () => s.selCount() >= 2,
                    keywords: 'line up',
                },
            );
        }
        cmd(
            `arrange.distribute.${ax}`,
            `Spread selection evenly along ${ax}`,
            'Arrange',
            () => a.distribute(i as 0 | 1 | 2),
            {
                enabled: sel3,
                keywords: 'distribute space',
            },
        );
    });

    // Camera and view
    cmd('cam.frame', 'Focus on selection', 'Camera', () => a.frame(), { keys: 'F', icon: 'focus', enabled: sel });
    cmd('cam.frameAll', 'Frame the whole level', 'Camera', () => a.frameAll(), { keys: 'Home', enabled: lvl });
    cmd('cam.top', 'Top-down view', 'Camera', () => a.topView(), { keys: 'T', enabled: lvl });
    cmd('cam.bookmark', 'Save the current camera view', 'Camera', () => a.saveBookmark(), {
        icon: 'camera',
        enabled: lvl,
        keywords: 'bookmark',
    });
    cmd('view.grid', 'Show or hide the grid', 'View', () => a.toggleGridVisible());
    cmd('view.links.selection', 'Show links of the selection only', 'View', () => a.setLinks('selection'));
    cmd('view.links.all', 'Show all links', 'View', () => a.setLinks('all'));
    cmd('view.links.off', 'Hide links', 'View', () => a.setLinks('off'));
    cmd('view.theme', 'Switch dark and light theme', 'View', () => a.theme(), { keys: 'Ctrl+Shift+L' });
    cmd('view.right', 'Toggle the Outliner and Details', 'View', () => a.togglePanel('right'));
    cmd('view.bottom', 'Toggle the bottom panel', 'View', () => a.togglePanel('bottom'));
    cmd('view.reset', 'Reset the panel layout', 'View', () => a.resetLayout());
    for (const [tab, title, ico] of [
        ['assets', 'Content Browser', 'folder-open'],
        ['output', 'Output Log', 'terminal'],
        ['validation', 'Validation', 'warn'],
        ['mod', 'Mod', 'download'],
    ] as const) {
        cmd(`panel.show.${tab}`, `Show ${title}`, 'Panels', () => s.openPanel(tab), { icon: ico });
    }

    // Help
    cmd('help.shortcuts', 'Keyboard shortcuts', 'Help', () => a.help(), { keys: 'F1', icon: 'help' });
    cmd('help.palette', 'Search everything', 'Help', () => s.openPalette(), { keys: 'Ctrl+P', icon: 'search' });

    // ---- dynamic sources ----------------------------------------------------------------------------------------
    registerSource({
        id: 'levels',
        label: 'Levels',
        prefix: ':',
        showEmpty: false,
        search(q, limit) {
            return s
                .levels()
                .map((l) => ({ l, sc: fuzzyScore(l.name, q) }))
                .filter((x) => x.sc > 0)
                .sort((x, y) => y.sc - x.sc)
                .slice(0, limit)
                .map(({ l }) => ({
                    id: l.dirPath,
                    title: `Open ${l.name}`,
                    icon: 'folder-open',
                    detail: 'Level',
                    run: () => a.openLevel(l.dirPath),
                }));
        },
    });
    registerSource({
        id: 'objects',
        label: 'Objects',
        prefix: '#',
        search(q, limit) {
            if (!lvl() || !q) return [];
            const v = s.view();
            const idQ = /^\d+$/.test(q) ? Number(q) : null;
            const out: { score: number; id: number; cls: string; kind: string }[] = [];
            const names = new Map<string, string>();
            const nameOf = (res: string, cls: string): string => {
                let n = names.get(res);
                if (n === undefined) {
                    n = s.catalog().find((c) => c.resource === res)?.name ?? cls;
                    names.set(res, n);
                }
                return n;
            };
            for (const o of v.doc.objects()) {
                const nm = nameOf(o.resource, o.class || 'Object');
                let sc = fuzzyScore(nm, q);
                if (idQ !== null && o.id === idQ) sc = 2000;
                else if (idQ !== null && String(o.id).startsWith(q)) sc = Math.max(sc, 200);
                if (sc > 0) out.push({ score: sc, id: o.id, cls: nm, kind: o.kind });
            }
            out.sort((x, y) => y.score - x.score || x.id - y.id);
            return out.slice(0, limit).map((o) => ({
                id: String(o.id),
                title: `${o.cls} #${o.id}`,
                detail: o.kind,
                icon: 'cube',
                run: () => s.ctx.locate([o.id]),
            }));
        },
    });
    registerSource({
        id: 'classes',
        label: 'Place a class',
        prefix: '@',
        search(q, limit) {
            if (!lvl() && !s.catalog().length) return [];
            return s
                .catalog()
                .map((c) => ({ c, sc: fuzzyScore(c.name, q) * 1.0 || fuzzyScore(c.category, q) * 0.5 }))
                .filter((x) => x.sc > 0)
                .sort((x, y) => y.sc - x.sc || y.c.instances - x.c.instances)
                .slice(0, limit)
                .map(({ c }) => ({
                    id: c.resource,
                    title: `Place ${c.name}`,
                    detail: c.instances ? `${c.category}, ${c.instances} in level` : c.category,
                    icon: 'plus',
                    run: () => s.ctx.placeClass(c),
                }));
        },
    });
    registerSource({
        id: 'views',
        label: 'Saved camera views',
        search(q, limit) {
            if (!q || !lvl()) return [];
            return s
                .view()
                .bookmarks.map((b, i) => ({ b, i, sc: fuzzyScore(b.name, q) }))
                .filter((x) => x.sc > 0)
                .slice(0, limit)
                .map(({ b, i }) => ({
                    id: `view${i}`,
                    title: `Go to ${b.name}`,
                    icon: 'camera',
                    run: () => a.gotoBookmark(i),
                }));
        },
    });
}
