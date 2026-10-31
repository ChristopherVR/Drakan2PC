// Worker-side loading of the item classes. Every item the inventory can hold is a class of the shared "Resources"
// object database (Common\Resources), which every level lists as a dependency. The save stores an item as a RAM
// resource id `(dbIndex << 16) | classId` with dbIndex 3 for that database, so a save can be shown without opening
// any level: read the class names from Resources.ODB and decode models through any level header that lists it.

import type { Iso9660, LvlHeader } from '@drakan/formats';
import {
    SrscReader,
    odbStemForDependency,
    parseLevelHeader,
    parseLevelList,
    parseOdbClasses,
    resolveDependencyStem,
    sliceSource,
} from '@drakan/formats';
import { loadModelData, type ModelData } from './scene/decode.ts';

/** The database index the running game (and so every save) uses for the Resources ODB. */
export const RESOURCES_DB_INDEX = 3;

export interface ItemCatalog {
    /** Class id and ODB name of every class in Resources.ODB. */
    classes: { classId: number; name: string }[];
}

interface Context {
    dirPath: string;
    header: LvlHeader;
    dbIndex: number;
    stem: string;
}

let ctx: Promise<Context | null> | null = null;
let catalog: Promise<ItemCatalog> | null = null;

/** Forget cached state (a new disc or language was opened). */
export function resetItemData(): void {
    ctx = null;
    catalog = null;
}

async function findContext(fs: Iso9660, language: string): Promise<Context | null> {
    const list = await fs.find(`${language}\\LEVELLST.TXT`);
    if (!list) return null;
    const levels = parseLevelList(new TextDecoder('latin1').decode(await fs.read(list)));
    for (const l of levels) {
        const dirPath = `${language}\\${l.dirPath}`;
        const lvl = (await fs.list(dirPath).catch(() => [])).find((e) => !e.isDir && /\.LVL$/i.test(e.name));
        if (!lvl) continue;
        try {
            const reader = await SrscReader.open(sliceSource(fs.source, lvl.extent * 2048, lvl.size));
            const e = reader.entries.find((x) => x.type === 0);
            if (!e) continue;
            const header = parseLevelHeader((await reader.read(e)).data);
            const dep = header.dependencies.find((d) => /^resource/i.test(d.path.text.split(/[\\/]/).pop() ?? ''));
            if (!dep) continue;
            const stem = resolveDependencyStem(dirPath, dep.path.text);
            if (odbStemForDependency(dep.path.text) && (await fs.find(`${stem}.ODB`))) {
                return { dirPath, header, dbIndex: dep.dbIndex, stem };
            }
        } catch {
            /* try the next level */
        }
    }
    return null;
}

function context(fs: Iso9660, language: string): Promise<Context | null> {
    return (ctx ??= findContext(fs, language));
}

export function loadItemCatalog(fs: Iso9660, language: string): Promise<ItemCatalog> {
    return (catalog ??= (async () => {
        const c = await context(fs, language);
        if (!c) throw new Error('this disc has no Resources object database');
        const f = (await fs.find(`${c.stem}.ODB`))!;
        const odb = await SrscReader.open(sliceSource(fs.source, f.extent * 2048, f.size));
        const classes = await parseOdbClasses(odb);
        return {
            classes: [...classes.values()].filter((k) => k.name).map((k) => ({ classId: k.id, name: k.name })),
        };
    })());
}

/** Models and textures of Resources classes; `dbIndex` in the result is the level-local one, callers key by classId. */
export async function loadItemModels(fs: Iso9660, language: string, classIds: number[]): Promise<ModelData[]> {
    const c = await context(fs, language);
    if (!c) return [];
    return loadModelData(
        fs,
        c.dirPath,
        c.header,
        classIds.map((classId) => ({ classId, dbIndex: c.dbIndex })),
    );
}
