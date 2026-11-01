import type { Ctx } from './ctx.ts';
import { startLinkPick } from './linking.ts';
import { changedIds, revertBlocked, revertToOriginal } from './revert.ts';
import { contextItems, type ContextItem, type ContextTarget } from './registry.ts';
import type { MenuItem } from './ui.ts';

function extraItem(x: ContextItem, t: ContextTarget): MenuItem {
    const why = x.unavailable?.(t) ?? null;
    return {
        label: x.label,
        ...(x.icon ? { icon: x.icon } : {}),
        ...(why ? { disabled: true, hint: why } : {}),
        run: () => x.run(t),
    };
}

/**
 * The right-click menu of objects, shared by the Outliner and the viewport. Other modules add entries with
 * `registerContextItem` (registry.ts); they show up under "More" at the end.
 */
export function buildObjectMenu(
    ctx: Ctx,
    ids: number[],
    opts: { ground?: [number, number, number] | null } = {},
): MenuItem[] {
    const v = ctx.view();
    if (ids.length && !ids.every((i) => v.selection.has(i))) v.select(ids);
    const point = opts.ground ?? null;
    const extra = contextItems({ ids, ground: point });
    if (!ids.length) {
        // Empty space: only what plugins offer for a point in the world.
        const own = extra.filter((x) => x.where !== 'object');
        if (!own.length) return [];
        return [{ heading: 'Here' }, ...own.map((x) => extraItem(x, { ids, ground: point }))];
    }
    const hidden = ids.every((i) => v.hiddenIds.has(i));
    const locked = ids.every((i) => v.lockedIds.has(i));
    const edit = v.policy.structural;
    const items: MenuItem[] = [
        { heading: ids.length === 1 ? `Object #${ids[0]}` : `${ids.length} objects` },
        { label: 'Frame in viewport', icon: 'focus', hint: 'F', run: () => ctx.locate(ids) },
        { label: 'Select same class', icon: 'select', hint: 'Ctrl+Shift+A', run: () => v.selectSameClass() },
        { sep: true },
        {
            label: 'Link to...',
            icon: 'link',
            disabled: !edit,
            run: () => startLinkPick(ctx, ids),
        },
        { sep: true },
        {
            label: hidden ? 'Show' : 'Hide',
            icon: hidden ? 'eye' : 'eye-off',
            hint: 'H',
            run: () => v.setHidden(ids, !hidden),
        },
        {
            label: locked ? 'Unlock' : 'Lock',
            icon: locked ? 'unlock' : 'lock',
            hint: 'L',
            run: () => v.setLocked(ids, !locked),
        },
        { sep: true },
        { label: 'Duplicate', icon: 'copy', hint: 'Ctrl+D', disabled: !edit, run: () => v.duplicateSelected() },
        { label: 'Delete', icon: 'trash', hint: 'Del', danger: true, disabled: !edit, run: () => v.deleteSelected() },
    ];
    // Only for objects that differ from the disc level; an added object is removed.
    const changed = changedIds(ctx, ids);
    if (changed.length) {
        const why = revertBlocked(ctx, changed);
        items.push({
            label:
                changed.length === ids.length ? 'Revert to original' : `Revert ${changed.length} changed to original`,
            icon: 'undo',
            ...(why ? { disabled: true, hint: why } : {}),
            run: () => void revertToOriginal(ctx, changed),
        });
    }
    if (extra.length) {
        items.push({ sep: true }, ...extra.map((x) => extraItem(x, { ids, ground: point })));
    }
    return items;
}
