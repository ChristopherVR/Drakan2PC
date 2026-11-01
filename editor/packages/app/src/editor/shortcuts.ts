import { h } from './dom.ts';

/** One keyboard shortcut. `keys` use `Ctrl+Shift+Z` notation; the matcher compares `KeyboardEvent.code`/`key`. */
export interface Shortcut {
    id: string;
    keys: string;
    label: string;
    group: string;
    /** Also fire while typing (rare: Esc). */
    inInput?: boolean;
    /** Also works while another editor tab (Story, Project Settings, ...) is shown instead of the Level Editor. */
    global?: boolean;
}

export const SHORTCUTS: Shortcut[] = [
    {
        id: 'palette',
        keys: 'Ctrl+P',
        label: 'Search everything (commands, levels, classes, objects)',
        group: 'Help',
        inInput: true,
        global: true,
    },
    {
        id: 'commands',
        keys: 'Ctrl+Shift+P',
        label: 'Command palette (commands only)',
        group: 'Help',
        inInput: true,
        global: true,
    },
    { id: 'select', keys: 'Q', label: 'Select tool', group: 'Tools' },
    { id: 'move', keys: 'W', label: 'Move tool', group: 'Tools' },
    { id: 'rotate', keys: 'E', label: 'Rotate tool', group: 'Tools' },
    { id: 'scale', keys: 'R', label: 'Scale tool', group: 'Tools' },
    { id: 'space', keys: 'X', label: 'Toggle local / world space', group: 'Tools' },
    { id: 'snap', keys: 'G', label: 'Toggle grid snap', group: 'Tools' },
    { id: 'angleSnap', keys: 'Shift+G', label: 'Toggle angle snap', group: 'Tools' },
    { id: 'ground', keys: 'End', label: 'Snap selection to ground', group: 'Tools' },
    { id: 'frame', keys: 'F', label: 'Focus (frame) selection', group: 'Camera' },
    { id: 'frameAll', keys: 'Home', label: 'Frame all', group: 'Camera' },
    { id: 'topView', keys: 'T', label: 'Top view', group: 'Camera' },
    { id: 'bookmark', keys: 'Ctrl+Shift+1..9', label: 'Save camera bookmark', group: 'Camera' },
    { id: 'gotoBookmark', keys: 'Shift+1..9', label: 'Jump to camera bookmark', group: 'Camera' },
    { id: 'fly', keys: 'Right mouse + WASD / QE', label: 'Fly camera (Shift = fast, wheel = speed)', group: 'Camera' },
    { id: 'orbit', keys: 'Alt + left mouse', label: 'Orbit around the focus point', group: 'Camera' },
    { id: 'pan', keys: 'Middle mouse', label: 'Pan', group: 'Camera' },
    { id: 'delete', keys: 'Delete', label: 'Delete selection', group: 'Edit' },
    { id: 'duplicate', keys: 'Ctrl+D', label: 'Duplicate', group: 'Edit' },
    { id: 'altdrag', keys: 'Alt + drag gizmo', label: 'Duplicate while moving', group: 'Edit' },
    { id: 'copy', keys: 'Ctrl+C', label: 'Copy', group: 'Edit' },
    { id: 'paste', keys: 'Ctrl+V', label: 'Paste (at the cursor)', group: 'Edit' },
    { id: 'undo', keys: 'Ctrl+Z', label: 'Undo', group: 'Edit' },
    { id: 'redo', keys: 'Ctrl+Y', label: 'Redo (also Ctrl+Shift+Z)', group: 'Edit' },
    { id: 'selectAll', keys: 'Ctrl+A', label: 'Select all visible', group: 'Selection' },
    { id: 'invert', keys: 'Ctrl+I', label: 'Invert selection', group: 'Selection' },
    { id: 'sameClass', keys: 'Ctrl+Shift+A', label: 'Select all of the same class', group: 'Selection' },
    { id: 'hide', keys: 'H', label: 'Hide selection', group: 'Selection' },
    { id: 'showAll', keys: 'Alt+H', label: 'Show all', group: 'Selection' },
    { id: 'lock', keys: 'L', label: 'Lock / unlock selection', group: 'Selection' },
    { id: 'deselect', keys: 'Esc', label: 'Clear selection / cancel a pick', group: 'Selection', inInput: false },
    { id: 'box', keys: 'Drag on empty space', label: 'Box select (Shift/Ctrl adds)', group: 'Selection' },
    { id: 'save', keys: 'Ctrl+S', label: 'Save (the project, or the level document)', group: 'File', global: true },
    { id: 'export', keys: 'Ctrl+E', label: 'Export mod', group: 'File', global: true },
    { id: 'projectSettings', keys: 'Ctrl+,', label: 'Project Settings', group: 'Window', global: true },
    { id: 'contentBrowser', keys: 'Ctrl+Space', label: 'Content Browser', group: 'Window', global: true },
    { id: 'story', keys: 'Ctrl+Shift+Y', label: 'Story editor', group: 'Window', global: true },
    { id: 'logic', keys: 'Ctrl+Shift+K', label: 'Logic graph', group: 'Window', global: true },
    { id: 'history', keys: 'Ctrl+Shift+H', label: 'Undo History', group: 'Window', global: true },
    { id: 'help', keys: 'F1', label: 'Show this help (also ?)', group: 'Help', inInput: true, global: true },
    { id: 'theme', keys: 'Ctrl+Shift+L', label: 'Toggle dark / light theme', group: 'Help', global: true },
];

export type Handler = (e: KeyboardEvent, arg?: number) => void;

function matches(e: KeyboardEvent, keys: string): boolean {
    const parts = keys.split('+');
    const key = parts[parts.length - 1]!;
    const want = { ctrl: false, shift: false, alt: false };
    for (const p of parts.slice(0, -1)) {
        const l = p.toLowerCase();
        if (l === 'ctrl' || l === 'cmd') want.ctrl = true;
        else if (l === 'shift') want.shift = true;
        else if (l === 'alt') want.alt = true;
    }
    if ((e.ctrlKey || e.metaKey) !== want.ctrl || e.shiftKey !== want.shift || e.altKey !== want.alt) return false;
    if (key.length === 1 && /[A-Z]/.test(key)) return e.code === `Key${key}`;
    if (key.length === 1 && /[0-9]/.test(key)) return e.code === `Digit${key}`;
    // The sheet says "Esc"; the key event says "Escape".
    const k = key === 'Esc' ? 'Escape' : key;
    return e.code === k || e.key === k;
}

/**
 * Global key handler. `active()` says whether the editor tab is showing; shortcuts never fire while typing in a field
 * (except those marked `inInput`). Digit shortcuts (`..9`) call the handler with the digit.
 */
export function initShortcuts(
    handlers: Record<string, Handler>,
    active: () => boolean,
    globalOnly: () => boolean = () => false,
): () => void {
    const onKey = (e: KeyboardEvent): void => {
        if (!active()) return;
        const onlyGlobal = globalOnly();
        const t = e.target as HTMLElement | null;
        const typing = !!t && (/^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName) || t.isContentEditable);
        // Esc belongs to an open picker, popover or dialog (it closes that, not the selection).
        if (e.key === 'Escape' && (e.defaultPrevented || t?.closest?.('.ed-pop, [role="dialog"], .ui-menu'))) return;
        // Bookmarks: Shift+digit jumps, Ctrl+Shift+digit saves.
        const digit = /^Digit([1-9])$/.exec(e.code);
        if (digit && !typing && e.shiftKey && !e.altKey && !onlyGlobal) {
            const fn = handlers[e.ctrlKey || e.metaKey ? 'bookmark' : 'gotoBookmark'];
            if (fn) {
                e.preventDefault();
                fn(e, Number(digit[1]) - 1);
                return;
            }
        }
        if (!typing && e.key === '?' && handlers.help) {
            e.preventDefault();
            handlers.help(e);
            return;
        }
        for (const s of SHORTCUTS) {
            if (s.keys.includes('..') || s.keys.includes(' ')) continue;
            const fn = handlers[s.id];
            if (!fn || (typing && !s.inInput) || (onlyGlobal && !s.global)) continue;
            // Alternative bindings
            const alt = s.id === 'redo' && matches(e, 'Ctrl+Shift+Z');
            const del = s.id === 'delete' && matches(e, 'Backspace');
            if (alt || del || matches(e, s.keys)) {
                e.preventDefault();
                fn(e);
                return;
            }
        }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
}

/** The help overlay: shortcuts grouped, plus a short mouse guide. Click outside or Esc closes it. */
export function helpOverlay(): { el: HTMLElement; toggle(open?: boolean): void } {
    const groups = new Map<string, Shortcut[]>();
    for (const s of SHORTCUTS) {
        let l = groups.get(s.group);
        if (!l) groups.set(s.group, (l = []));
        l.push(s);
    }
    const card = h('div', { class: 'help-card', role: 'dialog', 'aria-label': 'Keyboard shortcuts' });
    card.append(h('h2', {}, 'Keyboard and mouse'));
    const cols = h('div', { class: 'help-cols' });
    for (const [g, list] of groups) {
        const sec = h('section', {}, h('h3', {}, g));
        for (const s of list) {
            const keys = h('span', { class: 'keys' });
            for (const k of s.keys.split(/ \+ /).length > 1 && s.keys.includes(' ') ? [s.keys] : s.keys.split('+')) {
                keys.append(h('kbd', {}, k));
            }
            sec.append(h('div', { class: 'help-row' }, keys, h('span', {}, s.label)));
        }
        cols.append(sec);
    }
    card.append(cols);
    card.append(
        h(
            'p',
            { class: 'help-foot' },
            'Drag a class from the Asset Browser into the viewport to place it. Changes are autosaved in this browser; ' +
                'Export writes a mod pack for the game.',
        ),
    );
    const el = h('div', { class: 'help-overlay', hidden: true }, card);
    const toggle = (open?: boolean): void => {
        el.hidden = open === undefined ? !el.hidden : !open;
    };
    el.addEventListener('pointerdown', (e) => {
        if (e.target === el) toggle(false);
    });
    window.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' && !el.hidden) {
            toggle(false);
            e.stopPropagation();
        }
    });
    return { el, toggle };
}
