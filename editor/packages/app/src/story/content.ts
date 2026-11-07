// Shared state of the dialogue and cinematics editors: the story data of one level folder at a time (`loadLevelStory`),
// the edits on top (subtitle texts, dialog line sound refs, sequences) and their export as mod pack entries. Edits are kept
// per level folder, so switching the folder does not lose them. One session per opened disc.

import {
    ModError,
    buildCutscene,
    buildStoryPackFiles,
    checkEntrySize,
    dialogLineChanges,
    planDialogueLine,
    type CutsceneSpec,
    type DialogueDraft,
    zipModFiles,
    type ModMeta,
    type ProjectContentEdits,
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
    refParts,
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

export function toBase64(b: Uint8Array): string {
    let s = '';
    for (let i = 0; i < b.length; i += 0x8000) s += String.fromCharCode(...b.subarray(i, i + 0x8000));
    return btoa(s);
}
export function fromBase64(t: string): Uint8Array {
    const s = atob(t);
    const b = new Uint8Array(s.length);
    for (let i = 0; i < s.length; i++) b[i] = s.charCodeAt(i);
    return b;
}

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
    /** Story entries a converted Order of the Flame pack adds, by level folder (absent = none known). */
    converted?(): Promise<Record<string, { sequences: number[]; classes: number[]; texts?: number[] }>>;
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
    private convertedIds: Record<string, { sequences: number[]; classes: number[]; texts?: number[] }> = {};

    constructor(readonly host: ContentHost) {}

    /** What the converted pack added to the open level folder (null when it added nothing here). */
    converted(): { sequences: Set<number>; classes: Set<number> } | null {
        const c = this.stem ? this.convertedIds[this.stem] : undefined;
        return c ? { sequences: new Set(c.sequences), classes: new Set(c.classes) } : null;
    }

    subscribe(fn: () => void): () => void {
        this.listeners.add(fn);
        return () => this.listeners.delete(fn);
    }
    emit(): void {
        this.listeners.forEach((l) => l());
    }

    /** Forget everything (another disc); `keepEdits` keeps the edits (restored from a project before the disc opened). */
    reset(keepEdits = false): void {
        this.stems = [];
        this.stem = null;
        this.story = null;
        this.stories.clear();
        if (!keepEdits) this.edits.clear();
        this.emit();
    }

    /** The edits as plain JSON for the project file (sequences as base64 of their bytes). */
    exportState(): ProjectContentEdits[] {
        const out: ProjectContentEdits[] = [];
        const seq = (s: SsdSequence): { id: number; folder: number; data: string } => ({
            id: s.id,
            folder: s.folder,
            data: toBase64(writeSequence(s)),
        });
        for (const [stem, e] of [...this.edits].sort((a, b) => a[0].localeCompare(b[0]))) {
            const c: ProjectContentEdits = {
                stem,
                texts: [...e.texts].sort((a, b) => a[0] - b[0]),
                lineSounds: [...e.lineSounds]
                    .sort((a, b) => a[0] - b[0])
                    .map(([k, m]) => [k, [...m].sort((a, b) => a[0] - b[0])] as [number, [number, number][]]),
                seqs: [...e.seqs.values()].sort((a, b) => a.id - b.id).map(seq),
                newSeqs: [...e.newSeqs.values()].sort((a, b) => a.id - b.id).map(seq),
                newLines: [...e.newLines.values()].sort((a, b) => a.classId - b.classId).map((n) => ({ ...n })),
                phases: [...e.phases]
                    .sort((a, b) => a[0] - b[0])
                    .map(
                        ([k, m]) =>
                            [k, [...m].map(([ph, refs]) => [ph, [...refs]] as [string, number[]])] as [
                                number,
                                [string, number[]][],
                            ],
                    ),
            };
            if (
                c.texts.length ||
                c.lineSounds.length ||
                c.seqs.length ||
                c.newSeqs.length ||
                c.newLines.length ||
                c.phases.length
            )
                out.push(c);
        }
        return out;
    }

    /** Replace the edits with a project file's. Entries that do not decode are skipped. */
    importState(list: ProjectContentEdits[] | null): void {
        this.edits.clear();
        for (const c of list ?? []) {
            const e = this.ed(c.stem);
            for (const [id, t] of c.texts) e.texts.set(id, t);
            for (const [k, m] of c.lineSounds) e.lineSounds.set(k, new Map(m));
            for (const [dst, src] of [
                [e.seqs, c.seqs],
                [e.newSeqs, c.newSeqs],
            ] as const)
                for (const q of src) {
                    const seq = parseSequence(q.id, q.folder, fromBase64(q.data));
                    if (seq) dst.set(q.id, seq);
                }
            for (const n of c.newLines) e.newLines.set(n.classId, { ...n });
            for (const [hid, phases] of c.phases)
                e.phases.set(hid, new Map(phases.map(([ph, refs]) => [ph as HandlerPhaseKey, [...refs]])));
        }
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

    private load(stem: string): Promise<LevelStory> | null {
        const fs = this.host.fs();
        if (!fs) return this.stories.get(stem) ?? null;
        let p = this.stories.get(stem);
        if (!p) {
            p = loadLevelStory(fs, this.host.language(), stem);
            this.stories.set(stem, p);
            p.catch(() => this.stories.delete(stem));
        }
        return p;
    }

    async use(stem: string): Promise<void> {
        const p = this.load(stem);
        if (!p) return;
        this.story = await p;
        this.stem = stem;
        this.convertedIds = (await this.host.converted?.().catch(() => undefined)) ?? {};
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
    /**
     * New dialogue in one step: a line class named after `speaker` that plays `soundRef`, the subtitle `text` on that sound,
     * and the line inserted into the phase of a conversation handler. Nothing changes when any part is refused.
     */
    composeLine(d: DialogueDraft & { templateId?: number }, opts: { classOnly?: boolean } = {}): number {
        const story = this.story;
        if (!story || !this.stem) throw new Error('Open a level folder first.');
        const handler = story.handlers.find((x) => x.classId === d.handlerId);
        if (!handler && !opts.classOnly) throw new Error('Pick the conversation the line belongs to.');
        const tpl =
            d.templateId ??
            this.phaseLines(d.handlerId, d.phase)
                .map((r) => r & 0xffff)
                .find((id) => story.lines.some((l) => l.classId === id)) ??
            (handler?.phases ?? [])
                .flatMap((p) => p.lines.map((r) => r & 0xffff))
                .find((id) => story.lines.some((l) => l.classId === id)) ??
            story.lines[0]?.classId;
        if (tpl === undefined)
            throw new Error('This level folder has no dialog line to copy the camera and animation from.');
        const sound = refParts(d.soundRef);
        const snd = sound.db === 0 ? story.sounds.get(sound.id) : undefined;
        const plan = planDialogueLine(
            { ...d, soundName: d.soundName || snd?.name || '' },
            this.phaseLines(d.handlerId, d.phase),
            0,
        );
        if (d.text.trim() && sound.db !== 0)
            throw new Error(
                'The subtitle is stored on a sound of this level; pick a sound of this level to give it text.',
            );
        const before = this.snapshot();
        try {
            const id = this.addLine(tpl, plan.name, plan.soundRef);
            const full = planDialogueLine(
                { ...d, soundName: d.soundName || snd?.name || '' },
                this.phaseLines(d.handlerId, d.phase),
                id,
            );
            if (!opts.classOnly) this.setPhaseLines(d.handlerId, d.phase, full.phaseLines);
            if (snd && d.text !== this.text(sound.id) && d.text.trim()) this.setText(sound.id, d.text);
            return id;
        } catch (e) {
            this.restore(before);
            throw e;
        }
    }
    private snapshot(): StemEdits | null {
        const e = this.stem ? this.edits.get(this.stem) : undefined;
        if (!e) return null;
        return {
            texts: new Map(e.texts),
            lineSounds: new Map([...e.lineSounds].map(([k, v]) => [k, new Map(v)])),
            seqs: new Map(e.seqs),
            newSeqs: new Map(e.newSeqs),
            newLines: new Map(e.newLines),
            phases: new Map([...e.phases].map(([k, v]) => [k, new Map(v)])),
        };
    }
    private restore(s: StemEdits | null): void {
        if (!this.stem) return;
        if (s) this.edits.set(this.stem, s);
        else this.edits.delete(this.stem);
        this.emit();
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
    /** Add a cut scene described by `spec` (camera points and steps) as a new sequence; returns its id. */
    addCutscene(spec: Omit<CutsceneSpec, 'id'>): number {
        const story = this.story;
        if (!story || !this.stem) throw new Error('Open a level folder first.');
        const e = this.ed();
        const used = new Set<number>([...story.sequences.map((s) => s.id), ...e.newSeqs.keys()]);
        const id = freeSequenceId(used);
        e.newSeqs.set(id, buildCutscene({ ...spec, id }));
        this.emit();
        return id;
    }
    /** Level object id most often used by the `Camera` actors of this level's sequences (0 when it has none). */
    cameraRef(): number {
        const n = new Map<number, number>();
        for (const r of this.sequences())
            for (const a of r.seq?.actors ?? [])
                if (/^camera$/i.test(a.name.trim())) n.set(a.ref, (n.get(a.ref) ?? 0) + 1);
        return [...n].sort((a, b) => b[1] - a[1])[0]?.[0] ?? 0;
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
            // Edits restored from a project file wait for their level folder to be read.
            const story = await this.load(stem);
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
