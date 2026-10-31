import { cloneObject, type LevelDocument, type LevelObject, type Transform } from './document.ts';

export interface Command {
    readonly label: string;
    apply(doc: LevelDocument): void;
    revert(doc: LevelDocument): void;
}

const cloneT = (t: Transform): Transform => ({ pos: [...t.pos], rot: [...t.rot] });

export class TransformCommand implements Command {
    readonly label = 'Transform';
    constructor(
        private readonly before: Map<number, Transform>,
        private readonly after: Map<number, Transform>,
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

export class DeleteCommand implements Command {
    readonly label = 'Delete';
    private removed: LevelObject[] = [];
    constructor(private readonly ids: number[]) {}
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
        const cmd = new AddCommand(copies, 'Duplicate');
        this.run(cmd);
        return cmd;
    }
}
