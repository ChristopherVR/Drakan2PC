// A small modal that asks for one line of text (Save As asks for the new project's name). The native dialog keeps the
// focus inside, closes on Escape and hands the focus back to what opened it.

import { h } from './dom.ts';

export function askText(title: string, label: string, initial: string, okLabel = 'OK'): Promise<string | null> {
    return new Promise((resolve) => {
        const opener = document.activeElement instanceof HTMLElement ? document.activeElement : null;
        const input = h('input', { type: 'text', spellcheck: 'false', 'aria-label': label }) as HTMLInputElement;
        input.value = initial;
        const dlg = h('dialog', { class: 'pj-dlg ask-dlg', 'aria-label': title }) as HTMLDialogElement;
        let answer: string | null = null;
        const ok = h('button', { class: 'ui-btn primary', type: 'submit' }, okLabel);
        const form = h(
            'form',
            { method: 'dialog', class: 'ask-form' },
            h('h2', {}, title),
            h('label', { class: 'pj-f' }, h('span', {}, label), input),
            h('div', { class: 'ask-foot' }, h('button', { class: 'ui-btn', type: 'button', onclick: () => dlg.close() }, 'Cancel'), ok),
        );
        form.addEventListener('submit', () => {
            const v = input.value.trim();
            answer = v || null;
        });
        dlg.append(form);
        dlg.addEventListener('close', () => {
            dlg.remove();
            opener?.focus({ preventScroll: true });
            resolve(answer);
        });
        document.body.append(dlg);
        dlg.showModal();
        input.focus();
        input.select();
    });
}
