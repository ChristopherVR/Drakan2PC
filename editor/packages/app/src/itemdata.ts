// Worker-side loading of the item classes. Every item the inventory can hold is a class of the shared "Resources"
// object database (Common\Resources), which every level lists as a dependency. The save stores an item as a RAM
// resource id `(dbIndex << 16) | classId` with dbIndex 3 for that database, so a save can be shown without opening
// any level: read the class names from Resources.ODB and decode models through any level header that lists it.

import type { Iso9660, LvlHeader } from '@drakan/formats';
import {
    SrscReader,
    classFieldDefault,
    languageSlot,
    odbStemForDependency,
    parseLevelHeader,
    parseLevelList,
    parseOdbClasses,
    parseXdbStrings,
    resolveDependencyStem,
    sliceSource,
    xdbText,
} from '@drakan/formats';
import { loadModelData, type ModelData } from './scene/decode.ts';

/** The database index the running game (and so every save) uses for the Resources ODB. */
export const RESOURCES_DB_INDEX = 3;

export interface ItemCatalog {
    /**
     * Class id and ODB name of every class in Resources.ODB, with the game's own name for it (Resources.XDB string
     * `DisplayName`, in the disc's language) and its shop price when the class has them.
     */
    classes: { classId: number; name: string; displayName?: string; price?: number; facts?: string[] }[];
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

const FACT_FIELDS: [string, string, (v: number) => string][] = [
    ['Damage Maximum', 'Damage', (v) => `up to ${v}`],
    ['Weapon Speed', 'Speed', (v) => String(Number(v.toFixed(2)))],
    ['Health Boost', 'Restores health', (v) => String(v)],
    ['Total Durability Points', 'Durability', (v) => String(v)],
    ['Skill Level', 'Needs skill level', (v) => String(v)],
    ['Icon Slot Size (1-3)', 'Inventory cells', (v) => String(v)],
];

/**
 * The game has no item description text (Resources.XDB holds names only), so the tooltip is built from the class
 * fields that say what the item does. Floats are stored as raw bits and read back with `classFieldDefault`.
 */
export function itemFacts(k: Parameters<typeof classFieldDefault>[0], at: (name: string) => number): string[] {
    const out: string[] = [];
    for (const [field, label, fmt] of FACT_FIELDS) {
        const i = at(field);
        if (i < 0) continue;
        const v = Number(classFieldDefault(k, i));
        // 0xffffffff durability means "never breaks"; 0 means the field is unused.
        if (!Number.isFinite(v) || v === 0) continue;
        if (field === 'Total Durability Points' && (v < 0 || v > 1e6)) {
            out.push('Durability: unbreakable');
            continue;
        }
        out.push(`${label}: ${fmt(v)}`);
    }
    if (at('Droppable?') >= 0 && Number(classFieldDefault(k, at('Droppable?'))) === 0) out.push('Cannot be dropped');
    return out;
}

export function loadItemCatalog(fs: Iso9660, language: string): Promise<ItemCatalog> {
    return (catalog ??= (async () => {
        const c = await context(fs, language);
        if (!c) throw new Error('this disc has no Resources object database');
        const f = (await fs.find(`${c.stem}.ODB`))!;
        const odb = await SrscReader.open(sliceSource(fs.source, f.extent * 2048, f.size));
        const classes = await parseOdbClasses(odb);
        const xf = await fs.find(`${c.stem}.XDB`);
        const strings = xf
            ? await parseXdbStrings(await SrscReader.open(sliceSource(fs.source, xf.extent * 2048, xf.size)))
            : null;
        const slot = languageSlot(language);
        return {
            classes: [...classes.values()]
                .filter((k) => k.name)
                .map((k) => {
                    const at = (n: string): number => k.fields?.findIndex((f) => f.name === n) ?? -1;
                    const iName = at('DisplayName');
                    const iPrice = at('Cost to Buy (in Gold)');
                    // The dword is the string id itself (a level object parameter would hold an offset/length pair).
                    const text = strings && iName >= 0 ? xdbText(strings, k.data![iName]! & 0xffff, slot) : '';
                    const price = iPrice >= 0 ? Number(classFieldDefault(k, iPrice)) : NaN;
                    const facts = itemFacts(k, (n) => at(n));
                    return {
                        classId: k.id,
                        ...(facts.length ? { facts } : {}),
                        name: k.name,
                        ...(text ? { displayName: text } : {}),
                        ...(price > 0 ? { price } : {}),
                    };
                }),
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
