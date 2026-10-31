// Shared state of the dialogue and cinematics editors: the story data of one level folder at a time (`loadLevelStory`),
// the edits on top (subtitle texts, dialog line sound refs, sequences) and their export as mod pack entries. Edits are kept
// per level folder, so switching the folder does not lose them. One session per opened disc.

import {
    ModError,
    buildStoryPackFiles,
    checkEntrySize,
    dialogLineChanges,
    zipModFiles,
    type ModMeta,
    type StoryEntryChange,
} from '@drakan/editor-core';
import {
    HANDLER_PHASES,
    ODB_CLASS_TYPE,
    SDB_DESCRIPTOR,
    SSD_SEQUENCE,
    addActor,
    dialogLineSoundEdit,
    freeClassId,
    freeSequenceId,
    handlerPhaseRecord,
    loadLevelStory,
    newSequence,
    parseDialogLine,
    parseSdbText,
    parseSequence,
    patchClassData,
    writeSdbText,
    writeSequence,
    type DialogLine,
    type HandlerPhaseKey,
    type LevelStory,
    type SsdSequence,
    type StoryFsList,
    type StorySequence,
} from '@drakan/formats';
import { downloadFile } from '../platform.ts';

export interface ContentHost {
    fs(): StoryFsList | null;
    language(): string;
    /** Disc profile target for the manifest; null until a disc is open. */
    target(): ModMeta['target'] | null;
    status(msg: string, bad?: boolean): void;
    /** Play a sound of an SDB stem (the RefService's `play`); absent = no audio. */
    play?(stem: string, id: number): Promise<boolean>;
    stop?(): void;
    /** Handlers and cut scene players placed in the open level (class defaults are used when absent). */
    placed?(): PlacedStoryObject[];
    /** Show the camera path of a sequence in the level's 3D view (game units). */
    showPath?(points: [number, number, number][] | null): void;
    /** Point the 3D view at the path last shown. */
    framePath?(): void;
}

export interface PlacedStoryObject {
    id: number;
    name: string;
    /** `Sequence List`, `Beginning Dialog Exchange`, ... by field name. */
    fields: Map<string, number | number[]>;
}

/** A dialog line class this session adds: a copy of `templateId`'s record, named `name`, playing `soundRef` first. */
export interface NewLineEdit {
    classId: number;
    templateId: number;
    name: string;
    soundRef: number;
}

interface StemEdits {
    texts: Map<number, string>;
    lineSounds: Map<number, Map<number, number>>;
    seqs: Map<number, SsdSequence>;
    /** Sequences this session adds (always exported, as new entries). */
    newSeqs: Map<number, SsdSequence>;
    newLines: Map<number, NewLineEdit>;
    /** Handler class -> phase -> replacement line class refs. */
    phases: Map<number, Map<HandlerPhaseKey, number[]>>;
}

export class ContentSession {
    stems: string[] = [];
    stem: string | null = null;
    story: LevelStory | null = null;
    private stories = new Map<string, Promise<LevelStory>>();
    private edits = new Map<string, StemEdits>();
    private listeners = new Set<() => void>();

    constructor(readonly host: ContentHost) {}

    subscribe(fn: () => void): () => void {
        this.listeners.add(fn);
        return () => this.listeners.delete(fn);
    }
    emit(): void {
        this.listeners.forEach((l) => l());
    }

    /** Forget everything (another disc). */
    reset(): void {
        this.stems = [];
        this.stem = null;
        this.story = null;
        this.stories.clear();
        this.edits.clear();
        this.emit();
    }

    /** List the level folders and open the first (or `prefer`). */
    async ensure(prefer?: string): Promise<void> {
        const fs = this.host.fs();
        if (!fs) throw new Error('Open a disc first.');
        if (!this.stems.length) this.stems = await fs.stems();
        const want = prefer && this.stems.includes(prefer.toUpperCase()) ? prefer.toUpperCase() : null;
        if (!this.story || want) await this.use(want ?? this.stem ?? this.stems[0]!);
    }

    async use(stem: string): Promise<void> {
        const fs = this.host.fs();
        if (!fs) return;
        let p = this.stories.get(stem);
        if (!p) {
            p = loadLevelStory(fs, this.host.language(), stem);
            this.stories.set(stem, p);
            p.catch(() => this.stories.delete(stem));
        }
        this.story = await p;
        this.stem = stem;
        this.emit();
    }

    private ed(stem = this.stem!): StemEdits {
        let e = this.edits.get(stem);
        if (!e)
            this.edits.set(
                stem,
                (e = {
                    texts: new Map(),
                    lineSounds: new Map(),
                    seqs: new Map(),
                    newSeqs: new Map(),
                    newLines: new Map(),
                    phases: new Map(),
                }),
            );
        return e;
    }

    // ---- subtitle texts -------------------------------------------------------------------------------------------

    text(id: number): string {
        const e = this.stem ? this.edits.get(this.stem)?.texts.get(id) : undefined;
        return e ?? this.story?.sounds.get(id)?.text ?? '';
    }
    textEdited(id: number): boolean {
        return !!this.stem && this.edits.get(this.stem)?.texts.has(id) === true;
    }
    setText(id: number, text: string): void {
        const s = this.story?.sounds.get(id);
        if (!s || !this.stem) return;
        if (text === s.text) this.ed().texts.delete(id);
        else this.ed().texts.set(id, text);
        this.emit();
    }

    // ---- dialog line sounds ---------------------------------------------------------------------------------------

    /** The level's dialog lines plus the ones this session adds. */
    lines(): DialogLine[] {
        const base = this.story?.lines ?? [];
        const added = this.stem ? [...(this.edits.get(this.stem)?.newLines.values() ?? [])] : [];
        if (!added.length) return base;
        const out = [...base];
        for (const n of added) {
            const t = base.find((l) => l.classId === n.templateId);
            if (!t) continue;
            const dash = n.name.indexOf(' - ');
            out.push({
                ...t,
                classId: n.classId,
                name: n.name,
                speaker: dash > 0 ? n.name.slice(0, dash) : n.name,
                sounds: t.sounds.map((r, i) => (i === 0 ? n.soundRef : r)),
            });
        }
        return out;
    }
    lineIsNew(classId: number): boolean {
        return !!this.stem && this.edits.get(this.stem)?.newLines.has(classId) === true;
    }
    /** Add a dialog line class copying `templateId`; returns its class id. */
    addLine(templateId: number, name: string, soundRef: number): number {
        const story = this.story;
        if (!story || !this.stem) throw new Error('Open a level folder first.');
        if (!story.classRecord.has(templateId) || !story.lines.some((l) => l.classId === templateId))
            throw new Error('Pick an existing dialog line to copy.');
        if (!story.fieldLists) throw new Error('This level folder has no ODB field lists to extend.');
        const clean = name.trim();
        if (!clean) throw new Error('Give the line a name (for example Farmer - MYLINE01).');
        const e = this.ed();
        const used = new Set<number>([...story.classes.keys(), ...e.newLines.keys()]);
        const classId = freeClassId(used);
        if (classId === null) throw new Error('No free class id left in this ODB.');
        e.newLines.set(classId, { classId, templateId, name: clean, soundRef: soundRef >>> 0 });
        this.emit();
        return classId;
    }
    removeNewLine(classId: number): void {
        const e = this.stem ? this.edits.get(this.stem) : undefined;
        if (!e?.newLines.delete(classId)) return;
        // no handler may keep pointing at a class that will not exist
        for (const m of e.phases.values())
            for (const [k, v] of m)
                m.set(
                    k,
                    v.filter((r) => (r & 0xffff) !== classId),
                );
        this.emit();
    }

    // ---- handler phases -------------------------------------------------------------------------------------------

    /** Line refs of a handler phase as edited (or as read). */
    phaseLines(handlerId: number, phase: HandlerPhaseKey): number[] {
        const o = this.stem ? this.edits.get(this.stem)?.phases.get(handlerId)?.get(phase) : undefined;
        if (o) return o;
        const h = this.story?.handlers.find((x) => x.classId === handlerId);
        const i = HANDLER_PHASES.findIndex((p) => p.key === phase);
        return h?.phases[i]?.lines ?? [];
    }
    phaseEdited(handlerId: number, phase?: HandlerPhaseKey): boolean {
        const m = this.stem ? this.edits.get(this.stem)?.phases.get(handlerId) : undefined;
        return !!m && (phase ? m.has(phase) : m.size > 0);
    }
    setPhaseLines(handlerId: number, phase: HandlerPhaseKey, refs: number[]): void {
        const h = this.story?.handlers.find((x) => x.classId === handlerId);
        if (!h || !this.stem) return;
        const i = HANDLER_PHASES.findIndex((p) => p.key === phase);
        const orig = h.phases[i]?.lines ?? [];
        const e = this.ed();
        const m = e.phases.get(handlerId) ?? new Map<HandlerPhaseKey, number[]>();
        if (refs.length === orig.length && refs.every((r, k) => r === orig[k])) m.delete(phase);
        else
            m.set(
                phase,
                refs.map((r) => r >>> 0),
            );
        if (m.size) e.phases.set(handlerId, m);
        else e.phases.delete(handlerId);
        this.emit();
    }
    revertPhases(handlerId: number): void {
        if (this.stem) this.edits.get(this.stem)?.phases.delete(handlerId);
        this.emit();
    }

    lineSound(classId: number, i: number): number {
        const nl = this.stem ? this.edits.get(this.stem)?.newLines.get(classId) : undefined;
        if (nl && i === 0) return nl.soundRef;
        const o = this.stem ? this.edits.get(this.stem)?.lineSounds.get(classId)?.get(i) : undefined;
        if (o !== undefined) return o;
        return this.story?.lines.find((l) => l.classId === classId)?.sounds[i] ?? 0;
    }
    setLineSound(classId: number, i: number, ref: number): void {
        const nl = this.stem ? this.edits.get(this.stem)?.newLines.get(classId) : undefined;
        if (nl) {
            if (i === 0) nl.soundRef = ref >>> 0;
            this.emit();
            return;
        }
        const line = this.story?.lines.find((l) => l.classId === classId);
        if (!line || !this.stem) return;
        const m = this.ed().lineSounds.get(classId) ?? new Map<number, number>();
        if (ref === line.sounds[i]) m.delete(i);
        else m.set(i, ref >>> 0);
        if (m.size) this.ed().lineSounds.set(classId, m);
        else this.ed().lineSounds.delete(classId);
        this.emit();
    }

    // ---- sequences ------------------------------------------------------------------------------------------------

    /** Stored sequences plus the ones this session adds (`raw` of a new one is its first encoding). */
    sequences(): StorySequence[] {
        const base = this.story?.sequences ?? [];
        const added = this.stem ? [...(this.edits.get(this.stem)?.newSeqs.values() ?? [])] : [];
        if (!added.length) return base;
        return [...base, ...added.map((s) => ({ id: s.id, folder: s.folder, raw: writeSequence(s), seq: s }))];
    }
    sequenceIsNew(id: number): boolean {
        return !!this.stem && this.edits.get(this.stem)?.newSeqs.has(id) === true;
    }
    /** Add an empty sequence in `folder`; returns its id. */
    addSequence(name: string, folder: number): number {
        const story = this.story;
        if (!story || !this.stem) throw new Error('Open a level folder first.');
        const clean = name.trim();
        if (!clean) throw new Error('Give the sequence a name.');
        const e = this.ed();
        const used = new Set<number>([...story.sequences.map((s) => s.id), ...e.newSeqs.keys()]);
        const id = freeSequenceId(used);
        e.newSeqs.set(id, newSequence(id, folder, clean));
        this.emit();
        return id;
    }
    /** Add an action-less actor (name, object ref) to a sequence. */
    addSequenceActor(id: number, name: string, ref: number): void {
        this.editSequence(id, (s) => void addActor(s, name.trim() || 'Actor', ref));
    }

    /** The sequence as edited (or as read). */
    sequence(id: number): SsdSequence | null {
        return (
            (this.stem && this.edits.get(this.stem)?.newSeqs.get(id)) ||
            (this.stem && this.edits.get(this.stem)?.seqs.get(id)) ||
            this.story?.sequences.find((s) => s.id === id)?.seq ||
            null
        );
    }
    sequenceEdited(id: number): boolean {
        return !!this.stem && (this.edits.get(this.stem)?.seqs.has(id) === true || this.sequenceIsNew(id));
    }
    /** Change a sequence on a private copy; the edit is dropped again when the bytes equal the original. */
    editSequence(id: number, fn: (s: SsdSequence) => void): void {
        if (!this.stem) return;
        const fresh = this.edits.get(this.stem)?.newSeqs.get(id);
        if (fresh) {
            const copy = parseSequence(id, fresh.folder, writeSequence(fresh))!;
            fn(copy);
            this.ed().newSeqs.set(id, parseSequence(id, fresh.folder, writeSequence(copy))!);
            this.emit();
            return;
        }
        const rec = this.story?.sequences.find((s) => s.id === id);
        if (!rec?.seq) return;
        const cur = this.sequence(id)!;
        const copy = parseSequence(id, rec.folder, writeSequence(cur))!;
        fn(copy);
        const bytes = writeSequence(copy);
        const same = bytes.length === rec.raw.length && bytes.every((b, i) => b === rec.raw[i]);
        if (same) this.ed().seqs.delete(id);
        else this.ed().seqs.set(id, parseSequence(id, rec.folder, bytes)!);
        this.emit();
    }
    revertSequence(id: number): void {
        if (this.stem) this.edits.get(this.stem)?.seqs.delete(id);
        if (this.stem) this.edits.get(this.stem)?.newSeqs.delete(id);
        this.emit();
    }

    // ---- export ---------------------------------------------------------------------------------------------------

    count(): number {
        let n = 0;
        for (const e of this.edits.values())
            n += e.texts.size + e.lineSounds.size + e.seqs.size + e.newSeqs.size + e.newLines.size + e.phases.size;
        return n;
    }

    /** Entries of every edit, with the runtime's size rule applied (`ModError` on an entry that would be rejected). */
    async changes(): Promise<StoryEntryChange[]> {
        const out: StoryEntryChange[] = [];
        for (const [stem, e] of this.edits) {
            const story = await this.stories.get(stem);
            if (!story) continue;
            for (const [id, text] of [...e.texts].sort((a, b) => a[0] - b[0])) {
                const s = story.sounds.get(id);
                const entry = story.soundEntry.get(id);
                if (!s || !entry) continue;
                const data = writeSdbText(s.descriptor, text);
                if (!data) throw new ModError(`${stem}.SDB sound ${id}: the descriptor layout is not known`);
                if (parseSdbText(data)?.text !== text)
                    throw new ModError(`${stem}.SDB sound ${id}: the text did not survive encoding`);
                out.push({ file: story.files.sdb, type: SDB_DESCRIPTOR, id, sub: entry.sub, data, isNew: false });
            }
            for (const [classId, edits] of [...e.lineSounds].sort((a, b) => a[0] - b[0])) {
                const rec = story.classRecord.get(classId);
                const cls = story.classes.get(classId);
                const line = cls && parseDialogLine(cls);
                if (!rec || !line) continue;
                const merged = new Map<number, number>();
                for (const [i, ref] of edits) for (const [k, v] of dialogLineSoundEdit(line, i, ref)) merged.set(k, v);
                out.push({
                    file: story.files.odb,
                    type: ODB_CLASS_TYPE,
                    id: classId,
                    sub: rec.sub,
                    data: patchClassData(rec.raw, merged),
                    isNew: false,
                });
            }
            const added = [...e.newLines.values()].sort((a, b) => a.classId - b.classId);
            if (added.length) {
                const templates = added.map((n) => {
                    const cls = story.classes.get(n.templateId);
                    const rec = story.classRecord.get(n.templateId);
                    const line = story.lines.find((l) => l.classId === n.templateId);
                    if (!cls?.fields || !rec || !line)
                        throw new ModError(`${stem}.ODB: line class ${n.templateId} is gone`);
                    return {
                        template: { line, group: rec.sub, record: rec.raw, defs: cls.fields },
                        name: n.name,
                        soundRef: n.soundRef,
                        classId: n.classId,
                    };
                });
                const r = await dialogLineChanges(
                    story.files.odb,
                    new Set(story.classes.keys()),
                    templates,
                    story.fieldLists ?? undefined,
                );
                out.push(...r.changes);
            }
            for (const [hid, phases] of [...e.phases].sort((a, b) => a[0] - b[0])) {
                const rec = story.classRecord.get(hid);
                const defs = story.classes.get(hid)?.fields;
                if (!rec || !defs) continue;
                let data = rec.raw;
                for (const [phase, refs] of phases) data = handlerPhaseRecord(data, defs, phase, refs);
                out.push({ file: story.files.odb, type: ODB_CLASS_TYPE, id: hid, sub: rec.sub, data, isNew: false });
            }
            for (const [id, seq] of [...e.newSeqs].sort((a, b) => a[0] - b[0])) {
                const data = writeSequence(seq);
                out.push({ file: story.files.ssd, type: SSD_SEQUENCE, id, sub: seq.folder, data, isNew: true });
            }
            for (const [id, seq] of [...e.seqs].sort((a, b) => a[0] - b[0])) {
                const entry = story.sequenceEntry.get(id);
                if (!entry) continue;
                const data = writeSequence(seq);
                const bad = checkEntrySize(story.files.ssd, entry.size, data.length);
                if (bad) throw new ModError(bad);
                out.push({ file: story.files.ssd, type: SSD_SEQUENCE, id, sub: entry.sub, data, isNew: false });
            }
        }
        return out;
    }

    async exportZip(meta: ModMeta): Promise<StoryEntryChange[]> {
        const changes = await this.changes();
        if (!changes.length) throw new ModError('No story edits to export.');
        const files = buildStoryPackFiles(meta, changes);
        await downloadFile(`${meta.id}.zip`, zipModFiles(files) as BlobPart, 'application/zip');
        return changes;
    }
}
