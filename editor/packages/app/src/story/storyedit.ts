// Worker-side loading for the story editors (Strings, Quests): every XDB string table of the disc, the System.ODB quest
// class records with what they need to be re-exported, and (on request) where each quest is placed and triggered in the
// levels. Nothing here is written back; edits are exported as mod pack entries (editor-core story.ts).

import type {
    ClassFieldDef,
    Iso9660,
    LvlHeader,
    LvlObjectList,
    OdbIndex,
    QuestClass,
    ObjectStringRef,
    StringRef,
} from '@drakan/formats';
import {
    ODB_CLASS_TYPE,
    ODB_FIELDS_TYPE,
    SrscReader,
    classStringRefs,
    objectStringRefs,
    odbStemForDependency,
    languageSlot,
    parseOdbClasses,
    parseOdbGroups,
    parseQuestClasses,
    parseXdbNames,
    parseXdbStrings,
    questPlacements,
    sliceSource,
    type QuestPlacement,
    type XdbString,
} from '@drakan/formats';

export interface StoryXdb {
    /** File name as the pack addresses it (`SYSTEM.XDB`). */
    file: string;
    /** Disc path inside the language tree (`COMMON\SYSTEM\SYSTEM.XDB`). */
    path: string;
    /** Display name (`System`, `Level 1A`...). */
    title: string;
    strings: XdbString[];
    names: [number, string][];
    /** Class default references per string id (`[id, refs]`), from the ODB of the same stem. */
    refs: [number, StringRef[]][];
}

export interface StoryQuestClass {
    cls: QuestClass;
    /** Record as stored in System.ODB. */
    record: Uint8Array;
    defs: ClassFieldDef[];
}

export interface StoryEditData {
    language: string;
    slot: number;
    xdbs: StoryXdb[];
    /** System.ODB quest classes. */
    quests: StoryQuestClass[];
    /** Every class id of System.ODB. */
    usedIds: number[];
    groups: [number, { name: string; parent: number }][];
    fieldLists: { id: number; sub: number; stored: Uint8Array } | null;
    /** The ODB file name for exports. */
    odbFile: string;
}

export interface QuestUsageLevel {
    /** Level folder (`LEVEL1NA`). */
    level: string;
    /** Quest class id -> its placements in this level. */
    placements: [number, QuestPlacement[]][];
}

async function open(fs: Iso9660, path: string): Promise<SrscReader | null> {
    const f = await fs.find(path);
    return f && !f.isDir ? SrscReader.open(sliceSource(fs.source, f.extent * 2048, f.size)) : null;
}

const stemOf = (file: string): string => file.replace(/\.[^.]*$/, '').toUpperCase();

export async function loadStoryEdit(fs: Iso9660, language: string): Promise<StoryEditData> {
    const sysOdb = await open(fs, `${language}/COMMON/SYSTEM/SYSTEM.ODB`);
    if (!sysOdb) throw new Error('this disc has no System object database');
    const classes = await parseOdbClasses(sysOdb);
    const quests = await parseQuestClasses(sysOdb);
    const groups = await parseOdbGroups(sysOdb);
    const outQuests: StoryQuestClass[] = [];
    for (const q of quests.values()) {
        const e = sysOdb.entries.find((x) => x.type === ODB_CLASS_TYPE && x.id === q.classId);
        const defs = classes.get(q.classId)?.fields;
        if (!e || !defs) continue;
        outQuests.push({ cls: q, record: (await sysOdb.readRaw(e)).slice(), defs });
    }
    const fl = sysOdb.entries.find((x) => x.type === ODB_FIELDS_TYPE);

    const xdbs: StoryXdb[] = [];
    const dirs: { dir: string; title: (stem: string) => string }[] = [];
    for (const top of ['COMMON', 'LEVELS']) {
        for (const d of await fs.list(`${language}/${top}`)) {
            if (d.isDir) dirs.push({ dir: `${language}/${top}/${d.name}`, title: (s) => s });
        }
    }
    for (const { dir } of dirs) {
        for (const f of await fs.list(dir)) {
            if (f.isDir || !/\.XDB$/i.test(f.name)) continue;
            const r = await open(fs, `${dir}/${f.name}`);
            if (!r) continue;
            const strings = [...(await parseXdbStrings(r)).values()];
            if (!strings.length) continue;
            const stem = stemOf(f.name);
            const odb = await open(fs, `${dir}/${stemOf(f.name)}.ODB`);
            const refs: [number, StringRef[]][] = odb
                ? [...classStringRefs(stem, (await parseOdbClasses(odb)).values())]
                : [];
            xdbs.push({
                file: f.name.toUpperCase(),
                path: `${dir.slice(language.length + 1)}/${f.name}`.replace(/\//g, '\\'),
                title: stem,
                strings,
                names: [...(await parseXdbNames(r))],
                refs,
            });
        }
    }
    return {
        language,
        slot: languageSlot(language),
        xdbs,
        quests: outQuests,
        usedIds: [...classes.keys()],
        groups: [...groups],
        fieldLists: fl ? { id: fl.id, sub: fl.sub, stored: (await sysOdb.readRaw(fl)).slice() } : null,
        odbFile: 'SYSTEM.ODB',
    };
}

/** Per level: quest objects and the objects linking to them. `load` is the worker's own level loader. */
export async function scanQuestUsage(
    questIds: ReadonlySet<number>,
    levelDirs: readonly string[],
    load: (dirPath: string) => Promise<{ header: LvlHeader; list: LvlObjectList; odbs: OdbIndex }>,
): Promise<QuestUsageLevel[]> {
    const out: QuestUsageLevel[] = [];
    for (const dir of levelDirs) {
        try {
            const { header, list, odbs } = await load(dir);
            const m = questPlacements(header, list, odbs, questIds);
            if (m.size) out.push({ level: dir.slice(dir.lastIndexOf('\\') + 1), placements: [...m] });
        } catch {
            // a level that does not load contributes nothing
        }
    }
    return out;
}

export interface StringUsageLevel {
    /** Level folder (`LEVEL1NA`). */
    level: string;
    /** XDB the ids point into: the level's own string table (first `LEVEL...` dependency, `LEVEL1ON.XDB`). */
    file: string;
    /** String id -> placed objects whose string parameter holds it. */
    refs: [number, ObjectStringRef[]][];
}

/** Per level: string ids held by string parameters of placed objects (exit destinations, signs, door texts...). */
export async function scanStringUsage(
    levelDirs: readonly string[],
    load: (dirPath: string) => Promise<{ header: LvlHeader; list: LvlObjectList; odbs: OdbIndex }>,
): Promise<StringUsageLevel[]> {
    const out: StringUsageLevel[] = [];
    for (const dir of levelDirs) {
        try {
            const { header, list, odbs } = await load(dir);
            const dep = header.dependencies
                .map((d) => odbStemForDependency(d.path.text))
                .find((stem) => /^LEVEL/i.test(stem));
            if (!dep) continue;
            const m = objectStringRefs(header, list, odbs);
            if (m.size)
                out.push({
                    level: dir.slice(dir.lastIndexOf('\\') + 1),
                    file: `${dep.toUpperCase()}.XDB`,
                    refs: [...m],
                });
        } catch {
            // a level that does not load contributes nothing
        }
    }
    return out;
}
