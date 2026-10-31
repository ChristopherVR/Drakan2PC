// Shared state of the dialogue and cinematics editors: the story data of one level folder at a time (`loadLevelStory`),
// the edits on top (subtitle texts, dialog line sound refs, sequences) and their export as mod pack entries. Edits are kept
// per level folder, so switching the folder does not lose them. One session per opened disc.

import {
    ModError,
    buildStoryPackFiles,
    checkEntrySize,
    zipModFiles,
    type ModMeta,
    type StoryEntryChange,
} from '@drakan/editor-core';
import {
    ODB_CLASS_TYPE,
    SDB_DESCRIPTOR,
    SSD_SEQUENCE,
    dialogLineSoundEdit,
    loadLevelStory,
    parseDialogLine,
    parseSdbText,
    parseSequence,
    patchClassData,
    writeSdbText,
    writeSequence,
    type LevelStory,
    type SsdSequence,
    type StoryFsList,
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
}

export interface PlacedStoryObject {
    id: number;
    name: string;
    /** `Sequence List`, `Beginning Dialog Exchange`, ... by field name. */
    fields: Map<string, number | number[]>;
}

interface StemEdits {
    texts: Map<number, string>;
    lineSounds: Map<number, Map<number, number>>;
    seqs: Map<number, SsdSequence>;
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
        if (!e) this.edits.set(stem, (e = { texts: new Map(), lineSounds: new Map(), seqs: new Map() }));
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

    lineSound(classId: number, i: number): number {
        const o = this.stem ? this.edits.get(this.stem)?.lineSounds.get(classId)?.get(i) : undefined;
        if (o !== undefined) return o;
        return this.story?.lines.find((l) => l.classId === classId)?.sounds[i] ?? 0;
    }
    setLineSound(classId: number, i: number, ref: number): void {
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

    /** The sequence as edited (or as read). */
    sequence(id: number): SsdSequence | null {
        return (
            (this.stem && this.edits.get(this.stem)?.seqs.get(id)) ||
            this.story?.sequences.find((s) => s.id === id)?.seq ||
            null
        );
    }
    sequenceEdited(id: number): boolean {
        return !!this.stem && this.edits.get(this.stem)?.seqs.has(id) === true;
    }
    /** Change a sequence on a private copy; the edit is dropped again when the bytes equal the original. */
    editSequence(id: number, fn: (s: SsdSequence) => void): void {
        const rec = this.story?.sequences.find((s) => s.id === id);
        if (!rec?.seq || !this.stem) return;
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
        this.emit();
    }

    // ---- export ---------------------------------------------------------------------------------------------------

    count(): number {
        let n = 0;
        for (const e of this.edits.values()) n += e.texts.size + e.lineSounds.size + e.seqs.size;
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
