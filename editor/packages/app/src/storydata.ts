// Worker-side loading of the quest classes and their journal text (System.ODB + System.XDB). Both live in
// `<language>\Common\System`, which every level lists as a dependency, so a save can be read without opening a level.

import type { Iso9660, QuestClass, XdbString } from '@drakan/formats';
import { SrscReader, languageSlot, parseQuestClasses, parseXdbStrings, sliceSource } from '@drakan/formats';

export interface StoryCatalog {
    /** XDB slot of the disc's language folder (0 English, 1 German, ...). */
    slot: number;
    classes: QuestClass[];
    /** Only the strings the quest classes refer to. */
    strings: XdbString[];
}

let cache: Promise<StoryCatalog> | null = null;

export function resetStoryData(): void {
    cache = null;
}

async function openSrsc(fs: Iso9660, path: string): Promise<SrscReader | null> {
    const f = await fs.find(path);
    return f && !f.isDir ? SrscReader.open(sliceSource(fs.source, f.extent * 2048, f.size)) : null;
}

export function loadStoryCatalog(fs: Iso9660, language: string): Promise<StoryCatalog> {
    if (cache) return cache;
    const loaded = (async () => {
        const dir = `${language}/COMMON/SYSTEM`;
        const odb = await openSrsc(fs, `${dir}/SYSTEM.ODB`);
        if (!odb) throw new Error('this disc has no System object database');
        const classes = [...(await parseQuestClasses(odb)).values()];
        const xdb = await openSrsc(fs, `${dir}/SYSTEM.XDB`);
        const all = xdb ? await parseXdbStrings(xdb) : new Map<number, XdbString>();
        const strings: XdbString[] = [];
        const seen = new Set<number>();
        for (const c of classes) {
            for (const id of [c.journalId, c.mapTextId]) {
                const s = all.get(id);
                if (s && !seen.has(id)) {
                    seen.add(id);
                    strings.push(s);
                }
            }
        }
        return { slot: languageSlot(language), classes, strings };
    })();
    cache = loaded;
    loaded.catch(() => {
        if (cache === loaded) cache = null; // do not keep a failure (the disc may still be opening)
    });
    return loaded;
}
