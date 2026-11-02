/**
 * Landscape mode, like Unreal Engine 5's: sculpt the ground (raise, lower, smooth, flatten, ramp, noise), paint it with
 * the level's own ground textures, and manage layers (pick, grow an edge, add a new one). The tools panel takes the
 * place of the Place panel on the left while the mode is on; the viewport shows the brush circle on the ground and
 * the layer outlines. Editing itself is in editor-core `terrain-edit.ts`; the live mesh in `scene/terrainlive.ts`.
 */
import * as THREE from 'three';
import {
    TerrainStroke,
    addLayerCommand,
    connectedLayers,
    expandLayerCommand,
    layerAt,
    terrainPalette,
    type BrushSettings,
    type LayerEdge,
    type SculptTool,
} from '@drakan/editor-core';
import { CELL_SIZE, TEX_HOLE, type TerrainLayer } from '@drakan/formats';
import { SCENE_SCALE } from '../../scene/terrain.ts';
import type { Ctx, Panel } from '../ctx.ts';
import { clamp, fill, h, icon, store } from '../dom.ts';
import { registerCommand } from '../registry.ts';
import { LandscapeOverlay } from './overlay.ts';
import { buildSwatches, type Swatch } from './palette.ts';
import { flushTerrain, levelTerrain, onTerrainChange } from './terrainbind.ts';
import './landscape.css';

const S = SCENE_SCALE;

export type LandscapeTool =
    'raise' | 'lower' | 'smooth' | 'flatten' | 'ramp' | 'noise' | 'paint' | 'hole' | 'select' | 'expand' | 'add';
type Group = 'sculpt' | 'paint' | 'manage';

interface ToolInfo {
    id: LandscapeTool;
    group: Group;
    label: string;
    /** One plain sentence: what a drag or click does. */
    how: string;
    brush: boolean;
}

const TOOLS: ToolInfo[] = [
    {
        id: 'raise',
        group: 'sculpt',
        label: 'Raise',
        how: 'Drag on the ground to build it up. Hold Shift to dig down.',
        brush: true,
    },
    {
        id: 'lower',
        group: 'sculpt',
        label: 'Lower',
        how: 'Drag to dig the ground down. Hold Shift to build it up.',
        brush: true,
    },
    {
        id: 'smooth',
        group: 'sculpt',
        label: 'Smooth',
        how: 'Drag to even out bumps and soften sharp edges.',
        brush: true,
    },
    {
        id: 'flatten',
        group: 'sculpt',
        label: 'Flatten',
        how: 'Drag to level the ground to the height where you start (or a fixed height).',
        brush: true,
    },
    {
        id: 'ramp',
        group: 'sculpt',
        label: 'Ramp',
        how: 'Drag from one point to another: the ground between them becomes a straight slope.',
        brush: true,
    },
    {
        id: 'noise',
        group: 'sculpt',
        label: 'Noise',
        how: 'Drag to roughen the ground with natural bumps.',
        brush: true,
    },
    {
        id: 'paint',
        group: 'paint',
        label: 'Paint',
        how: 'Pick a ground texture, then drag to paint it. Lower strength scatters it for a soft edge.',
        brush: true,
    },
    {
        id: 'hole',
        group: 'paint',
        label: 'Hole',
        how: 'Drag to cut holes (no ground, nothing to stand on). Hold Shift to fill them with the picked texture.',
        brush: true,
    },
    {
        id: 'select',
        group: 'manage',
        label: 'Pick layer',
        how: 'Click the ground to pick the layer it belongs to.',
        brush: false,
    },
    {
        id: 'expand',
        group: 'manage',
        label: 'Expand',
        how: 'Point near an edge of the picked layer and click to grow it there, or use the arrows below.',
        brush: false,
    },
    {
        id: 'add',
        group: 'manage',
        label: 'New layer',
        how: 'Click the ground to add a flat new layer there, at that height, with the picked texture.',
        brush: false,
    },
];

const GROUPS: { id: Group; label: string }[] = [
    { id: 'sculpt', label: 'Sculpt' },
    { id: 'paint', label: 'Paint' },
    { id: 'manage', label: 'Manage' },
];

/** Tool icons (24-unit viewBox, stroked like the shell's icon set). */
const ICONS: Record<string, string> = {
    landscape: '<path d="M2 19l6-9 4 5 3-4 7 8z"/><circle cx="17" cy="6" r="2"/>',
    raise: '<path d="M3 20l6-7 4 4 3-3 5 6z"/><path d="M12 3v7M9 6l3-3 3 3"/>',
    lower: '<path d="M3 20l6-7 4 4 3-3 5 6z"/><path d="M12 3v7M9 7l3 3 3-3"/>',
    smooth: '<path d="M3 15c3-4 6-4 9 0s6 4 9 0"/><path d="M3 9c3-2 6-2 9 0s6 2 9 0" opacity=".5"/>',
    flatten: '<path d="M3 17h18M6 13h12"/><path d="M12 3v6M9.5 6.5 12 9l2.5-2.5"/>',
    ramp: '<path d="M3 19 21 7v12z"/>',
    noise: '<path d="M3 15l3-5 3 7 3-9 3 8 3-4 3 3"/>',
    paint: '<path d="M15 4l5 5-8 8H7v-5z"/><path d="M4 21h5"/>',
    hole: '<rect x="3" y="4" width="18" height="16" rx="2"/><ellipse cx="12" cy="12" rx="5" ry="3.2"/>',
    select: '<path d="M4 8l8-4 8 4-8 4z"/><path d="M4 12l8 4 8-4M4 16l8 4 8-4"/>',
    expand: '<rect x="8" y="8" width="8" height="8" rx="1"/><path d="M12 2v4M12 18v4M2 12h4M18 12h4"/>',
    add: '<rect x="4" y="4" width="16" height="16" rx="2"/><path d="M12 8v8M8 12h8"/>',
    up: '<path d="M12 19V5M6 11l6-6 6 6"/>',
    down: '<path d="M12 5v14M6 13l6 6 6-6"/>',
    left: '<path d="M19 12H5M11 6l-6 6 6 6"/>',
    right: '<path d="M5 12h14M13 6l6 6-6 6"/>',
};

export function landscapeIcon(name: string, size = 16): SVGElement {
    const svg = icon('generic', size);
    svg.innerHTML = ICONS[name] ?? ICONS.landscape!;
    return svg;
}

interface Settings {
    tool: LandscapeTool;
    radius: number;
    strength: number;
    falloff: number;
    /** Edit the layers stitched to the one under the brush too (seams stay closed). */
    blend: boolean;
    flatten: 'start' | 'fixed';
    flattenHeight: number;
    paintRef: number | null;
    fillHoles: boolean;
    expandCells: number;
    newWidth: number;
    newHeight: number;
    outlines: boolean;
}

const DEFAULTS: Settings = {
    tool: 'raise',
    radius: 6144,
    strength: 0.35,
    falloff: 0.5,
    blend: true,
    flatten: 'start',
    flattenHeight: 0,
    paintRef: null,
    fillHoles: false,
    expandCells: 4,
    newWidth: 8,
    newHeight: 8,
    outlines: true,
};
const MIN_R = 512;
const MAX_R = 65536;
const KEY = 'landscape-settings';

const fmt = (n: number): string => Math.round(n).toLocaleString('en-US');
const cellsText = (r: number): string => {
    const c = (r * 2) / CELL_SIZE;
    return `${c < 10 ? c.toFixed(1).replace(/\.0$/, '') : Math.round(c)} cells across`;
};

export interface LandscapeHost {
    /** The left dock and the Place panel it normally shows. */
    leftEl: HTMLElement;
    placeEl: HTMLElement;
    /** Open the left dock. */
    showLeft(): void;
    /** The mode changed (sync the toolbar). */
    changed(): void;
}

export interface Landscape {
    panel: Panel;
    active(): boolean;
    setActive(on: boolean): void;
    toggle(): void;
}

export function installLandscape(ctx: Ctx, host: LandscapeHost): Landscape {
    const saved = store.json<Partial<Settings>>(KEY, {});
    const st: Settings = { ...DEFAULTS, ...saved };
    if (!TOOLS.some((t) => t.id === st.tool)) st.tool = DEFAULTS.tool;
    let on = false;
    let activeLayer = -1;
    let shift = false;
    let swatches: Swatch[] = [];
    const overlay = new LandscapeOverlay();
    let wired = false;
    let lastHit: [number, number, number] | null = null;
    let lastPointer: { clientX: number; clientY: number } | null = null;
    const connected = new Map<number, number[]>();
    const raycaster = new THREE.Raycaster();
    const ndc = new THREE.Vector2();

    const save = (): void => store.set(KEY, JSON.stringify(st));
    const info = (): ToolInfo => TOOLS.find((t) => t.id === st.tool)!;
    const terrain = (): readonly TerrainLayer[] | null => {
        const b = levelTerrain();
        return b ? (b.view.doc.terrain ?? b.base) : null;
    };

    // ---- panel ---------------------------------------------------------------------------------------------------

    const groupTabs = h('div', { class: 'ui-seg ls-groups', role: 'tablist', 'aria-label': 'Landscape tool groups' });
    const toolGrid = h('div', { class: 'ls-tools', role: 'toolbar', 'aria-label': 'Landscape tools' });
    const howText = h('p', { class: 'ls-how' });
    const brushBox = h('div', { class: 'ls-section ls-brush' });
    const optionsBox = h('div', { class: 'ls-section ls-options' });
    const footer = h('div', { class: 'ls-foot' });
    const body = h('div', { class: 'ls-body' }, groupTabs, toolGrid, howText, brushBox, optionsBox);
    const emptyBox = h(
        'div',
        { class: 'ls-empty' },
        landscapeIcon('landscape', 28),
        h('strong', {}, 'No ground to edit yet'),
        h('span', {}, 'Open a level from the disc. Its ground loads a moment after the objects.'),
    );
    const el = h(
        'div',
        { class: 'panel landscape', hidden: true },
        h(
            'div',
            { class: 'panel-head' },
            h('span', { class: 'panel-title' }, landscapeIcon('landscape', 14), 'Landscape'),
            h('span', { class: 'spacer' }),
            h(
                'button',
                {
                    class: 'btn sm ls-exit',
                    type: 'button',
                    title: 'Back to placing and moving objects (Shift+L)',
                    onclick: () => setActive(false),
                },
                'Done',
            ),
        ),
        emptyBox,
        body,
        footer,
    );
    host.leftEl.append(el);

    function slider(
        label: string,
        value: number,
        min: number,
        max: number,
        step: number,
        text: (v: number) => string,
        set: (v: number) => void,
        hint: string,
    ): HTMLElement {
        const out = h('span', { class: 'ls-val' }, text(value));
        const input = h('input', {
            type: 'range',
            min,
            max,
            step,
            value,
            'aria-label': label,
            title: hint,
        }) as HTMLInputElement;
        input.addEventListener('input', () => {
            const v = Number(input.value);
            set(v);
            out.textContent = text(v);
            save();
            refreshCursor();
        });
        return h('label', { class: 'ls-slider', title: hint }, h('span', { class: 'ls-lab' }, label), input, out);
    }

    // Size slider: logarithmic between MIN_R and MAX_R.
    const toLog = (r: number): number => Math.log(r / MIN_R) / Math.log(MAX_R / MIN_R);
    const fromLog = (t: number): number => MIN_R * Math.pow(MAX_R / MIN_R, t);

    function buildBrush(): void {
        const t = info();
        brushBox.hidden = !t.brush;
        if (!t.brush) return fill(brushBox);
        fill(
            brushBox,
            h(
                'div',
                { class: 'ls-head' },
                'Brush',
                h(
                    'span',
                    { class: 'ls-keys' },
                    h('kbd', { class: 'ui-kbd' }, '['),
                    h('kbd', { class: 'ui-kbd' }, ']'),
                    ' size',
                ),
            ),
            slider(
                st.tool === 'ramp' ? 'Width' : 'Size',
                toLog(st.radius),
                0,
                1,
                0.001,
                (v) => `${fmt(fromLog(v) * 2)} (${cellsText(fromLog(v))})`,
                (v) => (st.radius = fromLog(v)),
                'How wide the brush is, in game units (a ground cell is 2,048)',
            ),
            slider(
                'Strength',
                st.strength,
                0.01,
                1,
                0.01,
                (v) => `${Math.round(v * 100)}%`,
                (v) => (st.strength = v),
                st.tool === 'paint' || st.tool === 'hole'
                    ? 'Below 100% only some cells are painted, scattered, for a soft edge'
                    : 'How fast the ground changes while you drag',
            ),
            slider(
                'Falloff',
                st.falloff,
                0,
                1,
                0.01,
                (v) => `${Math.round(v * 100)}%`,
                (v) => (st.falloff = v),
                'How much of the brush edge fades out (0% = hard edge)',
            ),
        );
    }

    function check(label: string, value: boolean, set: (v: boolean) => void, hint: string): HTMLElement {
        const c = h('input', { type: 'checkbox', checked: value }) as HTMLInputElement;
        c.addEventListener('change', () => {
            set(c.checked);
            save();
            syncView();
        });
        return h('label', { class: 'ui-check ls-check', title: hint }, c, label);
    }

    function layerCard(): HTMLElement {
        const t = terrain();
        const L = t?.[activeLayer];
        if (!t || !L)
            return h('div', { class: 'ls-card ls-card-empty' }, 'No layer picked. Click the ground with Pick layer.');
        const lo = Math.min(...L.heights);
        const hi = Math.max(...L.heights);
        const doc = ctx.doc();
        const tag = doc.isAppendedLayer(activeLayer)
            ? 'New'
            : doc.terrainChanged.has(activeLayer)
              ? 'Edited'
              : 'Original';
        const kind = ['Floor', 'Ceiling', 'In between'][L.type] ?? 'Layer';
        return h(
            'div',
            { class: 'ls-card' },
            h(
                'div',
                { class: 'ls-card-title' },
                h('strong', {}, L.name || `Layer ${activeLayer}`),
                h('span', { class: `ls-tag ${tag.toLowerCase()}` }, tag),
            ),
            h('div', { class: 'ls-card-row' }, h('span', {}, kind), h('span', {}, `${L.width} x ${L.height} cells`)),
            h(
                'div',
                { class: 'ls-card-row' },
                h('span', {}, 'Height'),
                h('span', {}, lo === hi ? fmt(lo) : `${fmt(lo)} to ${fmt(hi)}`),
            ),
        );
    }

    function swatchGrid(): HTMLElement {
        const grid = h('div', { class: 'ls-swatches', role: 'listbox', 'aria-label': 'Ground textures' });
        if (!swatches.length)
            grid.append(h('p', { class: 'ls-hint' }, 'This level has no ground textures to paint with.'));
        for (const s of swatches) {
            const pct = s.share >= 0.01 ? `${Math.round(s.share * 100)}%` : s.cells ? 'under 1%' : 'unused';
            grid.append(
                h(
                    'button',
                    {
                        type: 'button',
                        class: `ls-swatch${s.ref === st.paintRef ? ' sel' : ''}`,
                        role: 'option',
                        'aria-selected': s.ref === st.paintRef ? 'true' : 'false',
                        title: `${s.name}: ${pct} of the ground. ${s.file}.`,
                        onclick: () => {
                            st.paintRef = s.ref;
                            if (st.tool !== 'paint' && st.tool !== 'add' && st.tool !== 'hole') st.tool = 'paint';
                            save();
                            rebuild();
                        },
                    },
                    s.url ? h('img', { src: s.url, alt: '', draggable: 'false' }) : h('span', { class: 'ls-noimg' }),
                    h('span', { class: 'ls-sw-name' }, s.name),
                    h('span', { class: 'ls-sw-share' }, pct),
                ),
            );
        }
        return grid;
    }

    function edgeButton(edge: LayerEdge, ico: string, label: string): HTMLElement {
        return h(
            'button',
            {
                type: 'button',
                class: `tool ls-edge ls-edge-${edge}`,
                title: `Grow the picked layer ${label}`,
                'aria-label': `Grow ${label}`,
                disabled: activeLayer < 0,
                onclick: () => expand(edge),
                onpointerenter: () => previewEdge(edge),
                onpointerleave: () => overlay.clearPreview(),
            },
            landscapeIcon(ico, 14),
        );
    }

    function numberField(
        label: string,
        value: number,
        min: number,
        max: number,
        set: (v: number) => void,
    ): HTMLElement {
        const input = h('input', {
            type: 'number',
            class: 'ui-input ls-num',
            min,
            max,
            step: 1,
            value,
            'aria-label': label,
        }) as HTMLInputElement;
        input.addEventListener('change', () => {
            const v = clamp(Math.round(Number(input.value) || min), min, max);
            input.value = String(v);
            set(v);
            save();
        });
        return h('label', { class: 'ls-field' }, h('span', { class: 'ls-lab' }, label), input);
    }

    function buildOptions(): void {
        const t = st.tool;
        const parts: (HTMLElement | null)[] = [];
        if (info().group === 'sculpt') {
            if (t === 'flatten') {
                const radio = (v: Settings['flatten'], label: string): HTMLElement => {
                    const r = h('input', {
                        type: 'radio',
                        name: 'ls-flat',
                        checked: st.flatten === v,
                    }) as HTMLInputElement;
                    r.addEventListener('change', () => {
                        st.flatten = v;
                        save();
                        buildOptions();
                    });
                    return h('label', { class: 'ui-check ls-check' }, r, label);
                };
                const hv = h('input', {
                    type: 'number',
                    class: 'ui-input ls-num',
                    step: 2,
                    value: Math.round(st.flattenHeight),
                    'aria-label': 'Fixed height',
                    disabled: st.flatten !== 'fixed',
                }) as HTMLInputElement;
                hv.addEventListener('change', () => {
                    st.flattenHeight = Number(hv.value) || 0;
                    save();
                });
                parts.push(
                    h('div', { class: 'ls-head' }, 'Level to'),
                    radio('start', 'The height where I start dragging'),
                    h('div', { class: 'ls-inline' }, radio('fixed', 'A fixed height'), hv),
                );
            }
            parts.push(
                check(
                    'Blend into neighbouring layers',
                    st.blend,
                    (v) => (st.blend = v),
                    'The ground is made of many layers stitched together. On: the brush edits them all so no gaps open at the seams. Off: only the layer under the brush.',
                ),
            );
        } else if (info().group === 'paint') {
            parts.push(
                h(
                    'div',
                    { class: 'ls-head' },
                    'Ground textures',
                    h('span', { class: 'ls-keys' }, `${swatches.length} in this level`),
                ),
            );
            parts.push(swatchGrid());
            if (t === 'paint')
                parts.push(
                    check(
                        'Fill holes too',
                        st.fillHoles,
                        (v) => (st.fillHoles = v),
                        'Also paint over holes and invisible cells, closing them',
                    ),
                );
            parts.push(
                check(
                    'Blend into neighbouring layers',
                    st.blend,
                    (v) => (st.blend = v),
                    'Paint every stitched layer under the brush, not only the one you start on',
                ),
            );
        } else {
            parts.push(h('div', { class: 'ls-head' }, 'Picked layer'), layerCard());
            if (t === 'expand') {
                parts.push(
                    h('div', { class: 'ls-head' }, 'Grow an edge'),
                    h(
                        'div',
                        { class: 'ls-compass' },
                        edgeButton('north', 'up', 'north (towards -Z)'),
                        edgeButton('west', 'left', 'west (towards -X)'),
                        h('span', { class: 'ls-compass-mid' }, landscapeIcon('select', 18)),
                        edgeButton('east', 'right', 'east (towards +X)'),
                        edgeButton('south', 'down', 'south (towards +Z)'),
                    ),
                    numberField('Cells to add', st.expandCells, 1, 64, (v) => (st.expandCells = v)),
                    h(
                        'p',
                        { class: 'ls-hint' },
                        'New ground continues at the edge height with the edge texture. Sculpt it afterwards.',
                    ),
                );
            } else if (t === 'add') {
                const sw = swatches.find((s) => s.ref === st.paintRef) ?? swatches[0];
                parts.push(
                    h('div', { class: 'ls-head' }, 'New layer'),
                    h(
                        'div',
                        { class: 'ls-inline' },
                        numberField('Width', st.newWidth, 1, 128, (v) => (st.newWidth = v)),
                        numberField('Depth', st.newHeight, 1, 128, (v) => (st.newHeight = v)),
                    ),
                    h(
                        'div',
                        { class: 'ls-card-row ls-tex' },
                        h('span', {}, 'Texture'),
                        h(
                            'span',
                            {},
                            sw ? sw.name : 'none',
                            ' ',
                            h(
                                'button',
                                { type: 'button', class: 'btn sm', onclick: () => setGroup('paint') },
                                'Change',
                            ),
                        ),
                    ),
                    h('p', { class: 'ls-hint' }, 'Sizes are in cells (2,048 units each).'),
                );
            }
            parts.push(
                check(
                    'Show all layer outlines',
                    st.outlines,
                    (v) => (st.outlines = v),
                    'Draw the border of every layer in the viewport',
                ),
            );
        }
        fill(optionsBox, ...parts);
    }

    function buildFooter(): void {
        const doc = ctx.doc();
        const n = doc.terrainChanged.size;
        fill(
            footer,
            h(
                'div',
                { class: 'ls-notice' },
                icon('info', 14),
                h(
                    'span',
                    {},
                    'The game sees ground edits only in a play test or an exported mod. The editor shows them at once.',
                ),
            ),
            h('div', { class: 'ls-count' }, n ? `${n} layer${n === 1 ? '' : 's'} changed` : 'Ground unchanged'),
        );
    }

    function setGroup(g: Group): void {
        if (info().group === g) return;
        st.tool = TOOLS.find((t) => t.group === g)!.id;
        save();
        rebuild();
    }

    function setTool(t: LandscapeTool): void {
        st.tool = t;
        if ((t === 'paint' || t === 'add') && st.paintRef === null) st.paintRef = swatches[0]?.ref ?? null;
        save();
        rebuild();
    }

    function rebuild(): void {
        const has = !!terrain();
        emptyBox.hidden = has;
        body.hidden = !has;
        const g = info().group;
        fill(
            groupTabs,
            ...GROUPS.map((x) =>
                h(
                    'button',
                    {
                        type: 'button',
                        role: 'tab',
                        class: `ui-seg-btn${x.id === g ? ' active' : ''}`,
                        'aria-selected': x.id === g ? 'true' : 'false',
                        onclick: () => setGroup(x.id),
                    },
                    x.label,
                ),
            ),
        );
        fill(
            toolGrid,
            ...TOOLS.filter((t) => t.group === g).map((t) =>
                h(
                    'button',
                    {
                        type: 'button',
                        class: `ls-tool${t.id === st.tool ? ' active' : ''}`,
                        'aria-pressed': t.id === st.tool ? 'true' : 'false',
                        title: t.how,
                        onclick: () => setTool(t.id),
                    },
                    landscapeIcon(t.id, 20),
                    h('span', {}, t.label),
                ),
            ),
        );
        howText.textContent = info().how;
        buildBrush();
        buildOptions();
        buildFooter();
        syncChip();
        syncView();
    }

    // ---- viewport ------------------------------------------------------------------------------------------------

    const chip = h('div', { class: 'ls-chip', hidden: true });

    function syncChip(): void {
        const t = info();
        const verb =
            (st.tool === 'raise' || st.tool === 'lower') && shift
                ? st.tool === 'raise'
                    ? 'Lower'
                    : 'Raise'
                : st.tool === 'hole' && shift
                  ? 'Fill holes'
                  : t.label;
        fill(
            chip,
            landscapeIcon(st.tool, 13),
            h('strong', {}, verb),
            h('span', {}, t.brush ? 'Drag on the ground' : 'Click the ground'),
        );
        chip.hidden = !on;
    }

    function canvas(): HTMLCanvasElement {
        return ctx.view().renderer.domElement;
    }

    function hitAt(e: { clientX: number; clientY: number }): [number, number, number] | null {
        const b = levelTerrain();
        if (!b) return null;
        const v = ctx.view();
        const r = canvas().getBoundingClientRect();
        ndc.set(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1);
        raycaster.setFromCamera(ndc, v.camera);
        const hit = raycaster.intersectObject(b.live.group, true)[0];
        return hit ? [hit.point.x / S, hit.point.y / S, hit.point.z / S] : null;
    }

    function syncView(): void {
        const v = ctx.view();
        const t = terrain();
        if (!on || !t) {
            overlay.hideAll();
            return;
        }
        if (!overlay.group.parent) v.scene.add(overlay.group);
        overlay.outlines(st.outlines && info().group === 'manage' ? t : null);
        overlay.activeLayer(info().group === 'manage' ? (t[activeLayer] ?? null) : null);
        if (info().group !== 'manage') overlay.hoverLayer(null);
        refreshCursor();
    }

    function refreshCursor(): void {
        const t = terrain();
        if (!on || !t || !lastHit || !info().brush) return overlay.hideBrush();
        const warm = (shift && (st.tool === 'raise' || st.tool === 'hole')) || (!shift && st.tool === 'lower');
        overlay.brush(t, lastHit[0], lastHit[1], lastHit[2], st.radius, st.falloff, warm);
    }

    function layerSet(index: number): number[] {
        const t = terrain()!;
        if (!st.blend) return [index];
        let c = connected.get(index);
        if (!c) {
            c = connectedLayers(t, index);
            for (const i of c) connected.set(i, c);
        }
        return c;
    }

    const brushOf = (): BrushSettings => ({ radius: st.radius, strength: st.strength, falloff: st.falloff });

    /** Edge of `L` nearest a world point. */
    function nearestEdge(L: TerrainLayer, x: number, z: number): LayerEdge {
        const px = x / CELL_SIZE;
        const pz = z / CELL_SIZE;
        const out = (v: number, a: number, b: number): number => Math.max(0, a - v, v - b);
        const x0 = L.originX;
        const x1 = L.originX + L.width;
        const z0 = L.originZ;
        const z1 = L.originZ + L.height;
        const d: [LayerEdge, number][] = [
            ['west', Math.abs(px - x0) + out(pz, z0, z1)],
            ['east', Math.abs(px - x1) + out(pz, z0, z1)],
            ['north', Math.abs(pz - z0) + out(px, x0, x1)],
            ['south', Math.abs(pz - z1) + out(px, x0, x1)],
        ];
        d.sort((a, b) => a[1] - b[1]);
        return d[0]![0];
    }

    function edgeRect(
        L: TerrainLayer,
        edge: LayerEdge,
        n: number,
    ): { x0: number; z0: number; x1: number; z1: number; y: number } {
        const x0 = L.originX;
        const z0 = L.originZ;
        const x1 = x0 + L.width;
        const z1 = z0 + L.height;
        const y = L.heights.reduce((a, b) => a + b, 0) / L.heights.length;
        if (edge === 'west') return { x0: x0 - n, z0, x1: x0, z1, y };
        if (edge === 'east') return { x0: x1, z0, x1: x1 + n, z1, y };
        if (edge === 'north') return { x0, z0: z0 - n, x1, z1: z0, y };
        return { x0, z0: z1, x1, z1: z1 + n, y };
    }

    function previewEdge(edge: LayerEdge): void {
        const L = terrain()?.[activeLayer];
        overlay.previewRect(L ? edgeRect(L, edge, st.expandCells) : null);
    }

    function expand(edge: LayerEdge): void {
        const doc = ctx.doc();
        try {
            const cmd = expandLayerCommand(doc, activeLayer, edge, st.expandCells);
            if (!cmd) return;
            ctx.view().run(cmd);
            connected.clear();
            ctx.toast(`${cmd.label}.`, 'ok');
        } catch (e) {
            ctx.toast(
                e instanceof Error && /edge of the level/.test(e.message)
                    ? 'This layer already touches the edge of the level map on that side.'
                    : `Could not expand: ${e instanceof Error ? e.message : String(e)}`,
                'warn',
            );
        }
    }

    function newLayerRect(p: [number, number, number]): { x0: number; z0: number; x1: number; z1: number; y: number } {
        const x0 = Math.max(0, Math.round(p[0] / CELL_SIZE - st.newWidth / 2));
        const z0 = Math.max(0, Math.round(p[2] / CELL_SIZE - st.newHeight / 2));
        return { x0, z0, x1: x0 + st.newWidth, z1: z0 + st.newHeight, y: p[1] };
    }

    function addLayerAt(p: [number, number, number]): void {
        const doc = ctx.doc();
        const t = terrain();
        const ref = st.paintRef ?? swatches[0]?.ref;
        if (!t || ref === undefined) return ctx.toast('Pick a ground texture first (Paint tab).', 'warn');
        const r = newLayerRect(p);
        const under = layerAt(t, p[0], p[2], p[1], 1e6);
        const cmd = addLayerCommand(
            doc,
            {
                originX: r.x0,
                originZ: r.z0,
                width: st.newWidth,
                height: st.newHeight,
                worldHeight: Math.round(p[1]),
                ref,
            },
            under >= 0 ? under : undefined,
        );
        if (!cmd) return;
        ctx.view().run(cmd);
        activeLayer = cmd.index;
        connected.clear();
        ctx.toast(`Added a ${st.newWidth} x ${st.newHeight} cell layer. Sculpt it, or Expand it from its edges.`, 'ok');
        rebuild();
    }

    // ---- strokes -------------------------------------------------------------------------------------------------

    interface Stroke {
        s: TerrainStroke;
        pointerId: number;
        last: number;
        raf: number;
        target?: number;
        a?: [number, number, number];
    }
    let stroke: Stroke | null = null;
    let seed = (Date.now() & 0xffff) | 1;

    function dab(): void {
        const k = stroke;
        if (!k) return;
        const now = performance.now();
        const dt = Math.min(0.05, (now - k.last) / 1000);
        k.last = now;
        const p = lastPointer ? hitAt(lastPointer) : null;
        if (p) lastHit = p;
        const at = lastHit;
        if (at) {
            const b = brushOf();
            const tool = st.tool;
            if (tool === 'paint' || tool === 'hole') {
                const ref = tool === 'hole' && !shift ? TEX_HOLE : st.paintRef;
                if (ref !== null) k.s.paint(ref, at[0], at[2], b, { fillHoles: tool === 'hole' || st.fillHoles });
            } else if (tool === 'ramp') {
                if (k.a) {
                    k.s.ramp(
                        k.a,
                        at,
                        b.radius * 2,
                        b.falloff,
                        b.strength < 0.99 ? Math.max(0.05, b.strength * 1.5) : 1,
                    );
                    overlay.rampLine(k.a, at, b.radius * 2);
                }
            } else {
                const sculpt: SculptTool = tool === 'lower' ? 'raise' : (tool as SculptTool);
                const invert = tool === 'lower' ? !shift : shift;
                k.s.sculpt(sculpt, at[0], at[2], b, dt, { invert, target: k.target });
            }
            flushTerrain();
            refreshCursor();
        }
        k.raf = requestAnimationFrame(dab);
    }

    function beginStroke(e: PointerEvent, p: [number, number, number]): void {
        const t = terrain();
        if (!t) return;
        const L = layerAt(t, p[0], p[2], p[1], 1500);
        if (L < 0) return;
        const set = layerSet(L);
        const s = new TerrainStroke(
            ctx.doc(),
            set,
            info().label + (st.tool === 'paint' ? ' ground texture' : ' ground'),
            seed++,
        );
        const target = st.tool === 'flatten' ? (st.flatten === 'fixed' ? st.flattenHeight : p[1]) : undefined;
        stroke = {
            s,
            pointerId: e.pointerId,
            last: performance.now(),
            raf: 0,
            target,
            a: st.tool === 'ramp' ? p : undefined,
        };
        canvas().setPointerCapture(e.pointerId);
        dab();
    }

    function endStroke(cancel = false): void {
        const k = stroke;
        if (!k) return;
        stroke = null;
        cancelAnimationFrame(k.raf);
        overlay.clearPreview();
        if (canvas().hasPointerCapture(k.pointerId)) canvas().releasePointerCapture(k.pointerId);
        if (cancel) {
            k.s.cancel();
            flushTerrain();
            return;
        }
        const cmd = k.s.finish();
        if (cmd) ctx.view().run(cmd);
    }

    // ---- pointer and keys ----------------------------------------------------------------------------------------

    const onDown = (e: PointerEvent): void => {
        if (!on || e.button !== 0 || e.altKey || e.target !== canvas()) return;
        e.stopPropagation();
        e.preventDefault();
        shift = e.shiftKey;
        lastPointer = e;
        const p = hitAt(e);
        if (!p) {
            if (st.tool !== 'add') ctx.toast('Point at the ground to use the landscape tools.');
            return;
        }
        lastHit = p;
        const t = terrain()!;
        if (info().brush) return beginStroke(e, p);
        const L = layerAt(t, p[0], p[2], p[1], 1e6);
        if (st.tool === 'select') {
            activeLayer = L;
            rebuild();
        } else if (st.tool === 'expand') {
            if (activeLayer < 0 || !t[activeLayer]) {
                activeLayer = L;
                rebuild();
                if (L >= 0) ctx.toast('Layer picked. Click near one of its edges to grow it.');
            } else expand(nearestEdge(t[activeLayer]!, p[0], p[2]));
        } else if (st.tool === 'add') addLayerAt(p);
    };

    let hoverRaf = 0;
    const onMove = (e: PointerEvent): void => {
        if (!on) return;
        lastPointer = e;
        if (stroke) {
            e.stopPropagation();
            shift = e.shiftKey;
            return;
        }
        // Hover: keep the object highlight off while landscaping, but let camera drags (middle, alt, right) through.
        if (e.buttons === 0 && e.target === canvas()) e.stopPropagation();
        if (hoverRaf) return;
        hoverRaf = requestAnimationFrame(() => {
            hoverRaf = 0;
            const lp = lastPointer;
            if (!lp || stroke) return;
            const p = hitAt(lp);
            lastHit = p;
            refreshCursor();
            const t = terrain();
            if (!t || !p) {
                overlay.hoverLayer(null);
                if (st.tool !== 'ramp') overlay.clearPreview();
                return;
            }
            if (st.tool === 'select' || st.tool === 'expand') {
                const L = layerAt(t, p[0], p[2], p[1], 1e6);
                overlay.hoverLayer(L >= 0 && L !== activeLayer ? t[L]! : null);
                const A = t[activeLayer];
                if (st.tool === 'expand' && A)
                    overlay.previewRect(edgeRect(A, nearestEdge(A, p[0], p[2]), st.expandCells));
            } else if (st.tool === 'add') overlay.previewRect(newLayerRect(p));
        });
    };

    const onUp = (e: PointerEvent): void => {
        if (!stroke || e.pointerId !== stroke.pointerId) return;
        e.stopPropagation();
        endStroke();
    };

    const onKey = (e: KeyboardEvent): void => {
        if (e.key === 'Shift' && shift !== (e.type === 'keydown')) {
            shift = e.type === 'keydown';
            syncChip();
            refreshCursor();
        }
        if (!on || e.type !== 'keydown') return;
        const tg = e.target as HTMLElement | null;
        if (tg && (/^(INPUT|TEXTAREA|SELECT)$/.test(tg.tagName) || tg.isContentEditable)) return;
        if (e.code === 'BracketLeft' || e.code === 'BracketRight' || e.key === '[' || e.key === ']') {
            e.preventDefault();
            const up = e.code === 'BracketRight' || e.key === ']';
            st.radius = clamp(st.radius * (up ? 1.25 : 0.8), MIN_R, MAX_R);
            save();
            buildBrush();
            refreshCursor();
        } else if (e.key === 'Escape' && stroke) {
            e.preventDefault();
            e.stopImmediatePropagation();
            endStroke(true);
            ctx.toast('Stroke cancelled');
        }
    };

    function wire(): void {
        if (wired) return;
        wired = true;
        const box = canvas().parentElement!;
        box.addEventListener('pointerdown', onDown, true);
        box.addEventListener('pointermove', onMove, true);
        box.addEventListener('pointerup', onUp, true);
        box.addEventListener('pointercancel', onUp, true);
        box.addEventListener('pointerleave', () => {
            if (!stroke) {
                lastHit = null;
                overlay.hideBrush();
            }
        });
        box.append(chip);
        window.addEventListener('keydown', onKey, true);
        window.addEventListener('keyup', onKey, true);
        window.addEventListener('blur', () => {
            shift = false;
            if (stroke) endStroke();
        });
    }

    // ---- terrain changes -----------------------------------------------------------------------------------------

    let outlineTimer = 0;
    onTerrainChange((d) => {
        if (!d || !d.rect) connected.clear();
        if (!d) {
            if (activeLayer >= (terrain()?.length ?? 0)) activeLayer = -1;
            refreshSwatches();
            if (on) rebuild();
            return;
        }
        if (!on || info().group !== 'manage') return;
        window.clearTimeout(outlineTimer);
        outlineTimer = window.setTimeout(syncView, stroke ? 250 : 0);
    });

    let baseRefsOf: readonly TerrainLayer[] | null = null;
    let baseRefs: number[] = [];
    function refreshSwatches(): void {
        const b = levelTerrain();
        const t = terrain();
        if (!b || !t) {
            swatches = [];
            return;
        }
        // The disc level's own textures stay in the palette even when painted away.
        if (baseRefsOf !== b.base) {
            baseRefsOf = b.base;
            baseRefs = terrainPalette(b.base).map((p) => p.ref);
        }
        swatches = buildSwatches(t, b.textures, baseRefs);
        if (st.paintRef !== null && !swatches.some((s) => s.ref === st.paintRef)) st.paintRef = null;
        if (st.paintRef === null) st.paintRef = swatches[0]?.ref ?? null;
    }

    // ---- mode ----------------------------------------------------------------------------------------------------

    function setActive(next: boolean): void {
        if (next === on) return;
        if (!next && stroke) endStroke();
        on = next;
        if (on) {
            wire();
            refreshSwatches();
            host.placeEl.hidden = true;
            el.hidden = false;
            host.showLeft();
            ctx.view().select([]);
        } else {
            el.hidden = true;
            host.placeEl.hidden = false;
            overlay.hideAll();
        }
        canvas().parentElement?.classList.toggle('ls-on', on);
        rebuild();
        host.changed();
    }

    registerCommand({
        id: 'landscape.mode',
        title: 'Landscape mode',
        category: 'Tools',
        keys: 'Shift+L',
        icon: 'ground',
        keywords: 'terrain ground sculpt paint hill raise lower smooth flatten biome texture expand layer',
        hint: 'Sculpt the ground, paint its textures and grow its layers (on/off)',
        run: () => setActive(!on),
    });
    for (const t of TOOLS)
        registerCommand({
            id: `landscape.tool.${t.id}`,
            title: `Landscape: ${t.label}`,
            category: 'Tools',
            keywords: 'terrain ground',
            hint: t.how,
            run: () => {
                setActive(true);
                setTool(t.id);
            },
        });

    const panel: Panel = {
        el,
        onLoad: () => {
            activeLayer = -1;
            connected.clear();
            if (on) {
                refreshSwatches();
                rebuild();
            }
        },
        onChange: () => {
            if (!on) return;
            refreshSwatches();
            buildFooter();
            if (info().group === 'manage') buildOptions();
            else if (info().group === 'paint' && !stroke) buildOptions();
        },
    };

    return { panel, active: () => on, setActive, toggle: () => setActive(!on) };
}
