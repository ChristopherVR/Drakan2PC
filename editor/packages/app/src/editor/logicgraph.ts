import {
    NEW_NODE_KINDS,
    TEMPLATES,
    buildLogicGraph,
    graphProblems,
    layoutGraph,
    logicOf,
    matchScore,
    nodeName,
    nodeSummary,
    pinPhrase,
    plainMessage,
    plainRoleName,
    removeLink,
    type LevelObject,
    type LogicEnv,
    type LogicGraph,
    type LogicNode,
    type NewNodeKind,
    type NodeProblem,
    type Placement,
} from '@drakan/editor-core';
import type { LogicRole } from '@drakan/formats';
import type { Ctx } from './ctx.ts';
import { fill, h, icon } from './dom.ts';
import { buildFieldsPanel } from './fieldsui.ts';
import { askLinkMessage } from './linkmenu.ts';
import { startLinkPick } from './linking.ts';
import { buildObjectMenu } from './objectmenu.ts';
import { contextMenu } from './ui.ts';

/**
 * The Logic graph: objects as cards named by what they are (a Door called "Town Door", a Trigger, a Timer ...), an output
 * pin that says when the object fires, and a link that says what the target does ("opens", "plays the cut scene").
 * Everything is editable in place: drag from a pin to link (and pick the message from a short list), add cards from a
 * searchable menu or by dragging objects in from the Outliner, click a link's word to change it, click a card to select
 * the object in the viewport (and the other way round). Problems show on the card they belong to.
 */

const NW = 214;
const NH = 62;
const HD = 18;
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

const cap = (s: string): string => s.charAt(0).toUpperCase() + s.slice(1);

export interface GraphHost {
    ctx: Ctx;
    env(): LogicEnv | null;
    /** Open a ready-made setup form (the Logic panel's), optionally with parameters preset. */
    openSetup(id: string): void;
    /** Extra block for the selected card (an exit's destination picker). */
    extra?(o: LevelObject): HTMLElement | null;
}

export interface LogicGraphView {
    /** The canvas and the side panel. */
    el: HTMLElement;
    /** Toolbar controls this view owns, in order. */
    tools: HTMLElement[];
    info: HTMLElement;
    selectionChanged(): void;
    docChanged(): void;
    levelLoaded(): void;
    showLevel(): void;
    openAddMenu(at?: { x: number; y: number }, from?: number): void;
    /** Problems of the drawn cards (for tests and the status line). */
    problemCount(): number;
}

export function createLogicGraph(host: GraphHost): LogicGraphView {
    const { ctx } = host;
    const env = host.env;

    // ---- state ------------------------------------------------------------------------------------------------------
    let scope: 'selection' | 'level' = 'selection';
    let depth = 2;
    let graph: LogicGraph = { nodes: new Map(), edges: [] };
    let place = new Map<number, Placement>();
    let problems = new Map<number, NodeProblem[]>();
    const manual = new Map<number, { x: number; y: number }>();
    const pinned = new Set<number>();
    let selEdge: string | null = null;
    let cam = { x: 20, y: 20, k: 1 };
    let shownKey = '';

    // ---- DOM --------------------------------------------------------------------------------------------------------
    const scopeSel = h(
        'select',
        { class: 'sel', 'aria-label': 'What to show', title: 'Which objects the graph draws' },
        h('option', { value: 'selection' }, 'Selected objects'),
        h('option', { value: 'level' }, 'Whole level'),
    );
    const depthSel = h(
        'select',
        {
            class: 'sel',
            'aria-label': 'How far to follow links',
            title: 'How many links away from the selection to draw',
        },
        ...[1, 2, 3, 4].map((n) => h('option', { value: String(n) }, n === 1 ? 'Direct links' : `${n} links away`)),
    );
    depthSel.value = '2';
    const addBtn = h(
        'button',
        { class: 'btn primary', type: 'button', title: 'Add an object to the graph (A)', 'aria-haspopup': 'dialog' },
        icon('plus', 13),
        'Add node',
    );
    const tidyBtn = h(
        'button',
        { class: 'btn', type: 'button', title: 'Arrange the cards left to right, triggers first' },
        'Tidy up',
    );
    const fitBtn = h('button', { class: 'btn', type: 'button', title: 'Zoom to show every card' }, 'Fit');
    const info = h('span', { class: 'dim' });
    const canvas = svg('svg', { class: 'lg-canvas', width: '100%', height: '100%', tabindex: 0 });
    const world = svg('g');
    canvas.append(world);
    const empty = h('div', { class: 'lg-empty', hidden: true });
    const wrap = h('div', { class: 'lg-wrap' }, canvas, empty);
    const side = h('div', { class: 'lg-side' });
    const el = h('div', { class: 'lg-body' }, wrap, side);

    // ---- graph ------------------------------------------------------------------------------------------------------
    const pos = (id: number): { x: number; y: number } => manual.get(id) ?? place.get(id) ?? { x: 0, y: 0 };

    function rebuild(): void {
        const e = env();
        const doc = ctx.doc();
        if (!e || doc.size === 0) {
            graph = { nodes: new Map(), edges: [] };
            place = new Map();
            problems = new Map();
            info.textContent = e ? 'No level open.' : 'Open a disc level to see its logic.';
            draw();
            return;
        }
        if (scope === 'selection') {
            const sel = [...ctx.view().selection];
            const focus = [...new Set([...sel, ...pinned])].filter((id) => doc.has(id));
            if (!focus.length && graph.nodes.size > 0) {
                // nothing selected: keep what is drawn, but drop cards of objects that no longer exist
                if ([...graph.nodes.keys()].every((id) => doc.has(id))) return;
            }
            graph = buildLogicGraph(doc, e, { focus, depth });
        } else {
            graph = buildLogicGraph(doc, e);
        }
        place = layoutGraph(graph, { colWidth: NW + 80, rowHeight: NH + 34 });
        for (const id of [...manual.keys()]) if (!graph.nodes.has(id)) manual.delete(id);
        problems = graphProblems(doc, e, graph);
        const n = [...problems.values()].reduce((s, l) => s + l.length, 0);
        const parts = [
            `${graph.nodes.size} object${graph.nodes.size === 1 ? '' : 's'}`,
            `${graph.edges.length} link${graph.edges.length === 1 ? '' : 's'}`,
        ];
        if (n) parts.push(`${n} to fix`);
        info.textContent = parts.join(', ');
        info.classList.toggle('warn', n > 0);
        if (graph.nodes.size > 500) info.textContent += ' (large: show only the selected objects)';
        const key = [...graph.nodes.keys()].sort((a, b) => a - b).join(',');
        draw();
        if (key !== shownKey) {
            shownKey = key;
            fit();
        }
    }

    function relayout(): void {
        manual.clear();
        place = layoutGraph(graph, { colWidth: NW + 80, rowHeight: NH + 34 });
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
            1.25,
            Math.max(0.15, Math.min((r.width - 40) / (maxX - minX), (r.height - 40) / (maxY - minY))),
        );
        cam = {
            k,
            x: 20 - minX * k + (r.width - 40 - (maxX - minX) * k) / 2,
            y: 20 - minY * k + Math.max(0, (r.height - 40 - (maxY - minY) * k) / 2),
        };
        applyCam();
    }
    const applyCam = (): void => world.setAttribute('transform', `translate(${cam.x} ${cam.y}) scale(${cam.k})`);

    const outPin = (id: number): { x: number; y: number } => ({ x: pos(id).x + NW, y: pos(id).y + HD + (NH - HD) / 2 });
    const inPin = (id: number): { x: number; y: number } => ({ x: pos(id).x, y: pos(id).y + HD + (NH - HD) / 2 });

    const edgePath = (a: number, b: number): string => {
        const p = outPin(a),
            q = inPin(b);
        const dx = Math.max(40, Math.abs(q.x - p.x) / 2);
        return `M${p.x} ${p.y} C${p.x + dx} ${p.y} ${q.x - dx} ${q.y} ${q.x} ${q.y}`;
    };

    /** The words on a link: what the target does with the message its source sends. */
    function edgeWords(from: number, to: number): string {
        const edge = graph.edges.find((x) => x.from === from && x.to === to);
        const m = edge?.messages[0];
        const target = graph.nodes.get(to);
        if (!m || !target) return 'linked';
        return plainMessage(m.value, target.role);
    }

    function nodeTitle(n: LogicNode): string {
        const o = ctx.doc().get(n.id);
        return o ? nodeName(o, n.spec) : n.class;
    }

    function draw(): void {
        world.replaceChildren();
        const doc = ctx.doc();
        const sel = doc.size ? ctx.view().selection : new Set<number>();
        empty.hidden = graph.nodes.size > 0;
        if (!empty.hidden) renderEmpty();
        if (!world.querySelector('defs')) canvas.prepend(arrowDefs());
        for (const e of graph.edges) {
            const g = svg('g');
            const d = edgePath(e.from, e.to);
            const bad = problems.get(e.from)?.some((p) => p.code === 'ignored' && p.other === e.to) ?? false;
            const src = graph.nodes.get(e.from);
            const color = src ? ROLE_COLOR[src.role] : '#7b8697';
            const cls = `lg-edge${selEdge === e.key ? ' sel' : ''}${bad ? ' bad' : ''}`;
            const path = svg('path', { d, class: cls, stroke: bad ? '' : color, 'marker-end': 'url(#lg-arrow)' });
            const hit = svg('path', { d, class: 'lg-hit' });
            hit.addEventListener('click', (ev) => {
                ev.stopPropagation();
                selEdge = e.key;
                draw();
                renderSide();
            });
            const p = outPin(e.from),
                q = inPin(e.to);
            const words = edgeWords(e.from, e.to);
            const label = (bad ? `${words}?` : words) || 'linked';
            const w = label.length * 5.6 + 18;
            const mx = (p.x + q.x) / 2;
            const my = (p.y + q.y) / 2;
            const pill = svg('g', { class: `lg-pill${bad ? ' bad' : ''}`, transform: `translate(${mx} ${my})` });
            pill.append(
                svg('rect', { x: -w / 2, y: -10, width: w, height: 20, rx: 10 }),
                svg('text', { y: 4, 'text-anchor': 'middle' }, label),
                svg(
                    'title',
                    {},
                    bad
                        ? (problems.get(e.from)?.find((x) => x.other === e.to)?.text ?? '')
                        : 'Click to change what this link does',
                ),
            );
            pill.addEventListener('pointerdown', (ev) => ev.stopPropagation());
            pill.addEventListener('click', (ev) => {
                ev.stopPropagation();
                selEdge = e.key;
                if (!askLinkMessage(ctx, [e.from], e.to, { x: ev.clientX, y: ev.clientY })) renderSide();
                draw();
                renderSide();
            });
            g.append(path, hit, pill);
            world.append(g);
        }
        for (const n of graph.nodes.values()) world.append(drawNode(n, sel.has(n.id)));
        applyCam();
    }

    function arrowDefs(): SVGElement {
        return svg(
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
    }

    function drawNode(n: LogicNode, selected: boolean): SVGElement {
        const p = pos(n.id);
        const color = ROLE_COLOR[n.role];
        const probs = problems.get(n.id) ?? [];
        const g = svg('g', {
            class: `lg-node${selected ? ' sel' : ''}${probs.length ? ' warn' : ''}`,
            transform: `translate(${p.x} ${p.y})`,
            'data-id': n.id,
        });
        const title = nodeTitle(n);
        g.append(
            svg('rect', { class: 'box', width: NW, height: NH, rx: 8, stroke: color }),
            svg('path', { class: 'hd', d: `M0 8a8 8 0 0 1 8-8h${NW - 16}a8 8 0 0 1 8 8v${HD - 8}H0z`, fill: color }),
            svg('text', { class: 'role', x: 10, y: 13 }, plainRoleName(n.spec).toUpperCase()),
            svg('text', { class: 'name', x: 10, y: HD + 20 }, title.length > 27 ? `${title.slice(0, 26)}...` : title),
            svg('text', { class: 'sub', x: 10, y: HD + 36 }, nodeLine(n)),
        );
        g.append(svg('title', {}, `${title} (${plainRoleName(n.spec)}, object ${n.id}). ${nodeSummary(n.spec)}`));
        // typed pins: the input takes messages, the output sends the one this object is set to send
        const inp = svg('circle', {
            class: `lg-port in${n.inCount ? '' : ' idle'}`,
            cx: 0,
            cy: HD + (NH - HD) / 2,
            r: 6,
            stroke: color,
        });
        const outp = svg('circle', {
            class: `lg-port out${n.spec.out.length ? '' : ' idle'}`,
            cx: NW,
            cy: HD + (NH - HD) / 2,
            r: 6,
            stroke: color,
        });
        inp.append(svg('title', {}, 'Takes messages from other objects'));
        outp.append(
            svg(
                'title',
                {},
                n.spec.out.length
                    ? 'Drag to another object to link them'
                    : 'This kind of object sends nothing on its own',
            ),
        );
        g.append(inp, outp);
        if (probs.length) {
            const b = svg('g', { class: 'lg-badge', transform: `translate(${NW - 14} ${HD / 2})` });
            b.append(
                svg('circle', { r: 7 }),
                svg('text', { y: 3.5, 'text-anchor': 'middle' }, '!'),
                svg('title', {}, probs.map((x) => x.text).join('\n')),
            );
            g.append(b);
        }
        g.addEventListener('pointerdown', (ev) => startNodeDrag(ev, n.id));
        outp.addEventListener('pointerdown', (ev) => startLink(ev, n.id));
        g.addEventListener('dblclick', () => ctx.locate([n.id]));
        g.addEventListener('contextmenu', (ev) => {
            ev.preventDefault();
            ev.stopPropagation();
            const items = buildObjectMenu(ctx, [n.id]);
            if (pinned.has(n.id))
                items.unshift({
                    label: 'Remove from graph',
                    icon: 'x',
                    run: () => {
                        pinned.delete(n.id);
                        rebuild();
                    },
                });
            contextMenu(ev.clientX, ev.clientY, items);
        });
        return g;
    }

    /** The line under a card's name: when it fires, or what it waits for. */
    function nodeLine(n: LogicNode): string {
        const first = n.spec.out[0];
        if (first) return cap(pinPhrase(n.spec, first.label));
        const incoming = graph.edges.filter((e) => e.to === n.id);
        if (incoming.length) {
            const src = graph.nodes.get(incoming[0]!.from);
            return `${cap(edgeWords(incoming[0]!.from, n.id))} when ${src ? nodeTitle(src) : 'told'} fires`;
        }
        return 'Waits to be told what to do';
    }

    // ---- empty state ------------------------------------------------------------------------------------------------
    function renderEmpty(): void {
        const e = env();
        const doc = ctx.doc();
        fill(
            empty,
            icon('link', 28),
            h('strong', {}, e && doc.size ? 'Nothing in the graph yet' : 'No level open'),
            h(
                'span',
                {},
                e && doc.size
                    ? 'Click an object in the level, drag one in from the Outliner, or add one here. Then drag from a card’s round pin to another card to link them.'
                    : 'Open a level of the disc to see how its triggers, doors and cut scenes are wired.',
            ),
            e && doc.size
                ? h(
                      'div',
                      { class: 'lg-empty-row' },
                      h(
                          'button',
                          { class: 'btn primary', type: 'button', onclick: () => openAddMenu() },
                          icon('plus', 13),
                          'Add node',
                      ),
                      h('button', { class: 'btn', type: 'button', onclick: () => showLevel() }, 'Show the whole level'),
                  )
                : null,
        );
    }

    // ---- interaction ------------------------------------------------------------------------------------------------
    const toWorld = (ev: { clientX: number; clientY: number }): { x: number; y: number } => {
        const r = canvas.getBoundingClientRect();
        return { x: (ev.clientX - r.left - cam.x) / cam.k, y: (ev.clientY - r.top - cam.y) / cam.k };
    };

    function startNodeDrag(ev: PointerEvent, id: number): void {
        if (ev.button !== 0) return;
        ev.stopPropagation();
        const v = ctx.view();
        selEdge = null;
        const start = toWorld(ev);
        const p0 = { ...pos(id) };
        let moved = false;
        // clicking a card selects the object in the viewport; the graph keeps its layout while its own cards are picked
        v.select([id], ev.shiftKey || ev.ctrlKey);
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
        ev.preventDefault();
        const p = outPin(from);
        const line = svg('path', { class: 'lg-edge sel', 'stroke-dasharray': '5 4', d: '', 'pointer-events': 'none' });
        world.append(line);
        let over: number | null = null;
        const move = (e: PointerEvent): void => {
            const w = toWorld(e);
            line.setAttribute('d', `M${p.x} ${p.y} L${w.x} ${w.y}`);
            const hit = document.elementFromPoint(e.clientX, e.clientY)?.closest('.lg-node');
            const id = hit ? Number(hit.getAttribute('data-id')) : NaN;
            const next = Number.isNaN(id) || id === from ? null : id;
            if (next !== over) {
                if (over !== null) world.querySelector(`.lg-node[data-id="${over}"]`)?.classList.remove('drop');
                if (next !== null) world.querySelector(`.lg-node[data-id="${next}"]`)?.classList.add('drop');
                over = next;
            }
        };
        const up = (e: PointerEvent): void => {
            window.removeEventListener('pointermove', move);
            window.removeEventListener('pointerup', up);
            line.remove();
            world.querySelectorAll('.lg-node.drop').forEach((n) => n.classList.remove('drop'));
            const at = { x: e.clientX, y: e.clientY };
            if (over !== null) return void connect(from, over, at);
            const r = canvas.getBoundingClientRect();
            const inside = e.clientX >= r.left && e.clientX <= r.right && e.clientY >= r.top && e.clientY <= r.bottom;
            // dropped on empty canvas: offer to make something new right there and link it
            if (inside && Math.hypot(e.clientX - ev.clientX, e.clientY - ev.clientY) > 12) openAddMenu(at, from);
        };
        window.addEventListener('pointermove', move);
        window.addEventListener('pointerup', up);
    }

    /** Link two cards: ask what it should do (a short list), or just link when the source sends nothing. */
    function connect(from: number, to: number, at: { x: number; y: number }): void {
        const doc = ctx.doc();
        if (from === to) return;
        const fo = doc.get(from);
        pinned.add(to);
        if (fo?.links?.includes(to)) {
            selEdge = `${from}>${to}`;
            if (!askLinkMessage(ctx, [from], to, at)) ctx.toast('Those objects are already linked.', 'info');
            return;
        }
        if (askLinkMessage(ctx, [from], to, at)) {
            selEdge = `${from}>${to}`;
            return;
        }
        ctx.edit(
            `Link ${nodeLabel(from)} to ${nodeLabel(to)}`,
            [from],
            (o) => void (o.links = [...new Set([...(o.links ?? []), to])]),
        );
        selEdge = `${from}>${to}`;
    }

    function nodeLabel(id: number): string {
        const o = ctx.doc().get(id);
        const e = env();
        return o ? nodeName(o, e ? logicOf(o, e) : undefined) : `object ${id}`;
    }

    canvas.addEventListener('pointerdown', (ev) => {
        if (ev.button !== 0 && ev.button !== 1) return;
        closeAdd();
        selEdge = null;
        const s = { x: ev.clientX, y: ev.clientY, cx: cam.x, cy: cam.y };
        canvas.style.cursor = 'grabbing';
        canvas.focus({ preventScroll: true });
        let moved = false;
        const move = (e: PointerEvent): void => {
            if (Math.hypot(e.clientX - s.x, e.clientY - s.y) > 3) moved = true;
            cam.x = s.cx + e.clientX - s.x;
            cam.y = s.cy + e.clientY - s.y;
            applyCam();
        };
        const up = (): void => {
            canvas.style.cursor = '';
            window.removeEventListener('pointermove', move);
            window.removeEventListener('pointerup', up);
            if (!moved) {
                draw();
                renderSide();
            }
        };
        window.addEventListener('pointermove', move);
        window.addEventListener('pointerup', up);
    });
    canvas.addEventListener('dblclick', (ev) => {
        if ((ev.target as Element).closest('.lg-node, .lg-pill')) return;
        openAddMenu({ x: ev.clientX, y: ev.clientY });
    });
    canvas.addEventListener('contextmenu', (ev) => {
        if ((ev.target as Element).closest('.lg-node')) return;
        ev.preventDefault();
        openAddMenu({ x: ev.clientX, y: ev.clientY });
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
            unlink(a, b);
        } else if ((ev.key === 'a' || ev.key === 'A') && !ev.ctrlKey && !ev.metaKey) {
            ev.preventDefault();
            openAddMenu();
        } else if (ev.key === 'f' || ev.key === 'F') fit();
    });

    function unlink(a: number, b: number): void {
        ctx.edit(`Unlink ${nodeLabel(a)} from ${nodeLabel(b)}`, [a], (o) => void removeLink(o, b));
        selEdge = null;
    }

    // ---- Outliner drag: drop an object on the graph to add it ---------------------------------------------------------
    document.addEventListener(
        'pointerdown',
        (e) => {
            if (e.button !== 0 || !wrap.isConnected || wrap.offsetParent === null) return;
            const row = (e.target as Element | null)?.closest?.('.row.obj[data-id]');
            if (!row || (e.target as Element).closest('button')) return;
            const id = Number(row.getAttribute('data-id'));
            const sx = e.clientX;
            const sy = e.clientY;
            let ghost: HTMLElement | null = null;
            const move = (m: PointerEvent): void => {
                if (!ghost) {
                    if (Math.hypot(m.clientX - sx, m.clientY - sy) < 6) return;
                    const o = ctx.doc().get(id);
                    ghost = h('div', { class: 'drag-ghost live' }, o ? `${o.class} #${o.id}` : `#${id}`);
                    document.body.append(ghost);
                }
                ghost.style.left = `${m.clientX + 12}px`;
                ghost.style.top = `${m.clientY + 12}px`;
                const r = wrap.getBoundingClientRect();
                const inside =
                    m.clientX >= r.left && m.clientX <= r.right && m.clientY >= r.top && m.clientY <= r.bottom;
                wrap.classList.toggle('drop-target', inside);
                ghost.classList.toggle('over', inside);
            };
            const end = (u: PointerEvent): void => {
                window.removeEventListener('pointermove', move);
                window.removeEventListener('pointerup', end);
                window.removeEventListener('pointercancel', end);
                wrap.classList.remove('drop-target');
                if (!ghost) return;
                ghost.remove();
                const r = wrap.getBoundingClientRect();
                if (u.clientX < r.left || u.clientX > r.right || u.clientY < r.top || u.clientY > r.bottom) return;
                const sel = ctx.view().selection;
                const ids = sel.has(id) ? [...sel] : [id];
                addToGraph(ids, toWorld(u));
            };
            window.addEventListener('pointermove', move);
            window.addEventListener('pointerup', end);
            window.addEventListener('pointercancel', end);
        },
        true,
    );

    /** Keep what is drawn now when the graph is about to follow a new selection (adding a node must not drop the others). */
    function pinCurrent(): void {
        if (scope !== 'selection') return;
        for (const id of graph.nodes.keys()) pinned.add(id);
    }

    /** Add objects to the graph (they stay until removed), placing the first one where it was dropped. */
    function addToGraph(ids: number[], at?: { x: number; y: number }): void {
        const e = env();
        const doc = ctx.doc();
        if (!e) return;
        const ok = ids.filter((id) => {
            const o = doc.get(id);
            return o && logicOf(o, e);
        });
        if (!ok.length) return void ctx.toast('That object takes no part in the logic, so it has no card.', 'info');
        scope = 'selection';
        scopeSel.value = 'selection';
        pinCurrent();
        for (const id of ok) pinned.add(id);
        rebuild();
        if (at) ok.forEach((id, i) => manual.set(id, { x: at.x - NW / 2, y: at.y - NH / 2 + i * (NH + 20) }));
        draw();
        ctx.toast(
            ok.length === 1 ? `Added ${nodeLabel(ok[0]!)} to the graph.` : `Added ${ok.length} objects to the graph.`,
            'ok',
        );
    }

    // ---- add menu ---------------------------------------------------------------------------------------------------
    let addPanel: HTMLElement | null = null;
    function closeAdd(): void {
        addPanel?.remove();
        addPanel = null;
    }

    interface Row {
        key: string;
        title: string;
        sub: string;
        group: string;
        color?: string;
        disabled?: boolean;
        run: () => void;
    }

    function openAddMenu(at?: { x: number; y: number }, from?: number): void {
        closeAdd();
        const e = env();
        const doc = ctx.doc();
        if (!e || doc.size === 0) return void ctx.toast('Open a level of the disc first.', 'warn');
        const r = wrap.getBoundingClientRect();
        const px = at?.x ?? r.left + 40;
        const py = at?.y ?? r.top + 40;
        const input = h('input', {
            type: 'search',
            class: 'lg-add-q',
            placeholder:
                from === undefined
                    ? 'Find or make something: door, trigger, timer...'
                    : 'What should it tell? Find or make an object...',
            'aria-label': 'Search objects and kinds',
            spellcheck: 'false',
        });
        const list = h('div', { class: 'lg-add-list', role: 'listbox' });
        const head = h(
            'div',
            { class: 'lg-add-head' },
            from === undefined ? 'Add to the graph' : `Link ${nodeLabel(from)} to...`,
        );
        const panel = h('div', { class: 'lg-add', role: 'dialog', 'aria-label': 'Add a node' }, head, input, list);
        let active = 0;
        let rows: Row[] = [];

        const afterAdd = (id: number): void => {
            pinCurrent();
            pinned.add(id);
            scope = 'selection';
            scopeSel.value = 'selection';
            if (at) {
                const w = toWorld({ clientX: at.x, clientY: at.y });
                manual.set(id, { x: w.x + 30, y: w.y - NH / 2 });
            }
            rebuild();
            const ev = at ?? { x: px, y: py };
            if (from !== undefined && from !== id) connect(from, id, ev);
            ctx.view().select([id]);
        };

        const compute = (): void => {
            const q = input.value;
            rows = [];
            const sel = ctx.view().selection;
            const found: { o: LevelObject; role: string; score: number; name: string }[] = [];
            for (const o of doc.objects()) {
                const s = logicOf(o, e);
                if (!s || s.role === 'waypoint' || o.id === from) continue;
                const name = nodeName(o, s);
                const role = plainRoleName(s);
                const sc = matchScore(q, name, role, o.id);
                if (sc > 0) found.push({ o, role, score: sc + (sel.has(o.id) ? 4 : 0), name });
            }
            const inView = (id: number): boolean => pinned.has(id) || graph.nodes.has(id);
            // with nothing typed, offer what is already on screen first instead of the first objects of the level
            if (!q.trim()) {
                for (const f of found) f.score += inView(f.o.id) ? 20 : 0;
                found.splice(0, found.length, ...found.filter((f) => inView(f.o.id) || sel.has(f.o.id)));
            }
            found.sort((a, b) => b.score - a.score || a.o.id - b.o.id);
            for (const f of found.slice(0, 40))
                rows.push({
                    key: `o${f.o.id}`,
                    title: f.name,
                    sub: `${f.role}${sel.has(f.o.id) ? ', selected' : ''}${pinned.has(f.o.id) || graph.nodes.has(f.o.id) ? ', in the graph' : ''} (object ${f.o.id})`,
                    group: 'Objects in this level',
                    color: ROLE_COLOR[logicOf(f.o, e)!.role],
                    run: () => afterAdd(f.o.id),
                });
            for (const k of NEW_NODE_KINDS) {
                if (matchScore(q, `New ${k.title}`, `${k.summary} ${plainRole(k)}`, 0) === 0) continue;
                const cls = classFor(k);
                rows.push({
                    key: `k${k.id}`,
                    title: `New ${k.title.toLowerCase()}`,
                    sub: cls ? k.summary : `${k.summary} (this level has no such object to copy)`,
                    group: 'Make a new one',
                    color: ROLE_COLOR[k.role],
                    disabled: !cls,
                    run: () => makeNew(k, from, afterAdd),
                });
            }
            if (from === undefined)
                for (const t of TEMPLATES) {
                    if (q && matchScore(q, t.title, t.summary, 0) === 0) continue;
                    rows.push({
                        key: `t${t.id}`,
                        title: t.title,
                        sub: t.summary,
                        group: 'Ready-made setups',
                        run: () => host.openSetup(t.id),
                    });
                }
            active = Math.min(active, Math.max(0, rows.length - 1));
        };
        const paint = (): void => {
            let group = '';
            const kids: HTMLElement[] = [];
            rows.forEach((row, i) => {
                if (row.group !== group) {
                    group = row.group;
                    kids.push(h('div', { class: 'lg-add-group' }, group));
                }
                kids.push(
                    h(
                        'button',
                        {
                            type: 'button',
                            role: 'option',
                            class: `lg-add-row${i === active ? ' active' : ''}`,
                            disabled: row.disabled,
                            onclick: () => {
                                closeAdd();
                                row.run();
                            },
                            onpointermove: () => {
                                if (active !== i) {
                                    active = i;
                                    list.querySelector('.active')?.classList.remove('active');
                                    list.querySelectorAll('.lg-add-row')[i]?.classList.add('active');
                                }
                            },
                        },
                        h('span', { class: 'dot', style: `background:${row.color ?? 'var(--fg-faint)'}` }),
                        h(
                            'span',
                            { class: 'txt' },
                            h('span', { class: 'nm' }, row.title),
                            h('span', { class: 'sb' }, row.sub),
                        ),
                    ),
                );
            });
            if (!input.value.trim())
                kids.push(
                    h(
                        'div',
                        { class: 'lg-add-none' },
                        'Type to find any other object in the level by name, role or number.',
                    ),
                );
            if (!kids.length)
                kids.push(h('div', { class: 'lg-add-none' }, 'Nothing matches. Try "door", "trigger" or "timer".'));
            fill(list, ...kids);
            list.querySelector('.active')?.scrollIntoView({ block: 'nearest' });
        };
        input.addEventListener('input', () => {
            active = 0;
            compute();
            paint();
        });
        input.addEventListener('keydown', (ev) => {
            if (ev.key === 'Escape') {
                ev.stopPropagation();
                closeAdd();
                canvas.focus({ preventScroll: true });
            } else if (ev.key === 'ArrowDown' || ev.key === 'ArrowUp') {
                ev.preventDefault();
                const dir = ev.key === 'ArrowDown' ? 1 : -1;
                let n = active;
                for (let i = 0; i < rows.length; i++) {
                    n = (n + dir + rows.length) % rows.length;
                    if (!rows[n]!.disabled) break;
                }
                active = n;
                paint();
            } else if (ev.key === 'Enter') {
                ev.preventDefault();
                const row = rows[active];
                if (row && !row.disabled) {
                    closeAdd();
                    row.run();
                }
            }
        });
        compute();
        paint();
        document.body.append(panel);
        addPanel = panel;
        const pr = panel.getBoundingClientRect();
        panel.style.left = `${Math.max(8, Math.min(px, window.innerWidth - pr.width - 8))}px`;
        panel.style.top = `${Math.max(8, Math.min(py, window.innerHeight - pr.height - 8))}px`;
        const outside = (ev: PointerEvent): void => {
            if (!panel.contains(ev.target as Node)) {
                closeAdd();
                document.removeEventListener('pointerdown', outside, true);
            }
        };
        setTimeout(() => document.addEventListener('pointerdown', outside, true), 0);
        input.focus();
    }

    const plainRole = (k: NewNodeKind): string => k.role;
    function classFor(k: NewNodeKind): ReturnType<Ctx['catalog']>[number] | undefined {
        return ctx
            .catalog()
            .filter((c) => c.rflClass === k.rfl)
            .sort((a, b) => b.instances - a.instances || a.name.localeCompare(b.name))[0];
    }

    /** Place a new object of a kind next to the source (or at the view centre) and hand its id on. */
    function makeNew(k: NewNodeKind, from: number | undefined, done: (id: number) => void): void {
        const cls = classFor(k);
        if (!cls) return void ctx.toast(`This level has no ${k.title.toLowerCase()} to copy.`, 'warn');
        const src = from !== undefined ? ctx.doc().get(from) : undefined;
        const at: [number, number, number] | undefined = src
            ? [src.transform.pos[0] + 1536, src.transform.pos[1], src.transform.pos[2]]
            : undefined;
        const before = new Set([...ctx.doc().objects()].map((o) => o.id));
        ctx.placeClass(cls, at);
        const made = [...ctx.doc().objects()].find((o) => !before.has(o.id));
        if (made) done(made.id);
    }

    // ---- side panel -------------------------------------------------------------------------------------------------
    function problemList(id: number): HTMLElement | null {
        const list = problems.get(id);
        if (!list?.length) return null;
        return h(
            'div',
            { class: 'lg-problems' },
            ...list.map((p) =>
                h(
                    'div',
                    { class: 'lg-problem' },
                    icon('warn', 13),
                    h('span', {}, p.text),
                    p.other !== undefined
                        ? h(
                              'button',
                              {
                                  class: 'btn sm',
                                  type: 'button',
                                  onclick: (ev: Event) => {
                                      const r = (ev.currentTarget as HTMLElement).getBoundingClientRect();
                                      askLinkMessage(ctx, [id], p.other!, { x: r.left, y: r.bottom + 2 });
                                  },
                              },
                              'Change message',
                          )
                        : null,
                ),
            ),
        );
    }

    function renderSide(): void {
        const e = env();
        const doc = ctx.doc();
        if (!e) return fill(side, h('div', { class: 'dim' }, 'Open a level of the disc to see its logic.'));
        if (selEdge) {
            const edge = graph.edges.find((x) => x.key === selEdge);
            if (edge) {
                const [src, dst] = [doc.get(edge.from), doc.get(edge.to)];
                const ss = src ? logicOf(src, e) : undefined;
                const ts = dst ? logicOf(dst, e) : undefined;
                const bad = problems.get(edge.from)?.filter((p) => p.other === edge.to) ?? [];
                return fill(
                    side,
                    h('h4', {}, `${src ? nodeName(src, ss) : edge.from} → ${dst ? nodeName(dst, ts) : edge.to}`),
                    h(
                        'div',
                        { class: 'lg-sentence' },
                        `When ${src ? nodeName(src, ss) : 'it'} fires, ${dst ? nodeName(dst, ts) : 'the target'} `,
                        h('strong', {}, edgeWords(edge.from, edge.to)),
                        '.',
                    ),
                    ...bad.map((p) => h('div', { class: 'lg-problem' }, icon('warn', 13), h('span', {}, p.text))),
                    h(
                        'div',
                        { class: 'row' },
                        h(
                            'button',
                            {
                                class: 'btn primary',
                                type: 'button',
                                onclick: (ev: Event) => {
                                    const r = (ev.currentTarget as HTMLElement).getBoundingClientRect();
                                    askLinkMessage(ctx, [edge.from], edge.to, { x: r.left, y: r.bottom + 2 });
                                },
                            },
                            'Change what it does',
                        ),
                        h(
                            'button',
                            { class: 'btn', type: 'button', onclick: () => unlink(edge.from, edge.to) },
                            icon('trash', 12),
                            'Remove link',
                        ),
                    ),
                    ss && ss.out.length > 1
                        ? h(
                              'div',
                              { class: 'dim' },
                              `This ${plainRoleName(ss).toLowerCase()} can send ${ss.out.length} different messages (${ss.out.map((p) => pinPhrase(ss, p.label)).join(', ')}). Pick the others in All settings.`,
                          )
                        : null,
                    h('div', { class: 'dim' }, 'An object sends one message to everything it is linked to.'),
                );
            }
        }
        const ids = [...(doc.size ? ctx.view().selection : [])];
        const one = ids.length === 1 ? doc.get(ids[0]!) : undefined;
        if (!one)
            return fill(
                side,
                h('h4', {}, 'How it works'),
                h(
                    'ol',
                    { class: 'lg-howto' },
                    h('li', {}, 'Click an object in the level, or drag one in from the Outliner.'),
                    h('li', {}, 'Drag from the round pin on the right of a card to another card.'),
                    h('li', {}, 'Pick what it should do from the short list.'),
                ),
                h(
                    'div',
                    { class: 'dim' },
                    'Click the word on a link to change it. Select a link and press Delete to remove it. Double-click a card to find it in the level.',
                ),
            );
        const spec = logicOf(one, e);
        const fp = buildFieldsPanel(ctx, [one]);
        const outgoing = graph.edges.filter((x) => x.from === one.id);
        const incoming = graph.edges.filter((x) => x.to === one.id);
        const rowFor = (a: number, b: number, dir: 'out' | 'in'): HTMLElement => {
            const other = dir === 'out' ? b : a;
            const o = doc.get(other);
            const os = o ? logicOf(o, e) : undefined;
            return h(
                'div',
                { class: 'lg-linkrow' },
                h(
                    'button',
                    {
                        class: 'lg-link-name',
                        type: 'button',
                        title: 'Select it',
                        onclick: () => {
                            ctx.view().select([other]);
                        },
                    },
                    h('span', { class: 'dot', style: `background:${os ? ROLE_COLOR[os.role] : 'var(--fg-faint)'}` }),
                    o ? nodeName(o, os) : `object ${other}`,
                ),
                h(
                    'button',
                    {
                        class: 'btn sm',
                        type: 'button',
                        title: 'Change what it does',
                        onclick: (ev: Event) => {
                            const r = (ev.currentTarget as HTMLElement).getBoundingClientRect();
                            askLinkMessage(ctx, [a], b, { x: r.left, y: r.bottom + 2 });
                        },
                    },
                    cap(edgeWords(a, b)),
                    icon('chevron-down', 10),
                ),
                h(
                    'button',
                    {
                        class: 'btn sm',
                        type: 'button',
                        title: 'Remove this link',
                        'aria-label': 'Remove link',
                        onclick: () => unlink(a, b),
                    },
                    icon('x', 11),
                ),
            );
        };
        fill(
            side,
            h(
                'h4',
                {},
                nodeName(one, spec),
                spec
                    ? h('span', { class: 'lg-chip', style: `background:${ROLE_COLOR[spec.role]}` }, plainRoleName(spec))
                    : null,
            ),
            spec ? h('div', { class: 'dim' }, nodeSummary(spec)) : null,
            problemList(one.id),
            spec?.out.length
                ? h(
                      'div',
                      { class: 'lg-sect' },
                      h('div', { class: 'lg-sect-h' }, 'When it fires, it tells'),
                      ...(outgoing.length
                          ? outgoing.map((x) => rowFor(x.from, x.to, 'out'))
                          : [h('div', { class: 'dim' }, 'Nothing yet.')]),
                      h(
                          'div',
                          { class: 'row' },
                          h(
                              'button',
                              { class: 'btn', type: 'button', onclick: () => startLinkPick(ctx, [one.id]) },
                              icon('link', 12),
                              'Pick in the level',
                          ),
                          h(
                              'button',
                              {
                                  class: 'btn',
                                  type: 'button',
                                  onclick: (ev: Event) => {
                                      const r = (ev.currentTarget as HTMLElement).getBoundingClientRect();
                                      openAddMenu({ x: r.left, y: r.bottom + 2 }, one.id);
                                  },
                              },
                              icon('plus', 12),
                              'Add a node to tell',
                          ),
                      ),
                  )
                : null,
            incoming.length
                ? h(
                      'div',
                      { class: 'lg-sect' },
                      h('div', { class: 'lg-sect-h' }, 'Told by'),
                      ...incoming.map((x) => rowFor(x.from, x.to, 'in')),
                  )
                : null,
            spec?.role === 'exit' ? (host.extra?.(one) ?? null) : null,
            h('details', { class: 'lg-details' }, h('summary', {}, 'All settings'), ...fp.nodes),
        );
    }

    // ---- toolbar wiring -----------------------------------------------------------------------------------------------
    function showLevel(): void {
        scope = 'level';
        scopeSel.value = 'level';
        rebuild();
        fit();
    }
    addBtn.addEventListener('click', () => {
        const r = addBtn.getBoundingClientRect();
        openAddMenu({ x: r.left, y: r.bottom + 4 });
    });
    tidyBtn.addEventListener('click', relayout);
    fitBtn.addEventListener('click', fit);
    scopeSel.addEventListener('change', () => {
        scope = scopeSel.value as typeof scope;
        if (scope === 'selection') pinned.clear();
        shownKey = '';
        rebuild();
        fit();
    });
    depthSel.addEventListener('change', () => {
        depth = Number(depthSel.value);
        shownKey = '';
        rebuild();
        fit();
    });

    return {
        el,
        tools: [scopeSel, depthSel, addBtn, tidyBtn, fitBtn],
        info,
        selectionChanged() {
            // a click on a card of the drawn graph keeps the layout; selecting something new redraws around it
            const sel = [...(ctx.doc().size ? ctx.view().selection : [])];
            const inGraph = sel.length > 0 && sel.every((id) => graph.nodes.has(id));
            if (scope === 'level' || inGraph) draw();
            else rebuild();
            renderSide();
        },
        docChanged() {
            rebuild();
            renderSide();
        },
        levelLoaded() {
            manual.clear();
            pinned.clear();
            selEdge = null;
            shownKey = '';
            graph = { nodes: new Map(), edges: [] };
            problems = new Map();
            rebuild();
            renderSide();
        },
        showLevel,
        openAddMenu,
        problemCount: () => [...problems.values()].reduce((s, l) => s + l.length, 0),
    };
}
