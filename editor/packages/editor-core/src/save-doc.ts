import {
    RYNN_CLASS,
    SaveFieldError,
    applyFieldEdits,
    assembleSave,
    checkFieldValue,
    describeRecords,
    formatFieldValue,
    levelNameOf,
    listSkills,
    parseSave,
    probeField,
    resolveField,
    type FieldEdit,
    type FieldProbe,
    type FieldValue,
    type RecordInfo,
    type SaveFile,
    type SkillRow,
} from '@drakan/formats';
import type { Command } from './commands.ts';
import { LevelDocument, type LevelObject } from './document.ts';

export interface PendingEdit {
    name: string;
    label: string;
    before: FieldValue;
    after: FieldValue;
    confidence: 'high' | 'medium' | 'low';
    /** The field needed an explicit unlock (savetool `--force`). */
    unconfirmed: boolean;
}

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
            const isPlayer = r.classId === RYNN_CLASS;
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
    }

    static open(bytes: Uint8Array): SaveDocument {
        return new SaveDocument(bytes, parseSave(bytes));
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
        const names = [...this.fieldValues.keys()];
        if (this.playerObject !== null) names.push('pos');
        for (const name of names) {
            const before = this.originalValue(name);
            const after = this.value(name);
            if (before === undefined || after === undefined || feq(before, after)) continue;
            const { def } = resolveField(name);
            out.push({
                name,
                label: name.startsWith('skill.') ? name.replace('skill.', 'skill ') : def.label,
                before,
                after,
                confidence: def.confidence,
                unconfirmed: this.probe(name).status === 'unconfirmed',
            });
        }
        return out;
    }

    describe(name: string, v: FieldValue): string {
        return formatFieldValue(resolveField(name).def, v);
    }

    /** Apply the pending edits to the loaded file. Throws a `SaveFieldError` when the field table rejects one. */
    build(): { bytes: Uint8Array; save: SaveFile } {
        const edits: FieldEdit[] = this.pendingEdits().map((e) => ({
            name: e.name,
            value: e.after,
            force: e.unconfirmed,
        }));
        if (edits.length === 0) return { bytes: this.bytes.slice(), save: this.save };
        const save = applyFieldEdits(this.save, edits);
        return { bytes: assembleSave(save).bytes, save };
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
