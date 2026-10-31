// Pure scheduling rules for animated instances in the level viewport (no three.js): which instances animate, how often
// they re-pose, and where in the clip each one starts. Unit tested without a renderer.

export interface AnimCandidate {
    id: number;
    /** Distance from the camera in scene units. */
    dist: number;
    /** Inside the camera frustum. */
    visible: boolean;
}

/** Most instances that are skinned at once (each owns a CPU-skinned mesh). */
export const MAX_ANIMATED = 48;

/** Ids to animate: the nearest visible candidates within `range`, at most `max`. Off-screen and far ones stay frozen. */
export function chooseActive(cands: readonly AnimCandidate[], range: number, max = MAX_ANIMATED): number[] {
    const near = cands.filter((c) => c.visible && c.dist <= range);
    near.sort((a, b) => a.dist - b.dist || a.id - b.id);
    return near.slice(0, max).map((c) => c.id);
}

/** Seconds between re-poses: full rate close up, slower with distance (a far creature is a few pixels). */
export function poseInterval(dist: number): number {
    if (dist < 40) return 1 / 30;
    if (dist < 120) return 1 / 20;
    if (dist < 300) return 1 / 12;
    return 1 / 8;
}

/** Start time inside a clip for object `id`, so a crowd does not move in lockstep (stable across rebuilds). */
export function phaseOffset(id: number, duration: number): number {
    if (!(duration > 0)) return 0;
    const x = Math.imul(id | 0, 2654435761) >>> 0;
    return (x / 4294967296) * duration;
}

/** Animation range in scene units for a draw distance. */
export function animRange(drawDistance: number): number {
    return Math.min(600, Math.max(40, drawDistance * 0.25));
}
