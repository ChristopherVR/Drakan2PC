// A small modal for a failed save at close: what went wrong and three ways forward. Escape means "keep the editor open".

import { h } from './dom.ts';
import type { CloseChoice } from '../closeflow.ts';

export function askCloseFailure(message: string): Promise<CloseChoice> {
    return new Promise((resolve) => {
        let answer: CloseChoice = 'cancel';
        const dlg = h('dialog', {
            class: 'pj-dlg ask-dlg',
            'aria-label': 'Your work could not be saved',
        }) as HTMLDialogElement;
        const pick = (c: CloseChoice) => () => {
            answer = c;
            dlg.close();
        };
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
        dlg.addEventListener('close', () => {
            dlg.remove();
            resolve(answer);
        });
        document.body.append(dlg);
        dlg.showModal();
    });
}
