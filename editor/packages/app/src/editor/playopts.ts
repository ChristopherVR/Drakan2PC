// Desktop-only "Play in game" controls of the Mod panel: game folder, single player or multiplayer test mode, and the
// Play button. The environment they produce is built by `launch.ts`.

import { PLAY_MODES, checkPlayOptions, parsePlayOptions, type PlayMode, type PlayOptions } from '../launch.ts';
import { getSettings, pickFolder } from '../platform.ts';
import { h, icon } from './dom.ts';

const KEY = 'drakan-editor:play-options';

function load(): PlayOptions {
    try {
        return parsePlayOptions(localStorage.getItem(KEY));
    } catch {
        return parsePlayOptions(null);
    }
}

export interface PlayControls {
    el: HTMLElement;
    read(): PlayOptions;
    /** Re-read the remembered folders and the button state. */
    refresh(): void;
}

export function createPlayControls(onPlay: () => void, canPlay: () => boolean): PlayControls {
    let opts = load();
    const save = (): void => {
        try {
            localStorage.setItem(KEY, JSON.stringify(opts));
        } catch {
            // private window or blocked storage: the options just are not remembered
        }
    };

    const folder = h('span', { class: 'dim small', id: 'play-folder' }, '...');
    const problem = h('div', { class: 'dim small bad', id: 'play-problem' });
    const playBtn = h(
        'button',
        {
            class: 'btn primary',
            type: 'button',
            id: 'play-now',
            title: 'Write the mod, enable it, and start the game',
            onclick: onPlay,
        },
        icon('play', 14),
        'Play now',
    );

    const field = (label: string, el: HTMLElement): HTMLElement =>
        h('label', { class: 'mf' }, h('span', {}, label), el);
    const text = (key: 'port' | 'address' | 'name', placeholder = ''): HTMLInputElement => {
        const el = h('input', { type: 'text', value: opts[key], placeholder, spellcheck: 'false' });
        el.addEventListener('input', () => {
            opts = { ...opts, [key]: el.value };
            save();
            update();
        });
        return el;
    };
    const check = (key: 'startLevel' | 'autoStart', label: string): HTMLElement => {
        const el = h('input', { type: 'checkbox' });
        el.checked = opts[key];
        el.addEventListener('change', () => {
            opts = { ...opts, [key]: el.checked };
            save();
        });
        return h(
            'label',
            { class: 'check', style: 'display:flex;gap:6px;align-items:center;font-size:12px' },
            el,
            label,
        );
    };

    const mode = h('select', { id: 'play-mode' });
    for (const m of PLAY_MODES) mode.append(new Option(m.label, m.id, false, m.id === opts.mode));
    mode.addEventListener('change', () => {
        opts = { ...opts, mode: mode.value as PlayMode };
        save();
        update();
    });
    const port = text('port', '27960');
    const address = text('address', 'host or host:port');
    const name = text('name', 'default');
    const portF = field('Port', port);
    const addrF = field('Address', address);
    const nameF = field('Player name', name);

    const gameBtn = h(
        'button',
        {
            class: 'btn',
            type: 'button',
            id: 'play-pick-folder',
            title: 'The game folder that holds drakan.exe',
            onclick: () =>
                void pickFolder('game').then(
                    () => refresh(),
                    (e) => (problem.textContent = String(e)),
                ),
        },
        icon('open', 14),
        'Game folder...',
    );

    function update(): void {
        const multi = opts.mode !== 'single';
        portF.hidden = opts.mode === 'single';
        addrF.hidden = opts.mode !== 'join';
        nameF.hidden = !multi;
        const bad = checkPlayOptions(opts);
        problem.textContent = bad ?? '';
        playBtn.disabled = !!bad || !canPlay();
    }

    function refresh(): void {
        getSettings().then(
            (s) => {
                folder.textContent = s.gameDir
                    ? `Game folder: ${s.gameDir}`
                    : 'Game folder: not set (asked on first Play)';
                folder.title = s.gameDir ?? '';
            },
            () => undefined,
        );
        update();
    }

    const el = h(
        'div',
        { class: 'play-desktop' },
        h('div', { class: 'lbl' }, icon('play', 14), 'Play in game'),
        h('div', { class: 'row2' }, field('Mode', mode), portF),
        h('div', { class: 'row2' }, addrF, nameF),
        check('startLevel', 'New Game starts this level'),
        check('autoStart', 'Skip title, memory card prompt and intro'),
        h('div', { class: 'btnrow' }, playBtn, gameBtn),
        folder,
        problem,
    );
    refresh();
    return { el, read: () => ({ ...opts }), refresh };
}
