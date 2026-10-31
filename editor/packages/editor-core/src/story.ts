import { strToU8 } from 'fflate';
import {
    ODB_CLASS_TYPE,
    ODB_FIELDS_TYPE,
    appendFieldList,
    diffXdbStrings,
    draftEdits,
    editQuestRecord,
    freeClassId,
    sameDraft,
    type ClassFieldDef,
    type QuestDraft,
    type XdbStrings,
} from '@drakan/formats';
import { ModError, buildManifestToml, overridePath, type ModMeta } from './mod.ts';

/**
 * Story content as mod pack entries: edited XDB strings and quest classes. A pack ships a changed entry under
 * `overrides/<FILE>/` and an added one under `new/<FILE>/` (docs/reference/modding.md).
 */
export interface StoryEntryChange {
    file: string;
    type: number;
    id: number;
    sub: number;
    data: Uint8Array;
    isNew: boolean;
}

export const newEntryPath = (file: string, e: Pick<StoryEntryChange, 'type' | 'id' | 'sub'>): string =>
    `new/${file}/${e.type.toString(16).padStart(4, '0')}_${e.id}_${e.sub}.bin`;

/** Layout of a story pack: `overrides/` for replacements, `new/` for added entries, plus the manifest. */
export function buildStoryPackFiles(meta: ModMeta, changes: readonly StoryEntryChange[]): Record<string, Uint8Array> {
    const files: Record<string, Uint8Array> = { 'mod.toml': strToU8(buildManifestToml(meta)) };
    for (const c of changes) {
        if (c.data.length === 0) throw new ModError(`${c.file}: empty entries are rejected by the runtime`);
        const path = c.isNew ? newEntryPath(c.file, c) : overridePath(c.file, c);
        if (path in files) throw new ModError(`duplicate entry ${path}`);
        files[path] = c.data;
    }
    return files;
}

/** One XDB file: strings as read and as edited. */
export function stringChanges(file: string, original: XdbStrings, edited: XdbStrings): StoryEntryChange[] {
    return diffXdbStrings(original, edited).map((p) => ({ file, ...p }));
}

export interface QuestClassSource {
    id: number;
    /** ODB group id of the record (the SRSC sub). */
    group: number;
    /** Record as stored. */
    record: Uint8Array;
    defs: readonly ClassFieldDef[];
    draft: QuestDraft;
}

export interface NewQuest {
    /** Existing class whose record and field list are copied. */
    templateId: number;
    /** Chapter group (SRSC sub) the new record joins. */
    group: number;
    /** Developer name, shown nowhere in game. */
    name: string;
    draft: QuestDraft;
    /** Fixed id (kept stable between exports); chosen when missing. */
    classId?: number;
}

export interface QuestSources {
    file: string;
    classes: ReadonlyMap<number, QuestClassSource>;
    /** Every class id of the ODB (to choose free ids). */
    usedIds: ReadonlySet<number>;
    /** The 0x0103 entry as stored (with its key); an added record needs its list appended. Absent = leave the lists. */
    fieldLists?: { id: number; sub: number; stored: Uint8Array };
}

export interface QuestChangeSet {
    changes: StoryEntryChange[];
    /** Ids given to the new quests, in order. */
    newIds: number[];
}

/**
 * Entries for edited and added quest classes. A new quest copies the record of `templateId`, so it starts with every
 * other field (sound, indicator colour) of that quest. Its field list is appended to 0x0103 when given: the lists are
 * matched to records by record order and a record added with `new/` is last.
 */
export async function questChanges(
    src: QuestSources,
    edits: ReadonlyMap<number, QuestDraft>,
    added: readonly NewQuest[],
): Promise<QuestChangeSet> {
    const changes: StoryEntryChange[] = [];
    for (const [id, draft] of [...edits].sort((a, b) => a[0] - b[0])) {
        const c = src.classes.get(id);
        if (!c) throw new ModError(`no quest class ${id}`);
        if (sameDraft(c.draft, draft)) continue;
        changes.push({
            file: src.file,
            type: ODB_CLASS_TYPE,
            id,
            sub: c.group,
            data: editQuestRecord(c.record, c.defs, draft),
            isNew: false,
        });
    }
    const used = new Set(src.usedIds);
    const newIds: number[] = [];
    let lists = src.fieldLists?.stored;
    for (const q of added) {
        const t = src.classes.get(q.templateId);
        if (!t) throw new ModError(`no template quest class ${q.templateId}`);
        const id = q.classId ?? freeClassId(used);
        if (id === null || id === undefined) throw new ModError('no free class id left in the quest database');
        if (used.has(id) && !q.classId) throw new ModError(`class id ${id} is taken`);
        used.add(id);
        newIds.push(id);
        draftEdits(t.defs, q.draft);
        changes.push({
            file: src.file,
            type: ODB_CLASS_TYPE,
            id,
            sub: q.group,
            data: editQuestRecord(t.record, t.defs, q.draft, q.name),
            isNew: true,
        });
        if (lists) lists = await appendFieldList(lists, t.defs);
    }
    if (lists && added.length) {
        changes.push({
            file: src.file,
            type: ODB_FIELDS_TYPE,
            id: src.fieldLists!.id,
            sub: src.fieldLists!.sub,
            data: lists,
            isNew: false,
        });
    }
    return { changes, newIds };
}
