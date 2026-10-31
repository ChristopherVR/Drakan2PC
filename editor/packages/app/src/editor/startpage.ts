import { h, icon, store } from './dom.ts';
import './shellux.css';

/** First thing a modder sees in the Level tab: how to start, what the editor can do, recent levels. */
export interface StartPageActions {
    /** Ask the host to open a disc image (file picker or desktop dialog). */
    openIso(): void;
    /** Open a saved .level.json or world dump. */
    openDocument(f: File): void;
    openLevel(dirPath: string): void;
    openPalette(): void;
    openHelp(): void;
}

export interface LevelRef {
    name: string;
    dirPath: string;
}

export interface StartPage {
    el: HTMLElement;
    /** Re-render for the current disc (levels = null when no disc is open). */
    refresh(levels: LevelRef[] | null, isoName?: string): void;
}

const RECENT_KEY = 'recent-levels';
const MAX_RECENT = 4;

export const recentLevels = (): LevelRef[] =>
    store.json<LevelRef[]>(RECENT_KEY, []).filter((l) => l && typeof l.dirPath === 'string');

export function rememberLevel(l: LevelRef): void {
    store.set(
        RECENT_KEY,
        JSON.stringify([l, ...recentLevels().filter((r) => r.dirPath !== l.dirPath)].slice(0, MAX_RECENT)),
    );
}

const CAPABILITIES: { icon: string; title: string; text: string }[] = [
    {
        icon: 'move',
        title: 'Move things',
        text: 'Click an object, press W, drag an arrow. Snaps to a grid; End drops it on the ground.',
    },
    {
        icon: 'plus',
        title: 'Place creatures and items',
        text: 'Drag any class from the Place panel into the world, or search for it with Ctrl+P.',
    },
    {
        icon: 'sliders',
        title: 'Change how they behave',
        text: 'The Details panel shows health, speed, models and sounds with plain names.',
    },
    {
        icon: 'download',
        title: 'Make a mod',
        text: 'Export mod writes a pack the PC port loads on top of your own disc. The original is never changed.',
    },
];

export function createStartPage(a: StartPageActions): StartPage {
    const el = h('div', { class: 'start' });
    const docInput = h('input', { type: 'file', accept: '.json', hidden: true });
    docInput.addEventListener('change', () => {
        const f = docInput.files?.[0];
        docInput.value = '';
        if (f) a.openDocument(f);
    });

    const caps = h(
        'div',
        { class: 'start-caps' },
        ...CAPABILITIES.map((c) =>
            h('div', { class: 'start-cap' }, icon(c.icon, 18), h('strong', {}, c.title), h('span', {}, c.text)),
        ),
    );

    function refresh(levels: LevelRef[] | null, isoName?: string): void {
        const children: (Node | null)[] = [];
        if (!levels) {
            children.push(
                h('h2', {}, 'Open your game disc to start'),
                h(
                    'p',
                    { class: 'start-lead' },
                    'The editor reads levels straight from your own Drakan disc image. Nothing is uploaded and the disc is never modified.',
                ),
                h(
                    'div',
                    { class: 'start-actions' },
                    h(
                        'button',
                        { class: 'ui-btn primary lg', type: 'button', onclick: a.openIso },
                        icon('open', 15),
                        'Open disc image (.iso)',
                    ),
                    h(
                        'button',
                        { class: 'ui-btn lg', type: 'button', onclick: () => docInput.click() },
                        'Open a saved level (.json)',
                    ),
                ),
            );
        } else {
            const rec = recentLevels().filter((r) => levels.some((l) => l.dirPath === r.dirPath));
            children.push(
                h('h2', {}, 'Pick a level to edit'),
                h(
                    'p',
                    { class: 'start-lead' },
                    `${isoName ?? 'Disc'} is open. Choose a level; your edits are autosaved in this browser.`,
                ),
            );
            if (rec.length) {
                children.push(
                    h('h3', {}, 'Recent'),
                    h('div', { class: 'start-levels' }, ...rec.map((l) => levelButton(l, true))),
                );
            }
            children.push(
                h('h3', {}, 'All levels'),
                h('div', { class: 'start-levels' }, ...levels.map((l) => levelButton(l, false))),
                h(
                    'div',
                    { class: 'start-actions' },
                    h(
                        'button',
                        { class: 'ui-btn', type: 'button', onclick: () => docInput.click() },
                        'Open a saved level (.json)',
                    ),
                ),
            );
        }
        children.push(
            caps,
            h(
                'p',
                { class: 'start-foot' },
                h('button', { class: 'linkish', type: 'button', onclick: a.openPalette }, 'Search everything'),
                h('kbd', {}, 'Ctrl+P'),
                h('button', { class: 'linkish', type: 'button', onclick: a.openHelp }, 'Keyboard shortcuts'),
                h('kbd', {}, '?'),
            ),
            docInput,
        );
        el.replaceChildren(...children.filter((c): c is Node => !!c));
    }

    const levelButton = (l: LevelRef, recent: boolean): HTMLElement => {
        const m = /^(Level\s+\S+)\s*[-–]\s*(.*)$/.exec(l.name);
        return h(
            'button',
            { class: `start-level${recent ? ' recent' : ''}`, type: 'button', onclick: () => a.openLevel(l.dirPath) },
            h('span', { class: 'n' }, m ? m[1]! : l.name),
            h('span', { class: 'd' }, m ? m[2]! : ''),
        );
    };

    refresh(null);
    return { el, refresh };
}
