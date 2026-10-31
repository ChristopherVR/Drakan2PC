// Worker-side lookups for resource references (model, sound, texture, animation, sequence fields): which container file a
// `(db << 16) | id` value lands in, the names inside it, and the data behind a picker preview. Plain data out, no three.js.

import type { Iso9660, IsoEntry, TxdHeader } from '@drakan/formats';
import {
    ADB_ANIMATION,
    ADB_KEYS,
    SDB_DESCRIPTOR,
    SDB_SAMPLES,
    SSD_SEQUENCE,
    SrscReader,
    TXD_ENTRY_TYPE,
    adpcmSamples,
    decodeSpuAdpcm,
    decodeTxd,
    odbStemForDependency,
    parseAnimationHeader,
    parseDb,
    parseModel,
    parseModelName,
    parseSequenceName,
    parseSoundDescriptor,
    parseTxdHeader,
    resolveDependencyStem,
    sliceSource,
} from '@drakan/formats';
import { readTextures, type ModelData } from './scene/decode.ts';

export type RefKind = 'model' | 'sound' | 'texture' | 'animation' | 'sequence';

export interface RefEntry {
    id: number;
    /** The name stored in the container; empty for textures (they have none). */
    name: string;
    /** Short facts: texture size, sound length and subtitle, frame count, ... */
    detail: string;
    /** Texture size (textures only). */
    w?: number;
    h?: number;
    /** Sound length in seconds (sounds only). */
    seconds?: number;
}

export interface RefSound {
    name: string;
    text: string;
    rate: number;
    pcm: Int16Array;
}

export interface RefTexture {
    width: number;
    height: number;
    bpp: number;
    /** RGBA8, top row first. */
    rgba: Uint8Array;
}

const EXT: Record<RefKind, string> = { model: 'MOD', sound: 'SDB', texture: 'TXD', animation: 'ADB', sequence: 'SSD' };
const BACKSLASH = String.fromCharCode(92);

interface Disc {
    iso: Iso9660;
    language: string;
    /** Stem (upper case, no extension) -> extension -> file. Stems are unique across a language tree. */
    files: Map<string, Map<string, IsoEntry>>;
    readers: Map<string, Promise<SrscReader | null>>;
    lists: Map<string, Promise<RefEntry[] | null>>;
    deps: Map<string, Promise<Map<number, string>>>;
}

let disc: Disc | null = null;

async function discFor(iso: Iso9660, language: string): Promise<Disc> {
    if (disc && disc.iso === iso && disc.language === language) return disc;
    const files = new Map<string, Map<string, IsoEntry>>();
    for (const e of await iso.walk(language)) {
        const dot = e.name.lastIndexOf('.');
        if (dot <= 0) continue;
        const stem = e.name.slice(0, dot).toUpperCase();
        let m = files.get(stem);
        if (!m) files.set(stem, (m = new Map()));
        m.set(e.name.slice(dot + 1).toUpperCase(), e);
    }
    return (disc = {
        iso,
        language,
        files,
        readers: new Map(),
        lists: new Map(),
        deps: new Map(),
    });
}

function reader(d: Disc, stem: string, ext: string): Promise<SrscReader | null> {
    const key = `${stem}.${ext}`;
    let r = d.readers.get(key);
    if (!r) {
        const f = d.files.get(stem.toUpperCase())?.get(ext);
        r = f
            ? SrscReader.open(sliceSource(d.iso.source, f.extent * 2048, f.size)).catch(() => null)
            : Promise.resolve(null);
        d.readers.set(key, r);
    }
    return r;
}

/** The `.DB` dependency list of a container stem: index -> container stem (the base of `db` indices inside it). */
function depsOf(d: Disc, stem: string): Promise<Map<number, string>> {
    let p = d.deps.get(stem);
    if (!p) {
        p = (async () => {
            const out = new Map<number, string>();
            const f = d.files.get(stem.toUpperCase())?.get('DB');
            if (!f) return out;
            const dir = f.path.slice(0, f.path.lastIndexOf(BACKSLASH));
            const text = new TextDecoder('latin1').decode(await d.iso.read(f));
            for (const dep of parseDb(text).dependencies)
                out.set(dep.index, odbStemForDependency(resolveDependencyStem(dir, dep.path)));
            return out;
        })();
        d.deps.set(stem, p);
    }
    return p;
}

/** Which of `stems` have a container file for `kind` on the disc. */
export async function refContainers(
    iso: Iso9660,
    language: string,
    kind: RefKind,
    stems: string[],
): Promise<boolean[]> {
    const d = await discFor(iso, language);
    return stems.map((s) => d.files.get(s.toUpperCase())?.has(EXT[kind]) ?? false);
}

const secs = (bytes: number, rate: number): number => adpcmSamples(bytes) / rate;
const fmtSecs = (s: number): string => (s < 10 ? s.toFixed(2) : s.toFixed(1)) + ' s';

async function buildList(d: Disc, stem: string, kind: RefKind): Promise<RefEntry[] | null> {
    const r = await reader(d, stem, EXT[kind]);
    if (!r) return null;
    const out: RefEntry[] = [];
    const seen = new Set<number>();
    switch (kind) {
        case 'model': {
            const skinned = new Set<number>();
            for (const e of r.entries) if (e.type === 0x208) skinned.add(e.id);
            for (const e of r.entries) {
                if (e.type !== 0x200) continue;
                let name = '';
                let detail = '';
                try {
                    const info = parseModelName(await r.readRaw(e));
                    name = info.name;
                    const n = info.textures.length;
                    detail = `${n} texture${n === 1 ? '' : 's'}${skinned.has(e.id) ? ', skinned' : ''}`;
                } catch {
                    detail = 'unreadable';
                }
                out.push({ id: e.id, name, detail });
            }
            break;
        }
        case 'sound': {
            const sample = new Map<number, number>();
            for (const e of r.entries) if (e.type === SDB_SAMPLES && !sample.has(e.id)) sample.set(e.id, e.size);
            for (const e of r.entries) {
                if (e.type !== SDB_DESCRIPTOR || seen.has(e.id)) continue;
                seen.add(e.id);
                try {
                    const info = parseSoundDescriptor(await r.readRaw(e));
                    const bytes = sample.get(e.id);
                    const seconds = bytes === undefined ? undefined : secs(bytes, info.rate);
                    out.push({
                        id: e.id,
                        name: info.name,
                        detail: [seconds === undefined ? '' : fmtSecs(seconds), info.text].filter(Boolean).join('  '),
                        ...(seconds === undefined ? {} : { seconds }),
                    });
                } catch {
                    out.push({ id: e.id, name: '', detail: 'unreadable' });
                }
            }
            break;
        }
        case 'texture': {
            for (const e of r.entries) {
                if (e.type !== TXD_ENTRY_TYPE || seen.has(e.id)) continue;
                seen.add(e.id);
                let hd: TxdHeader | null = null;
                try {
                    hd = parseTxdHeader(await r.readRaw(e, 0x40));
                } catch {
                    /* leave the size out */
                }
                out.push({
                    id: e.id,
                    name: '',
                    detail: hd ? `${hd.width} x ${hd.height}, ${hd.bpp} bit` : 'unreadable',
                    ...(hd ? { w: hd.width, h: hd.height } : {}),
                });
            }
            break;
        }
        case 'animation': {
            const keys = new Map<number, number>();
            for (const e of r.entries) if (e.type === ADB_KEYS && !keys.has(e.id)) keys.set(e.id, e.size);
            for (const e of r.entries) {
                if (e.type !== ADB_ANIMATION || seen.has(e.id)) continue;
                seen.add(e.id);
                try {
                    const a = parseAnimationHeader(await r.readRaw(e));
                    const kb = keys.get(e.id);
                    out.push({
                        id: e.id,
                        name: a.name,
                        detail: [a.frames ? `${a.frames} frames` : '', kb ? `${(kb / 1024).toFixed(1)} KB keys` : '']
                            .filter(Boolean)
                            .join(', '),
                    });
                } catch {
                    out.push({ id: e.id, name: '', detail: 'unreadable' });
                }
            }
            break;
        }
        case 'sequence': {
            for (const e of r.entries) {
                if (e.type !== SSD_SEQUENCE || seen.has(e.id)) continue;
                seen.add(e.id);
                try {
                    out.push({ id: e.id, name: parseSequenceName(await r.readRaw(e)), detail: `${e.size} bytes` });
                } catch {
                    out.push({ id: e.id, name: '', detail: 'unreadable' });
                }
            }
            break;
        }
    }
    return out;
}

/** Entries of one container (names and facts), or null when the disc has no such container. Cached. */
export async function listRefs(
    iso: Iso9660,
    language: string,
    stem: string,
    kind: RefKind,
): Promise<RefEntry[] | null> {
    const d = await discFor(iso, language);
    const key = `${stem.toUpperCase()}:${kind}`;
    let p = d.lists.get(key);
    if (!p) {
        p = buildList(d, stem, kind);
        d.lists.set(key, p);
    }
    return p;
}

/** Model `id` of container `stem` with its textures (a texture's `db` 0 is the model container's own TXD, else its `.DB`). */
export async function loadRefModel(
    iso: Iso9660,
    language: string,
    stem: string,
    id: number,
): Promise<ModelData | null> {
    const d = await discFor(iso, language);
    const mod = await reader(d, stem, 'MOD');
    if (!mod) return null;
    let model;
    try {
        model = await parseModel(mod, id);
    } catch {
        return null;
    }
    const out: ModelData = { classId: id, dbIndex: 0, model, textures: [] };
    if (!model) return out;
    const deps = await depsOf(d, stem);
    const byDb = new Map<number, Set<number>>();
    for (const t of model.textures) {
        let s = byDb.get(t.db);
        if (!s) byDb.set(t.db, (s = new Set()));
        s.add(t.id);
    }
    for (const [db, ids] of byDb) {
        const tstem = db === 0 ? stem : deps.get(db);
        if (!tstem) continue;
        out.textures.push(...(await readTextures(await reader(d, tstem, 'TXD'), ids, db, undefined, tstem)));
    }
    return out;
}

/** One texture, top row first. */
export async function loadRefTexture(
    iso: Iso9660,
    language: string,
    stem: string,
    id: number,
): Promise<RefTexture | null> {
    const d = await discFor(iso, language);
    const txd = await reader(d, stem, 'TXD');
    const e = txd?.entries.find((x) => x.type === TXD_ENTRY_TYPE && x.id === id);
    if (!txd || !e) return null;
    try {
        const img = decodeTxd((await txd.read(e)).data);
        return { width: img.width, height: img.height, bpp: img.bpp, rgba: img.rgba };
    } catch {
        return null;
    }
}

/** A sound decoded to 16-bit PCM (SPU ADPCM, sample rate from the descriptor). */
export async function loadRefSound(iso: Iso9660, language: string, stem: string, id: number): Promise<RefSound | null> {
    const d = await discFor(iso, language);
    const sdb = await reader(d, stem, 'SDB');
    const desc = sdb?.entries.find((x) => x.type === SDB_DESCRIPTOR && x.id === id);
    if (!sdb || !desc) return null;
    const data =
        sdb.entries.find((x) => x.type === SDB_SAMPLES && x.id === id && x.sub === desc.sub) ??
        sdb.entries.find((x) => x.type === SDB_SAMPLES && x.id === id);
    if (!data) return null;
    const info = parseSoundDescriptor(await sdb.readRaw(desc));
    return { name: info.name, text: info.text, rate: info.rate, pcm: decodeSpuAdpcm(await sdb.readRaw(data)) };
}
