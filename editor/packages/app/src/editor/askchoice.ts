// A small modal for a failed save at close: what went wrong and three ways forward. Escape means "keep the editor open".

import { h } from './dom.ts';
import type { CloseChoice } from '../closeflow.ts';

export function askCloseFailure(message: string): Promise<CloseChoice> {
    return new Promise((resolve) => {
        let settled = false;
        // Resolve straight from the click; the dialog's own close event is not relied on (it is delayed while the window is in the background).
        const finish = (c: CloseChoice): void => {
            if (settled) return;
            settled = true;
            dlg.remove();
            resolve(c);
        };
        const dlg = h('dialog', {
            class: 'pj-dlg ask-dlg',
            'aria-label': 'Your work could not be saved',
        }) as HTMLDialogElement;
        const pick = (c: CloseChoice) => () => finish(c);
        dlg.append(
            h(
                'form',
                { method: 'dialog', class: 'ask-form' },
                h('h2', {}, 'Your work could not be saved'),
                h('p', {}, message),
                h(
                    'p',
                    {},
                    'Close anyway discards what was not written. Keep editing leaves the editor open so you can export or save elsewhere.',
                ),
                h(
                    'div',
                    { class: 'ask-foot' },
                    h('button', { class: 'ui-btn', type: 'button', onclick: pick('close') }, 'Close anyway'),
                    h('button', { class: 'ui-btn', type: 'button', onclick: pick('cancel') }, 'Keep editing'),
                    h('button', { class: 'ui-btn primary', type: 'button', onclick: pick('retry') }, 'Try again'),
                ),
            ),
        );
        dlg.addEventListener('close', () => finish('cancel')); // Escape
        document.body.append(dlg);
        dlg.showModal();
    });
}
