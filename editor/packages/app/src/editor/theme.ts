import { store } from './dom.ts';

export type Theme = 'dark' | 'light';

/** Saved choice, else dark (the editor is a dark workbench by default, whatever the OS prefers). */
export function currentTheme(): Theme {
    const saved = store.get('theme');
    if (saved === 'dark' || saved === 'light') return saved;
    return 'dark';
}

/** Apply a theme to the whole page (`data-theme` on the root; tokens.css defines the tokens). */
export function applyTheme(t: Theme): void {
    document.documentElement.dataset.theme = t;
    document.documentElement.style.colorScheme = t;
    store.set('theme', t);
    for (const fn of listeners) fn(t);
}

const listeners = new Set<(t: Theme) => void>();
export const onThemeChange = (fn: (t: Theme) => void): void => void listeners.add(fn);

export function toggleTheme(): Theme {
    const next: Theme = currentTheme() === 'dark' ? 'light' : 'dark';
    applyTheme(next);
    return next;
}

/** Viewport colours per theme (scene background and grid). */
export const VIEW_COLORS: Record<Theme, { bg: number; major: number; minor: number }> = {
    dark: { bg: 0x121214, major: 0x3a3a44, minor: 0x25252b },
    light: { bg: 0xd9dbe0, major: 0x9aa0ad, minor: 0xc3c7cf },
};
