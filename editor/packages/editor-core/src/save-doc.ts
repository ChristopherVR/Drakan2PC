import {
    isRynnClass,
    SaveFieldError,
    applyFieldEdits,
    assembleSave,
    checkFieldValue,
    describeRecords,
    listInventory,
    formatFieldValue,
    levelNameOf,
    listProgress,
    listSkills,
    parseSave,
    probeField,
    readItemPanel,
    resolveField,
    validateSave,
    type FieldEdit,
    type FieldProbe,
    type FieldValue,
    type InventoryCell,
    type Issue,
    type ItemPanel,
    type ProgressRow,
    type QuestRow,
    type RecordInfo,
    type SaveFile,
    type SkillRow,
    listQuests,
} from '@drakan/formats';
import type { Command } from './commands.ts';
import { LevelDocument, type LevelObject } from './document.ts';
import {
    addItem,
    itemCells,
    itemRows,
    itemResource,
    itemTemplates,
    placeItem,
    replaceItem,
    usedCells,
    type ItemTemplate,
} from './save-items.ts';

export interface PendingEdit {
    name: string;
    label: string;
    before: FieldValue;
    after: FieldValue;
    confidence: 'high' | 'medium' | 'low';
    /** The field needed an explicit unlock (savetool `--force`). */
    unconfirmed: boolean;
    /** Display text for edits that are not a field value (item replacement). */
    text?: { before: string; after: string };
}

function readQuests(save: SaveFile): QuestRow[] {
    try {
        return listQuests(save);
    } catch {
        return [];
    }
}

function pendingLabel(name: string, fallback: string): string {
    const inv = /^inv\.(\d+)\.(\w+)$/.exec(name);
    if (inv) return `cell ${inv[1]} ${inv[2] === 'hot' ? 'quick slot' : inv[2] === 'max' ? 'max count' : inv[2]}`;
    if (name.startsWith('skill.')) return name.replace('skill.', 'skill ');
    if (name.startsWith('progress.')) return name.replace('progress.', 'state key ');
    if (name.startsWith('quest.')) return name.replace('quest.', 'quest row ');
    return fallback;
}

export type { QuestRow };

const feq = (a: FieldValue | undefined, b: FieldValue | undefined): boolean => {
    if (a === undefined || b === undefined) return a === b;
    if (typeof a === 'number' || typeof b === 'number') return a === b;
    return a.every((v, i) => Math.fround(v) === Math.fround(b[i]!));
};

/**
 * An opened `.rsg` as an editable document. It *is* a `LevelDocument` (positioned object records become markers,
 * the player's position is the player marker's transform), so the Level scene and the shared `CommandStack` work on it
 * unchanged. Other edits (hp, gold, skills) live in `fieldValues`, changed through `SetFieldCommand`. Nothing touches
 * the loaded bytes until `build()`.
 */
export class SaveDocument extends LevelDocument {
    /** Pending field values by field name (hp, gold, skill.N, skill.N.max). */
    readonly fieldValues = new Map<string, FieldValue>();
    /** Fields the user explicitly unlocked although the field table could not confirm the layout. */
    readonly unlocked = new Set<string>();
    readonly records: RecordInfo[];
    /** Level object id by save entry index, and back. */
    readonly objectOfEntry = new Map<number, number>();
    readonly entryOfObject = new Map<number, number>();
    readonly playerObject: number | null;
    readonly skills: SkillRow[] | null;
    /** The state table rows as opened (kind, key, value); empty when the layout is not recognised. */
    readonly progress: ProgressRow[];
    /** The item text kept in Rynn's record (what the Equipment page last showed), or null. */
    readonly itemPanel: ItemPanel | null;
    /** Occupied inventory cells as opened (empty when the cell table is not recognised). */
    readonly inventory: InventoryCell[];
    /** A recognised empty pack is editable too; absence of items is not a missing table. */
    readonly hasInventory: boolean;
    /** Whether the raw stats block (the authoritative copy of HP, gold, XP...) was found. */
    readonly hasStats: boolean;
    /** Quest / map marker rows as opened: row N's raw state word and the row's other words. */
    readonly quests: QuestRow[];
    /** Pending item replacements: inventory cell -> entry id of the template record whose body it gets. */
    readonly itemReplacements = new Map<number, number>();
    /** Height in cells of the class each pending replacement takes (only for cells whose height is known). */
    readonly itemReplacementRows = new Map<number, number>();
    /** Pending new items, in the order they were added: template record entry id and height in cells of each. */
    readonly itemAdds: ItemAdd[] = [];
    private templatesCache: ItemTemplate[] | null = null;
    private issuesCache: Issue[] | null = null;
    private readonly probes = new Map<string, FieldProbe>();

    private constructor(
        readonly bytes: Uint8Array,
        readonly save: SaveFile,
    ) {
        super();
        this.level = levelNameOf(save);
        this.records = describeRecords(save);
        let player: number | null = null;
        for (const r of this.records) {
            if (!r.pos) continue;
            const isPlayer = isRynnClass(r.classId);
            const o = this.insert({
                id: r.index + 1,
                class: (r.classId ?? 0).toString(16).padStart(8, '0'),
                type: isPlayer ? 3 : -1,
                kind: isPlayer ? 'player' : 'other',
                resource: `0x${r.type.toString(16)} id ${r.id}`,
                model: '',
                transform: { pos: [...r.pos], rot: [0, 0, 0] },
                props: { entry: r.index, entryId: r.id },
            } satisfies LevelObject);
            this.objectOfEntry.set(r.index, o.id);
            this.entryOfObject.set(o.id, r.index);
            if (isPlayer && player === null) player = o.id;
        }
        this.playerObject = player;
        let skills: SkillRow[] | null = null;
        try {
            skills = listSkills(save);
        } catch {
            skills = null;
        }
        this.skills = skills;
        this.progress = listProgress(save);
        this.itemPanel = readItemPanel(save);
        let inv: InventoryCell[] = [];
        try {
            inv = listInventory(save);
            this.hasInventory = true;
        } catch {
            inv = [];
            this.hasInventory = false;
        }
        this.inventory = inv;
        this.hasStats = this.probe('stat.hp').status !== 'missing';
        this.quests = readQuests(save);
    }

    static open(bytes: Uint8Array): SaveDocument {
        return new SaveDocument(bytes, parseSave(bytes));
    }

    /** One template record per item class that the save contains (what "Replace with..." can offer). */
    itemTemplates(): ItemTemplate[] {
        return (this.templatesCache ??= itemTemplates(this.save));
    }

    /** New items need a recognised carried record to supply the game's ownership state. */
    get canAddItems(): boolean {
        return this.inventory.some((cell) => cell.marker === undefined && cell.entry !== undefined);
    }

    /**
     * First cell of each pending added item, in order (the first place where all of its cells are free, after the
     * items added before it); -1 for an item that does not fit. The same placement `build()` writes.
     */
    addedCells(): number[] {
        return this.addedPlaces().map((p) => p.cell);
    }

    /** Every cell each pending added item takes (empty for one that does not fit). */
    addedPlaces(): { cell: number; cells: number[] }[] {
        if (this.itemAdds.length === 0) return [];
        const used = this.usedNow();
        if (!used) return this.itemAdds.map(() => ({ cell: -1, cells: [] }));
        return this.itemAdds.map((a) => {
            const cell = placeItem(used, a.rows);
            const cells = cell === null ? [] : itemCells(cell, a.rows)!;
            for (const c of cells) used.add(c);
            return { cell: cell ?? -1, cells };
        });
    }

    /** First cell a new item `rows` cells tall would take after the pending adds, or null when there is no room. */
    placeFor(rows: number): number | null {
        const used = this.usedNow();
        if (!used) return null;
        for (const p of this.addedPlaces()) for (const c of p.cells) used.add(c);
        return placeItem(used, rows);
    }

    /** Cells taken by the items in the save (with the heights of pending replacements), or null when not recognised. */
    private usedNow(): Set<number> | null {
        try {
            const used = usedCells(this.save);
            for (const [cell, rows] of this.itemReplacementRows) {
                for (const c of this.ownCells(cell)) used.delete(c);
                for (const c of itemCells(cell, rows) ?? [cell]) used.add(c);
            }
            return used;
        } catch {
            return null;
        }
    }

    /** Cells the item whose first cell is `cell` takes in the save as opened. */
    private ownCells(cell: number): number[] {
        let rows = 1;
        try {
            rows = itemRows(this.save, cell);
        } catch {
            rows = 1;
        }
        return itemCells(cell, Math.max(1, Math.min(rows, 3))) ?? [cell];
    }

    /**
     * Whether the item in `cell` can become `rows` cells tall: every cell it then needs is inside the grid and free,
     * apart from the cells it takes now (pending added items count as taken).
     */
    replaceFits(cell: number, rows: number): boolean {
        const cells = itemCells(cell, rows);
        if (!cells) return false;
        const used = this.usedNow();
        if (!used) return false;
        for (const p of this.addedPlaces()) for (const c of p.cells) used.add(c);
        const pending = this.itemReplacementRows.get(cell);
        const own = new Set(pending === undefined ? this.ownCells(cell) : (itemCells(cell, pending) ?? [cell]));
        return cells.every((c) => own.has(c) || !used.has(c));
    }

    /** The item class of an inventory cell now: the pending replacement, else the stored one. */
    itemResourceOf(cell: number): number | undefined {
        const t = this.itemReplacements.get(cell);
        if (t !== undefined) {
            const rec = this.save.entries.find((e) => e.type === 0x102 && e.id === t);
            return rec ? (itemResource(rec) ?? undefined) : undefined;
        }
        return this.inventory.find((c) => c.cell === cell && c.marker === undefined)?.resource;
    }

    /** Validation report for the file as opened (cached). */
    issues(): Issue[] {
        return (this.issuesCache ??= validateSave(this.save));
    }

    /** Current value of a progress row: a pending edit or the stored value. */
    progressValue(key: number): number | undefined {
        const v = this.value(`progress.${key}`);
        return typeof v === 'number' ? v : undefined;
    }

    /** The field as stored in the opened file (cached). */
    probe(name: string): FieldProbe {
        let p = this.probes.get(name);
        if (!p) this.probes.set(name, (p = probeField(this.save, name)));
        return p;
    }

    originalValue(name: string): FieldValue | undefined {
        const p = this.probe(name);
        return p.status === 'missing' ? undefined : p.value;
    }

    /** Whether editing is allowed now: confirmed fields always, unconfirmed ones after an explicit unlock. */
    editable(name: string): boolean {
        const p = this.probe(name);
        return p.status === 'ok' || (p.status === 'unconfirmed' && this.unlocked.has(name));
    }

    /** The current value: a pending edit, the player marker's position, or the stored value. */
    value(name: string): FieldValue | undefined {
        if (name === 'pos' && this.playerObject !== null && this.probe('pos').status !== 'missing') {
            const p = this.get(this.playerObject)!.transform.pos;
            return [p[0], p[1], p[2]];
        }
        return this.fieldValues.get(name) ?? this.originalValue(name);
    }

    /** Null when `value` may be written to `name` now, else the reason it may not. */
    checkEdit(name: string, value: FieldValue): string | null {
        try {
            const { def, rowId } = resolveField(name);
            if (!this.editable(name)) {
                return this.probe(name).status === 'missing'
                    ? 'field not present in this save'
                    : 'unlock this field first';
            }
            checkFieldValue(name, value);
            if (def.bound && rowId !== null && typeof value === 'number') {
                const sibling = def.bound.sibling === 'skill.max' ? `skill.${rowId}.max` : `skill.${rowId}`;
                const other = Number(this.value(sibling));
                if (def.bound.op === '<=' && value > other) return `level must not exceed max (${other})`;
                if (def.bound.op === '>=' && value < other) return `max must not be below the level (${other})`;
            }
            return null;
        } catch (e) {
            if (e instanceof SaveFieldError) return e.message;
            throw e;
        }
    }

    pendingEdits(): PendingEdit[] {
        const out: PendingEdit[] = [];
        for (const [cell, tpl] of this.itemReplacements) {
            const before = this.inventory.find((c) => c.cell === cell)?.resource ?? 0;
            const after = this.itemResourceOf(cell) ?? 0;
            out.push({
                name: `item.${cell}`,
                label: `cell ${cell} item`,
                before,
                after,
                confidence: 'medium',
                unconfirmed: false,
                text: { before: `0x${before.toString(16)}`, after: `0x${after.toString(16)} (record ${tpl})` },
            });
        }
        const cells = this.addedCells();
        this.itemAdds.forEach(({ template: tpl }, i) => {
            const rec = this.save.entries.find((e) => e.type === 0x102 && e.id === tpl);
            const res = rec ? (itemResource(rec) ?? 0) : 0;
            out.push({
                name: `add.${i}`,
                label: `new item ${i + 1}${cells[i] !== undefined && cells[i] >= 0 ? ` (cell ${cells[i]})` : ' (no free cell)'}`,
                before: 0,
                after: res,
                confidence: 'medium',
                unconfirmed: false,
                text: { before: '(none)', after: `0x${res.toString(16)} (copy of record ${tpl})` },
            });
        });
        const names = [...this.fieldValues.keys()];
        if (this.playerObject !== null) names.push('pos');
        for (const name of names) {
            const before = this.originalValue(name);
            const after = this.value(name);
            if (before === undefined || after === undefined || feq(before, after)) continue;
            const { def } = resolveField(name);
            out.push({
                name,
                label: pendingLabel(name, def.label),
                before,
                after,
                confidence: def.confidence,
                unconfirmed: this.probe(name).status === 'unconfirmed',
            });
        }
        return out;
    }

    describe(name: string, v: FieldValue): string {
        if (name.startsWith('item.') || name.startsWith('add.')) return `0x${Number(v).toString(16)}`;
        return formatFieldValue(resolveField(name).def, v);
    }

    /** Apply the pending edits to the loaded file. Throws a `SaveFieldError` when the field table rejects one. */
    build(): { bytes: Uint8Array; save: SaveFile } {
        const edits: FieldEdit[] = this.pendingEdits()
            .filter((e) => !e.name.startsWith('item.') && !e.name.startsWith('add.'))
            .map((e) => ({
                name: e.name,
                value: e.after,
                force: e.unconfirmed,
            }));
        let base = this.save;
        for (const [cell, tpl] of this.itemReplacements) {
            base = replaceItem(base, cell, tpl, this.itemReplacementRows.get(cell));
        }
        for (const a of this.itemAdds) base = addItem(base, a.template, a.rows).save;
        if (edits.length === 0 && base === this.save) return { bytes: this.bytes.slice(), save: this.save };
        const save = edits.length ? applyFieldEdits(base, edits) : base;
        return { bytes: assembleSave(save).bytes, save };
    }
}

/** A pending new item: the template record's entry id and the item's height in cells (1..3). */
export interface ItemAdd {
    template: number;
    rows: number;
}

/**
 * Add a new carried item of a class this save has a record for (see `addItem`), `rows` cells tall. Undo removes it
 * again. Throws when the item has no place where all of its cells are free.
 */
export class AddItemCommand implements Command {
    readonly label: string;
    constructor(
        private readonly template: number,
        private readonly rows = 1,
    ) {
        this.label = `Add item (record ${template})`;
    }
    apply(doc: LevelDocument): void {
        if (!(doc instanceof SaveDocument)) throw new Error('AddItemCommand needs a SaveDocument');
        if (!doc.canAddItems)
            throw new SaveFieldError(
                'Pick up an item in the game and save first. Adding items needs an existing carried item.',
            );
        if (doc.placeFor(this.rows) === null) {
            throw new SaveFieldError(
                this.rows > 1
                    ? `There is no free column with ${this.rows} empty cells for this item.`
                    : 'The inventory is full.',
            );
        }
        doc.itemAdds.push({ template: this.template, rows: this.rows });
    }
    revert(doc: LevelDocument): void {
        if (!(doc instanceof SaveDocument)) throw new Error('AddItemCommand needs a SaveDocument');
        doc.itemAdds.pop();
    }
}

/** Drop pending added item number `index` (the tray's revert). */
export class RemoveAddedItemCommand implements Command {
    readonly label: string;
    private old: ItemAdd | undefined;
    constructor(private readonly index: number) {
        this.label = `Remove added item ${index + 1}`;
    }
    apply(doc: LevelDocument): void {
        if (!(doc instanceof SaveDocument)) throw new Error('RemoveAddedItemCommand needs a SaveDocument');
        this.old = doc.itemAdds.splice(this.index, 1)[0];
    }
    revert(doc: LevelDocument): void {
        if (!(doc instanceof SaveDocument)) throw new Error('RemoveAddedItemCommand needs a SaveDocument');
        if (this.old !== undefined) doc.itemAdds.splice(this.index, 0, this.old);
    }
}

export class SetFieldCommand implements Command {
    readonly label: string;
    private old: FieldValue | undefined;
    constructor(
        private readonly name: string,
        private readonly value: FieldValue | undefined,
    ) {
        this.label = `Set ${name}`;
    }
    private put(doc: LevelDocument, v: FieldValue | undefined): void {
        if (!(doc instanceof SaveDocument)) throw new Error('SetFieldCommand needs a SaveDocument');
        if (v === undefined) doc.fieldValues.delete(this.name);
        else doc.fieldValues.set(this.name, v);
    }
    apply(doc: LevelDocument): void {
        if (doc instanceof SaveDocument) this.old = doc.fieldValues.get(this.name);
        this.put(doc, this.value);
    }
    revert(doc: LevelDocument): void {
        this.put(doc, this.old);
    }
}

/**
 * Replace the item in an inventory cell by another class (undefined clears the replacement). `rows` is the new
 * class's height in cells when known: the item then takes that many cells of its column (refused when they are
 * taken). Without it the item keeps its height.
 */
export class ReplaceItemCommand implements Command {
    readonly label: string;
    private old: number | undefined;
    private oldRows: number | undefined;
    constructor(
        private readonly cell: number,
        private readonly template: number | undefined,
        private readonly rows?: number,
    ) {
        this.label = `Replace item in cell ${cell}`;
    }
    private put(doc: LevelDocument, v: number | undefined, rows: number | undefined): void {
        if (!(doc instanceof SaveDocument)) throw new Error('ReplaceItemCommand needs a SaveDocument');
        if (v === undefined) doc.itemReplacements.delete(this.cell);
        else doc.itemReplacements.set(this.cell, v);
        if (v === undefined || rows === undefined) doc.itemReplacementRows.delete(this.cell);
        else doc.itemReplacementRows.set(this.cell, rows);
    }
    apply(doc: LevelDocument): void {
        if (doc instanceof SaveDocument) {
            if (this.template !== undefined && this.rows !== undefined && !doc.replaceFits(this.cell, this.rows)) {
                throw new SaveFieldError(
                    `The new item is ${this.rows} cells tall and the cells under cell ${this.cell} are taken.`,
                );
            }
            this.old = doc.itemReplacements.get(this.cell);
            this.oldRows = doc.itemReplacementRows.get(this.cell);
        }
        this.put(doc, this.template, this.rows);
    }
    revert(doc: LevelDocument): void {
        this.put(doc, this.old, this.oldRows);
    }
}
