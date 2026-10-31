// Shared state of the story editors (Strings, Quests): what was read from the disc, the edits on top, and the export of
// those edits as a mod pack. One session per opened disc.

import {
    ModError,
    buildStoryPackFiles,
    questChanges,
    stringChanges,
    updateModsIni,
    zipModFiles,
    type ModMeta,
    type NewQuest,
    type StoryEntryChange,
} from '@drakan/editor-core';
import {
    draftOf,
    freeClassId,
    nextStringId,
    sameDraft,
    withDestination,
    type ObjectStringRef,
    sameXdbString,
    xdbTextProblem,
    type QuestDraft,
    type XdbString,
} from '@drakan/formats';
import type { Api } from '../rpc.ts';
import type { QuestUsageLevel, StoryEditData, StoryXdb, StringUsageLevel } from './storyedit.ts';
import { downloadFile, isDesktop, readModsIni, writeModPack } from '../platform.ts';
import { writePackToModsFolder, type LevelPack } from '../modio.ts';

export type Call = <M extends keyof Api>(method: M, ...args: Parameters<Api[M]>) => Promise<ReturnType<Api[M]>>;

export interface StoryHost {
    call: Call;
    /** Disc profile target for the manifest, null until a disc is open. */
    target(): ModMeta['target'] | null;
    status(msg: string, bad?: boolean): void;
}

export interface QuestRow {
    classId: number;
    /** True for a quest this session adds. */
    isNew: boolean;
    name: string;
    group: number;
    draft: QuestDraft;
    original: QuestDraft | null;
    edited: boolean;
}

export interface ChangeSource {
    changes(): Promise<StoryEntryChange[]>;
    subscribe(fn: () => void): () => void;
    count(): number;
}

export const SYSTEM_XDB = 'SYSTEM.XDB';

export class StorySession {
    data: StoryEditData | null = null;
    usage: QuestUsageLevel[] | null = null;
    /** Level objects' string parameters per level (after `scanStrings`). */
    stringUsage: StringUsageLevel[] | null = null;
    /** Edited copies of the string tables, by file. */
    private edited = new Map<string, Map<number, XdbString>>();
    private drafts = new Map<number, QuestDraft>();
    newQuests: NewQuest[] = [];
    private listeners = new Set<() => void>();
    private loading: Promise<StoryEditData> | null = null;

    constructor(readonly host: StoryHost) {}

    subscribe(fn: () => void): () => void {
        this.listeners.add(fn);
        return () => this.listeners.delete(fn);
    }
    private emit(): void {
        this.listeners.forEach((l) => l());
    }

    /** Read the disc data once. */
    ensure(): Promise<StoryEditData> {
        if (this.data) return Promise.resolve(this.data);
        this.loading ??= this.host.call('loadStoryEdit').then((d) => {
            this.data = d as StoryEditData;
            this.emit();
            return this.data;
        });
        this.loading.catch(() => (this.loading = null));
        return this.loading;
    }

    reset(): void {
        this.data = null;
        this.usage = null;
        this.stringUsage = null;
        this.loading = null;
        this.edited.clear();
        this.drafts.clear();
        this.newQuests = [];
        this.emit();
    }

    async scanUsage(): Promise<QuestUsageLevel[]> {
        this.usage = (await this.host.call('scanQuestUsage')) as QuestUsageLevel[];
        this.emit();
        return this.usage;
    }

    /** Find which placed objects of which levels hold string ids (seconds: loads every level). */
    async scanStrings(): Promise<StringUsageLevel[]> {
        this.stringUsage = (await this.host.call('scanStringUsage')) as StringUsageLevel[];
        this.emit();
        return this.stringUsage;
    }
    /** Placed objects whose string parameter holds this id, per level (empty until `scanStrings`). */
    objectRefs(file: string, id: number): { level: string; refs: ObjectStringRef[] }[] {
        const out: { level: string; refs: ObjectStringRef[] }[] = [];
        for (const u of this.stringUsage ?? []) {
            if (u.file !== file) continue;
            const r = u.refs.find((x) => x[0] === id)?.[1];
            if (r?.length) out.push({ level: u.level, refs: r });
        }
        return out;
    }

    /** Point an exit at another level: rewrites its destination string in every language slot that has text. */
    setExitDestination(file: string, id: number, text: string): boolean {
        const cur = this.str(file, id);
        if (!cur) return false;
        this.put(file, withDestination(cur, text));
        return true;
    }

    // ---- strings ----------------------------------------------------------------------------------------------

    xdb(file: string): StoryXdb | undefined {
        return this.data?.xdbs.find((x) => x.file === file);
    }
    /** The string as edited (or as read). */
    str(file: string, id: number): XdbString | undefined {
        return this.edited.get(file)?.get(id) ?? this.xdb(file)?.strings.find((s) => s.id === id);
    }
    original(file: string, id: number): XdbString | undefined {
        return this.xdb(file)?.strings.find((s) => s.id === id);
    }
    /** All strings of a file with edits and additions applied, by id. */
    strings(file: string): XdbString[] {
        const x = this.xdb(file);
        if (!x) return [];
        const m = new Map(x.strings.map((s) => [s.id, s] as const));
        for (const [id, s] of this.edited.get(file) ?? []) m.set(id, s);
        return [...m.values()].sort((a, b) => a.id - b.id);
    }
    isChanged(file: string, id: number): boolean {
        const e = this.edited.get(file)?.get(id);
        if (!e) return false;
        const o = this.original(file, id);
        return !o || !sameXdbString(o, e);
    }
    isNewString(file: string, id: number): boolean {
        return !this.original(file, id);
    }
    /** Text of a language slot, with the English fallback the game uses. */
    text(file: string, id: number, slot: number): string {
        const s = this.str(file, id);
        return s ? s.text[slot] || s.text[0] || '' : '';
    }
    setText(file: string, id: number, slot: number, text: string): void {
        const cur = this.str(file, id);
        if (!cur) return;
        const t = [...cur.text];
        while (t.length < 8) t.push('');
        t[slot] = text;
        this.put(file, { ...cur, text: t });
    }
    private put(file: string, s: XdbString): void {
        let m = this.edited.get(file);
        if (!m) this.edited.set(file, (m = new Map()));
        m.set(s.id, s);
        this.emit();
    }
    revertString(file: string, id: number): void {
        if (this.original(file, id)) this.edited.get(file)?.delete(id);
        this.emit();
    }
    /** A new string with a fresh id; `text` goes into every slot of the running language and English. */
    addString(file: string, text: string, sub?: number): number {
        const x = this.xdb(file);
        if (!x) throw new ModError(`no string table ${file}`);
        const all = new Map(this.strings(file).map((s) => [s.id, s] as const));
        const id = nextStringId(all);
        const slots = Array.from({ length: 8 }, () => '');
        slots[0] = text;
        slots[this.data!.slot] = text;
        const subCounts = new Map<number, number>();
        for (const s of x.strings) subCounts.set(s.sub, (subCounts.get(s.sub) ?? 0) + 1);
        const common = [...subCounts].sort((a, b) => b[1] - a[1])[0]?.[0] ?? 0;
        this.put(file, { id, sub: sub ?? common, text: slots });
        return id;
    }
    stringProblem(file: string, id: number): string | null {
        const s = this.str(file, id);
        if (!s) return null;
        for (const t of s.text) {
            const p = xdbTextProblem(t);
            if (p) return p;
        }
        return null;
    }
    /** Class default references of a string id (from the ODB of the same stem) plus quests of this session. */
    refCount(file: string, id: number): number {
        const x = this.xdb(file);
        let n = x?.refs.find((r) => r[0] === id)?.[1].length ?? 0;
        for (const u of this.objectRefs(file, id)) n += u.refs.length;
        if (file === SYSTEM_XDB) {
            for (const q of this.questRows())
                if (q.isNew && (q.draft.journalId === id || q.draft.mapTextId === id)) n++;
        }
        return n;
    }

    // ---- quests -----------------------------------------------------------------------------------------------

    questRows(): QuestRow[] {
        const d = this.data;
        if (!d) return [];
        const rows: QuestRow[] = d.quests.map((q) => {
            const original = draftOf(q.cls);
            const draft = this.drafts.get(q.cls.classId) ?? original;
            return {
                classId: q.cls.classId,
                isNew: false,
                name: q.cls.name,
                group: q.cls.groupId,
                draft,
                original,
                edited: !sameDraft(draft, original),
            };
        });
        for (const n of this.newQuests) {
            rows.push({
                classId: n.classId!,
                isNew: true,
                name: n.name,
                group: n.group,
                draft: n.draft,
                original: null,
                edited: true,
            });
        }
        return rows;
    }
    setDraft(classId: number, patch: Partial<QuestDraft>): void {
        const n = this.newQuests.find((q) => q.classId === classId);
        if (n) n.draft = { ...n.draft, ...patch };
        else {
            const q = this.data?.quests.find((x) => x.cls.classId === classId);
            if (!q) return;
            this.drafts.set(classId, { ...(this.drafts.get(classId) ?? draftOf(q.cls)), ...patch });
        }
        this.emit();
    }
    revertQuest(classId: number): void {
        this.drafts.delete(classId);
        this.emit();
    }
    removeNewQuest(classId: number): void {
        this.newQuests = this.newQuests.filter((q) => q.classId !== classId);
        this.emit();
    }
    /** New quest from a template: a fresh class id and a journal string with `title`. */
    addQuest(templateId: number, title: string, group?: number): number {
        const d = this.data!;
        const t = d.quests.find((q) => q.cls.classId === templateId) ?? d.quests[0]!;
        const used = new Set([...d.usedIds, ...this.newQuests.map((q) => q.classId!)]);
        const classId = freeClassId(used);
        if (classId === null) throw new ModError('No free class id left in the quest database.');
        const journalId = this.addString(SYSTEM_XDB, title);
        const base = draftOf(t.cls);
        this.newQuests.push({
            templateId: t.cls.classId,
            group: group ?? t.cls.groupId,
            name: title.slice(0, 40) || 'New Quest',
            classId,
            draft: { ...base, kind: 'journal', optional: false, journalId, mapTextId: 0, mapX: 0, mapY: 0 },
        });
        this.emit();
        return classId;
    }

    // ---- export -----------------------------------------------------------------------------------------------

    private sources: ChangeSource[] = [];
    /** Another editor's edits (dialogue, cut scenes) that this session's pack carries too. */
    addChangeSource(src: ChangeSource): void {
        this.sources.push(src);
        src.subscribe(() => this.emit());
    }

    async changes(): Promise<StoryEntryChange[]> {
        const out: StoryEntryChange[] = [];
        for (const src of this.sources) out.push(...(await src.changes()));
        out.push(...(await this.ownChanges()));
        return out;
    }

    private async ownChanges(): Promise<StoryEntryChange[]> {
        const d = this.data;
        if (!d) return [];
        const out: StoryEntryChange[] = [];
        for (const [file, edited] of this.edited) {
            const x = this.xdb(file);
            if (!x) continue;
            const orig = new Map(x.strings.map((s) => [s.id, s] as const));
            out.push(...stringChanges(file, orig, edited));
        }
        const classes = new Map(
            d.quests.map((q) => [
                q.cls.classId,
                { id: q.cls.classId, group: q.cls.groupId, record: q.record, defs: q.defs, draft: draftOf(q.cls) },
            ]),
        );
        const edits = new Map([...this.drafts].filter(([id]) => classes.has(id)));
        if (edits.size || this.newQuests.length) {
            const r = await questChanges(
                {
                    file: d.odbFile,
                    classes,
                    usedIds: new Set(d.usedIds),
                    ...(d.fieldLists ? { fieldLists: d.fieldLists } : {}),
                },
                edits,
                this.newQuests,
            );
            out.push(...r.changes);
        }
        return out;
    }

    meta(id: string, name: string, author: string): ModMeta {
        const target = this.host.target();
        if (!target) throw new ModError('The disc profile is unknown; open the ISO first.');
        return {
            id,
            name: name || undefined,
            author: author || undefined,
            version: '1.0.0',
            description: 'Story content (strings, quests, dialogue, cut scenes) made with the Drakan editor.',
            target,
        };
    }

    /** Pack files for the current edits; throws ModError when there is nothing to export. */
    async buildFiles(meta: ModMeta): Promise<{ files: Record<string, Uint8Array>; changes: StoryEntryChange[] }> {
        const changes = await this.changes();
        if (!changes.length) throw new ModError('No story edits to export.');
        return { files: buildStoryPackFiles(meta, changes), changes };
    }

    async exportZip(meta: ModMeta): Promise<StoryEntryChange[]> {
        const { files, changes } = await this.buildFiles(meta);
        await downloadFile(`${meta.id}.zip`, zipModFiles(files) as BlobPart, 'application/zip');
        return changes;
    }

    /** Write into the game's mods folder and enable the pack. Returns where it went, null when cancelled. */
    async exportToMods(meta: ModMeta): Promise<{ where: string | null; changes: StoryEntryChange[] }> {
        const { files, changes } = await this.buildFiles(meta);
        if (isDesktop) {
            const ini = updateModsIni(await readModsIni(), meta.id, true);
            return { where: await writeModPack(meta.id, files, ini), changes };
        }
        const where = await writePackToModsFolder({ meta, files } as unknown as LevelPack);
        return { where, changes };
    }

    /** Number of changed entries (cheap summary for badges). */
    editCount(): number {
        let n = this.drafts.size + this.newQuests.length + this.sources.reduce((a, x) => a + x.count(), 0);
        for (const [file, m] of this.edited) for (const id of m.keys()) if (this.isChanged(file, id)) n++;
        return n;
    }
}
