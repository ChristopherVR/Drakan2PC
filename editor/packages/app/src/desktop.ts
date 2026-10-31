// Desktop-shell glue: native menu actions, "open the last disc again" at start-up and the folder pickers. Does nothing
// in a plain browser (`initDesktop` returns inert functions), so the browser build is unaffected.

import * as platform from './platform.ts';
import { registerCommand } from './editor/registry.ts';

type Tab = 'inspect' | 'level' | 'save';

export interface DesktopHooks {
    openIso(src: platform.IsoSource): Promise<void>;
    showTab(tab: Tab): void;
    setStatus(msg: string, error?: boolean): void;
}

export interface Desktop {
    /** Native "open disc" dialog, then open it. */
    openIsoDialog(): void;
}

const click = (id: string): void => document.getElementById(id)?.click();

/** Send a keystroke to the editor's own shortcut handler (the menu items mirror its shortcuts). */
function press(key: string, mods: { ctrl?: boolean; shift?: boolean; alt?: boolean } = {}): void {
    const code = /^[a-z]$/i.test(key) ? `Key${key.toUpperCase()}` : key;
    document.body.dispatchEvent(
        new KeyboardEvent('keydown', {
            key,
            code,
            ctrlKey: !!mods.ctrl,
            shiftKey: !!mods.shift,
            altKey: !!mods.alt,
            bubbles: true,
            cancelable: true,
        }),
    );
}

function activeTab(): Tab {
    for (const t of ['level', 'save'] as const)
        if (document.getElementById(`tab-${t}`)?.classList.contains('active')) return t;
    return 'inspect';
}

function typingTarget(): HTMLInputElement | HTMLTextAreaElement | HTMLElement | null {
    const el = document.activeElement;
    if (el instanceof HTMLInputElement && !['checkbox', 'radio', 'button'].includes(el.type)) return el;
    if (el instanceof HTMLTextAreaElement) return el;
    return el instanceof HTMLElement && el.isContentEditable ? el : null;
}

async function pasteInto(el: HTMLElement): Promise<void> {
    const text = await navigator.clipboard.readText();
    el.focus();
    document.execCommand('insertText', false, text);
}

function editAction(kind: string): void {
    const typing = typingTarget();
    if (typing) {
        switch (kind) {
            case 'undo':
            case 'redo':
            case 'cut':
            case 'copy':
                document.execCommand(kind);
                return;
            case 'paste':
                void pasteInto(typing).catch(() => undefined);
                return;
            case 'select-all':
                if (typing instanceof HTMLInputElement || typing instanceof HTMLTextAreaElement) typing.select();
                else document.execCommand('selectAll');
                return;
        }
    }
    const tab = activeTab();
    if (tab === 'save') {
        if (kind === 'undo') click('sv-undo');
        else if (kind === 'redo') click('sv-redo');
        return;
    }
    if (tab !== 'level') return;
    const keys: Record<string, [string, { ctrl?: boolean }]> = {
        undo: ['z', { ctrl: true }],
        redo: ['y', { ctrl: true }],
        cut: ['x', { ctrl: true }],
        copy: ['c', { ctrl: true }],
        paste: ['v', { ctrl: true }],
        'select-all': ['a', { ctrl: true }],
        duplicate: ['d', { ctrl: true }],
        delete: ['Delete', {}],
    };
    const k = keys[kind];
    if (k) press(k[0], k[1]);
}

export function initDesktop(hooks: DesktopHooks): Desktop {
    if (!platform.isDesktop) return { openIsoDialog: () => undefined };

    const fail = (e: unknown): void => hooks.setStatus(e instanceof Error ? e.message : String(e), true);

    const openIsoDialog = (): void => {
        platform
            .pickIso()
            .then((src) => (src ? hooks.openIso(src) : undefined))
            .catch(fail);
    };

    const openRecent = async (index: number): Promise<void> => {
        const path = (await platform.getSettings()).recentIsos[index];
        if (!path) return;
        await hooks.openIso(await platform.openRecentIso(path));
    };

    const chooseFolder = (which: 'game' | 'mods'): void => {
        platform
            .pickFolder(which)
            .then((f) => {
                if (f) hooks.setStatus(`${which === 'game' ? 'Game' : 'Mods'} folder: ${f.path}`);
            })
            .catch(fail);
    };

    const tabs: Record<string, Tab> = { 'tab-inspect': 'inspect', 'tab-level': 'level', 'tab-save': 'save' };

    const onAction = (id: string): void => {
        if (id.startsWith('recent:')) {
            openRecent(Number(id.slice(7))).catch(fail);
            return;
        }
        if (id.startsWith('edit-')) {
            editAction(id.slice(5));
            return;
        }
        const tab = tabs[id];
        if (tab) {
            hooks.showTab(tab);
            return;
        }
        switch (id) {
            case 'open-iso':
                openIsoDialog();
                break;
            case 'open-save':
                hooks.showTab('save');
                click('sv-open');
                break;
            case 'open-save-folder':
                hooks.showTab('save');
                click('sv-folder');
                break;
            case 'save-document':
                hooks.showTab('level');
                press('s', { ctrl: true });
                break;
            case 'export-mod':
                hooks.showTab('level');
                press('e', { ctrl: true });
                break;
            case 'play':
                hooks.showTab('level');
                click('ed-play');
                break;
            case 'game-folder':
                chooseFolder('game');
                break;
            case 'mods-folder':
                chooseFolder('mods');
                break;
            case 'theme':
                press('l', { ctrl: true, shift: true });
                break;
            case 'help':
                press('F1');
                break;
        }
    };
    void platform.onMenuAction(onAction);

    registerCommand({
        id: 'desktop.gameFolder',
        title: 'Choose the game (play) folder...',
        category: 'File',
        hint: 'The folder with drakan.exe that Play and Write to mods folder use.',
        run: () => chooseFolder('game'),
    });
    registerCommand({
        id: 'desktop.modsFolder',
        title: 'Choose a separate mods folder...',
        category: 'File',
        hint: 'Only needed when mods live outside the game folder (the game gets DRAKAN_MODS).',
        run: () => chooseFolder('mods'),
    });

    // Re-open the last disc: only the directory is read, so this is quick even for a multi-GB image.
    platform
        .getSettings()
        .then((s) => (s.recentIsos.length ? openRecent(0) : undefined))
        .catch(() => hooks.setStatus('The last disc image could not be opened; use File > Open ISO.'));

    return { openIsoDialog };
}
