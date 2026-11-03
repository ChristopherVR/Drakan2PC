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

const changeListeners = new Set<() => void>();
/** The Menus panel changed its skin (the Campaign section's note says whether menus ship with the campaign). */
export const notifyMenusChanged = (): void => {
    for (const fn of changeListeners) fn();
};
export const onMenusChanged = (fn: () => void): (() => void) => {
    changeListeners.add(fn);
    return () => changeListeners.delete(fn);
};
