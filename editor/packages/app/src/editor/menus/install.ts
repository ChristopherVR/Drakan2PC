import type { ModMeta } from '@drakan/editor-core';
import type { LevelUi } from '../../levelui.ts';
import { registerPanel } from '../registry.ts';
import { setMenusProvider } from './bridge.ts';
import { createMenusPanel } from './panel.ts';
import './menus.css';

export interface MenusInstallHost {
    ui: LevelUi;
    download(name: string, data: BlobPart, type: string): void;
    setStatus(msg: string, error?: boolean): void;
}

/** Add the Menus dock tab (UI skin editor). Called once from `main.ts`. */
export function installMenuEditor(host: MenusInstallHost): void {
    const panel = createMenusPanel({
        target: (): ModMeta['target'] | null => host.ui.modTarget(),
        download: host.download,
        toast: (m, k) => host.ui.ctx().toast(m, k),
        setStatus: host.setStatus,
    });
    registerPanel({
        id: 'menus',
        title: 'Menus',
        icon: 'list',
        panel,
        command: {
            title: 'Open the Menus editor',
            keywords: 'menu ui hud theme title pause strings rename reorder colour accent skin',
            hint: 'Rename and reorder menu entries, restyle the theme and HUD, and export them as a mod',
        },
    });
    setMenusProvider({ dirty: () => panel.dirty(), packFiles: (meta) => panel.packFiles(meta) });
}
