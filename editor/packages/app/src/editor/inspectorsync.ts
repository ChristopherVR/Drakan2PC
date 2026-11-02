import type { LevelObject } from '@drakan/editor-core';

/**
 * The Details scroll position after a rebuild: a different selection starts at the top (so the header and the
 * "Changed from the original level" bar are in view), the same selection keeps where the user was.
 */
export const scrollAfterRebuild = (prevIds: string, nextIds: string, scrollTop: number): number =>
    prevIds === nextIds ? scrollTop : 0;

/** What the per-object sections (animation, vendor) are built from: changes whenever an edit or undo touches it. */
export const objectFieldsPrint = (o: LevelObject): string => JSON.stringify(o.fields ?? []);
