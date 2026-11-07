import type { AssetClass } from '@drakan/editor-core';
import type { Ctx } from './ctx.ts';
import { h } from './dom.ts';

/** True for a moment after a drag ended, so the click that follows the pointer-up is ignored. */
export const dragState = { suppressClick: false };

/**
 * What a drag looks like over the panels: they do not take a class or a story set-up, so the ghost turns red, says where
 * the drop works and the cursor shows "not allowed". Over the viewport the ghost goes translucent, over anything else it
 * stays as it is.
 */
function showDropTarget(ghost: HTMLElement, x: number, y: number, overViewport: boolean): void {
    const panel = !overViewport && !!document.elementFromPoint(x, y)?.closest('#ed-left, #ed-right, #ed-bottom, #ed-toolbar');
    ghost.classList.toggle('no', panel);
    ghost.title = panel ? 'Drop it in the level view' : '';
    document.body.classList.toggle('drag-no', panel);
}

/**
 * Pointer-based drag of a catalog class into the viewport (Content Browser tiles and list rows share it). Works
 * with a mouse, touch and pen, shows the model thumbnail as the ghost and a ground marker in the viewport. The native
 * HTML5 drag is not used: browsers differ and automation cannot start it.
 *
 * `thumb` returns a decoded image for the ghost when one exists; `load` asks for the thumbnail to be made; `onDrop`
 * hears about a drop on the viewport (the Place panel's recently placed list).
 */
export function attachAssetDrag(
    el: HTMLElement,
    cls: AssetClass,
    ctx: Ctx,
    thumb: (resource: string) => HTMLImageElement | undefined,
    load: (cls: AssetClass) => void,
    ignore = '.star',
    onDrop?: () => void,
): void {
    el.addEventListener('pointerdown', (e) => {
        if (e.button !== 0 || (ignore && (e.target as HTMLElement).closest(ignore))) return;
        const sx = e.clientX;
        const sy = e.clientY;
        const view = ctx.view();
        let ghost: HTMLElement | null = null;
        const move = (m: PointerEvent): void => {
            if (!ghost) {
                if (Math.hypot(m.clientX - sx, m.clientY - sy) < 5) return;
                view.dragKey = cls.resource;
                load(cls);
                const img = thumb(cls.resource);
                ghost = h('div', { class: 'drag-ghost live' }, img?.complete ? img.cloneNode() : null, cls.name);
                document.body.append(ghost);
                document.body.classList.add('dragging-asset');
            }
            ghost.style.left = `${m.clientX + 12}px`;
            ghost.style.top = `${m.clientY + 12}px`;
            const over = view.isOverViewport(m.clientX, m.clientY);
            if (over) {
                view.dragHover(m);
                ghost.classList.add('over');
            } else {
                view.placeGhostHide();
                ghost.classList.remove('over');
            }
            showDropTarget(ghost, m.clientX, m.clientY, over);
        };
        const end = (u: PointerEvent, cancelled: boolean): void => {
            window.removeEventListener('pointermove', move);
            window.removeEventListener('pointerup', up);
            window.removeEventListener('pointercancel', cancel);
            document.body.classList.remove('dragging-asset', 'drag-no');
            if (!ghost) return;
            ghost.remove();
            if (!cancelled && view.isOverViewport(u.clientX, u.clientY)) {
                view.dragDrop(u);
                onDrop?.();
            } else view.dragCancel();
            dragState.suppressClick = true;
            setTimeout(() => (dragState.suppressClick = false), 0);
        };
        const up = (u: PointerEvent): void => end(u, false);
        const cancel = (u: PointerEvent): void => end(u, true);
        window.addEventListener('pointermove', move);
        window.addEventListener('pointerup', up);
        window.addEventListener('pointercancel', cancel);
    });
}

/**
 * The same pointer drag for something that is not a class (the Place panel's story set-ups: a cut scene, a quest area).
 * The viewport shows its ground marker under the pointer; a drop calls `onDrop` with that ground point (game units).
 * `label` is the ghost's text and `glyph` an optional icon in front of it.
 */
export function attachPointDrag(
    el: HTMLElement,
    key: string,
    label: string,
    ctx: Ctx,
    onDrop: (at: [number, number, number]) => void,
    glyph?: () => Element,
): void {
    el.addEventListener('pointerdown', (e) => {
        if (e.button !== 0 || el.getAttribute('aria-disabled') === 'true') return;
        const sx = e.clientX;
        const sy = e.clientY;
        const view = ctx.view();
        let ghost: HTMLElement | null = null;
        const move = (m: PointerEvent): void => {
            if (!ghost) {
                if (Math.hypot(m.clientX - sx, m.clientY - sy) < 5) return;
                view.dragKey = key;
                ghost = h('div', { class: 'drag-ghost live point' }, glyph?.() ?? null, label);
                document.body.append(ghost);
                document.body.classList.add('dragging-asset');
            }
            ghost.style.left = `${m.clientX + 12}px`;
            ghost.style.top = `${m.clientY + 12}px`;
            const over = view.isOverViewport(m.clientX, m.clientY);
            if (over) {
                view.dragHover(m);
                ghost.classList.add('over');
            } else {
                view.placeGhostHide();
                ghost.classList.remove('over');
            }
            showDropTarget(ghost, m.clientX, m.clientY, over);
        };
        const end = (u: PointerEvent, cancelled: boolean): void => {
            window.removeEventListener('pointermove', move);
            window.removeEventListener('pointerup', up);
            window.removeEventListener('pointercancel', cancel);
            document.body.classList.remove('dragging-asset', 'drag-no');
            if (!ghost) return;
            ghost.remove();
            const at = !cancelled && view.isOverViewport(u.clientX, u.clientY) ? view.groundAt(u) : null;
            view.dragCancel();
            if (at) onDrop(at);
            dragState.suppressClick = true;
            setTimeout(() => (dragState.suppressClick = false), 0);
        };
        const up = (u: PointerEvent): void => end(u, false);
        const cancel = (u: PointerEvent): void => end(u, true);
        window.addEventListener('pointermove', move);
        window.addEventListener('pointerup', up);
        window.addEventListener('pointercancel', cancel);
    });
}
