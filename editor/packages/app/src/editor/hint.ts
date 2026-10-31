import { h, icon, store } from './dom.ts';
import './shellux.css';

/**
 * A small dismissible tip strip at the bottom of the viewport while nothing is selected. Each tip is shown until the
 * user closes the strip; "don't show again" is remembered per browser.
 */
export interface HintHost {
    /** True while a tip is useful (a level is open and nothing is selected). */
    shown(): boolean;
    openPalette(): void;
}

const TIPS: (() => (Node | string)[])[] = [
    () => ['Click an object to select it. Drag on empty space to box select.'],
    () => ['Drag a class from the Content Browser into the world to add it.'],
    () => ['Press ', h('kbd', {}, 'Ctrl+P'), ' to find any object, class, level or command.'],
    () => ['Right-drag and use ', h('kbd', {}, 'WASD'), ' to fly. ', h('kbd', {}, 'F'), ' focuses the selection.'],
    () => ['Press ', h('kbd', {}, '?'), ' for every shortcut. ', h('kbd', {}, 'Ctrl+Z'), ' undoes.'],
];

export function createViewportHint(parent: HTMLElement, host: HintHost): void {
    if (store.get('hint-dismissed') === '1') return;
    const text = h('span', {});
    const close = h(
        'button',
        { class: 'x', type: 'button', 'aria-label': 'Hide tips', title: 'Hide tips' },
        icon('x', 12),
    );
    const el = h('div', { class: 'vp-hint', role: 'note', hidden: true }, icon('info', 14), text, close);
    parent.append(el);
    let i = 0;
    let dismissed = false;
    const setTip = (): void => {
        text.replaceChildren(...TIPS[i % TIPS.length]!());
    };
    close.addEventListener('click', () => {
        dismissed = true;
        el.hidden = true;
        store.set('hint-dismissed', '1');
    });
    setTip();
    window.setInterval(() => {
        if (dismissed) return;
        const want = host.shown();
        if (el.hidden !== !want) el.hidden = !want;
    }, 400);
    window.setInterval(() => {
        if (dismissed || el.hidden) return;
        i++;
        setTip();
    }, 9000);
}
