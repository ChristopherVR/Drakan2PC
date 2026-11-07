// Worker-side decoding for the scene builders: terrain layers, models and the textures they use. No three.js here.
// The functions take an open disc and return plain typed-array data that can cross the worker boundary.

import type {
    ClassFieldDef,
    Iso9660,
    LvlHeader,
    Model,
    ModelTextureRef,
    TerrainLayer,
    TxdImage,
} from '@drakan/formats';
import { standInModelRefs } from './standin.ts';
import {
    LVL_TYPE_LAYERS,
    SrscReader,
    TXD_ENTRY_TYPE,
    decodeTxd,
    dependencyPath,
    odbRuntimeClass,
    parseClassBody,
    parseDb,
    parseFieldLists,
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
    /** Disc path of the TXD it came from, `.TXD` included (the PBR pack names textures by it). */
    txd?: string;
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
    /**
     * True when the class names no model of its own (detectors, timers, generators, waypoints, level logic): normal, the
     * editor draws an icon or a marker and nothing is wrong. Absent when the class names a model (then `model` null means
     * the reference did not resolve or decode) or the class record itself was not found.
     */
    noModel?: true;
    /** Engine class id (RFL) of the class record, when it was read. */
    rfl?: number;
    /** The object's own stand-in models the request named (`ModelRequest.standIn`), echoed back. */
    standIn?: number[];
}

/** A class whose model to load; `standIn`: an object's own "Model List" / "Morphing Models" refs for a model-less class. */
export interface ModelRequest {
    classId: number;
    dbIndex: number;
    standIn?: number[];
}

const ODB_CLASS = 0x0102;
const ODB_FIELDS = 0x0103;

export { standInModelRefs };

/**
 * LOD 0 of several models as one static model (the pieces of a break-away floor share the object's origin). Texture refs
 * are merged by `(id, db)`; pieces stop being added once the vertex count would not fit 16-bit indices.
 */
export function mergeModels(parts: readonly Model[], id: number, name: string): Model | null {
    const live = parts.filter((m) => m.lod0.triCount > 0);
    if (live.length <= 1) return live[0] ?? null;
    const use: Model[] = [];
    let verts = 0;
    let tris = 0;
    for (const m of live) {
        const n = m.positions.length / 3;
        if (verts + n > 0xffff) break;
        use.push(m);
        verts += n;
        tris += m.lod0.triCount;
    }
    const textures: ModelTextureRef[] = [];
    const texAt = new Map<string, number>();
    const positions = new Float32Array(verts * 3);
    const normals = new Float32Array(verts * 3);
    const indices = new Uint16Array(tris * 3);
    const triTexture = new Uint16Array(tris);
    const uvs = new Float32Array(tris * 6);
    const min: [number, number, number] = [Infinity, Infinity, Infinity];
    const max: [number, number, number] = [-Infinity, -Infinity, -Infinity];
    let v0 = 0;
    let t = 0;
    for (const m of use) {
        positions.set(m.positions, v0 * 3);
        normals.set(m.normals.subarray(0, m.positions.length), v0 * 3);
        const local = m.textures.map((r) => {
            const k = `${r.db}:${r.id}`;
            let at = texAt.get(k);
            if (at === undefined) {
                texAt.set(k, (at = textures.length));
                textures.push(r);
            }
            return at;
        });
        const { firstTri, triCount } = m.lod0;
        for (let k = firstTri; k < firstTri + triCount; k++, t++) {
            for (let c = 0; c < 3; c++) indices[t * 3 + c] = m.indices[k * 3 + c]! + v0;
            triTexture[t] = local[m.triTexture[k]!] ?? 0;
            uvs.set(m.uvs.subarray(k * 6, k * 6 + 6), t * 6);
        }
        for (let a = 0; a < 3; a++) {
            min[a] = Math.min(min[a]!, m.bounds.min[a]!);
            max[a] = Math.max(max[a]!, m.bounds.max[a]!);
        }
        v0 += m.positions.length / 3;
    }
    return {
        id,
        name,
        shading: use[0]!.shading,
        textures,
        positions,
        normals,
        indices,
        triTexture,
        uvs,
        lods: [],
        skinned: false,
        bounds: { min, max },
        lod0: { firstTri: 0, triCount: tris },
    };
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
    txdPath?: string,
): Promise<SceneTexture[]> {
    const out: SceneTexture[] = [];
    if (!txd) return out;
    const want = new Set(ids);
    for (const e of txd.entries) {
        if (e.type !== TXD_ENTRY_TYPE || !want.has(e.id)) continue;
        const mk = `${memoKey}:${e.id}`;
        if (memo?.has(mk)) {
            const m = memo.get(mk);
            if (m) out.push({ ...m, ref: e.id | (db << 16), txd: txdPath });
            continue;
        }
        try {
            const img: TxdImage = decodeTxd((await txd.read(e)).data, { upright: false });
            const t = { ref: e.id | (db << 16), width: img.width, height: img.height, rgba: img.rgba, txd: txdPath };
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
        {
            const path = `${resolveDependencyStem(dirPath, dep)}.TXD`;
            textures.push(...(await readTextures(await open(fs, path), ids, db, undefined, '', path)));
        }
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
    classes: ModelRequest[],
): Promise<ModelData[]> {
    const out: ModelData[] = [];
    type Db = {
        odb: SrscReader;
        stem: string;
        deps: Map<number, string>;
        /** Field definitions per class id, read once the first model-less class of this ODB needs them. */
        fields?: Map<number, ClassFieldDef[]> | null;
    };
    const odbCache = new Map<number, Db | null>();
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
    const fieldDefs = async (db: Db, classId: number): Promise<ClassFieldDef[] | null> => {
        if (db.fields === undefined) {
            db.fields = null;
            const fe = db.odb.entries.find((e) => e.type === ODB_FIELDS);
            const records = db.odb.entries.filter((e) => e.type === ODB_CLASS);
            const lists = fe ? parseFieldLists((await db.odb.read(fe)).data) : null;
            if (lists && lists.length === records.length)
                db.fields = new Map(records.map((e, k) => [e.id, lists[k]!] as [number, ClassFieldDef[]]));
        }
        return db.fields?.get(classId) ?? null;
    };
    const readModel = async (base: string, modelId: number): Promise<Model | null> => {
        try {
            const mod = await cached(modCache, base, () => open(fs, `${base}.MOD`));
            return mod ? await parseModel(mod, modelId) : null;
        } catch {
            return null;
        }
    };
    const baseOf = (db: Db, modelDb: number): string | undefined => (modelDb === 0 ? db.stem : db.deps.get(modelDb));
    for (const c of classes) {
        const r: ModelData = { classId: c.classId, dbIndex: c.dbIndex, model: null, textures: [] };
        if (c.standIn) r.standIn = [...c.standIn];
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
        const rec = db.odb.entries.find((x) => x.type === ODB_CLASS && x.id === c.classId);
        if (!rec) continue;
        const raw = await db.odb.readRaw(rec);
        const nl = raw[0]! | (raw[1]! << 8);
        const at = 2 + nl;
        if (at + 6 > raw.length) continue;
        const body = raw.subarray(at);
        const rt = odbRuntimeClass(body);
        if (rt !== null) r.rfl = rt & 0xffff;
        const modelId = raw[at + 2]! | (raw[at + 3]! << 8);
        const modelDb = raw[at + 4]! | (raw[at + 5]! << 8);
        let base: string | undefined;
        if (modelId === 0) {
            r.noModel = true;
            // No class model: a break-away floor or a Morph Plug still names the models it shows in its fields.
            const defs = await fieldDefs(db, c.classId);
            const parsed = defs ? parseClassBody(body) : null;
            // An object naming its own pieces (a per-object "Model List") overrides the class default.
            const refs = c.standIn?.length ? c.standIn : defs && parsed ? standInModelRefs(defs, parsed.data) : [];
            const pdb = refs.length ? refs[0]! >>> 16 : 0;
            base = refs.length ? baseOf(db, pdb) : undefined;
            if (!base) continue;
            const parts: Model[] = [];
            for (const ref of refs) {
                if (ref >>> 16 !== pdb) continue;
                const m = await readModel(base, ref & 0xffff);
                if (m) parts.push(m);
            }
            r.model = mergeModels(parts, refs[0]! & 0xffff, parts[0]?.name ?? '');
        } else {
            base = baseOf(db, modelDb);
            if (!base) continue;
            r.model = await readModel(base, modelId);
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
            r.textures.push(...(await readTextures(txd, ids, tdb, texMemo, tbase, `${tbase}.TXD`)));
        }
    }
    return out;
}
