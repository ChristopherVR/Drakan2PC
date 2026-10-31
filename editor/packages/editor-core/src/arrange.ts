import { editObjects, type EditObjectsCommand } from './commands.ts';
import type { LevelDocument, LevelObject } from './document.ts';

export type Axis = 0 | 1 | 2;
export type AlignMode = 'min' | 'center' | 'max';

const live = (doc: LevelDocument, ids: readonly number[]): LevelObject[] =>
    ids.map((i) => doc.get(i)).filter((o): o is LevelObject => !!o);

/** New positions that line the objects up on one axis (0 = x, 1 = y, 2 = z) at their min, centre or max extent. */
export function alignPositions(
    doc: LevelDocument,
    ids: readonly number[],
    axis: Axis,
    mode: AlignMode,
): Map<number, [number, number, number]> {
    const objs = live(doc, ids);
    const out = new Map<number, [number, number, number]>();
    if (objs.length < 2) return out;
    const vals = objs.map((o) => o.transform.pos[axis]);
    const lo = Math.min(...vals);
    const hi = Math.max(...vals);
    const target = mode === 'min' ? lo : mode === 'max' ? hi : (lo + hi) / 2;
    for (const o of objs) {
        const p: [number, number, number] = [...o.transform.pos];
        p[axis] = target;
        out.set(o.id, p);
    }
    return out;
}

/** New positions that spread the objects evenly on one axis between the two outermost ones (needs 3 or more). */
export function distributePositions(
    doc: LevelDocument,
    ids: readonly number[],
    axis: Axis,
): Map<number, [number, number, number]> {
    const objs = live(doc, ids).sort((a, b) => a.transform.pos[axis] - b.transform.pos[axis] || a.id - b.id);
    const out = new Map<number, [number, number, number]>();
    if (objs.length < 3) return out;
    const lo = objs[0]!.transform.pos[axis];
    const hi = objs[objs.length - 1]!.transform.pos[axis];
    objs.forEach((o, i) => {
        const p: [number, number, number] = [...o.transform.pos];
        p[axis] = lo + ((hi - lo) * i) / (objs.length - 1);
        out.set(o.id, p);
    });
    return out;
}

/** Command that moves objects to the given positions (one undo step); null when nothing moves. */
export function moveCommand(
    doc: LevelDocument,
    positions: Map<number, readonly [number, number, number]>,
    label = 'Move',
): EditObjectsCommand | null {
    return editObjects(doc, [...positions.keys()], label, (o) => {
        const p = positions.get(o.id)!;
        o.transform.pos = [p[0], p[1], p[2]];
    });
}
