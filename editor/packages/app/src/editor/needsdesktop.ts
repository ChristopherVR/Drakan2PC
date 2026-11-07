// The browser build cannot start the game. Every Play entry that would have thrown shows this dialog instead: what
// Play does, and where the desktop app comes from. The desktop app never shows it.

import { h, icon } from './dom.ts';

export const DESKTOP_RELEASES_URL = 'https://github.com/ChristopherVR/Drakan2PC/releases';

const STYLE = `
dialog.nd-dlg { background: var(--surface-2); color: var(--fg); border: 1px solid var(--border-strong);
  border-radius: var(--r-lg); padding: 0; width: min(460px, 94vw); font: var(--fs-md) var(--font); }
dialog.nd-dlg::backdrop { background: rgba(0,0,0,.55); }
.nd-dlg .nd-head { display: flex; gap: var(--sp-3); align-items: center; padding: var(--sp-4) var(--sp-5);
  border-bottom: 1px solid var(--border); font-size: var(--fs-lg); }
.nd-dlg .nd-body { padding: var(--sp-4) var(--sp-5); display: grid; gap: var(--sp-3); color: var(--fg-muted); line-height: 1.5; }
.nd-dlg .nd-body b { color: var(--fg); font-weight: 600; }
.nd-dlg .nd-foot { padding: var(--sp-3) var(--sp-5); border-top: 1px solid var(--border); display: flex;
  gap: var(--sp-2); justify-content: flex-end; }
`;

/** Open the "Play needs the desktop app" dialog. Returns the dialog (tests inspect it). */
export function showPlayNeedsDesktop(): HTMLDialogElement {
    document.getElementById('nd-dlg')?.remove();
    if (!document.getElementById('nd-style')) {
        const st = document.createElement('style');
        st.id = 'nd-style';
        st.textContent = STYLE;
        document.head.append(st);
    }
    const dlg = h('dialog', { class: 'nd-dlg', id: 'nd-dlg', 'aria-label': 'Play needs the desktop app' });
    const close = h('button', { class: 'btn', type: 'button', onclick: () => dlg.close() }, 'Close');
    dlg.append(
        h('div', { class: 'nd-head' }, icon('play', 16), 'Play needs the desktop app'),
        h(
            'div',
            { class: 'nd-body' },
            h(
                'p',
                {},
                'A browser page cannot start the game. The desktop app starts it in the level you are editing, with your changes, on a throw-away memory card.',
            ),
            h(
                'p',
                {},
                h('b', {}, 'Here in the browser: '),
                'export the mod (Ctrl+E), put the zip in the game’s mods folder and turn it on in the game’s Mods settings.',
            ),
        ),
        h(
            'div',
            { class: 'nd-foot' },
            close,
            h(
                'a',
                { class: 'btn primary', href: DESKTOP_RELEASES_URL, target: '_blank', rel: 'noopener noreferrer' },
                'Get the desktop app',
            ),
        ),
    );
    document.body.append(dlg);
    dlg.addEventListener('close', () => dlg.remove());
    dlg.showModal();
    return dlg;
}
