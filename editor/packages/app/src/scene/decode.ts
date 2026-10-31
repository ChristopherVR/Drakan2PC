// Worker-side decoding for the scene builders: terrain layers, models and the textures they use. No three.js here.
// The functions take an open disc and return plain typed-array data that can cross the worker boundary.

import type { Iso9660, LvlHeader, Model, TerrainLayer, TxdImage } from '@drakan/formats';
import {
    LVL_TYPE_LAYERS,
    SrscReader,
    TXD_ENTRY_TYPE,
    decodeTxd,
    dependencyPath,
    parseDb,
    parseModel,
    parseTerrain,
    resolveDependencyStem,
    sliceSource,
    texDb,
    texId,
    TEX_HOLE,
    TEX_INVISIBLE,
} from '@drakan/formats';

/** A decoded texture (RGBA8, row 0 = bottom, GL convention). */
export interface SceneTexture {
    /** `id | (db << 16)` as stored in the data it came from. */
    ref: number;
    width: number;
    height: number;
    rgba: Uint8Array;
}

export interface TerrainData {
    layers: TerrainLayer[];
    textures: SceneTexture[];
}

export interface ModelData {
    /** The class (ODB record id) the request named. */
    classId: number;
    dbIndex: number;
    model: Model | null;
    textures: SceneTexture[];
}

async function open(fs: Iso9660, path: string): Promise<SrscReader | null> {
    const f = await fs.find(path);
    if (!f || f.isDir) return null;
    return SrscReader.open(sliceSource(fs.source, f.extent * 2048, f.size));
}

export async function readTextures(
    txd: SrscReader | null,
    ids: Iterable<number>,
    db: number,
    memo?: Map<string, SceneTexture | null>,
    memoKey = '',
): Promise<SceneTexture[]> {
    const out: SceneTexture[] = [];
    if (!txd) return out;
    const want = new Set(ids);
    for (const e of txd.entries) {
        if (e.type !== TXD_ENTRY_TYPE || !want.has(e.id)) continue;
        const mk = `${memoKey}:${e.id}`;
        if (memo?.has(mk)) {
            const m = memo.get(mk);
            if (m) out.push({ ...m, ref: e.id | (db << 16) });
            continue;
        }
        try {
            const img: TxdImage = decodeTxd((await txd.read(e)).data, { upright: false });
            const t = { ref: e.id | (db << 16), width: img.width, height: img.height, rgba: img.rgba };
            memo?.set(mk, t);
            out.push(t);
        } catch {
            memo?.set(mk, null);
            /* undecodable texture: the builder falls back to a flat colour */
        }
    }
    return out;
}

/** Terrain layers of a level plus the textures its cells use (dependency indices as stored in the LVL header). */
export async function loadTerrainData(
    fs: Iso9660,
    dirPath: string,
    lvl: SrscReader,
    header: LvlHeader,
): Promise<TerrainData> {
    const e = lvl.entries.find((x) => x.type === LVL_TYPE_LAYERS);
    if (!e) return { layers: [], textures: [] };
    const layers = parseTerrain(await lvl.readRaw(e));
    const byDb = new Map<number, Set<number>>();
    for (const L of layers)
        for (const arr of [L.cellTexLeft, L.cellTexRight])
            for (const ref of arr) {
                if (ref === TEX_HOLE || ref === TEX_INVISIBLE) continue;
                let s = byDb.get(texDb(ref));
                if (!s) byDb.set(texDb(ref), (s = new Set()));
                s.add(texId(ref));
            }
    const textures: SceneTexture[] = [];
    for (const [db, ids] of byDb) {
        const dep = dependencyPath(header, db);
        if (!dep) continue;
        textures.push(...(await readTextures(await open(fs, `${resolveDependencyStem(dirPath, dep)}.TXD`), ids, db)));
    }
    return { layers, textures };
}

/**
 * Models for object classes. A class record (ODB 0x0102) holds, after its name, `u16 group, u16 model_id, u16 model_db`;
 * `model_db` 0 means the MOD of the same database, otherwise an index into that database's `.DB`.
 * `classes` are `(classId, dbIndex)` pairs from the object list; `odbPathOf` maps a dbIndex to the ODB stem path.
 */
export async function loadModelData(
    fs: Iso9660,
    dirPath: string,
    header: LvlHeader,
    classes: { classId: number; dbIndex: number }[],
): Promise<ModelData[]> {
    const out: ModelData[] = [];
    const odbCache = new Map<number, { odb: SrscReader; stem: string; deps: Map<number, string> } | null>();
    const modCache = new Map<string, SrscReader | null>();
    const txdCache = new Map<string, SrscReader | null>();
    const texMemo = new Map<string, SceneTexture | null>();
    const cached = async <T>(
        m: Map<string, T | null>,
        key: string,
        make: () => Promise<T | null>,
    ): Promise<T | null> => {
        if (!m.has(key)) m.set(key, await make());
        return m.get(key) ?? null;
    };
    for (const c of classes) {
        const r: ModelData = { ...c, model: null, textures: [] };
        out.push(r);
        let db = odbCache.get(c.dbIndex);
        if (db === undefined) {
            db = null;
            const dep = dependencyPath(header, c.dbIndex);
            if (dep) {
                const stem = resolveDependencyStem(dirPath, dep);
                const odb = await open(fs, `${stem}.ODB`);
                if (odb) {
                    const deps = new Map<number, string>();
                    const dbf = await fs.find(`${stem}.DB`);
                    if (dbf && !dbf.isDir) {
                        const dir = stem.slice(0, stem.lastIndexOf(String.fromCharCode(92)));
                        for (const d of parseDb(new TextDecoder('latin1').decode(await fs.read(dbf))).dependencies)
                            deps.set(d.index, resolveDependencyStem(dir, d.path));
                    }
                    db = { odb, stem, deps };
                }
            }
            odbCache.set(c.dbIndex, db);
        }
        if (!db) continue;
        const rec = db.odb.entries.find((x) => x.type === 0x0102 && x.id === c.classId);
        if (!rec) continue;
        const raw = await db.odb.readRaw(rec);
        const nl = raw[0]! | (raw[1]! << 8);
        const at = 2 + nl;
        if (at + 6 > raw.length) continue;
        const modelId = raw[at + 2]! | (raw[at + 3]! << 8);
        const modelDb = raw[at + 4]! | (raw[at + 5]! << 8);
        if (modelId === 0) continue;
        const base = modelDb === 0 ? db.stem : db.deps.get(modelDb);
        if (!base) continue;
        try {
            const mod = await cached(modCache, base, () => open(fs, `${base}.MOD`));
            r.model = mod ? await parseModel(mod, modelId) : null;
        } catch {
            r.model = null;
        }
        if (!r.model) continue;
        // Model texture refs: db 0 = the container's own TXD, otherwise a dependency of the same .DB.
        const byDb = new Map<number, Set<number>>();
        for (const t of r.model.textures) {
            let s = byDb.get(t.db);
            if (!s) byDb.set(t.db, (s = new Set()));
            s.add(t.id);
        }
        for (const [tdb, ids] of byDb) {
            const tbase = tdb === 0 ? base : db.deps.get(tdb);
            if (!tbase) continue;
            const txd = await cached(txdCache, tbase, () => open(fs, `${tbase}.TXD`));
            r.textures.push(...(await readTextures(txd, ids, tdb, texMemo, tbase)));
        }
    }
    return out;
}
