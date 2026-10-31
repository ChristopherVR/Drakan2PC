import type { LevelObject } from '@drakan/editor-core';
import type { Ctx } from './ctx.ts';
import { h, icon } from './dom.ts';

/**
 * Linking objects: a link means "when this object triggers, it sends its message to that object". Entry points (the
 * Details panel, the right-click "Link to...", the link handle on the selection in the viewport) all end up in
 * `addLinks`, which goes through `ctx.edit`, so the logic graph, link lines, undo and the mod export stay in sync.
 */

/** `Orc #12` for an object of the open document, `#12` when it is gone (undo labels name what they change). */
export const nameOf = (ctx: Ctx, id: number): string => {
    const o = ctx.doc().get(id);
    return o ? objectLabel(o) : `#${id}`;
};

export const objectLabel = (o: Pick<LevelObject, 'class' | 'id'>): string => `${o.class} #${o.id}`;

/** Link every source to `to` as one undo step; returns how many links were new. */
export function addLinks(ctx: Ctx, sources: readonly number[], to: number): number {
    const doc = ctx.doc();
    const target = doc.get(to);
    const from = sources.filter((i) => i !== to && !doc.get(i)?.links?.includes(to));
    if (!target || !from.length) return 0;
    const first = doc.get(from[0]!);
    const what = from.length === 1 && first ? objectLabel(first) : `${from.length} objects`;
    ctx.edit(`Link ${what} to ${objectLabel(target)}`, from, (o) => {
        o.links = [...new Set([...(o.links ?? []), to])];
    });
    return from.length;
}

let banner: HTMLElement | null = null;

function setBanner(ctx: Ctx, text: string | null): void {
    banner?.remove();
    banner = null;
    if (!text) return;
    const host = ctx.view().renderer.domElement.parentElement;
    if (!host) return;
    banner = h(
        'div',
        { class: 'pick-banner', role: 'status' },
        icon('link', 14),
        h('span', {}, text),
        h(
            'button',
            { class: 'btn sm', type: 'button', onclick: () => ctx.view().cancelPick() },
            'Cancel',
            h('span', { class: 'ui-kbd' }, 'Esc'),
        ),
    );
    host.append(banner);
}

/**
 * Pick mode for a link target: click an object in the viewport or a row in the Outliner (Esc or Cancel ends it). The
 * sources get a link to it.
 */
export function startLinkPick(ctx: Ctx, sources: readonly number[]): void {
    const v = ctx.view();
    const doc = ctx.doc();
    const ids = sources.filter((i) => doc.get(i));
    if (!ids.length || !v.policy.structural) return;
    const first = doc.get(ids[0]!);
    const who = ids.length === 1 && first ? objectLabel(first) : `${ids.length} objects`;
    setBanner(ctx, `Click the object that ${who} should trigger: in the viewport or the Outliner.`);
    v.pickObject((to) => {
        if (to === null) return ctx.toast('Nothing there to link to.', 'warn');
        if (ids.length === 1 && ids[0] === to) return ctx.toast('An object cannot link to itself.', 'warn');
        const n = addLinks(ctx, ids, to);
        const t = ctx.doc().get(to);
        ctx.toast(n ? `Linked to ${t ? objectLabel(t) : `#${to}`}.` : 'Already linked.', n ? 'ok' : 'info');
    });
}

/** Banner while a pick is active, and the drag handle on the selected object. Call once after the view exists. */
export function installLinking(ctx: Ctx): void {
    const v = ctx.view();
    v.onPickChange = () => {
        if (!v.picking) setBanner(ctx, null);
    };
    const host = v.renderer.domElement.parentElement;
    if (!host) return;

    const handle = h(
        'button',
        {
            class: 'link-handle',
            type: 'button',
            title: 'Drag onto another object to link them (the selection triggers it)',
            'aria-label': 'Link handle',
            hidden: true,
        },
        icon('link', 14),
    );
    const svgNs = 'http://www.w3.org/2000/svg';
    const line = document.createElementNS(svgNs, 'svg');
    line.setAttribute('class', 'link-drag-line');
    const path = document.createElementNS(svgNs, 'line');
    line.append(path);
    line.style.display = 'none';
    host.append(line, handle);

    let drag: { id: number } | null = null;
    let lastKey = '';

    const place = (): void => {
        requestAnimationFrame(place);
        const ids = [...v.selection];
        const show = ids.length > 0 && v.policy.structural && !v.picking && !v.gizmo.dragging && v.doc.size > 0;
        let p: { x: number; y: number } | null = null;
        if (show) p = v.objectScreenPos(ids[0]!);
        if (!p || (!show && !drag)) {
            if (!handle.hidden && !drag) handle.hidden = true;
            return;
        }
        const r = host.getBoundingClientRect();
        const x = p.x - r.left + 22;
        const y = p.y - r.top - 30;
        const inside = x > 0 && y > 0 && x < r.width - 20 && y < r.height - 20;
        handle.hidden = !inside && !drag;
        const key = `${Math.round(x)},${Math.round(y)}`;
        if (key !== lastKey && !drag) {
            lastKey = key;
            handle.style.transform = `translate(${x - 12}px, ${y - 12}px)`;
        }
    };
    requestAnimationFrame(place);

    const rel = (e: PointerEvent): { x: number; y: number } => {
        const r = host.getBoundingClientRect();
        return { x: e.clientX - r.left, y: e.clientY - r.top };
    };
    handle.addEventListener('pointerdown', (e) => {
        if (e.button !== 0) return;
        const ids = [...v.selection];
        if (!ids.length) return;
        e.preventDefault();
        e.stopPropagation();
        drag = { id: ids[0]! };
        handle.setPointerCapture(e.pointerId);
        handle.classList.add('dragging');
        const hr = handle.getBoundingClientRect();
        const r = host.getBoundingClientRect();
        const sx = hr.left + hr.width / 2 - r.left;
        const sy = hr.top + hr.height / 2 - r.top;
        const src = v.objectScreenPos(drag.id);
        const ox = src ? src.x - r.left : sx;
        const oy = src ? src.y - r.top : sy;
        path.setAttribute('x1', String(ox));
        path.setAttribute('y1', String(oy));
        path.setAttribute('x2', String(rel(e).x));
        path.setAttribute('y2', String(rel(e).y));
        line.style.display = '';
    });
    handle.addEventListener('pointermove', (e) => {
        if (!drag) return;
        path.setAttribute('x2', String(rel(e).x));
        path.setAttribute('y2', String(rel(e).y));
        const over = v.pickAt(e, false);
        line.classList.toggle('ok', over !== null && !v.selection.has(over));
    });
    const end = (e: PointerEvent, commit: boolean): void => {
        if (!drag) return;
        drag = null;
        handle.classList.remove('dragging');
        line.style.display = 'none';
        line.classList.remove('ok');
        if (handle.hasPointerCapture(e.pointerId)) handle.releasePointerCapture(e.pointerId);
        lastKey = '';
        if (!commit) return;
        const to = v.pickAt(e, false);
        if (to === null) return ctx.toast('Drop the handle on an object to link it.', 'warn');
        if (v.selection.has(to) && v.selection.size === 1) return ctx.toast('An object cannot link to itself.', 'warn');
        const n = addLinks(ctx, [...v.selection], to);
        const t = ctx.doc().get(to);
        ctx.toast(n ? `Linked to ${t ? objectLabel(t) : `#${to}`}.` : 'Already linked.', n ? 'ok' : 'info');
    };
    handle.addEventListener('pointerup', (e) => end(e, true));
    handle.addEventListener('pointercancel', (e) => end(e, false));
}
