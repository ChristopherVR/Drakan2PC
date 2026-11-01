import { cloneObject, type LevelDocument, type LevelObject, type Transform } from './document.ts';

export interface Command {
    readonly label: string;
    apply(doc: LevelDocument): void;
    revert(doc: LevelDocument): void;
}

/** Name for history labels: `Orc #123` for one object, `3 objects` for several. */
export function describeObjects(objs: readonly Pick<LevelObject, 'class' | 'id'>[]): string {
    if (objs.length === 1) return `${objs[0]!.class} #${objs[0]!.id}`;
    return `${objs.length} objects`;
}

const cloneT = (t: Transform): Transform => ({ pos: [...t.pos], rot: [...t.rot] });

export class TransformCommand implements Command {
    constructor(
        private readonly before: Map<number, Transform>,
        private readonly after: Map<number, Transform>,
        readonly label = 'Transform',
    ) {}
    private set(doc: LevelDocument, m: Map<number, Transform>): void {
        for (const [id, t] of m) {
            const o = doc.get(id);
            if (o) o.transform = cloneT(t);
        }
    }
    apply(doc: LevelDocument): void {
        this.set(doc, this.after);
    }
    revert(doc: LevelDocument): void {
        this.set(doc, this.before);
    }
}

export class SetScaleCommand implements Command {
    readonly label = 'Scale';
    private old: [number, number, number] | undefined;
    constructor(
        private readonly id: number,
        private readonly value: [number, number, number],
    ) {}
    apply(doc: LevelDocument): void {
        const o = doc.get(this.id);
        if (!o?.scale) return;
        this.old = [...o.scale];
        o.scale = [...this.value];
    }
    revert(doc: LevelDocument): void {
        const o = doc.get(this.id);
        if (o?.scale && this.old) o.scale = [...this.old];
    }
}

export class DeleteCommand implements Command {
    private removed: LevelObject[] = [];
    constructor(
        private readonly ids: number[],
        readonly label = 'Delete',
    ) {}
    apply(doc: LevelDocument): void {
        this.removed = [];
        for (const id of this.ids) {
            const o = doc.remove(id);
            if (o) this.removed.push(o);
        }
    }
    revert(doc: LevelDocument): void {
        for (const o of this.removed) doc.insert(o);
    }
}

export class AddCommand implements Command {
    /** Ids of the objects added (allocated on first apply, stable across redo). */
    readonly ids: number[] = [];
    constructor(
        private readonly objects: LevelObject[],
        readonly label = 'Add',
    ) {}
    apply(doc: LevelDocument): void {
        if (this.ids.length === 0) {
            const placed = this.objects.map((o) => doc.insert({ ...cloneObject(o), id: 0 }));
            this.ids.push(...placed.map((o) => o.id));
            this.objects.splice(0, this.objects.length, ...placed.map(cloneObject));
        } else {
            for (const o of this.objects) doc.insert(cloneObject(o));
        }
    }
    revert(doc: LevelDocument): void {
        for (const id of this.ids) doc.remove(id);
    }
}

export class SetPropCommand implements Command {
    readonly label = 'Set property';
    private old: LevelObject['props'][string] | undefined;
    constructor(
        private readonly id: number,
        private readonly key: string,
        private readonly value: LevelObject['props'][string] | undefined,
    ) {}
    private put(doc: LevelDocument, v: LevelObject['props'][string] | undefined): void {
        const o = doc.get(this.id);
        if (!o) return;
        if (v === undefined) delete o.props[this.key];
        else o.props[this.key] = v;
    }
    apply(doc: LevelDocument): void {
        this.old = doc.get(this.id)?.props[this.key];
        this.put(doc, this.value);
    }
    revert(doc: LevelDocument): void {
        this.put(doc, this.old);
    }
}

/**
 * Snapshot command for any change to objects (properties, fields, links, scale, several objects at once). Built with
 * `editObjects`, which clones the objects, runs the mutator on the clones and keeps before/after copies, so a multi-edit
 * across a selection is one undo step.
 */
export class EditObjectsCommand implements Command {
    constructor(
        private readonly before: Map<number, LevelObject>,
        private readonly after: Map<number, LevelObject>,
        readonly label = 'Edit',
    ) {}
    get ids(): number[] {
        return [...this.after.keys()];
    }
    private put(doc: LevelDocument, m: Map<number, LevelObject>): void {
        for (const o of m.values()) doc.replace(cloneObject(o));
    }
    apply(doc: LevelDocument): void {
        this.put(doc, this.after);
    }
    revert(doc: LevelDocument): void {
        this.put(doc, this.before);
    }
}

/** Build an `EditObjectsCommand` that applies `fn` to every listed object (skipping unknown ids). Null when nothing changes. */
export function editObjects(
    doc: LevelDocument,
    ids: readonly number[],
    label: string,
    fn: (o: LevelObject) => void,
): EditObjectsCommand | null {
    const before = new Map<number, LevelObject>();
    const after = new Map<number, LevelObject>();
    for (const id of ids) {
        const o = doc.get(id);
        if (!o) continue;
        const b = cloneObject(o);
        const a = cloneObject(o);
        fn(a);
        if (JSON.stringify(a) === JSON.stringify(b)) continue;
        before.set(id, b);
        after.set(id, a);
    }
    return after.size ? new EditObjectsCommand(before, after, label) : null;
}

/**
 * Before/after snapshot of whole objects where either side may be missing (`null`): the object is removed, put back or
 * replaced. Used by `revertObjects`, which can remove an added object and restore a deleted one in the same step.
 */
export class SnapshotCommand implements Command {
    constructor(
        private readonly before: Map<number, LevelObject | null>,
        private readonly after: Map<number, LevelObject | null>,
        readonly label: string,
    ) {}
    get ids(): number[] {
        return [...this.after.keys()];
    }
    private put(doc: LevelDocument, m: Map<number, LevelObject | null>): void {
        for (const [id, o] of m) {
            if (!o) doc.remove(id);
            else if (doc.has(id)) doc.replace(cloneObject(o));
            else doc.insert(cloneObject(o));
        }
    }
    apply(doc: LevelDocument): void {
        this.put(doc, this.after);
    }
    revert(doc: LevelDocument): void {
        this.put(doc, this.before);
    }
}

/** One object to revert and its original (disc) version; `original` is undefined for an object that is not on the disc. */
export interface RevertTarget {
    id: number;
    original: LevelObject | undefined;
}

/**
 * Build one undo step that puts objects back the way they were on the disc:
 *   - a changed object gets its original position, rotation, scale, links and fields back (editor notes in `props`
 *     are kept);
 *   - an added object (no original) is removed, and links from other objects to it are dropped;
 *   - a deleted object (not in the document) is put back with its original id.
 * Null when nothing would change.
 */
export function revertObjects(
    doc: LevelDocument,
    targets: readonly RevertTarget[],
    label?: string,
): SnapshotCommand | null {
    const before = new Map<number, LevelObject | null>();
    const after = new Map<number, LevelObject | null>();
    const named: Pick<LevelObject, 'class' | 'id'>[] = [];
    const removed = new Set<number>();
    for (const { id, original } of targets) {
        const cur = doc.get(id);
        if (!cur && !original) continue;
        let next: LevelObject | null;
        if (!original) {
            next = null;
            removed.add(id);
        } else if (!cur) {
            next = cloneObject(original);
        } else {
            next = cloneObject(cur);
            const o = cloneObject(original);
            next.transform = o.transform;
            if (o.scale) next.scale = o.scale;
            else delete next.scale;
            if (o.links) next.links = o.links;
            else delete next.links;
            if (o.fields) next.fields = o.fields;
            else delete next.fields;
            if (JSON.stringify(next) === JSON.stringify(cur)) continue;
        }
        before.set(id, cur ? cloneObject(cur) : null);
        after.set(id, next);
        named.push(cur ?? original!);
    }
    // Links to a removed object would point at nothing.
    if (removed.size) {
        for (const o of doc.objects()) {
            if (removed.has(o.id)) continue;
            const base = after.get(o.id) ?? o;
            if (!base.links?.some((l) => removed.has(l))) continue;
            const next = cloneObject(base);
            next.links = next.links!.filter((l) => !removed.has(l));
            if (!before.has(o.id)) before.set(o.id, cloneObject(o));
            after.set(o.id, next);
        }
    }
    if (!after.size) return null;
    return new SnapshotCommand(before, after, label ?? `Revert ${describeObjects(named)} to original`);
}

/** Several commands run as one undo step. */
export class CompositeCommand implements Command {
    constructor(
        private readonly cmds: Command[],
        readonly label: string,
    ) {}
    apply(doc: LevelDocument): void {
        for (const c of this.cmds) c.apply(doc);
    }
    revert(doc: LevelDocument): void {
        for (const c of [...this.cmds].reverse()) c.revert(doc);
    }
}

/** Undo/redo stack. `onChange` fires after every run/undo/redo with the command involved. */
export class CommandStack {
    private undoList: Command[] = [];
    private redoList: Command[] = [];
    onChange: (cmd: Command) => void = () => {};
    constructor(readonly doc: LevelDocument) {}

    get canUndo(): boolean {
        return this.undoList.length > 0;
    }
    get canRedo(): boolean {
        return this.redoList.length > 0;
    }
    get undoLabel(): string | undefined {
        return this.undoList.at(-1)?.label;
    }
    get redoLabel(): string | undefined {
        return this.redoList.at(-1)?.label;
    }
    run(cmd: Command): void {
        cmd.apply(this.doc);
        this.undoList.push(cmd);
        this.redoList = [];
        this.onChange(cmd);
    }
    undo(): boolean {
        const c = this.undoList.pop();
        if (!c) return false;
        c.revert(this.doc);
        this.redoList.push(c);
        this.onChange(c);
        return true;
    }
    redo(): boolean {
        const c = this.redoList.pop();
        if (!c) return false;
        c.apply(this.doc);
        this.undoList.push(c);
        this.onChange(c);
        return true;
    }
    clear(): void {
        this.undoList = [];
        this.redoList = [];
    }
    /** Labels oldest first; `done` of them are applied, the rest are redoable (newest redo last). */
    history(): { labels: string[]; done: number } {
        const labels = [...this.undoList.map((c) => c.label), ...[...this.redoList].reverse().map((c) => c.label)];
        return { labels, done: this.undoList.length };
    }
    /** Undo or redo until exactly `done` commands are applied. */
    goTo(done: number): void {
        while (this.undoList.length > done && this.undo());
        while (this.undoList.length < done && this.redo());
    }
    /** Number of applied commands. */
    get depth(): number {
        return this.undoList.length;
    }
    /** Identity of the newest applied command: remember it at save time; the document is dirty when it differs. */
    get top(): Command | undefined {
        return this.undoList.at(-1);
    }

    /** Duplicate objects, offset in x/z by `offset` units. The returned command's `ids` are the new objects. */
    duplicate(ids: number[], offset = 500): AddCommand {
        const copies = ids
            .map((id) => this.doc.get(id))
            .filter((o): o is LevelObject => !!o)
            .map((o) => {
                const c = cloneObject(o);
                c.id = 0;
                c.transform.pos[0] += offset;
                c.transform.pos[2] += offset;
                return c;
            });
        const sources = ids.map((id) => this.doc.get(id)).filter((o): o is LevelObject => !!o);
        const cmd = new AddCommand(copies, `Duplicate ${describeObjects(sources)}`);
        this.run(cmd);
        return cmd;
    }
}
