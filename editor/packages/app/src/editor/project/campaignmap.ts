/**
 * Campaign map: the project's levels as cards on a pannable, zoomable canvas, each exit an arrow from its row on the
 * card to the level it leads to (a stub when that level is not in the project). It is the "Map" view of the levels in
 * Project Settings. The graph and the layout are pure (editor-core campaign-map.ts); this file only draws them and
 * turns clicks and drags into the panel's own actions.
 *
 *   click a card        open the level          double-click a card   play from it (desktop app)
 *   click an arrow/row  open the level with that exit selected and the Logic panel shown
 *   drag a card         move it (saved in the project)    drag the background / wheel   pan / zoom
 */
import {
    MAP_CARD_W,
    MAP_HEAD_H,
    MAP_IN_PIN_Y,
    MAP_ROW_H,
    autoLayout,
    buildCampaignGraph,
    mapBounds,
    mapCardHeight,
    mapExitPinY,
    mapPositions,
    mapStart,
    type CampaignGraph,
    type ExitIndex,
    type MapExit,
    type MapNode,
    type MapPoint,
    type ModProject,
    type ProjectIssue,
    type ProjectLevel,
} from '@drakan/editor-core';
import { h, icon } from '../dom.ts';
import './campaignmap.css';

export interface CampaignMapDeps {
    openLevel(l: ProjectLevel): void;
    openExit(l: ProjectLevel, exitId: number): void;
    /** Play the project from a level; null where playing is not possible (browser). */
    play: ((title: string) => void) | null;
    /** Save the card places in the project. */
    setLayout(layout: Record<string, MapPoint>): void;
}

export interface CampaignMapState {
    project: ModProject;
    exits: ExitIndex | null;
    issues: readonly ProjectIssue[];
    /** Changes per level title. */
    counts: ReadonlyMap<string, number>;
    /** One line about the exits while they are missing (reading, failed, no disc); empty when they are known. */
    note: string;
}

export interface CampaignMap {
    /** Draw the state; returns the map's element (the same one every time, so pan and zoom survive). */
    update(s: CampaignMapState): HTMLElement;
}

const SVG = 'http://www.w3.org/2000/svg';
const ZOOM_MIN = 0.25;
const ZOOM_MAX = 2;
const DRAG_PX = 4;
const CLICK_MS = 240;
/** Length of a stub's arrow before its label. */
const STUB_LEN = 34;

const svg = <K extends keyof SVGElementTagNameMap>(tag: K, attrs: Record<string, string | number> = {}) => {
    const el = document.createElementNS(SVG, tag);
    for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, String(v));
    return el;
};

const plural = (n: number, one: string, many = `${one}s`): string => `${n} ${n === 1 ? one : many}`;

export function createCampaignMap(deps: CampaignMapDeps): CampaignMap {
    let state: CampaignMapState | null = null;
    let graph: CampaignGraph = { nodes: [], exitsKnown: false };
    let pos: Record<string, MapPoint> = {};
    let pan = { x: 40, y: 40 };
    let zoom = 1;
    let fittedFor = '';
    /** True once the user has panned, zoomed or dragged a card; until then the view keeps fitting every card. */
    let touched = false;
    let dragging = false;
    let pending: CampaignMapState | null = null;
    let clickTimer = 0;

    const edges = svg('svg', { class: 'cm-edges', 'aria-hidden': 'true' });
    const edgeLayer = svg('g');
    edges.append(edgeLayer);
    const cards = h('div', { class: 'cm-cards' });
    const world = h('div', { class: 'cm-world' }, edges, cards);
    const zoomLabel = h('span', { class: 'cm-zoom', title: 'Zoom' }, '100%');
    const noteEl = h('div', { class: 'cm-note', hidden: true });
    const emptyEl = h(
        'div',
        { class: 'cm-empty', hidden: true },
        'Add levels to the project to see them on the map. Each level becomes a card; each exit an arrow to the level it leads to.',
    );
    const bar = h(
        'div',
        { class: 'cm-bar' },
        h(
            'button',
            {
                class: 'ui-btn sm',
                type: 'button',
                id: 'cm-arrange',
                title: 'Place the cards in the order a player reaches them from the start level',
                onclick: () => arrange(),
            },
            icon('distribute', 13),
            'Auto arrange',
        ),
        h(
            'button',
            {
                class: 'ui-btn sm',
                type: 'button',
                id: 'cm-fit',
                title: 'Show every card (F)',
                onclick: () => fit(),
            },
            icon('focus', 13),
            'Fit',
        ),
        h('span', { class: 'cm-sep' }),
        h('button', { class: 'ui-tool sm', type: 'button', title: 'Zoom out', onclick: () => zoomBy(1 / 1.2) }, '−'),
        zoomLabel,
        h('button', { class: 'ui-tool sm', type: 'button', title: 'Zoom in', onclick: () => zoomBy(1.2) }, '+'),
    );
    const legend = h(
        'div',
        { class: 'cm-legend' },
        h('span', { class: 'cm-key ok' }, 'Exit'),
        h('span', { class: 'cm-key warn' }, 'Exit to fix'),
        h('span', { class: 'cm-key stub' }, 'Leads out of the project'),
        h(
            'span',
            { class: 'cm-help' },
            deps.play
                ? 'Click a card to open it, double-click to play from it. Click an arrow to change where it leads. Drag cards to arrange them.'
                : 'Click a card to open it. Click an arrow to change where it leads. Drag cards to arrange them.',
        ),
    );
    const root = h(
        'div',
        { class: 'cm', tabindex: 0, 'aria-label': 'Campaign map' },
        world,
        bar,
        noteEl,
        emptyEl,
        legend,
    );

    // ---- view transform ---------------------------------------------------------------------------------------------

    function applyView(): void {
        world.style.transform = `translate(${pan.x}px, ${pan.y}px) scale(${zoom})`;
        const minor = 16 * zoom;
        root.style.backgroundSize = `${minor * 8}px ${minor * 8}px, ${minor * 8}px ${minor * 8}px, ${minor}px ${minor}px, ${minor}px ${minor}px`;
        root.style.backgroundPosition = `${pan.x}px ${pan.y}px`;
        zoomLabel.textContent = `${Math.round(zoom * 100)}%`;
    }

    function zoomAt(z: number, cx: number, cy: number): void {
        touched = true;
        const nz = Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, z));
        pan = { x: cx - ((cx - pan.x) * nz) / zoom, y: cy - ((cy - pan.y) * nz) / zoom };
        zoom = nz;
        applyView();
    }

    const zoomBy = (f: number): void => zoomAt(zoom * f, root.clientWidth / 2, root.clientHeight / 2);

    function fit(): void {
        const vw = root.clientWidth;
        const vh = root.clientHeight;
        const b = mapBounds(graph, pos);
        if (!vw || !vh || !b.w) return;
        touched = false;
        // Frame the cards and stubs with a small margin; the back-wire lanes (a little left of and below the cards)
        // are allowed to overflow it rather than shrinking everything.
        const pad = 24;
        const top = 44; // the toolbar sits over the top-left corner
        zoom = Math.min(1, Math.max(ZOOM_MIN, Math.min((vw - pad * 2) / b.w, (vh - pad * 2 - top) / b.h)));
        pan = { x: (vw - b.w * zoom) / 2 - b.x * zoom, y: top + (vh - top - b.h * zoom) / 2 - b.y * zoom };
        applyView();
    }

    new ResizeObserver(() => {
        if (state && (fittedFor !== state.project.id || !touched) && root.clientWidth) {
            fittedFor = state.project.id;
            fit();
        }
    }).observe(root);

    function arrange(): void {
        if (!state) return;
        pos = autoLayout(graph, mapStart(state.project));
        deps.setLayout(pos);
        draw();
        fit();
    }

    // ---- drawing ----------------------------------------------------------------------------------------------------

    function badge(text: string, cls = '', title = ''): HTMLElement {
        return h('span', { class: `cm-badge ${cls}`, title: title || undefined }, text);
    }

    function exitRow(n: MapNode, e: MapExit): HTMLElement {
        const label = e.dest ?? 'unknown destination';
        const tip = e.problem
            ? `${e.name || `Exit ${e.id}`} leads to ${label}: ${e.problem}. Click to change where it leads.`
            : `${e.name || `Exit ${e.id}`} leads to ${label}${e.to ? '' : ', which is not in the project'}. Click to change where it leads.`;
        return h(
            'button',
            {
                class: `cm-exit${e.problem ? ' warn' : ''}${e.to ? '' : ' out'}`,
                type: 'button',
                'data-exit': e.id,
                title: tip,
                style: `height:${MAP_ROW_H}px`,
                onclick: (ev: Event) => {
                    ev.stopPropagation();
                    deps.openExit(n.level, e.id);
                },
                onpointerenter: () => hot(n.title, e.id, true),
                onpointerleave: () => hot(n.title, e.id, false),
            },
            e.problem ? icon('warn', 11) : null,
            h('span', { class: 'cm-exit-dest' }, label),
            h('span', { class: 'cm-exit-name' }, e.name || `Exit ${e.id}`),
            h('span', { class: 'cm-pin out' }),
        );
    }

    function card(n: MapNode, s: CampaignMapState): HTMLElement {
        const p = pos[n.title]!;
        const edits = s.counts.get(n.title) ?? 0;
        const toFix = n.problems.length;
        const head = h(
            'div',
            { class: 'cm-head', style: `height:${MAP_HEAD_H}px` },
            h(
                'div',
                { class: 'cm-row1' },
                h('span', { class: 'cm-idx' }, String(n.index + 1)),
                h('span', { class: 'cm-title' }, n.title),
                deps.play
                    ? h(
                          'button',
                          {
                              class: 'cm-play',
                              type: 'button',
                              title: `Play the whole project in the game, starting in ${n.title}`,
                              'aria-label': `Play from ${n.title}`,
                              onclick: (ev: Event) => {
                                  ev.stopPropagation();
                                  deps.play!(n.title);
                              },
                          },
                          icon('play', 11),
                      )
                    : null,
            ),
            h(
                'div',
                { class: 'cm-row2' },
                n.start ? badge('Start', 'cm-start', 'Play starts in this level') : null,
                n.level.copyOf ? badge('Copy', 'copy', `A new level that starts as ${n.level.copyOf}`) : null,
                edits ? badge(plural(edits, 'change'), 'edits') : badge('No changes', 'cm-dim'),
                toFix ? badge(`${toFix} to fix`, 'warn', n.problems.join('\n')) : null,
                n.unreachable
                    ? badge(
                          'No way in',
                          'cm-dim',
                          'No exit of the project leads here; a player reaches it only from the level list',
                      )
                    : null,
            ),
        );
        const el = h(
            'div',
            {
                class: `cm-card${n.start ? ' cm-start' : ''}${n.level.copyOf ? ' copy' : ''}${toFix ? ' warn' : ''}`,
                'data-level': n.title,
                style: `left:${p.x}px;top:${p.y}px;width:${MAP_CARD_W}px;height:${mapCardHeight(n)}px`,
                title: deps.play ? 'Click to open, double-click to play from here' : 'Click to open this level',
            },
            h('span', { class: 'cm-pin in', style: `top:${MAP_IN_PIN_Y - 5}px` }),
            head,
            n.exits.length ? h('div', { class: 'cm-exits' }, ...n.exits.map((e) => exitRow(n, e))) : null,
        );
        el.addEventListener('pointerenter', () => hot(n.title, null, true));
        el.addEventListener('pointerleave', () => hot(n.title, null, false));
        el.addEventListener('pointerdown', (ev) => startCardDrag(ev, n, el));
        el.addEventListener('dblclick', (ev) => {
            if ((ev.target as Element).closest('.cm-exit, .cm-play')) return;
            window.clearTimeout(clickTimer);
            if (deps.play) deps.play(n.title);
        });
        return el;
    }

    function stubs(n: MapNode): HTMLElement[] {
        const p = pos[n.title]!;
        return n.exits
            .filter((e) => !e.to)
            .map((e) =>
                h(
                    'button',
                    {
                        class: `cm-stub${e.problem ? ' warn' : ''}`,
                        type: 'button',
                        title: `${e.dest ?? 'This exit'} is not one of the project levels. Click to change where the exit leads.`,
                        style: `left:${p.x + MAP_CARD_W + STUB_LEN}px;top:${p.y + mapExitPinY(e.row) - 10}px`,
                        'data-from': n.title,
                        'data-exit': e.id,
                        onclick: () => deps.openExit(n.level, e.id),
                    },
                    e.dest ?? 'unknown destination',
                ),
            );
    }

    function edgePath(n: MapNode, e: MapExit): string {
        const a = pos[n.title]!;
        const x1 = a.x + MAP_CARD_W;
        const y1 = a.y + mapExitPinY(e.row);
        if (!e.to) return `M${x1} ${y1}H${x1 + STUB_LEN - 2}`;
        const b = pos[e.to]!;
        const x2 = b.x - 1;
        const y2 = b.y + MAP_IN_PIN_Y;
        const dx = x2 - x1;
        if (dx < 48) {
            // A wire going back (or into its own level) turns down and runs back along a lane under every card in the
            // span it crosses, instead of cutting across them.
            const r = 44;
            const left = x2 - r;
            const right = x1 + r;
            let bottom = Math.max(a.y + mapCardHeight(n), b.y + MAP_IN_PIN_Y);
            for (const t of graph.nodes) {
                const q = pos[t.title]!;
                if (q.x < right && q.x + MAP_CARD_W > left) bottom = Math.max(bottom, q.y + mapCardHeight(t));
            }
            const lane = bottom + 22 + ((n.index * 3 + e.row) % 6) * 7;
            return `M${x1} ${y1}C${x1 + r} ${y1} ${x1 + r} ${lane} ${x1} ${lane}L${x2} ${lane}C${x2 - r} ${lane} ${x2 - r} ${y2} ${x2} ${y2}`;
        }
        // Blueprint style: leave and arrive horizontally.
        const c = Math.min(220, Math.max(50, dx * 0.5));
        return `M${x1} ${y1}C${x1 + c} ${y1} ${x2 - c} ${y2} ${x2} ${y2}`;
    }

    function arrowHead(n: MapNode, e: MapExit): string {
        const a = pos[n.title]!;
        const [x, y] = e.to
            ? [pos[e.to]!.x - 1, pos[e.to]!.y + MAP_IN_PIN_Y]
            : [a.x + MAP_CARD_W + STUB_LEN, a.y + mapExitPinY(e.row)];
        return `M${x} ${y}l-9 -5v10z`;
    }

    function drawEdges(): void {
        if (!state) return;
        const b = mapBounds(graph, pos);
        const m = 400;
        edges.setAttribute('width', String(b.w + m * 2));
        edges.setAttribute('height', String(b.h + m * 2));
        edges.style.left = `${b.x - m}px`;
        edges.style.top = `${b.y - m}px`;
        edgeLayer.setAttribute('transform', `translate(${m - b.x} ${m - b.y})`);
        const groups: SVGGElement[] = [];
        for (const n of graph.nodes) {
            for (const e of n.exits) {
                const d = edgePath(n, e);
                const tip = `${n.title}: ${e.name || `exit ${e.id}`} to ${e.dest ?? 'an unknown level'}${e.problem ? ` (${e.problem})` : ''}. Click to change where it leads.`;
                const g = svg('g', {
                    class: `cm-edge${e.problem ? ' warn' : ''}${e.to ? '' : ' out'}`,
                    'data-from': n.title,
                    'data-to': e.to ?? '',
                    'data-exit': e.id,
                });
                const title = svg('title');
                title.textContent = tip;
                g.append(title, svg('path', { class: 'hit', d }), svg('path', { class: 'wire', d }));
                g.append(svg('path', { class: 'head', d: arrowHead(n, e) }));
                g.addEventListener('click', () => deps.openExit(n.level, e.id));
                g.addEventListener('pointerenter', () => g.classList.add('hot'));
                g.addEventListener('pointerleave', () => g.classList.remove('hot'));
                groups.push(g);
            }
        }
        // Problem exits on top, so a warning colour is never hidden under a fine wire.
        groups.sort((x, y) => Number(x.classList.contains('warn')) - Number(y.classList.contains('warn')));
        edgeLayer.replaceChildren(...groups);
    }

    /** Light up a card's wires (or one exit's wire) while the pointer is over it. */
    function hot(title: string, exitId: number | null, on: boolean): void {
        for (const g of edgeLayer.querySelectorAll<SVGGElement>('.cm-edge')) {
            const from = g.dataset['from'] === title;
            const hit =
                exitId === null ? from || g.dataset['to'] === title : from && g.dataset['exit'] === String(exitId);
            if (hit) g.classList.toggle('hot', on);
        }
    }

    function draw(): void {
        const s = state;
        if (!s) return;
        cards.replaceChildren(...graph.nodes.flatMap((n) => [card(n, s), ...stubs(n)]));
        drawEdges();
        emptyEl.hidden = graph.nodes.length > 0;
        noteEl.hidden = !s.note || !graph.nodes.length;
        noteEl.textContent = s.note;
        bar.querySelector<HTMLButtonElement>('#cm-arrange')!.disabled = graph.nodes.length < 2;
        bar.querySelector<HTMLButtonElement>('#cm-fit')!.disabled = !graph.nodes.length;
    }

    // ---- pointer ----------------------------------------------------------------------------------------------------

    function startCardDrag(ev: PointerEvent, n: MapNode, el: HTMLElement): void {
        if (ev.button !== 0 || (ev.target as Element).closest('.cm-exit, .cm-play')) return;
        ev.stopPropagation();
        const start = { x: ev.clientX, y: ev.clientY };
        const from = { ...pos[n.title]! };
        let moved = false;
        el.setPointerCapture(ev.pointerId);
        const move = (m: PointerEvent): void => {
            const dx = (m.clientX - start.x) / zoom;
            const dy = (m.clientY - start.y) / zoom;
            if (!moved && Math.hypot(m.clientX - start.x, m.clientY - start.y) < DRAG_PX) return;
            if (!moved) {
                moved = dragging = true;
                touched = true;
                el.classList.add('dragging');
            }
            // Snap to the minor grid (16 units), like dragging a node in a Blueprint graph.
            pos[n.title] = { x: Math.round((from.x + dx) / 16) * 16, y: Math.round((from.y + dy) / 16) * 16 };
            el.style.left = `${pos[n.title]!.x}px`;
            el.style.top = `${pos[n.title]!.y}px`;
            for (const st of cards.querySelectorAll<HTMLElement>('.cm-stub')) {
                if (st.dataset['from'] !== n.title) continue;
                const e = n.exits.find((x) => String(x.id) === st.dataset['exit'])!;
                st.style.left = `${pos[n.title]!.x + MAP_CARD_W + STUB_LEN}px`;
                st.style.top = `${pos[n.title]!.y + mapExitPinY(e.row) - 10}px`;
            }
            drawEdges();
            hot(n.title, null, true);
        };
        const up = (): void => {
            el.removeEventListener('pointermove', move);
            el.removeEventListener('pointerup', up);
            el.removeEventListener('pointercancel', up);
            el.classList.remove('dragging');
            dragging = false;
            if (moved) {
                deps.setLayout({ ...pos });
                if (pending) {
                    const s = pending;
                    pending = null;
                    update(s);
                }
                return;
            }
            window.clearTimeout(clickTimer);
            clickTimer = window.setTimeout(() => deps.openLevel(n.level), deps.play ? CLICK_MS : 0);
        };
        el.addEventListener('pointermove', move);
        el.addEventListener('pointerup', up);
        el.addEventListener('pointercancel', up);
    }

    root.addEventListener('pointerdown', (ev) => {
        const t = ev.target as Element;
        if (t.closest('.cm-bar, .cm-legend, .cm-edge, .cm-stub') || (t.closest('.cm-card') && ev.button === 0)) return;
        root.focus({ preventScroll: true });
        const start = { x: ev.clientX - pan.x, y: ev.clientY - pan.y };
        root.setPointerCapture(ev.pointerId);
        root.classList.add('panning');
        const move = (m: PointerEvent): void => {
            touched = true;
            pan = { x: m.clientX - start.x, y: m.clientY - start.y };
            applyView();
        };
        const up = (): void => {
            root.removeEventListener('pointermove', move);
            root.removeEventListener('pointerup', up);
            root.removeEventListener('pointercancel', up);
            root.classList.remove('panning');
        };
        root.addEventListener('pointermove', move);
        root.addEventListener('pointerup', up);
        root.addEventListener('pointercancel', up);
    });
    root.addEventListener(
        'wheel',
        (ev) => {
            ev.preventDefault();
            const r = root.getBoundingClientRect();
            zoomAt(zoom * Math.exp(-ev.deltaY * 0.0015), ev.clientX - r.left, ev.clientY - r.top);
        },
        { passive: false },
    );
    root.addEventListener('keydown', (ev) => {
        if (ev.target !== root || ev.ctrlKey || ev.altKey || ev.metaKey) return;
        if (ev.key === 'f' || ev.key === 'F') {
            ev.preventDefault();
            fit();
        }
    });

    // ---- state ------------------------------------------------------------------------------------------------------

    function update(s: CampaignMapState): HTMLElement {
        if (dragging) {
            pending = s;
            return root;
        }
        state = s;
        graph = buildCampaignGraph(s.project, s.exits, s.issues);
        pos = mapPositions(graph, s.project.mapLayout, mapStart(s.project));
        draw();
        applyView();
        // The exits arrive after the cards (they widen the map with stubs): keep fitting until the user takes over.
        if ((fittedFor !== s.project.id || !touched) && root.clientWidth) {
            fittedFor = s.project.id;
            fit();
        }
        return root;
    }

    applyView();
    return { update };
}
