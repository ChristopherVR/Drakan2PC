// Lets the project export (editor/project/export.ts) include the Menus panel's skin without importing the panel.
import type { ModMeta } from '@drakan/editor-core';

export interface MenusProvider {
    dirty(): boolean;
    packFiles(meta: ModMeta): Record<string, Uint8Array>;
}

let provider: MenusProvider | null = null;
export const setMenusProvider = (p: MenusProvider | null): void => {
    provider = p;
};
export const getMenusProvider = (): MenusProvider | null => provider;
