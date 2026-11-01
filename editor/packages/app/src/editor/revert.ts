import { objectChangeVsLvl, revertToLvl } from '@drakan/editor-core';
import type { Ctx } from './ctx.ts';

/**
 * Per-object "Revert to original": put objects back the way they are on the disc (one undo step). Shared by the
 * right-click menu, the Details panel and the Package and Play change list.
 */

export type ObjectChange = 'added' | 'deleted' | 'changed';

/** How an object differs from the disc level, or null (also null when the level did not come from the disc). */
export function changeOf(ctx: Ctx, id: number): ObjectChange | null {
    const src = ctx.src();
    return src ? objectChangeVsLvl(src.lvl.list, ctx.doc(), id) : null;
}

/** The listed ids that differ from the disc level. */
export function changedIds(ctx: Ctx, ids: readonly number[]): number[] {
    return ids.filter((id) => changeOf(ctx, id) !== null);
}

/** Why the objects cannot be reverted now, or null when they can. */
export function revertBlocked(ctx: Ctx, ids: readonly number[]): string | null {
    const v = ctx.view();
    if (!v.policy.structural) return 'Not available in this mode';
    if (ids.length && ids.every((id) => v.lockedIds.has(id))) return 'Locked';
    return null;
}

/** Revert the objects (locked ones are skipped). Returns false when there was nothing to revert. */
export function revertToOriginal(ctx: Ctx, ids: readonly number[]): boolean {
    const src = ctx.src();
    if (!src || revertBlocked(ctx, ids)) return false;
    const v = ctx.view();
    const cmd = revertToLvl(
        ctx.doc(),
        src.lvl,
        src.odbs,
        ids.filter((id) => !v.lockedIds.has(id)),
    );
    if (!cmd) return false;
    v.run(cmd);
    ctx.toast(cmd.label.replace(/^Revert /, 'Reverted '), 'ok');
    return true;
}
