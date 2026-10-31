import {
    ApplyTemplateCommand,
    TEMPLATES,
    addLink,
    buildLogicGraph,
    destinationLevelName,
    isError,
    layoutGraph,
    logicOf,
    numField,
    removeLink,
    resourceParts,
    setMessageField,
    validateLogic,
    type ExitIndex,
    type ExitIndexEntry,
    type LevelObject,
    type LogicEdge,
    type LogicEnv,
    type LogicGraph,
    type LogicTemplate,
    type ParamValues,
    type Placement,
    type TemplateParam,
    type ValidationIssue,
} from '@drakan/editor-core';
import { MESSAGE_NAMES, exitDestinationText, type LogicRole } from '@drakan/formats';
import type { ExitIndexData } from '../logicdata.ts';
import type { Ctx, Panel } from './ctx.ts';
import { fill, h, icon, raf } from './dom.ts';
import { buildFieldsPanel } from './fieldsui.ts';
import { addValidationProvider } from './panels.ts';
import { registerCommand, registerPanel, runCommand } from './registry.ts';

import { exitEditHost, onExitEditChange } from '../story/exitbridge.ts';
import { createVolumeLayer, type VolumeMode } from './logicview.ts';

/** Set by the open Logic panel: opens a template form with some parameters preset. */
let openTemplateHook: ((id: string, preset: ParamValues) => void) | null = null;

/** Open the Logic panel's template form (for example the Quests panel's "Place quest object"). False without a panel. */
export function openTemplate(id: string, preset: ParamValues = {}): boolean {
    if (!openTemplateHook) return false;
    runCommand('panel.logic');
    openTemplateHook(id, preset);
    return true;
}

const NW = 176;
const NH = 46;
const SVGNS = 'http://www.w3.org/2000/svg';

const ROLE_COLOR: Record<LogicRole, string> = {
    detector: '#4cc2ff',
    timer: '#c79bff',
    counter: '#ffb454',
    relay: '#9aa7b8',
    switch: '#7ee08c',
    door: '#ff8a6b',
    cutscene: '#ff6bb5',
    movie: '#ff6bb5',
    conversation: '#ffd866',
    exit: '#5eead4',
    elevator: '#a3e635',
    plate: '#7ee08c',
    lock: '#fb923c',
    visibility: '#9aa7b8',
    sound: '#60a5fa',
    creature: '#f87171',
    quest: '#facc15',
    chest: '#d6a85f',
    teleporter: '#34d399',
    mechanism: '#cbd5e1',
    waypoint: '#64748b',
    other: '#64748b',
};

function svg<K extends keyof SVGElementTagNameMap>(
    tag: K,
    attrs: Record<string, string | number> = {},
    ...kids: (Node | string)[]
): SVGElementTagNameMap[K] {
    const el = document.createElementNS(SVGNS, tag);
    for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, String(v));
    for (const k of kids) el.append(k);
    return el;
}

const norm = (s: string): string => s.toLowerCase().replace(/[^a-z0-9]/g, '');

/** Install the logic graph panel, the volume overlay and the logic validation checks. */
export function installLogic(ctx: Ctx, loadExits: () => Promise<ExitIndexData>): Panel {
    const env = (): LogicEnv | null => ctx.schemas();
    let exitData: ExitIndexData | null = null;
    let exitLoading = false;
    const volumes = createVolumeLayer(ctx, env);

    // ---- state ------------------------------------------------------------------------------------------------------
    let scope: 'selection' | 'level' = 'selection';
    let depth = 2;
    let graph: LogicGraph = { nodes: new Map(), edges: [] };
    let place = new Map<number, Placement>();
    const manual = new Map<number, { x: number; y: number }>();
    let selEdge: string | null = null;
    let cam = { x: 20, y: 20, k: 1 };
    const listeners = new Set<() => void>();

    // ---- DOM --------------------------------------------------------------------------------------------------------
    const scopeSel = h(
        'select',
        { class: 'sel', 'aria-label': 'Graph scope' },
        h('option', { value: 'selection' }, 'Around selection'),
        h('option', { value: 'level' }, 'Whole level'),
    );
    const depthSel = h(
        'select',
        { class: 'sel', 'aria-label': 'Link distance' },
        ...[1, 2, 3, 4].map((n) => h('option', { value: String(n) }, `${n} link${n > 1 ? 's' : ''}`)),
    );
    depthSel.value = '2';
    const volSel = h(
        'select',
        { class: 'sel', 'aria-label': 'Show volumes', title: 'Trigger volumes and radii in the viewport' },
        h('option', { value: 'selected' }, 'Volumes: selected'),
        h('option', { value: 'all' }, 'Volumes: all'),
        h('option', { value: 'off' }, 'Volumes: off'),
    );
    const tplSel = h(
        'select',
        { class: 'sel', 'aria-label': 'Template' },
        h('option', { value: '' }, 'Add setup...'),
        ...TEMPLATES.map((t) => h('option', { value: t.id }, t.title)),
    );
    const info = h('span', { class: 'dim' });
    const canvas = svg('svg', { class: 'lg-canvas', width: '100%', height: '100%', tabindex: 0 });
    const world = svg('g');
    canvas.append(world);
    const side = h('div', { class: 'lg-side' });
    const form = h('div', { class: 'lg-form', hidden: true });
    const root = h(
        'div',
        { class: 'panel logic-panel' },
        h(
            'div',
            { class: 'panel-tools' },
            scopeSel,
            depthSel,
            volSel,
            tplSel,
            h(
                'button',
                { class: 'btn', type: 'button', title: 'Re-run the automatic layout', onclick: () => relayout(true) },
                'Auto layout',
            ),
            h('button', { class: 'btn', type: 'button', onclick: () => fit() }, 'Fit'),
            h('span', { class: 'spacer' }),
            info,
        ),
        form,
        h('div', { class: 'lg-body' }, h('div', { class: 'lg-wrap' }, canvas), side),
    );
    const style = document.createElement('style');
    style.textContent = `
.logic-panel{display:flex;flex-direction:column;height:100%;min-height:0}
.logic-panel .lg-body{display:flex;flex:1;min-height:0}
.logic-panel .lg-wrap{flex:1;min-width:0;position:relative;background:var(--surface-0);overflow:hidden}
.logic-panel .lg-canvas{display:block;user-select:none;outline:none;cursor:grab}
.logic-panel .lg-side{width:300px;overflow:auto;border-left:1px solid var(--border);padding:8px;font-size:12px}
.logic-panel .lg-form{padding:8px;border-bottom:1px solid var(--border);display:flex;flex-wrap:wrap;gap:8px;align-items:end}
.logic-panel .lg-form label{display:flex;flex-direction:column;gap:2px;font-size:11px}
.logic-panel .lg-form .lg-note{flex-basis:100%;font-size:11px;opacity:.75}
.logic-panel .lg-node rect.box{fill:var(--surface-2);stroke-width:1.5}
.logic-panel .lg-node.sel rect.box{fill:var(--surface-3);stroke-width:3}
.logic-panel .lg-node text{fill:var(--fg);font-size:11px;pointer-events:none}
.logic-panel .lg-node text.sub{fill:var(--fg-muted);font-size:10px}
.logic-panel .lg-port{fill:var(--surface-0);stroke:var(--fg-muted);stroke-width:1.5;cursor:crosshair}
.logic-panel .lg-port:hover{fill:var(--fg)}
.logic-panel .lg-edge{fill:none;stroke:var(--fg-faint);stroke-width:1.5}
.logic-panel .lg-edge.sel{stroke:var(--warn);stroke-width:3}
.logic-panel .lg-hit{fill:none;stroke:transparent;stroke-width:12;cursor:pointer}
.logic-panel .lg-elabel{fill:var(--fg-muted);font-size:10px;pointer-events:none}
.logic-panel .lg-side h4{margin:0 0 6px;font-size:12px}
.logic-panel .lg-side .row{display:flex;gap:6px;align-items:center;margin:4px 0}
`;
    root.append(style);

    // ---- graph ------------------------------------------------------------------------------------------------------
    const pos = (id: number): { x: number; y: number } => manual.get(id) ?? place.get(id) ?? { x: 0, y: 0 };

    function rebuild(): void {
        const e = env();
        const doc = ctx.doc();
        if (!e || doc.size === 0) {
            graph = { nodes: new Map(), edges: [] };
            place = new Map();
            info.textContent = e ? 'No level open.' : 'Open a disc level to see its logic.';
            draw();
            return;
        }
        if (scope === 'selection') {
            const sel = [...ctx.view().selection];
            if (!sel.length) {
                // keep the last graph visible when nothing is selected, but empty on first use
                if (graph.nodes.size === 0)
                    info.textContent = 'Select a trigger, door, cut scene... to see what it is wired to.';
                return;
            }
            graph = buildLogicGraph(doc, e, { focus: sel, depth });
        } else {
            graph = buildLogicGraph(doc, e);
        }
        place = layoutGraph(graph);
        const stale = [...manual.keys()].filter((id) => !graph.nodes.has(id));
        for (const id of stale) manual.delete(id);
        info.textContent = `${graph.nodes.size} objects, ${graph.edges.length} links`;
        if (graph.nodes.size > 500) info.textContent += ' (large: use Around selection)';
        draw();
    }

    function relayout(reset: boolean): void {
        if (reset) manual.clear();
        place = layoutGraph(graph);
        draw();
        fit();
    }

    function fit(): void {
        const ids = [...graph.nodes.keys()];
        if (!ids.length) return;
        const r = canvas.getBoundingClientRect();
        let minX = Infinity,
            minY = Infinity,
            maxX = -Infinity,
            maxY = -Infinity;
        for (const id of ids) {
            const p = pos(id);
            minX = Math.min(minX, p.x);
            minY = Math.min(minY, p.y);
            maxX = Math.max(maxX, p.x + NW);
            maxY = Math.max(maxY, p.y + NH);
        }
        const k = Math.min(
            1.4,
            Math.max(0.15, Math.min((r.width - 40) / (maxX - minX), (r.height - 40) / (maxY - minY))),
        );
        cam = { k, x: 20 - minX * k + (r.width - 40 - (maxX - minX) * k) / 2, y: 20 - minY * k };
        applyCam();
    }
    const applyCam = (): void => world.setAttribute('transform', `translate(${cam.x} ${cam.y}) scale(${cam.k})`);

    const edgePath = (a: number, b: number): string => {
        const p = pos(a),
            q = pos(b);
        const x1 = p.x + NW,
            y1 = p.y + NH / 2,
            x2 = q.x,
            y2 = q.y + NH / 2;
        const dx = Math.max(40, Math.abs(x2 - x1) / 2);
        return `M${x1} ${y1} C${x1 + dx} ${y1} ${x2 - dx} ${y2} ${x2} ${y2}`;
    };

    function edgeLabel(e: LogicEdge): string {
        if (!e.messages.length) return '';
        const m = e.messages[0]!;
        return e.messages.length > 1 ? `${m.label}: ${m.name} +${e.messages.length - 1}` : m.name;
    }

    function draw(): void {
        world.replaceChildren();
        const sel = ctx.doc().size ? ctx.view().selection : new Set<number>();
        for (const e of graph.edges) {
            const g = svg('g');
            const d = edgePath(e.from, e.to);
            const path = svg('path', {
                d,
                class: `lg-edge${selEdge === e.key ? ' sel' : ''}`,
                'marker-end': 'url(#lg-arrow)',
            });
            const hit = svg('path', { d, class: 'lg-hit' });
            hit.addEventListener('click', (ev) => {
                ev.stopPropagation();
                selEdge = e.key;
                draw();
                renderSide();
            });
            const p = pos(e.from),
                q = pos(e.to);
            const label = svg(
                'text',
                { class: 'lg-elabel', x: (p.x + NW + q.x) / 2, y: (p.y + q.y + NH) / 2 - 4, 'text-anchor': 'middle' },
                edgeLabel(e),
            );
            g.append(path, hit, label);
            world.append(g);
        }
        for (const n of graph.nodes.values()) {
            const p = pos(n.id);
            const color = ROLE_COLOR[n.role];
            const g = svg('g', {
                class: `lg-node${sel.has(n.id) ? ' sel' : ''}`,
                transform: `translate(${p.x} ${p.y})`,
                'data-id': n.id,
            });
            const rect = svg('rect', { class: 'box', width: NW, height: NH, rx: 6, stroke: color });
            const title = svg('text', { x: 10, y: 18 }, n.class.length > 24 ? `${n.class.slice(0, 23)}...` : n.class);
            const sub = svg(
                'text',
                { class: 'sub', x: 10, y: 34 },
                `#${n.id}  ${n.spec.title}${n.hidden ? `  +${n.hidden} hidden` : ''}`,
            );
            const port = svg('circle', { class: 'lg-port', cx: NW, cy: NH / 2, r: 6 });
            g.append(rect, title, sub, port);
            if (n.spec.out.length === 0 && n.role !== 'creature') port.setAttribute('opacity', '0.35');
            g.addEventListener('pointerdown', (ev) => startNodeDrag(ev, n.id));
            port.addEventListener('pointerdown', (ev) => startLink(ev, n.id));
            g.addEventListener('dblclick', () => ctx.locate([n.id]));
            world.append(g);
        }
        if (!world.querySelector('defs')) {
            const defs = svg(
                'defs',
                {},
                svg(
                    'marker',
                    {
                        id: 'lg-arrow',
                        viewBox: '0 0 10 10',
                        refX: 9,
                        refY: 5,
                        markerWidth: 7,
                        markerHeight: 7,
                        orient: 'auto-start-reverse',
                    },
                    svg('path', { d: 'M0 0L10 5L0 10z', fill: '#7b8697' }),
                ),
            );
            canvas.prepend(defs);
        }
        applyCam();
    }

    // ---- interaction ------------------------------------------------------------------------------------------------
    const toWorld = (ev: PointerEvent): { x: number; y: number } => {
        const r = canvas.getBoundingClientRect();
        return { x: (ev.clientX - r.left - cam.x) / cam.k, y: (ev.clientY - r.top - cam.y) / cam.k };
    };

    function startNodeDrag(ev: PointerEvent, id: number): void {
        if (ev.button !== 0) return;
        ev.stopPropagation();
        const v = ctx.view();
        v.select([id], ev.shiftKey || ev.ctrlKey);
        selEdge = null;
        const start = toWorld(ev);
        const p0 = { ...pos(id) };
        let moved = false;
        const move = (e: PointerEvent): void => {
            const w = toWorld(e);
            if (!moved && Math.hypot(w.x - start.x, w.y - start.y) < 3) return;
            moved = true;
            manual.set(id, { x: p0.x + w.x - start.x, y: p0.y + w.y - start.y });
            draw();
        };
        const up = (): void => {
            window.removeEventListener('pointermove', move);
            window.removeEventListener('pointerup', up);
            if (!moved) renderSide();
        };
        window.addEventListener('pointermove', move);
        window.addEventListener('pointerup', up);
    }

    function startLink(ev: PointerEvent, from: number): void {
        if (ev.button !== 0) return;
        ev.stopPropagation();
        const p = pos(from);
        const line = svg('path', { class: 'lg-edge sel', 'stroke-dasharray': '5 4', d: '' });
        world.append(line);
        const move = (e: PointerEvent): void => {
            const w = toWorld(e);
            line.setAttribute('d', `M${p.x + NW} ${p.y + NH / 2} L${w.x} ${w.y}`);
        };
        const up = (e: PointerEvent): void => {
            window.removeEventListener('pointermove', move);
            window.removeEventListener('pointerup', up);
            line.remove();
            const hit = document.elementFromPoint(e.clientX, e.clientY)?.closest('.lg-node');
            const to = hit ? Number(hit.getAttribute('data-id')) : NaN;
            if (Number.isNaN(to)) {
                ctx.toast('Drop the link on another object in the graph.', 'info');
                return;
            }
            if (to === from) return;
            if (ctx.doc().get(from)?.links?.includes(to)) {
                ctx.toast('Those objects are already linked.', 'info');
                return;
            }
            ctx.edit(`Link #${from} to #${to}`, [from], (o) => void addLink(o, to));
            selEdge = `${from}>${to}`;
        };
        window.addEventListener('pointermove', move);
        window.addEventListener('pointerup', up);
    }

    canvas.addEventListener('pointerdown', (ev) => {
        if (ev.button !== 0 && ev.button !== 1) return;
        selEdge = null;
        const s = { x: ev.clientX, y: ev.clientY, cx: cam.x, cy: cam.y };
        canvas.style.cursor = 'grabbing';
        const move = (e: PointerEvent): void => {
            cam.x = s.cx + e.clientX - s.x;
            cam.y = s.cy + e.clientY - s.y;
            applyCam();
        };
        const up = (): void => {
            canvas.style.cursor = '';
            window.removeEventListener('pointermove', move);
            window.removeEventListener('pointerup', up);
            draw();
            renderSide();
        };
        window.addEventListener('pointermove', move);
        window.addEventListener('pointerup', up);
    });
    canvas.addEventListener(
        'wheel',
        (ev) => {
            ev.preventDefault();
            const r = canvas.getBoundingClientRect();
            const mx = ev.clientX - r.left,
                my = ev.clientY - r.top;
            const k = Math.min(3, Math.max(0.1, cam.k * (ev.deltaY < 0 ? 1.12 : 1 / 1.12)));
            cam.x = mx - ((mx - cam.x) / cam.k) * k;
            cam.y = my - ((my - cam.y) / cam.k) * k;
            cam.k = k;
            applyCam();
        },
        { passive: false },
    );
    canvas.addEventListener('keydown', (ev) => {
        if ((ev.key === 'Delete' || ev.key === 'Backspace') && selEdge) {
            const [a, b] = selEdge.split('>').map(Number) as [number, number];
            ctx.edit(`Unlink #${a} from #${b}`, [a], (o) => void removeLink(o, b));
            selEdge = null;
        }
    });

    // ---- side panel -------------------------------------------------------------------------------------------------
    function renderSide(): void {
        const e = env();
        const doc = ctx.doc();
        if (!e) return fill(side, h('div', { class: 'dim' }, 'No class data (open a level from the disc).'));
        if (selEdge) {
            const edge = graph.edges.find((x) => x.key === selEdge);
            if (edge) {
                const [src, dst] = [doc.get(edge.from), doc.get(edge.to)];
                const rows = edge.messages.map((m) => {
                    const sel = h('select', { class: 'sel', 'aria-label': m.field });
                    MESSAGE_NAMES.forEach((n, i) => sel.append(h('option', { value: String(i) }, `${i}  ${n}`)));
                    sel.value = String(m.value);
                    sel.addEventListener('change', () =>
                        ctx.edit(
                            `${m.field}: ${MESSAGE_NAMES[Number(sel.value)]}`,
                            [edge.from],
                            (o) => void setMessageField(o, e, m.field, Number(sel.value)),
                        ),
                    );
                    return h(
                        'div',
                        {},
                        h('div', { class: 'dim' }, `${m.label}  (${m.field})${m.overridden ? '' : '  default'}`),
                        sel,
                    );
                });
                const ts = dst ? logicOf(dst, e) : undefined;
                return fill(
                    side,
                    h('h4', {}, `${src?.class} #${edge.from}  ->  ${dst?.class} #${edge.to}`),
                    rows.length
                        ? h(
                              'div',
                              {},
                              ...rows,
                              h('div', { class: 'dim' }, 'The source sends this message to every object it links to.'),
                          )
                        : h('div', { class: 'dim' }, 'This class sends no configurable message along its links.'),
                    ts?.accepts?.length
                        ? h(
                              'div',
                              { class: 'dim' },
                              `${dst?.class} reacts to: ${ts.accepts.map((v) => MESSAGE_NAMES[v] ?? v).join(', ')}`,
                          )
                        : null,
                    ts?.note ? h('div', { class: 'dim' }, ts.note) : null,
                    h(
                        'div',
                        { class: 'row' },
                        h(
                            'button',
                            {
                                class: 'btn',
                                type: 'button',
                                onclick: () => {
                                    ctx.edit(
                                        `Unlink #${edge.from} from #${edge.to}`,
                                        [edge.from],
                                        (o) => void removeLink(o, edge.to),
                                    );
                                    selEdge = null;
                                },
                            },
                            icon('trash', 12),
                            'Remove link',
                        ),
                    ),
                );
            }
        }
        const ids = [...(doc.size ? ctx.view().selection : [])];
        const one = ids.length === 1 ? doc.get(ids[0]!) : undefined;
        if (!one)
            return fill(
                side,
                h(
                    'div',
                    { class: 'dim' },
                    "Click a node or a link. Drag from a node's right-hand dot to another node to link them. Select a link and press Delete to remove it. Double-click a node to frame it in the viewport.",
                ),
            );
        const spec = logicOf(one, e);
        const fp = buildFieldsPanel(ctx, [one]);
        fill(
            side,
            h('h4', {}, `${one.class} #${one.id}`),
            spec ? h('div', { class: 'dim' }, `${spec.title}. ${spec.note ?? ''}`) : null,
            spec?.role === 'exit' ? exitPicker(one) : null,
            ...fp.nodes,
        );
    }

    // ---- exit destination ---------------------------------------------------------------------------------------------
    /** Destination level picker of an exit: rewrites the class's destination string of the level XDB (exported with the Story pack). */
    function exitPicker(o: LevelObject): HTMLElement | null {
        const host = exitEditHost();
        if (!host) return null;
        loadExitsOnce();
        if (!exitData) return h('div', { class: 'dim' }, 'Reading where the exits lead...');
        const doc = ctx.doc();
        const mine = exitData.levels.find(
            (l) => norm(l.name) === norm(doc.level) || norm(destinationLevelName(l.name)) === norm(doc.level),
        );
        const { dbIndex, classId } = resourceParts(o.resource);
        const c = mine?.classes[`${dbIndex}:${classId}`];
        if (!c?.destRef) {
            return h(
                'div',
                { class: 'dim' },
                'Destination: ' + (c?.dest ?? 'unknown') + ' (no editable level string behind this exit).',
            );
        }
        const { file, id } = c.destRef;
        const text = host.text(file, id);
        if (text === undefined) {
            void host.ensure().catch(() => undefined);
            return h('div', { class: 'dim' }, 'Loading the level strings...');
        }
        const sel = h('select', { class: 'sel', 'aria-label': 'Destination level' });
        const names = exitData.levels.map((l) => l.name);
        const now = destinationLevelName(text);
        if (!names.some((n) => norm(n) === norm(now))) names.unshift(now);
        names.forEach((n) => sel.append(h('option', { value: n }, n)));
        sel.value = names.find((n) => norm(n) === norm(now)) ?? now;
        sel.addEventListener('change', () => {
            host.set(file, id, exitDestinationText(text, sel.value));
            ctx.toast(`Exit now leads to ${sel.value}; export it with the Story pack.`, 'ok');
        });
        return h(
            'div',
            { class: 'lg-exit' },
            h('div', { class: 'dim' }, `Destination (${file} string ${id}${host.changed(file, id) ? ', edited' : ''})`),
            sel,
            h(
                'div',
                { class: 'dim' },
                'Edits the level string, so every exit of this class changes. Arrival: an exit of the destination that leads back here (check Validation).',
            ),
        );
    }

    // ---- templates --------------------------------------------------------------------------------------------------
    let presets: ParamValues = {};
    function paramInput(t: LogicTemplate, p: TemplateParam, vals: ParamValues): HTMLElement {
        const e = env()!;
        const doc = ctx.doc();
        let control: HTMLElement;
        if (p.kind === 'number') {
            const i = h('input', {
                type: 'number',
                class: 'num',
                step: p.step ?? 1,
                min: p.min,
                max: p.max,
                value: String(presets[p.id] ?? p.default ?? 0),
            });
            vals[p.id] = Number(presets[p.id] ?? p.default ?? 0);
            i.addEventListener('input', () => (vals[p.id] = Number(i.value)));
            control = i;
        } else if (p.kind === 'bool') {
            const i = h('input', { type: 'checkbox', class: 'cb' });
            i.checked = p.default === true;
            vals[p.id] = i.checked;
            i.addEventListener('change', () => (vals[p.id] = i.checked));
            control = i;
        } else if (p.kind === 'choice') {
            const s = h(
                'select',
                { class: 'sel' },
                ...(p.options ?? []).map((o) => h('option', { value: o.value }, o.label)),
            );
            s.value = String(p.default ?? p.options?.[0]?.value ?? '');
            vals[p.id] = s.value;
            s.addEventListener('change', () => (vals[p.id] = s.value));
            control = s;
        } else if (p.kind === 'class') {
            const list = ctx
                .catalog()
                .filter((c) => c.rflClass === p.rfl)
                .sort((a, b) => b.instances - a.instances || a.name.localeCompare(b.name));
            const s = h(
                'select',
                { class: 'sel' },
                ...list.map((c) =>
                    h('option', { value: c.resource }, `${c.name}${c.instances ? ` (${c.instances} in level)` : ''}`),
                ),
            );
            vals[p.id] = list[0]?.resource;
            s.addEventListener('change', () => (vals[p.id] = s.value));
            control = list.length ? s : h('span', { class: 'dim' }, 'none in this level');
        } else {
            // object / objects
            const sel = new Set(ctx.doc().size ? ctx.view().selection : []);
            const cands = [...doc.objects()].filter((o) => {
                const s = logicOf(o, e);
                return s && (!p.roles || p.roles.includes(s.role));
            });
            // selected first, then by id
            cands.sort((a, b) => Number(sel.has(b.id)) - Number(sel.has(a.id)) || a.id - b.id);
            if (p.kind === 'object') {
                const s = h(
                    'select',
                    { class: 'sel' },
                    ...(p.optional ? [h('option', { value: '0' }, 'new')] : []),
                    ...cands
                        .slice(0, 400)
                        .map((o) =>
                            h(
                                'option',
                                { value: String(o.id) },
                                `#${o.id} ${o.class}${sel.has(o.id) ? ' (selected)' : ''}`,
                            ),
                        ),
                );
                vals[p.id] = Number(s.value);
                s.addEventListener('change', () => (vals[p.id] = Number(s.value)));
                control = s;
            } else {
                control = h('span', { class: 'dim' }, `${[...sel].filter((id) => doc.has(id)).length} selected`);
                vals[p.id] = undefined;
            }
        }
        void t;
        return h('label', { title: p.hint ?? '' }, p.label, control);
    }

    openTemplateHook = (id, preset) => {
        presets = preset;
        tplSel.value = id;
        tplSel.dispatchEvent(new Event('change'));
        presets = {};
    };
    tplSel.addEventListener('change', () => {
        const t = TEMPLATES.find((x) => x.id === tplSel.value);
        tplSel.value = '';
        if (!t) return void (form.hidden = true);
        if (!env() || !ctx.doc().size) return ctx.toast('Open a level from the disc first.', 'warn');
        const vals: ParamValues = {};
        const inputs = t.params.map((p) => paramInput(t, p, vals));
        const go = h('button', { class: 'btn primary', type: 'button' }, `Create`);
        go.addEventListener('click', () => {
            const v = ctx.view();
            const sel = [...v.selection];
            const at =
                (sel.length ? ctx.doc().get(sel[0]!)?.transform.pos : null) ?? v.groundAtCentre() ?? v.cameraGamePos();
            const r = t.build(
                { doc: ctx.doc(), env: env()!, catalog: ctx.catalog(), at: [at[0], at[1], at[2]], selection: sel },
                vals,
            );
            if (isError(r)) return ctx.toast(r.error, 'error');
            const cmd = new ApplyTemplateCommand(r, t.title);
            v.run(cmd);
            v.select(cmd.ids);
            for (const w of r.warnings) ctx.toast(w, 'warn');
            ctx.toast(r.notes.join('. '), 'ok');
            form.hidden = true;
            scope = 'selection';
            scopeSel.value = 'selection';
            rebuild();
            fit();
        });
        fill(
            form,
            h('strong', {}, t.title),
            ...inputs,
            go,
            h('button', { class: 'btn', type: 'button', onclick: () => (form.hidden = true) }, 'Cancel'),
            h(
                'div',
                { class: 'lg-note' },
                `${t.summary} Placed at the first selected object, else the viewport centre.`,
            ),
        );
        form.hidden = false;
    });

    scopeSel.addEventListener('change', () => {
        scope = scopeSel.value as typeof scope;
        rebuild();
        fit();
    });
    depthSel.addEventListener('change', () => {
        depth = Number(depthSel.value);
        rebuild();
        fit();
    });
    volSel.addEventListener('change', () => volumes.setMode(volSel.value as VolumeMode));

    // ---- validation provider ----------------------------------------------------------------------------------------
    function exitIndex(): ExitIndex | undefined {
        if (!exitData) return undefined;
        return { levels: new Map(exitData.levels.map((l) => [l.name, l.exits as readonly ExitIndexEntry[]])) };
    }
    function loadExitsOnce(): void {
        if (exitData || exitLoading) return;
        exitLoading = true;
        loadExits()
            .then((d) => {
                exitData = d;
                listeners.forEach((f) => f());
            })
            .catch(() => undefined)
            .finally(() => (exitLoading = false));
    }
    addValidationProvider({
        issues(): ValidationIssue[] {
            const e = env();
            const doc = ctx.doc();
            if (!e || doc.size === 0) return [];
            loadExitsOnce();
            const mine = exitData?.levels.find(
                (l) => norm(l.name) === norm(doc.level) || norm(destinationLevelName(l.name)) === norm(doc.level),
            );
            return validateLogic(doc, e, {
                level: mine?.name ?? doc.level,
                ...(exitData ? { exits: exitIndex()! } : {}),
                exitDestination: (o: LevelObject) => {
                    if (!exitData) return { dest: '(loading)', portal: false };
                    const placed = mine?.exits.find((x) => x.id === o.id && o.origin !== undefined);
                    if (placed) return { dest: placed.dest, portal: placed.portal };
                    const { dbIndex, classId } = resourceParts(o.resource);
                    const c = mine?.classes[`${dbIndex}:${classId}`];
                    return c ? { dest: c.dest, portal: c.portal } : null;
                },
            });
        },
        onChange(refresh: () => void) {
            listeners.add(refresh);
        },
    });

    // ---- panel registration -----------------------------------------------------------------------------------------
    const soon = raf(() => {
        rebuild();
        volumes.refresh();
        renderSide();
    });
    const panel: Panel = {
        el: root,
        onSelection: soon,
        onChange: soon,
        onVisibility: () => volumes.refresh(),
        onLoad: () => {
            manual.clear();
            graph = { nodes: new Map(), edges: [] };
            soon();
        },
    };
    onExitEditChange(soon);
    registerPanel({
        id: 'logic',
        title: 'Logic',
        icon: 'link',
        panel,
        command: {
            keys: 'Ctrl+Shift+L',
            keywords: 'trigger graph link message cut scene door puzzle',
            hint: 'Wire triggers, timers, doors and cut scenes in a graph',
        },
    });
    registerCommand({
        id: 'logic.volumes',
        title: 'Toggle trigger volumes in the viewport',
        category: 'View',
        keywords: 'trigger box radius',
        run: () => {
            const m = volumes.mode() === 'all' ? 'selected' : 'all';
            volSel.value = m;
            volumes.setMode(m);
        },
    });
    registerCommand({
        id: 'logic.level',
        title: 'Show the whole level logic graph',
        category: 'View',
        run: () => {
            scope = 'level';
            scopeSel.value = 'level';
            rebuild();
            fit();
        },
    });
    void numField;
    return panel;
}
