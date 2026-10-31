import { store } from './dom.ts';

export type Theme = 'dark' | 'light';

/** Saved choice, else the OS preference. */
export function currentTheme(): Theme {
    const saved = store.get('theme');
    if (saved === 'dark' || saved === 'light') return saved;
    return window.matchMedia?.('(prefers-color-scheme: light)').matches ? 'light' : 'dark';
}

/** Apply a theme to the whole page (`data-theme` on the root; editor.css defines the tokens). */
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
    dark: { bg: 0x14161a, major: 0x3a4150, minor: 0x24282f },
    light: { bg: 0xdfe3ea, major: 0x9aa4b5, minor: 0xc2c9d4 },
};
