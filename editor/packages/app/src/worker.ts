import type { BlobLike, DiscProfile, LvlHeader } from '@drakan/formats';
import {
    Iso9660,
    SrscReader,
    LVL_TYPE_OBJECTS,
    TXD_ENTRY_TYPE,
    buildOdbIndex,
    odbStemForDependency,
    parseSystemCnf,
    readLvl,
    blobSource,
    decodeTxd,
    parseDb,
    parseLevelHeader,
    parseLevelList,
    resolveDependencyStem,
    sliceSource,
} from '@drakan/formats';
import { destinationLevelName, srscRoundTrips } from '@drakan/editor-core';
import { loadModelData, loadTerrainData } from './scene/decode.ts';
import { loadItemCatalog, loadItemModels, resetItemData } from './itemdata.ts';
import { loadStoryCatalog, resetStoryData } from './storydata.ts';
import { levelExitData, type ExitIndexData } from './logicdata.ts';
import { isoStoryFs } from './story/storyfs.ts';
import { loadStoryEdit, scanQuestUsage, scanStringUsage } from './story/storyedit.ts';
import { classModelRef, loadRefClip, loadRefRig, rigForTracks } from './animdata.ts';
import { listRefs, loadRefModel, loadRefSound, loadRefTexture, refContainers } from './refdata.ts';
import type {
    Api,
    EntryBytes,
    EntryInfo,
    FileInfo,
    FileOpenResult,
    LevelInfo,
    LevelLoadResult,
    OpenResult,
    Request,
    Response,
    RemoteBlob,
    RoundTripResult,
    TextureResult,
} from './rpc.ts';

// Everything here runs off the UI thread. The ISO is read through Blob.slice, so a multi-GB image is never loaded whole.

/** Blob-like view of a URL that supports range requests (the desktop shell serves the disc this way). */
function remoteBlob(r: RemoteBlob): BlobLike {
    return {
        size: r.size,
        slice: (start = 0, end = r.size) => ({
            async arrayBuffer() {
                // Tauri's asset protocol answers a Range request with at most about 1 MiB (206 + Content-Range), however
                // much was asked for; a larger read (a terrain layer table is 1.2 MB) has to be assembled from several.
                const want = Math.max(0, end - start);
                const out = new Uint8Array(want);
                let got = 0;
                while (got < want) {
                    const res = await fetch(r.url, { headers: { Range: `bytes=${start + got}-${end - 1}` } });
                    if (res.status !== 206 && res.status !== 200)
                        throw new Error(`reading ${r.name}: HTTP ${res.status}`);
                    const buf = new Uint8Array(await res.arrayBuffer());
                    // A server that ignores Range sends the whole file; cut out what was asked for.
                    const part = res.status === 200 ? buf.subarray(start + got, end) : buf;
                    if (part.length === 0) throw new Error(`reading ${r.name}: empty answer at ${start + got}`);
                    const n = Math.min(part.length, want - got);
                    out.set(part.subarray(0, n), got);
                    got += n;
                }
                return out.buffer;
            },
        }),
    };
}

let iso: Iso9660 | null = null;
let language = '';
const srscCache = new Map<string, SrscReader>();
const inflatedCache = new Map<string, { data: Uint8Array }>();

const decoder = new TextDecoder('latin1');

function need(): Iso9660 {
    if (!iso) throw new Error('no disc image open');
    return iso;
}

/** The .DB dependency list beside an ODB (stem path without extension): dependency index -> container stem. */
async function readOdbDeps(fs: Iso9660, stemPath: string): Promise<Record<number, string>> {
    const out: Record<number, string> = {};
    const dbf = await fs.find(`${stemPath}.DB`);
    if (!dbf || dbf.isDir) return out;
    const dir = stemPath.slice(0, stemPath.lastIndexOf('\\'));
    for (const dep of parseDb(decoder.decode(await fs.read(dbf))).dependencies) {
        out[dep.index] = odbStemForDependency(resolveDependencyStem(dir, dep.path));
    }
    return out;
}

async function loadLevels(): Promise<LevelInfo[]> {
    const fs = need();
    const f = await fs.find(`${language}\\LEVELLST.TXT`);
    if (!f) return [];
    const list = parseLevelList(decoder.decode(await fs.read(f)));
    return Promise.all(
        list.map(async (l) => {
            const dirPath = `${language}\\${l.dirPath}`;
            const dir = await fs.find(dirPath);
            return { name: l.name, dirPath, present: !!dir?.isDir };
        }),
    );
}

async function getSrsc(path: string): Promise<SrscReader> {
    let r = srscCache.get(path);
    if (!r) {
        const fs = need();
        const e = await fs.find(path);
        if (!e || e.isDir) throw new Error(`not a file: ${path}`);
        r = await SrscReader.open(sliceSource(fs.source, e.extent * 2048, e.size));
        srscCache.clear(); // keep memory bounded: one container at a time
        inflatedCache.clear();
        srscCache.set(path, r);
    }
    return r;
}

async function openLevel(fs: Iso9660, dirPath: string): Promise<{ reader: SrscReader; header: LvlHeader }> {
    const lvlFile = (await fs.list(dirPath)).find((e) => !e.isDir && /\.LVL$/i.test(e.name));
    if (!lvlFile) throw new Error(`no .LVL file in ${dirPath}`);
    const reader = await SrscReader.open(sliceSource(fs.source, lvlFile.extent * 2048, lvlFile.size));
    const e = reader.entries.find((x) => x.type === 0);
    if (!e) throw new Error('LVL has no header record');
    return { reader, header: parseLevelHeader((await reader.read(e)).data) };
}

const api: { [K in keyof Api]: (...a: Parameters<Api[K]>) => Promise<ReturnType<Api[K]>> } = {
    async open(file) {
        iso = await Iso9660.open(blobSource('url' in file ? remoteBlob(file) : file));
        srscCache.clear();
        const languages: string[] = [];
        for (const e of await iso.list('')) {
            if (e.isDir && (await iso.find(`${e.name}\\LEVELLST.TXT`))) languages.push(e.name);
        }
        language = languages.includes('ENGLISH') ? 'ENGLISH' : (languages[0] ?? '');
        resetItemData();
        resetStoryData();
        let profile: DiscProfile | null = null;
        const cnf = await iso.find('SYSTEM.CNF');
        if (cnf && !cnf.isDir && cnf.size < 4096) profile = parseSystemCnf(decoder.decode(await iso.read(cnf)));
        const out: OpenResult = { volumeId: iso.volumeId, languages, language, levels: await loadLevels(), profile };
        return out;
    },
    async setLanguage(l) {
        language = l;
        resetItemData();
        resetStoryData();
        return loadLevels();
    },
    async listDir(path) {
        const out: FileInfo[] = (await need().list(path)).map((e) => ({
            name: e.name,
            path: e.path,
            size: e.size,
            isDir: e.isDir,
        }));
        return out.sort((a, b) => Number(b.isDir) - Number(a.isDir) || a.name.localeCompare(b.name));
    },
    async openFile(path): Promise<FileOpenResult> {
        const fs = need();
        const head = await fs.read(path, 0, 4);
        if (new TextDecoder('latin1').decode(head) === 'SRSC') {
            const r = await getSrsc(path);
            const flags = await r.zlibFlags();
            const entries: EntryInfo[] = r.entries.map((e, i) => ({ ...e, zlib: flags[i]! }));
            return { kind: 'srsc', entries, warnings: r.warnings, size: r.source.size };
        }
        const e = await fs.find(path);
        const isText = /\.(DB|TXT|DEF|REF|CNF)$/i.test(path) && e !== null && e.size < 1 << 20;
        if (isText) {
            const text = decoder.decode(await fs.read(path));
            if (/\.DB$/i.test(path)) {
                const dir = path.slice(0, path.lastIndexOf('\\'));
                const deps = parseDb(text).dependencies.map((d) => ({
                    ...d,
                    resolved: resolveDependencyStem(dir, d.path),
                }));
                return { kind: 'text', text, deps };
            }
            return { kind: 'text', text };
        }
        return { kind: 'binary', size: e?.size ?? 0 };
    },
    async readEntry(path, index, offset, length, inflate): Promise<EntryBytes> {
        const r = await getSrsc(path);
        const e = r.entries[index];
        if (!e) throw new Error('no such entry');
        if (inflate && (await r.isCompressed(e))) {
            let c = inflatedCache.get(`${index}`);
            if (!c) {
                const x = await r.read(e);
                if (x.compressed) {
                    c = { data: x.data };
                    inflatedCache.set(`${index}`, c);
                }
            }
            if (c) {
                return { bytes: c.data.slice(offset, offset + length), total: c.data.length, inflated: true };
            }
        }
        return {
            bytes: (await r.source.read(e.offset + offset, Math.min(length, Math.max(e.size - offset, 0)))).slice(),
            total: e.size,
            inflated: false,
        };
    },
    async exportRoundTrip(path, max): Promise<RoundTripResult> {
        const r = await getSrsc(path);
        const idx = r.entries
            .map((e, i) => ({ e, i }))
            .filter(({ e }) => e.size > 0 && e.size < 1 << 20)
            .slice(0, max);
        const roundTrips = await srscRoundTrips(
            r,
            idx.map((x) => x.i),
        );
        const entries = [];
        for (const { e } of idx)
            entries.push({ type: e.type, id: e.id, sub: e.sub, data: (await r.readRaw(e)).slice() });
        return { file: path.slice(path.lastIndexOf('\\') + 1), entries, roundTrips };
    },
    async readLevelFile(dirPath): Promise<Uint8Array> {
        const fs = need();
        const lvlFile = (await fs.list(dirPath)).find((e) => !e.isDir && /\.LVL$/i.test(e.name));
        if (!lvlFile) throw new Error(`no .LVL file in ${dirPath}`);
        return fs.source.read(lvlFile.extent * 2048, lvlFile.size);
    },
    async loadLevel(dirPath): Promise<LevelLoadResult> {
        const fs = need();
        const lvlFile = (await fs.list(dirPath)).find((e) => !e.isDir && /\.LVL$/i.test(e.name));
        if (!lvlFile) throw new Error(`no .LVL file in ${dirPath}`);
        const reader = await SrscReader.open(sliceSource(fs.source, lvlFile.extent * 2048, lvlFile.size));
        const lvl = await readLvl(reader);
        const e = reader.entries.find((x) => x.type === LVL_TYPE_OBJECTS)!;
        const odbReaders = new Map<string, SrscReader>();
        const missingOdbs: string[] = [];
        const odbDeps: Record<string, Record<number, string>> = {};
        for (const d of lvl.header.dependencies) {
            const stem = odbStemForDependency(d.path.text);
            if (odbReaders.has(stem)) continue;
            const f = await fs.find(`${resolveDependencyStem(dirPath, d.path.text)}.ODB`);
            if (!f || f.isDir) {
                missingOdbs.push(d.path.text);
                continue;
            }
            try {
                odbReaders.set(stem, await SrscReader.open(sliceSource(fs.source, f.extent * 2048, f.size)));
            } catch {
                missingOdbs.push(d.path.text);
                continue;
            }
            odbDeps[stem] = await readOdbDeps(fs, resolveDependencyStem(dirPath, d.path.text));
        }
        return {
            path: lvlFile.path,
            file: lvlFile.name,
            objects: { type: e.type, id: e.id, sub: e.sub, size: e.size },
            lvl,
            odbs: await buildOdbIndex(odbReaders),
            odbDeps,
            missingOdbs,
        };
    },
    async exitIndex(only) {
        const fs = need();
        const levels = (await loadLevels()).filter((l) => l.present);
        const out: ExitIndexData = { levels: [] };
        if (only) {
            // One level only (the destination picker needs just the open level's exits); the whole disc takes much longer.
            const key = (s: string): string => s.toLowerCase().replace(/[^a-z0-9]/g, '');
            const l = levels.find((x) => key(x.name) === key(only) || key(destinationLevelName(x.name)) === key(only));
            if (l) out.levels.push(await levelExitData(fs, l.dirPath, l.name, await api.loadLevel(l.dirPath)));
            out.levelNames = levels.map((x) => x.name);
            return out;
        }
        for (const l of levels) {
            const r = await api.loadLevel(l.dirPath);
            out.levels.push(await levelExitData(fs, l.dirPath, l.name, r));
        }
        return out;
    },
    async loadTerrain(dirPath) {
        const fs = need();
        const { reader, header } = await openLevel(fs, dirPath);
        return loadTerrainData(fs, dirPath, reader, header);
    },
    async loadModels(dirPath, classes) {
        const fs = need();
        const { header } = await openLevel(fs, dirPath);
        return loadModelData(fs, dirPath, header, classes);
    },
    async loadItemCatalog() {
        return loadItemCatalog(need(), language);
    },
    async loadStoryEdit() {
        return loadStoryEdit(need(), language);
    },
    async scanQuestUsage() {
        const data = await loadStoryEdit(need(), language);
        const ids = new Set(data.quests.map((q) => q.cls.classId));
        const dirs = (await loadLevels()).filter((l) => l.present).map((l) => l.dirPath);
        return scanQuestUsage(ids, dirs, async (dir) => {
            const r = await api.loadLevel(dir);
            return { header: r.lvl.header, list: r.lvl.list, odbs: r.odbs };
        });
    },
    async readStoryFile(path) {
        return isoStoryFs(need(), language).read(path);
    },
    async listStoryStems() {
        return isoStoryFs(need(), language).stems();
    },
    async scanStringUsage() {
        const dirs = (await loadLevels()).filter((l) => l.present).map((l) => l.dirPath);
        return scanStringUsage(dirs, async (dir) => {
            const r = await api.loadLevel(dir);
            return { header: r.lvl.header, list: r.lvl.list, odbs: r.odbs };
        });
    },
    async loadStoryCatalog() {
        return loadStoryCatalog(need(), language);
    },
    async loadItemModels(classIds) {
        return loadItemModels(need(), language, classIds);
    },
    async refContainers(kind, stems) {
        return refContainers(need(), language, kind, stems);
    },
    async listRefs(stem, kind) {
        return listRefs(need(), language, stem, kind);
    },
    async loadRefModel(stem, id) {
        return loadRefModel(need(), language, stem, id);
    },
    async loadRefTexture(stem, id) {
        return loadRefTexture(need(), language, stem, id);
    },
    async loadRefSound(stem, id) {
        return loadRefSound(need(), language, stem, id);
    },
    async loadRefRig(stem, id) {
        return loadRefRig(need(), language, stem, id);
    },
    async loadRefClip(stem, id) {
        return loadRefClip(need(), language, stem, id);
    },
    async classModelRef(dbStem, classId) {
        return classModelRef(need(), language, dbStem, classId);
    },
    async rigForTracks(stem, tracks) {
        return rigForTracks(need(), language, stem, tracks);
    },
    async decodeTextures(path, indices) {
        const r = await getSrsc(path);
        const out: TextureResult[] = [];
        for (const e of r.entries) {
            if (e.type !== TXD_ENTRY_TYPE) continue;
            if (indices && !indices.includes(e.index)) continue;
            const t: TextureResult = { index: e.index, id: e.id, sub: e.sub };
            try {
                const img = decodeTxd((await r.read(e)).data, { upright: false });
                t.width = img.width;
                t.height = img.height;
                t.bpp = img.bpp;
                t.rgba = img.rgba;
            } catch (err) {
                t.error = err instanceof Error ? err.message : String(err);
            }
            out.push(t);
        }
        return out;
    },
};

const scope = self as unknown as {
    onmessage: ((e: MessageEvent<Request>) => void) | null;
    postMessage(msg: Response, transfer?: Transferable[]): void;
};

scope.onmessage = async (ev) => {
    const { id, method, args } = ev.data;
    try {
        const fn = api[method] as (...a: unknown[]) => Promise<unknown>;
        const result = await fn(...args);
        const transfer: Transferable[] = [];
        if (Array.isArray(result)) {
            for (const t of result as TextureResult[]) if (t?.rgba) transfer.push(t.rgba.buffer);
        } else if (result && typeof result === 'object' && 'bytes' in result) {
            transfer.push((result as EntryBytes).bytes.buffer);
        }
        scope.postMessage({ id, ok: true, result }, transfer);
    } catch (err) {
        scope.postMessage({ id, ok: false, error: err instanceof Error ? err.message : String(err) });
    }
};
