// The story overlay of the level view: cut scenes, shops, quest areas and talking characters drawn where they are.
// Badges and labels are DOM elements over the canvas (crisp text, real hover / click / double-click); camera paths, actor
// lines and trigger boxes are three.js objects in the scene. What is drawn comes from `storymap.ts`; the Show menu's
// "Story" checkboxes (view options `story`) switch each group. Click a badge to select its object, double-click it (or
// the object itself) to open the matching editor on that item.

import * as THREE from 'three';
import type { Ctx } from '../editor/ctx.ts';
import { h } from '../editor/dom.ts';
import type { LevelView } from '../level.ts';
import type { StoryGroup } from '../scene/viewopts.ts';
import { buildStoryMarks, type StoryFocus, type StoryMapSource, type StoryMark, type Vec3 } from './storymap.ts';
import './storyview.css';

const S = 0.001;

/** Group colours (badges, lines, paths); quest areas use green to start and amber to finish. */
export const STORY_COLORS = {
    cutscene: 0xff5fd2,
    vendor: 0xb28cff,
    quest: 0x6ad37a,
    talk: 0x6cc4ff,
    start: 0x6ad37a,
    complete: 0xf0b43c,
    trigger: 0xff5fd2,
} as const;

/** 24x24 stroke icons for the badges (the shared icon set has no film, shop or speech glyphs). */
const GLYPHS: Record<StoryGroup, string> = {
    cutscene:
        '<rect x="3" y="8" width="18" height="12" rx="1.5"/><path d="M3 8l3-4h3l-3 4M9 8l3-4h3l-3 4M15 8l3-4h3l-3 4"/>',
    vendor: '<path d="M4 9h16l-1 11H5z"/><path d="M8 9V7a4 4 0 0 1 8 0v2"/><path d="M10 13h4"/>',
    quest: '<path d="M12 4v10"/><path d="M12 19.5v.01" stroke-width="3"/>',
    talk: '<path d="M4 5h16v11H9l-5 4z"/><path d="M8 9h8M8 12h5"/>',
};

export function storyGlyph(g: StoryGroup, size = 14): SVGElement {
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('viewBox', '0 0 24 24');
    svg.setAttribute('width', String(size));
    svg.setAttribute('height', String(size));
    svg.setAttribute('fill', 'none');
    svg.setAttribute('stroke', 'currentColor');
    svg.setAttribute('stroke-width', '2');
    svg.setAttribute('stroke-linecap', 'round');
    svg.setAttribute('stroke-linejoin', 'round');
    svg.setAttribute('aria-hidden', 'true');
    svg.innerHTML = GLYPHS[g];
    return svg;
}

export interface StoryViewHost {
    ctx: () => Ctx;
    /** Names, paths and titles from the story sessions (they may still be loading: rebuild when they change). */
    source: () => StoryMapSource;
    /** Open the editor for a mark (double-click). */
    open: (focus: StoryFocus, mark: StoryMark) => void;
}

export interface StoryView {
    /** Recompute the marks (document, sessions or view options changed). */
    refresh(): void;
    /** Marks currently drawn. */
    marks(): readonly StoryMark[];
}

interface Badge {
    el: HTMLElement;
    mark: StoryMark;
    /** Stacked badges on one object: index from the top. */
    slot: number;
}

const hex = (c: number): string => `#${c.toString(16).padStart(6, '0')}`;
const v3 = (p: Vec3): THREE.Vector3 => new THREE.Vector3(p[0] * S, p[1] * S, p[2] * S);
const unitBox = new THREE.BoxGeometry(1, 1, 1);
const unitEdges = new THREE.EdgesGeometry(unitBox);
const dot = new THREE.SphereGeometry(1, 10, 8);
/** Labels show within this distance of the camera (scene units; 1 = 1000 game units), or when hovered / selected. */
const LABEL_NEAR = 45;

export function createStoryView(host: StoryViewHost): StoryView {
    let view: LevelView | null = null;
    let layer: HTMLElement | null = null;
    let tip: HTMLElement | null = null;
    const root = new THREE.Group();
    root.name = 'story-overlay';
    root.renderOrder = 21;
    let marks: StoryMark[] = [];
    let badges: Badge[] = [];
    let areaLabels: { el: HTMLElement; pos: THREE.Vector3; owner: number }[] = [];
    let hot: number | null = null;
    let optsKey = '';
    let selKey = '';
    let camKey = '';
    let dirty = true;

    const groupsOn = (v: LevelView): Set<StoryGroup> =>
        new Set((Object.entries(v.viewOptions.story) as [StoryGroup, boolean][]).filter((e) => e[1]).map((e) => e[0]));

    function attach(v: LevelView): void {
        if (view === v) return;
        detach();
        view = v;
        const parent = v.renderer.domElement.parentElement;
        if (!parent) return;
        layer = h('div', { class: 'sv-layer', 'aria-label': 'Story in the level' });
        tip = h('div', { class: 'sv-tip', role: 'tooltip', hidden: true });
        parent.append(layer, tip);
        v.scene.add(root);
        v.afterFrame.add(tick);
        v.renderer.domElement.addEventListener('dblclick', onCanvasDblClick);
    }

    function detach(): void {
        if (!view) return;
        view.afterFrame.delete(tick);
        view.renderer.domElement.removeEventListener('dblclick', onCanvasDblClick);
        root.parent?.remove(root);
        layer?.remove();
        tip?.remove();
        layer = tip = null;
        view = null;
    }

    function currentView(): LevelView | null {
        try {
            const v = host.ctx().view();
            attach(v);
            return v;
        } catch {
            return null;
        }
    }

    const markFor = (id: number): StoryMark | undefined => marks.find((m) => m.id === id && m.focus);

    function onCanvasDblClick(e: MouseEvent): void {
        const v = view;
        if (!v || e.button !== 0) return;
        const id = v.pickAt(e, true);
        const m = id === null ? undefined : markFor(id);
        if (m?.focus) host.open(m.focus, m);
    }

    // ---- three.js part ------------------------------------------------------------------------------------------------

    function clearScene(): void {
        for (const c of [...root.children]) {
            root.remove(c);
            c.traverse((x) => {
                const m = x as THREE.Mesh;
                if (m.geometry && m.geometry !== unitBox && m.geometry !== unitEdges && m.geometry !== dot)
                    m.geometry.dispose();
                (m.material as THREE.Material | undefined)?.dispose?.();
            });
        }
    }

    function drawScene(v: LevelView): void {
        clearScene();
        areaLabels.forEach((a) => a.el.remove());
        areaLabels = [];
        const sel = v.selection;
        for (const m of marks) {
            const focus = sel.has(m.id) || hot === m.id || m.areas.some((a) => sel.has(a.id));
            const strength = focus ? 1 : 0.4;
            const color = STORY_COLORS[m.group];
            for (const a of m.areas) {
                const c = STORY_COLORS[a.tone];
                const g = new THREE.Group();
                g.position.copy(v3(a.box.center));
                g.rotation.set(a.box.rot[0], a.box.rot[1], a.box.rot[2]);
                g.scale.set(a.box.half[0] * 2 * S, a.box.half[1] * 2 * S, a.box.half[2] * 2 * S);
                const fill = new THREE.Mesh(
                    unitBox,
                    new THREE.MeshBasicMaterial({
                        color: c,
                        transparent: true,
                        opacity: focus ? 0.2 : 0.1,
                        depthWrite: false,
                        side: THREE.DoubleSide,
                    }),
                );
                const edge = new THREE.LineSegments(
                    unitEdges,
                    new THREE.LineBasicMaterial({ color: c, transparent: true, opacity: 0.35 + 0.5 * strength }),
                );
                fill.renderOrder = edge.renderOrder = 21;
                g.add(fill, edge);
                root.add(g);
                if (m.group === 'quest' && layer) {
                    const el = h(
                        'div',
                        { class: `sv-area-label sv-${a.tone}` },
                        a.label.length > 60 ? `${a.label.slice(0, 58)}...` : a.label,
                    );
                    layer.append(el);
                    const top = v3([a.box.center[0], a.box.center[1] + a.box.half[1], a.box.center[2]]);
                    areaLabels.push({ el, pos: top, owner: m.id });
                }
            }
            for (const l of m.lines) {
                const line = new THREE.Line(
                    new THREE.BufferGeometry().setFromPoints([v3(l.from), v3(l.to)]),
                    new THREE.LineDashedMaterial({
                        color,
                        dashSize: 0.4,
                        gapSize: 0.25,
                        transparent: true,
                        opacity: 0.25 + 0.65 * strength,
                        depthTest: false,
                    }),
                );
                line.computeLineDistances();
                line.renderOrder = 22;
                root.add(line);
            }
            for (const p of m.paths) {
                const pts = p.map(v3);
                if (pts.length > 1) {
                    const line = new THREE.Line(
                        new THREE.BufferGeometry().setFromPoints(pts),
                        new THREE.LineBasicMaterial({
                            color,
                            transparent: true,
                            opacity: 0.3 + 0.7 * strength,
                            depthTest: false,
                        }),
                    );
                    line.renderOrder = 22;
                    root.add(line);
                }
                pts.forEach((q, i) => {
                    const first = i === 0;
                    const last = i === pts.length - 1;
                    const d = new THREE.Mesh(
                        dot,
                        new THREE.MeshBasicMaterial({
                            color: first ? 0x7ee08c : last ? 0xff6b6b : color,
                            transparent: true,
                            opacity: 0.35 + 0.65 * strength,
                            depthTest: false,
                        }),
                    );
                    d.position.copy(q);
                    d.scale.setScalar(first || last ? 0.45 : 0.25);
                    d.renderOrder = 23;
                    root.add(d);
                });
            }
        }
    }

    // ---- DOM part ---------------------------------------------------------------------------------------------------

    function showTip(m: StoryMark, x: number, y: number): void {
        if (!tip || !layer) return;
        const r = layer.getBoundingClientRect();
        tip.replaceChildren(
            h('div', { class: 'sv-tip-head' }, storyGlyph(m.group, 13), h('strong', {}, m.title)),
            ...m.detail.map((d) => h('div', { class: 'sv-tip-line' }, d)),
        );
        tip.hidden = false;
        const w = tip.offsetWidth;
        const left = Math.min(Math.max(4, x - r.left + 14), r.width - w - 4);
        tip.style.transform = `translate(${Math.round(left)}px, ${Math.round(y - r.top + 16)}px)`;
    }
    const hideTip = (): void => {
        if (tip) tip.hidden = true;
    };

    function setHot(id: number | null): void {
        if (hot === id) return;
        hot = id;
        dirty = true;
    }

    function drawBadges(): void {
        badges.forEach((b) => b.el.remove());
        badges = [];
        if (!layer) return;
        const slots = new Map<number, number>();
        for (const m of marks) {
            const slot = slots.get(m.id) ?? 0;
            slots.set(m.id, slot + 1);
            const el = h(
                'button',
                {
                    type: 'button',
                    class: `sv-badge sv-${m.group}`,
                    'aria-label': `${m.title}. ${m.detail[0] ?? ''}`,
                    style: `--sv-c:${hex(STORY_COLORS[m.group])}`,
                },
                h('span', { class: 'sv-ico' }, storyGlyph(m.group, 13)),
                h('span', { class: 'sv-label' }, m.title.length > 48 ? `${m.title.slice(0, 46)}...` : m.title),
            );
            el.addEventListener('pointerenter', (e) => {
                setHot(m.id);
                showTip(m, e.clientX, e.clientY);
            });
            el.addEventListener('pointermove', (e) => showTip(m, e.clientX, e.clientY));
            el.addEventListener('pointerleave', () => {
                if (hot === m.id) setHot(null);
                hideTip();
            });
            el.addEventListener('click', (e) => {
                e.stopPropagation();
                const v = view;
                if (!v) return;
                v.select([m.id], e.shiftKey || e.ctrlKey);
            });
            el.addEventListener('dblclick', (e) => {
                e.stopPropagation();
                hideTip();
                if (m.focus) host.open(m.focus, m);
            });
            el.addEventListener('contextmenu', (e) => e.preventDefault());
            // the wheel still zooms the view
            el.addEventListener('wheel', (e) => view?.renderer.domElement.dispatchEvent(new WheelEvent('wheel', e)), {
                passive: true,
            });
            layer.append(el);
            badges.push({ el, mark: m, slot });
        }
    }

    const tmp = new THREE.Vector3();
    function place(v: LevelView): void {
        if (!layer) return;
        const cv = v.renderer.domElement;
        const w = cv.clientWidth;
        const hgt = cv.clientHeight;
        const camPos = v.camera.position;
        const project = (p: THREE.Vector3): { x: number; y: number; d: number } | null => {
            tmp.copy(p).project(v.camera);
            if (tmp.z > 1 || tmp.z < -1) return null;
            return { x: ((tmp.x + 1) / 2) * w, y: ((1 - tmp.y) / 2) * hgt, d: camPos.distanceTo(p) };
        };
        for (const b of badges) {
            const p = project(v3(b.mark.pos));
            if (!p || p.x < -40 || p.y < -40 || p.x > w + 40 || p.y > hgt + 40) {
                b.el.hidden = true;
                continue;
            }
            b.el.hidden = false;
            const focus = v.selection.has(b.mark.id) || hot === b.mark.id;
            b.el.classList.toggle('sel', v.selection.has(b.mark.id));
            b.el.classList.toggle('far', !focus && p.d > LABEL_NEAR);
            b.el.style.transform = `translate(${Math.round(p.x)}px, ${Math.round(p.y - 30 - b.slot * 24)}px)`;
            b.el.style.zIndex = String(focus ? 100000 : Math.max(1, 10000 - Math.round(p.d)));
        }
        for (const a of areaLabels) {
            const p = project(a.pos);
            const focus = v.selection.has(a.owner) || hot === a.owner;
            if (!p || (!focus && p.d > LABEL_NEAR)) {
                a.el.hidden = true;
                continue;
            }
            a.el.hidden = false;
            a.el.style.transform = `translate(${Math.round(p.x)}px, ${Math.round(p.y)}px)`;
        }
    }

    function rebuild(v: LevelView): void {
        const on = groupsOn(v);
        const env = (() => {
            try {
                return host.ctx().schemas();
            } catch {
                return null;
            }
        })();
        marks =
            env && on.size && v.doc.size
                ? buildStoryMarks(v.doc, env, host.source(), {
                      skip: (id) => v.hiddenIds.has(id),
                      groups: on,
                  })
                : [];
        drawBadges();
        drawScene(v);
        camKey = '';
    }

    function tick(): void {
        const v = view;
        if (!v) return;
        const ok = JSON.stringify(v.viewOptions.story);
        if (ok !== optsKey) {
            optsKey = ok;
            rebuild(v);
            dirty = false;
        }
        const sk = [...v.selection].join(',');
        if (sk !== selKey || dirty) {
            selKey = sk;
            dirty = false;
            drawScene(v);
            camKey = '';
        }
        const e = v.camera.matrixWorld.elements;
        const ck = `${e[12]},${e[13]},${e[14]},${e[0]},${e[5]},${e[9]},${e[2]},${v.camera.fov},${v.renderer.domElement.clientWidth},${v.renderer.domElement.clientHeight},${hot}`;
        if (ck !== camKey) {
            camKey = ck;
            place(v);
        }
    }

    return {
        refresh() {
            const v = currentView();
            if (!v) return;
            hideTip();
            hot = null;
            optsKey = JSON.stringify(v.viewOptions.story);
            rebuild(v);
        },
        marks: () => marks,
    };
}
