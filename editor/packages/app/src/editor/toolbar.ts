import { ANGLE_STEPS, GRID_SIZES, type AlignMode, type Axis } from '@drakan/editor-core';
import { KINDS, type CameraBookmark, type ToolMode } from '../level.ts';
import { HELPER_GROUPS, type HelperGroup } from '../scene/instances.ts';
import { LIGHTING, type LightingPreset, type ViewOptions } from '../scene/viewopts.ts';
import { fill, h, icon, iconButton } from './dom.ts';
import { buildQuickAddMenu, installQuickAdd } from './quickadd.ts';

/** What the toolbar can trigger and read; implemented by `levelui.ts`. */
export interface ToolbarActions {
    tool(m: ToolMode): void;
    toggleSpace(): void;
    toggleGridSnap(): void;
    toggleAngleSnap(): void;
    setGridSize(n: number): void;
    setAngleStep(n: number): void;
    undo(): void;
    redo(): void;
    duplicate(): void;
    remove(): void;
    copy(): void;
    paste(): void;
    frame(): void;
    frameAll(): void;
    topView(): void;
    toSnapGround(): void;
    align(axis: Axis, mode: AlignMode): void;
    distribute(axis: Axis): void;
    selectAll(): void;
    invert(): void;
    sameClass(): void;
    hide(): void;
    showAll(): void;
    lock(): void;
    save(): void;
    exportMod(): void;
    /** Save the open level as an engine-neutral Drakan Content Format package. */
    exportContent(): void;
    /** Show the Mod panel's steps for launching the mod in the game. */
    play(): void;
    help(): void;
    theme(): void;
    openLevel(dirPath: string): void;
    openDumpFile(f: File): void;
    toggleGridVisible(): void;
    setLinks(m: 'selection' | 'all' | 'off'): void;
    toggleKind(kind: string, shown: boolean): void;
    toggleGhosts(on: boolean): void;
    /** Display options (lighting, terrain, models, helpers, fog). `live` = a slider is moving: do not rebuild menus. */
    setViewOptions(patch: Partial<ViewOptions>, live?: boolean): void;
    setFlySpeed(v: number): void;
    saveBookmark(): void;
    gotoBookmark(i: number): void;
    removeBookmark(i: number): void;
    resetLayout(): void;
    togglePanel(k: 'right' | 'bottom'): void;
}

export interface ToolbarState {
    mode: ToolMode;
    space: 'world' | 'local';
    gridSnap: boolean;
    angleSnap: boolean;
    gridSize: number;
    angleStep: number;
    links: 'selection' | 'all' | 'off';
    gridVisible: boolean;
    flySpeed: number;
    hiddenKinds: Set<string>;
    bookmarks: CameraBookmark[];
    ghosts: boolean | null;
    viewOptions: ViewOptions;
    /** Objects per helper class group in the open level. */
    helperCounts: Record<HelperGroup, number>;
    hasLevels: boolean;
    levels: { name: string; dirPath: string; present: boolean }[];
    canUndo: boolean;
    canRedo: boolean;
    selCount: number;
    hasDoc: boolean;
    hasSource: boolean;
    structural: boolean;
}

export interface Toolbar {
    sync(s: ToolbarState): void;
    closeMenus(): void;
}

/** A button that opens a popover; `build` fills it each time it opens. Only one is open at a time. */
function menuButton(
    label: string,
    ico: string,
    build: (box: HTMLElement, close: () => void) => void,
    title: string,
    cls = 'tool wide',
): { wrap: HTMLElement; btn: HTMLButtonElement; close: () => void; refresh: () => void } {
    const btn = h(
        'button',
        { class: cls, type: 'button', title, 'aria-haspopup': 'true', 'aria-expanded': 'false' },
        icon(ico),
        h('span', {}, label),
        icon('chevron', 10),
    );
    const box = h('div', { class: 'popover', hidden: true, role: 'menu' });
    const wrap = h('div', { class: 'menuwrap' }, btn, box);
    const close = (): void => {
        box.hidden = true;
        btn.setAttribute('aria-expanded', 'false');
    };
    const refresh = (): void => {
        if (box.hidden) return;
        fill(box);
        build(box, close);
    };
    btn.addEventListener('click', (e) => {
        e.stopPropagation();
        const wasOpen = !box.hidden;
        document.dispatchEvent(new CustomEvent('ed-close-menus'));
        if (wasOpen) return;
        fill(box);
        build(box, close);
        box.hidden = false;
        btn.setAttribute('aria-expanded', 'true');
    });
    document.addEventListener('ed-close-menus', close);
    document.addEventListener('pointerdown', (e) => {
        if (!wrap.contains(e.target as Node)) close();
    });
    return { wrap, btn, close, refresh };
}

const group = (...kids: Node[]): HTMLElement => h('div', { class: 'tgroup' }, ...kids);
const sep = (): HTMLElement => h('span', { class: 'tsep', role: 'separator' });

/** Where the viewport's overlay toolbars live (top-left: display menus and camera; top-right: transform tools). */
export interface ViewportBars {
    left: HTMLElement;
    right: HTMLElement;
}

export function createToolbar(
    root: HTMLElement,
    a: ToolbarActions,
    getState: () => ToolbarState,
    bars: ViewportBars,
): Toolbar {
    const q: Record<string, HTMLButtonElement> = {};
    const reg = (id: string, b: HTMLButtonElement): HTMLButtonElement => ((q[id] = b), b);

    // ---- quick add (the "+ Add" entry point)
    installQuickAdd();
    const quickAdd = menuButton(
        'Add',
        'plus',
        (box, close) => buildQuickAddMenu(box, close, getState().hasLevels),
        'Create something: an arena, a puzzle, a cut scene, a dialogue line, a quest or a level copy',
        'btn primary sm qa-btn',
    );
    quickAdd.btn.id = 'ed-add';

    // ---- file
    const levelSelect = h('select', {
        id: 'ed-level',
        class: 'levelpick ui-select',
        title: 'Open a level from the disc',
        'aria-label': 'Open level',
    });
    levelSelect.addEventListener('change', () => {
        if (levelSelect.value) a.openLevel(levelSelect.value);
    });
    const dump = h('input', { type: 'file', accept: '.json', hidden: true, id: 'lv-load' });
    dump.addEventListener('change', () => {
        const f = dump.files?.[0];
        dump.value = '';
        if (f) a.openDumpFile(f);
    });
    const fileBtn = h(
        'label',
        { class: 'tool', title: 'Open a saved document (.level.json) or a world dump (.json)' },
        icon('open'),
        dump,
    );
    const fileGroup = group(
        levelSelect,
        fileBtn,
        reg(
            'save',
            iconButton('save', 'Save document (download .level.json)', a.save, { hint: 'Ctrl+S', id: 'ed-save' }),
        ),
    );

    // ---- tools
    const toolGroup = group(
        reg(
            'select',
            iconButton('select', 'Select (no gizmo)', () => a.tool('select'), { hint: 'Q', id: 'ed-tool-select' }),
        ),
        reg(
            'translate',
            iconButton('move', 'Move', () => a.tool('translate'), { hint: 'W', id: 'ed-tool-move' }),
        ),
        reg(
            'rotate',
            iconButton('rotate', 'Rotate (yaw)', () => a.tool('rotate'), { hint: 'E', id: 'ed-tool-rotate' }),
        ),
        reg(
            'scale',
            iconButton('scale', 'Scale', () => a.tool('scale'), { hint: 'R', id: 'ed-tool-scale' }),
        ),
    );
    const spaceBtn = reg(
        'space',
        h('button', {
            class: 'tool wide',
            type: 'button',
            title: 'Gizmo space: world or local (X)',
            onclick: a.toggleSpace,
            id: 'ed-space',
        }),
    );

    // ---- snapping
    const gridSel = h(
        'select',
        {
            class: 'mini',
            title: 'Grid size (game units; 2048 = one layer cell)',
            'aria-label': 'Grid size',
            id: 'ed-grid-size',
        },
        ...GRID_SIZES.map((n) => h('option', { value: n }, String(n))),
    );
    gridSel.addEventListener('change', () => a.setGridSize(Number(gridSel.value)));
    const angSel = h(
        'select',
        { class: 'mini', title: 'Angle step (degrees)', 'aria-label': 'Angle step', id: 'ed-angle-step' },
        ...ANGLE_STEPS.map((n) => h('option', { value: n }, `${n}°`)),
    );
    angSel.addEventListener('change', () => a.setAngleStep(Number(angSel.value)));
    const snapGroup = group(
        reg('gridSnap', iconButton('magnet', 'Grid snap', a.toggleGridSnap, { hint: 'G', id: 'ed-grid-snap' })),
        gridSel,
        reg(
            'angleSnap',
            iconButton('rotate', 'Angle snap', a.toggleAngleSnap, { hint: 'Shift+G', id: 'ed-angle-snap' }),
        ),
        angSel,
    );

    // ---- edit
    const editGroup = group(
        reg('undo', iconButton('undo', 'Undo', a.undo, { hint: 'Ctrl+Z', id: 'ed-undo' })),
        reg('redo', iconButton('redo', 'Redo', a.redo, { hint: 'Ctrl+Y', id: 'ed-redo' })),
        reg('dup', iconButton('copy', 'Duplicate', a.duplicate, { hint: 'Ctrl+D', id: 'ed-dup' })),
        reg('del', iconButton('trash', 'Delete', a.remove, { hint: 'Del', id: 'ed-del' })),
    );

    // ---- arrange menu
    const arrange = menuButton(
        'Arrange',
        'align',
        (box, close) => {
            const s = getState();
            const item = (label: string, fn: () => void, enabled: boolean, ico?: string): HTMLElement =>
                h(
                    'button',
                    {
                        class: 'mi',
                        type: 'button',
                        role: 'menuitem',
                        disabled: !enabled,
                        onclick: () => {
                            fn();
                            close();
                        },
                    },
                    ico ? icon(ico, 14) : h('span', { class: 'ph' }),
                    label,
                );
            box.append(h('div', { class: 'mh' }, 'Align (needs 2+ selected)'));
            for (const [ax, name] of [
                [0, 'X'],
                [1, 'Y'],
                [2, 'Z'],
            ] as [Axis, string][]) {
                for (const [m, ml] of [
                    ['min', 'min'],
                    ['center', 'centre'],
                    ['max', 'max'],
                ] as [AlignMode, string][]) {
                    box.append(
                        item(
                            `Align ${name} ${ml}`,
                            () => a.align(ax, m),
                            s.selCount >= 2 && s.structural !== undefined,
                        ),
                    );
                }
            }
            box.append(h('div', { class: 'mh' }, 'Distribute (needs 3+ selected)'));
            for (const [ax, name] of [
                [0, 'X'],
                [1, 'Y'],
                [2, 'Z'],
            ] as [Axis, string][])
                box.append(item(`Distribute along ${name}`, () => a.distribute(ax), s.selCount >= 3, 'distribute'));
            box.append(h('div', { class: 'mh' }, 'Placement'));
            box.append(item('Snap to ground', a.toSnapGround, s.selCount > 0, 'ground'));
        },
        'Align, distribute and snap to ground',
    );

    // ---- selection menu
    const selectMenu = menuButton(
        'Select',
        'select',
        (box, close) => {
            const s = getState();
            const item = (label: string, hint: string, fn: () => void, enabled = true): HTMLElement =>
                h(
                    'button',
                    {
                        class: 'mi',
                        type: 'button',
                        role: 'menuitem',
                        disabled: !enabled,
                        onclick: () => {
                            fn();
                            close();
                        },
                    },
                    h('span', { class: 'ph' }),
                    label,
                    h('span', { class: 'hint' }, hint),
                );
            box.append(
                item('Select all visible', 'Ctrl+A', a.selectAll, s.hasDoc),
                item('Invert selection', 'Ctrl+I', a.invert, s.hasDoc),
                item('Select same class', 'Ctrl+Shift+A', a.sameClass, s.selCount > 0),
                h('div', { class: 'mh' }, 'Visibility'),
                item('Hide selection', 'H', a.hide, s.selCount > 0),
                item('Show all', 'Alt+H', a.showAll, s.hasDoc),
                item('Lock / unlock selection', 'L', a.lock, s.selCount > 0),
                h('div', { class: 'mh' }, 'Clipboard'),
                item('Copy', 'Ctrl+C', a.copy, s.selCount > 0),
                item('Paste', 'Ctrl+V', a.paste, s.hasDoc),
            );
        },
        'Selection, visibility and clipboard',
    );

    // ---- view menu
    const view = menuButton(
        'Show',
        'eye',
        (box) => {
            const s = getState();
            const check = (label: string, on: boolean, fn: (v: boolean) => void): HTMLElement => {
                const cb = h('input', { type: 'checkbox', checked: on });
                cb.addEventListener('change', () => fn(cb.checked));
                return h('label', { class: 'mi check' }, cb, label);
            };
            const radio = (label: string, v: 'selection' | 'all' | 'off'): HTMLElement => {
                const r = h('input', { type: 'radio', name: 'ed-links', checked: s.links === v });
                r.addEventListener('change', () => a.setLinks(v));
                return h('label', { class: 'mi check' }, r, label);
            };
            box.append(
                check('Grid', s.gridVisible, a.toggleGridVisible),
                h('div', { class: 'mh' }, 'Link lines'),
                radio('Selected objects', 'selection'),
                radio('All links', 'all'),
                radio('Off', 'off'),
                h('div', { class: 'mh' }, 'Editor icons'),
                check('Draw helpers as icons', s.viewOptions.editorIcons, (v) => a.setViewOptions({ editorIcons: v })),
            );
            for (const g of HELPER_GROUPS) {
                const o = s.viewOptions;
                const n = s.helperCounts[g.id];
                const row = check(g.label, o.helpers[g.id], (v) =>
                    a.setViewOptions({ helpers: { ...o.helpers, [g.id]: v } }),
                );
                row.append(h('span', { class: 'dim' }, n ? ` (${n})` : ''));
                box.append(row);
            }
            box.append(h('div', { class: 'mh' }, 'Show kinds'));
            for (const k of KINDS) {
                const row = check(k.label, !s.hiddenKinds.has(k.kind), (v) => a.toggleKind(k.kind, v));
                row.prepend(h('i', { class: 'dot', style: `background:#${k.color.toString(16).padStart(6, '0')}` }));
                box.append(row);
            }
            if (s.ghosts !== null)
                box.append(
                    h('div', { class: 'mh' }, 'World dump'),
                    check('Show entities not in the level list', s.ghosts, a.toggleGhosts),
                );
            box.append(h('div', { class: 'mh' }, 'Panels'));
            for (const [k, l] of [
                ['right', 'Outliner and Details'],
                ['bottom', 'Content Browser and logs'],
            ] as const) {
                box.append(
                    h(
                        'button',
                        { class: 'mi', type: 'button', onclick: () => a.togglePanel(k) },
                        h('span', { class: 'ph' }),
                        `Toggle ${l}`,
                    ),
                );
            }
            box.append(
                h(
                    'button',
                    { class: 'mi', type: 'button', onclick: a.resetLayout },
                    h('span', { class: 'ph' }),
                    'Reset layout',
                ),
            );
        },
        'Show: grid, link lines, object kinds and panels',
    );

    // ---- display menu (terrain, models, helper classes, lighting, fog)
    const display = menuButton(
        'Display',
        'layers',
        (box) => {
            const s = getState();
            const o = s.viewOptions;
            const check = (label: string, on: boolean, fn: (v: boolean) => void, note = ''): HTMLElement => {
                const cb = h('input', { type: 'checkbox', checked: on });
                cb.addEventListener('change', () => fn(cb.checked));
                return h(
                    'label',
                    { class: 'mi check' },
                    cb,
                    label,
                    note ? h('span', { class: 'dim' }, ` ${note}`) : null,
                );
            };
            const markerRadio = (label: string, v: ViewOptions['markers']): HTMLElement => {
                const r = h('input', { type: 'radio', name: 'ed-markers', checked: o.markers === v });
                r.addEventListener('change', () => a.setViewOptions({ markers: v }));
                return h('label', { class: 'mi check' }, r, label);
            };
            const light = h(
                'select',
                { 'aria-label': 'Lighting preset' },
                ...(Object.keys(LIGHTING) as LightingPreset[]).map((k) =>
                    h('option', { value: k, selected: k === o.lighting }, LIGHTING[k].label),
                ),
            );
            light.addEventListener('change', () => a.setViewOptions({ lighting: light.value as LightingPreset }));
            const dist = h('input', {
                type: 'range',
                min: 20,
                max: 6000,
                step: 50,
                value: o.drawDistance,
                'aria-label': 'Draw distance',
                class: 'slider',
            });
            dist.addEventListener('input', () => a.setViewOptions({ drawDistance: Number(dist.value) }, true));
            box.append(
                h('div', { class: 'mh' }, 'Scene'),
                check('Terrain', o.terrain, (v) => a.setViewOptions({ terrain: v })),
                check('Terrain wireframe', o.wireframe, (v) => a.setViewOptions({ wireframe: v })),
                check('Textured models', o.models, (v) => a.setViewOptions({ models: v })),
                check(
                    'Play animations',
                    o.animate,
                    (v) => a.setViewOptions({ animate: v }),
                    '(idle, nearest on screen)',
                ),
                h('div', { class: 'mh' }, 'Markers (pyramids)'),
                markerRadio('Only where a model is missing', 'fallback'),
                markerRadio('On every object', 'all'),
                markerRadio('Off', 'off'),
            );
            box.append(
                h('div', { class: 'mh' }, 'Lighting and distance'),
                h('div', { class: 'disp-row' }, h('span', {}, 'Lighting'), light),
                check('Fog', o.fog, (v) => a.setViewOptions({ fog: v })),
                h('div', { class: 'disp-row' }, h('span', {}, 'Draw distance'), dist),
                check('Keep the fly camera above the ground', o.keepAboveGround, (v) =>
                    a.setViewOptions({ keepAboveGround: v }),
                ),
            );
        },
        'Terrain, models, helper classes, lighting, fog and draw distance',
    );

    // ---- camera
    const speed = h('input', {
        type: 'range',
        min: -20,
        max: 20,
        value: 0,
        class: 'slider fly',
        'aria-label': 'Camera speed',
        id: 'ed-fly',
    });
    const speedBox = h(
        'label',
        { class: 'camspeed', title: 'Camera speed (also the mouse wheel while flying)' },
        icon('gauge', 14),
        speed,
    );
    speed.addEventListener('input', () => a.setFlySpeed(2 ** (Number(speed.value) / 5)));
    const bookmarks = menuButton(
        'Views',
        'camera',
        (box, close) => {
            const s = getState();
            box.append(
                h(
                    'button',
                    {
                        class: 'mi',
                        type: 'button',
                        onclick: () => {
                            a.saveBookmark();
                            bookmarks.refresh();
                        },
                    },
                    icon('plus', 14),
                    'Save current view',
                    h('span', { class: 'hint' }, 'Ctrl+Shift+1-9'),
                ),
            );
            if (!s.bookmarks.length) box.append(h('div', { class: 'mh' }, 'No saved views yet'));
            s.bookmarks.forEach((b, i) => {
                box.append(
                    h(
                        'div',
                        { class: 'mi split' },
                        h(
                            'button',
                            {
                                class: 'plain',
                                type: 'button',
                                onclick: () => {
                                    a.gotoBookmark(i);
                                    close();
                                },
                            },
                            h('span', { class: 'hint left' }, `Shift+${i + 1}`),
                            b.name,
                        ),
                        h(
                            'button',
                            {
                                class: 'tool sm',
                                type: 'button',
                                title: 'Delete view',
                                'aria-label': 'Delete view',
                                onclick: () => {
                                    a.removeBookmark(i);
                                    bookmarks.refresh();
                                },
                            },
                            icon('x', 12),
                        ),
                    ),
                );
            });
        },
        'Camera bookmarks',
    );
    const camGroup = group(
        iconButton('focus', 'Frame selection', a.frame, { hint: 'F', id: 'ed-frame' }),
        iconButton('top', 'Top view', a.topView, { hint: 'T', id: 'ed-top' }),
        bookmarks.wrap,
    );

    // ---- right side
    const dirty = h('span', { class: 'dirty', id: 'ed-dirty' });
    const rightGroup = h(
        'div',
        { class: 'tgroup right' },
        dirty,
        reg(
            'export',
            h(
                'button',
                {
                    class: 'btn primary sm',
                    type: 'button',
                    id: 'ed-export',
                    title: 'Export the mod pack (Ctrl+E)',
                    onclick: a.exportMod,
                },
                icon('download', 14),
                'Export',
            ),
        ),
        reg(
            'play',
            h(
                'button',
                {
                    class: 'ui-btn play sm',
                    type: 'button',
                    id: 'ed-play',
                    title: 'Play this level in the game from where the camera looks (F5). No title, intro or save.',
                    onclick: a.play,
                },
                icon('play', 13),
                'Play',
            ),
        ),
        iconButton('sun', 'Toggle dark / light theme', a.theme, { hint: 'Ctrl+Shift+L', id: 'ed-theme' }),
        iconButton('help', 'Keyboard shortcuts', a.help, { hint: 'F1', id: 'ed-help' }),
    );

    root.append(
        quickAdd.wrap,
        sep(),
        fileGroup,
        sep(),
        editGroup,
        sep(),
        selectMenu.wrap,
        arrange.wrap,
        h('span', { class: 'spacer' }),
        rightGroup,
    );
    bars.left.append(view.wrap, display.wrap, sep(), camGroup);
    bars.right.append(toolGroup, spaceBtn, sep(), snapGroup, sep(), speedBox);

    let lastLevelsKey = '';
    return {
        closeMenus: () => document.dispatchEvent(new CustomEvent('ed-close-menus')),
        sync(s) {
            const on = (id: string, v: boolean): void => void q[id]?.classList.toggle('active', v);
            on('select', s.mode === 'select');
            on('translate', s.mode === 'translate');
            on('rotate', s.mode === 'rotate');
            on('scale', s.mode === 'scale');
            on('gridSnap', s.gridSnap);
            on('angleSnap', s.angleSnap);
            gridSel.value = String(s.gridSize);
            angSel.value = String(s.angleStep);
            fill(
                q.space!,
                icon(s.space === 'world' ? 'world' : 'local'),
                h('span', {}, s.space === 'world' ? 'World' : 'Local'),
            );
            q.undo!.disabled = !s.canUndo;
            q.redo!.disabled = !s.canRedo;
            q.dup!.disabled = q.del!.disabled = s.selCount === 0 || !s.structural;
            q.save!.disabled = !s.hasDoc;
            q.export!.disabled = !s.hasSource;
            q.play!.disabled = !s.hasSource;
            const key = s.levels.map((l) => l.dirPath).join('|');
            if (key !== lastLevelsKey) {
                lastLevelsKey = key;
                fill(
                    levelSelect,
                    h('option', { value: '' }, s.levels.length ? 'Open level...' : 'Open an ISO to list levels'),
                    ...s.levels.map((l) => h('option', { value: l.dirPath, disabled: !l.present }, l.name)),
                );
            }
            levelSelect.disabled = !s.levels.length;
            speed.value = String(Math.round(Math.log2(s.flySpeed) * 5));
            arrange.refresh();
            selectMenu.refresh();
            view.refresh();
            display.refresh();
        },
    };
}
