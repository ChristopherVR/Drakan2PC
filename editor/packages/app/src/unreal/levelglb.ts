// Level to glTF 2.0 for Unreal Engine 5. Pure: takes decoded data (terrain layers, class models, rigs, clips, placed
// objects) and returns a .glb plus the PNG textures. Conventions and the import script contract:
// docs/reference/unreal-export.md. Coordinates: ./coords.ts.

import type { Model, Ps2Anim, Rig } from '@drakan/formats';
import { mat12ToQuat } from '@drakan/formats';
import type { SceneTexture, TerrainData } from '../scene/decode.ts';
import { helperGroupOf, type HelperGroup } from '../scene/instances.ts';
import { terrainBuckets } from '../scene/terrain.ts';
import {
    GAME_UNIT_M,
    MODEL_UNIT_M,
    UE_CM_PER_GAME_UNIT,
    UE_FROM_GLTF,
    docRotToQuat,
    gamePosToGltf,
    gltfTransformToUe,
    type UeTransform,
} from './coords.ts';
import { COMPONENT, GltfBuilder, TARGET, encodePng, type Json } from './glb.ts';

// ---- input and options ---------------------------------------------------------------------------------------------

export interface ExportClip {
    id: number;
    /** Name stored in the ADB. */
    name: string;
    /** ADB container stem the clip comes from. */
    stem: string;
    anim: Ps2Anim;
}

/** A class with a model, as the viewport loads it (`loadModels` / `loadRefRig` results). */
export interface ExportClass {
    /** The class resource string (`dbIndex << 16 | classId`), the key objects use. */
    resource: string;
    name: string;
    model: Model;
    textures: readonly SceneTexture[];
    /** Present for skinned characters. */
    rig?: Rig | null;
    clips?: readonly ExportClip[];
}

/** An object as the editor document stores it (a subset of `LevelObject`). */
export interface ExportObject {
    id: number;
    class: string;
    type: number;
    kind: string;
    resource: string;
    transform: { pos: readonly number[]; rot: readonly number[] };
    scale?: readonly number[];
    props?: Record<string, string | number | boolean>;
    links?: readonly number[];
    origin?: number;
    db?: string;
    fields?: readonly {
        slot: number;
        fieldIndex: number;
        type: number;
        name: string;
        value: number | string | number[];
    }[];
}

export interface LevelExportInput {
    level: string;
    terrain?: TerrainData | null;
    classes: readonly ExportClass[];
    objects: readonly ExportObject[];
}

/** What a texture is, for the material resolver. */
export interface TextureSource {
    /** `terrain:<ref>` or `class:<resource>:<textureIndex>`. */
    key: string;
    kind: 'terrain' | 'model';
    /** Class name for models, `Terrain` for terrain. */
    scope: string;
    /** Stored texture reference (`id | db << 16`). */
    ref: number;
    /** Disc path of the source TXD (`LEVELS\LEVEL1NA\LEVEL1NA.TXD`) when known. */
    txd?: string;
}

export interface ResolvedTexture {
    name: string;
    png: Uint8Array;
}

/** A UE5-style PBR material another tool produced (`M_<Name>`, `T_<Name>_BC / _N / _ORM`). */
export interface ResolvedMaterial {
    /** Material asset name, e.g. `M_Stone`. */
    name: string;
    baseColor?: ResolvedTexture;
    normal?: ResolvedTexture;
    /** Occlusion in R, roughness in G, metallic in B (glTF packing). */
    orm?: ResolvedTexture;
    blendMode?: 'Opaque' | 'Masked' | 'Translucent' | 'Additive';
    opacityMaskClip?: number | null;
    /** Surface two-sidedness from the material data; models stay double sided (per-polygon flags are not exposed). */
    twoSided?: boolean;
    emissive?: boolean;
}

export type MaterialResolver = (src: TextureSource) => ResolvedMaterial | null;

export type RootMotionMode = 'keep' | 'strip' | 'separate';

export interface ExportOptions {
    /** Root joint translation in animations: keep it, play in place (horizontal part removed), or move it to a separate node track. */
    rootMotion: RootMotionMode;
    /** One EXT_mesh_gpu_instancing node per class instead of one node per object. */
    instancing: boolean;
    /** Textures next to the glb (`textures/T_*_BC.png`) or inside it. */
    textures: 'external' | 'embedded';
    /** Terrain chunk edge in cells. */
    chunkCells: number;
    includeTerrain: boolean;
    includeModels: boolean;
    includeSkinned: boolean;
    /** Objects without a model (triggers, markers) become empty nodes carrying their extras. */
    includeMarkers: boolean;
    /** Editor helper classes (trigger boxes, sound emitters, waypoints...) keep their model; off = empty nodes with extras. */
    helperModels: boolean;
    /** Clips per skinned class (0 = none). */
    maxClipsPerClass: number;
    /** A bind-pose static mesh per skinned class so instance nodes show something in viewers. */
    skinnedBindPose: boolean;
    resolver?: MaterialResolver | null;
}

export const DEFAULT_EXPORT_OPTIONS: ExportOptions = {
    rootMotion: 'separate',
    instancing: false,
    textures: 'external',
    chunkCells: 16,
    includeTerrain: true,
    includeModels: true,
    includeSkinned: true,
    includeMarkers: true,
    helperModels: false,
    maxClipsPerClass: 24,
    skinnedBindPose: true,
};

export interface ExportReport {
    terrainChunks: number;
    terrainTriangles: number;
    staticClasses: number;
    skinnedClasses: number;
    instances: number;
    markers: number;
    animations: number;
    textures: number;
    materials: number;
    lights: number;
    cameras: number;
    warnings: string[];
}

export interface LevelExport {
    glb: Uint8Array;
    /** Extra files by relative path (`textures/T_Name_BC.png`); empty when textures are embedded. */
    files: Map<string, Uint8Array>;
    /** The glTF JSON (for inspection and tests). */
    json: Json;
    report: ExportReport;
}

export const EXTRAS_KEY = 'drakan';
export const EXTRAS_VERSION = 1;

// ---- names ---------------------------------------------------------------------------------------------------------

/** UE asset-safe identifier: letters, digits and underscores. */
export function ident(s: string): string {
    const t = s.replace(/[^A-Za-z0-9_]+/g, '_').replace(/^_+|_+$/g, '');
    return t || 'Unnamed';
}

class Namer {
    private readonly used = new Map<string, number>();
    unique(base: string): string {
        const n = this.used.get(base) ?? 0;
        this.used.set(base, n + 1);
        return n === 0 ? base : `${base}_${n}`;
    }
}

// ---- extras --------------------------------------------------------------------------------------------------------

/** The extras object (under `extras.drakan`) of an instance node: what a UE importer script needs to spawn the actor. */
export function instanceExtras(
    o: ExportObject,
    asset: { staticMesh?: string; skeletalMesh?: string } | null,
    ue: UeTransform,
): Json {
    const d: Json = {
        type: 'instance',
        id: o.id,
        class: o.class,
        resource: o.resource,
        kind: o.kind,
        typeId: o.type,
        gamePos: [...o.transform.pos],
        rot: [...o.transform.rot],
        props: { ...(o.props ?? {}) },
        ue: { location: ue.location, quat: ue.quat, scale: ue.scale },
    };
    if (o.db) d.db = o.db;
    if (o.origin !== undefined) d.origin = o.origin;
    if (o.links?.length) d.links = [...o.links];
    if (o.scale) d.gameScale = [...o.scale];
    if (o.fields?.length)
        d.fields = o.fields.map((f) => ({
            slot: f.slot,
            index: f.fieldIndex,
            type: f.type,
            name: f.name,
            value: f.value,
        }));
    if (asset?.staticMesh) d.staticMesh = asset.staticMesh;
    if (asset?.skeletalMesh) d.skeletalMesh = asset.skeletalMesh;
    return { [EXTRAS_KEY]: d };
}

// ---- geometry ------------------------------------------------------------------------------------------------------

interface Prim {
    /** Index into `Model.textures` (or the terrain texture ref). */
    tex: number;
    pos: number[];
    nrm: number[];
    uv: number[];
    idx: number[];
    jnt?: number[];
    wgt?: number[];
}

/** Unit normal of triangle `t` (fallback for vertices whose stored normal is zero); +y for a degenerate triangle. */
function faceNormal(model: Model, t: number): [number, number, number] {
    const P = model.positions;
    const a = model.indices[t * 3]! * 3;
    const b = model.indices[t * 3 + 1]! * 3;
    const c = model.indices[t * 3 + 2]! * 3;
    const ux = P[b]! - P[a]!;
    const uy = P[b + 1]! - P[a + 1]!;
    const uz = P[b + 2]! - P[a + 2]!;
    const vx = P[c]! - P[a]!;
    const vy = P[c + 1]! - P[a + 1]!;
    const vz = P[c + 2]! - P[a + 2]!;
    const n: [number, number, number] = [uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx];
    const l = Math.hypot(...n);
    return l > 1e-12 ? [n[0] / l, n[1] / l, n[2] / l] : [0, 1, 0];
}

/** Unit quaternion with components clamped to [-1, 1] (glTF validators reject 1.0000001). */
export function normQuat(q: readonly number[]): [number, number, number, number] {
    const l = Math.hypot(q[0]!, q[1]!, q[2]!, q[3]!) || 1;
    const f = (v: number): number => Math.max(-1, Math.min(1, v / l));
    return [f(q[0]!), f(q[1]!), f(q[2]!), f(q[3]!)];
}

/** LOD 0 of a model as indexed primitives, one per texture, scaled to metres; UVs flipped to the glTF origin. */
export function modelPrims(model: Model, scale: number, rig: Rig | null): Prim[] {
    const { firstTri, triCount } = model.lod0;
    const by = new Map<number, number[]>();
    for (let t = firstTri; t < firstTri + triCount; t++) {
        const tx = model.triTexture[t]!;
        let l = by.get(tx);
        if (!l) by.set(tx, (l = []));
        l.push(t);
    }
    const out: Prim[] = [];
    for (const [tex, tris] of [...by].sort((a, b) => a[0] - b[0])) {
        const p: Prim = { tex, pos: [], nrm: [], uv: [], idx: [] };
        if (rig) {
            p.jnt = [];
            p.wgt = [];
        }
        const seen = new Map<string, number>();
        for (const t of tris) {
            for (let k = 0; k < 3; k++) {
                const v = model.indices[t * 3 + k]!;
                const u = model.uvs[t * 6 + k * 2]!;
                const w = model.uvs[t * 6 + k * 2 + 1]!;
                const key = `${v}:${u}:${w}`;
                let n = seen.get(key);
                if (n === undefined) {
                    n = p.pos.length / 3;
                    seen.set(key, n);
                    p.pos.push(
                        model.positions[v * 3]! * scale,
                        model.positions[v * 3 + 1]! * scale,
                        model.positions[v * 3 + 2]! * scale,
                    );
                    const nx = model.normals[v * 3]!;
                    const ny = model.normals[v * 3 + 1]!;
                    const nz = model.normals[v * 3 + 2]!;
                    const nl = Math.hypot(nx, ny, nz);
                    if (nl > 1e-4) p.nrm.push(nx / nl, ny / nl, nz / nl);
                    else p.nrm.push(...faceNormal(model, t));
                    p.uv.push(u, 1 - w);
                    if (rig) {
                        for (let i = 0; i < 4; i++) {
                            p.jnt!.push(rig.jointIndex[v * 4 + i]!);
                            p.wgt!.push(rig.jointWeight[v * 4 + i]!);
                        }
                    }
                }
                p.idx.push(n);
            }
        }
        out.push(p);
    }
    return out;
}

/** Terrain primitives of one chunk: indexed, UV flipped, normals smoothed over shared positions of the chunk. */
function terrainChunkPrims(buckets: { ref: number; pos: number[]; uv: number[] }[]): (Prim & { ref: number })[] {
    const acc = new Map<string, [number, number, number]>();
    const key = (x: number, y: number, z: number): string =>
        `${Math.round(x * 1e4)},${Math.round(y * 1e4)},${Math.round(z * 1e4)}`;
    for (const b of buckets) {
        for (let i = 0; i + 8 < b.pos.length; i += 9) {
            const p = b.pos;
            const ux = p[i + 3]! - p[i]!;
            const uy = p[i + 4]! - p[i + 1]!;
            const uz = p[i + 5]! - p[i + 2]!;
            const vx = p[i + 6]! - p[i]!;
            const vy = p[i + 7]! - p[i + 1]!;
            const vz = p[i + 8]! - p[i + 2]!;
            const n: [number, number, number] = [uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx];
            for (let k = 0; k < 3; k++) {
                const kk = key(p[i + k * 3]!, p[i + k * 3 + 1]!, p[i + k * 3 + 2]!);
                const a = acc.get(kk);
                if (a) {
                    a[0] += n[0];
                    a[1] += n[1];
                    a[2] += n[2];
                } else acc.set(kk, [...n]);
            }
        }
    }
    return buckets.map((b) => {
        const p: Prim & { ref: number } = { ref: b.ref, tex: b.ref, pos: [], nrm: [], uv: [], idx: [] };
        const seen = new Map<string, number>();
        for (let i = 0; i < b.pos.length / 3; i++) {
            const x = b.pos[i * 3]!;
            const y = b.pos[i * 3 + 1]!;
            const z = b.pos[i * 3 + 2]!;
            const u = b.uv[i * 2]!;
            const v = b.uv[i * 2 + 1]!;
            const k = `${x},${y},${z},${u},${v}`;
            let n = seen.get(k);
            if (n === undefined) {
                n = p.pos.length / 3;
                seen.set(k, n);
                p.pos.push(x, y, z);
                const a = acc.get(key(x, y, z)) ?? [0, 1, 0];
                const l = Math.hypot(a[0], a[1], a[2]) || 1;
                p.nrm.push(a[0] / l, a[1] / l, a[2] / l);
                p.uv.push(u, 1 - v);
            }
            p.idx.push(n);
        }
        return p;
    });
}

// ---- builder -------------------------------------------------------------------------------------------------------

function fnv(b: Uint8Array): number {
    let h = 0x811c9dc5;
    for (let i = 0; i < b.length; i++) h = Math.imul(h ^ b[i]!, 0x01000193);
    return h >>> 0;
}

const hasTranslucentPixels = (rgba: Uint8Array): boolean => {
    for (let i = 3; i < rgba.length; i += 4) if (rgba[i]! < 128) return true;
    return false;
};

interface Img {
    texture: number;
    name: string;
}

class Ctx {
    readonly b = new GltfBuilder();
    readonly files = new Map<string, Uint8Array>();
    readonly names = new Namer();
    readonly report: ExportReport = {
        terrainChunks: 0,
        terrainTriangles: 0,
        staticClasses: 0,
        skinnedClasses: 0,
        instances: 0,
        markers: 0,
        animations: 0,
        textures: 0,
        materials: 0,
        lights: 0,
        cameras: 0,
        warnings: [],
    };
    private readonly images = new Map<string, Img>();
    private readonly materials = new Map<string, number>();
    private sampler = -1;

    constructor(readonly opt: ExportOptions) {}

    private samplerIndex(): number {
        if (this.sampler < 0) {
            // 9729 linear, 9987 linear mipmap linear, 10497 repeat
            this.b.doc.samplers.push({ magFilter: 9729, minFilter: 9987, wrapS: 10497, wrapT: 10497 });
            this.sampler = this.b.doc.samplers.length - 1;
        }
        return this.sampler;
    }

    private async addImage(cacheKey: string, name: string, make: () => Promise<Uint8Array>): Promise<Img> {
        const hit = this.images.get(cacheKey);
        if (hit) return hit;
        const png = await make();
        const unique = this.names.unique(name);
        const img: Json = { name: unique, mimeType: 'image/png' };
        if (this.opt.textures === 'embedded') img.bufferView = this.b.addBufferView(png);
        else {
            const path = `textures/${unique}.png`;
            this.files.set(path, png);
            img.uri = path;
        }
        this.b.doc.images.push(img);
        this.b.doc.textures.push({ sampler: this.samplerIndex(), source: this.b.doc.images.length - 1, name: unique });
        const out = { texture: this.b.doc.textures.length - 1, name: unique };
        this.images.set(cacheKey, out);
        this.report.textures++;
        return out;
    }

    /** A material index for one texture (decoded RGBA) of a terrain or model scope. */
    async material(src: TextureSource, tex: SceneTexture | null, forModel: boolean): Promise<number> {
        const resolved = this.opt.resolver?.(src) ?? null;
        const sig = tex ? `${tex.width}x${tex.height}:${fnv(tex.rgba)}` : 'none';
        const mask = resolved ? resolved.blendMode === 'Masked' : forModel && !!tex && hasTranslucentPixels(tex.rgba);
        const ck = resolved ? `res:${resolved.name}:${mask}` : `${sig}:${forModel}:${mask}`;
        const hit = this.materials.get(ck);
        if (hit !== undefined) return hit;
        const base = ident(src.scope);
        const m: Json = {
            pbrMetallicRoughness: { metallicFactor: 0, roughnessFactor: 1 },
            doubleSided: forModel || !resolved || resolved.twoSided !== false,
        };
        let matName: string;
        let bcName = '';
        if (resolved) {
            matName = ident(resolved.name);
            const pbr = m.pbrMetallicRoughness;
            if (resolved.baseColor) {
                const bc = resolved.baseColor;
                const i = await this.addImage(`png:${bc.name}`, ident(bc.name), async () => bc.png);
                pbr.baseColorTexture = { index: i.texture };
                bcName = i.name;
            } else if (tex) {
                const i = await this.addImage(`bc:${sig}`, `T_${base}_${src.ref.toString(16)}_BC`, () =>
                    encodePng(tex.rgba, tex.width, tex.height),
                );
                pbr.baseColorTexture = { index: i.texture };
                bcName = i.name;
            }
            if (resolved.normal) {
                const n = resolved.normal;
                const i = await this.addImage(`png:${n.name}`, ident(n.name), async () => n.png);
                m.normalTexture = { index: i.texture };
            }
            if (resolved.orm) {
                const o = resolved.orm;
                const i = await this.addImage(`png:${o.name}`, ident(o.name), async () => o.png);
                pbr.metallicRoughnessTexture = { index: i.texture };
                m.occlusionTexture = { index: i.texture };
                pbr.metallicFactor = 1;
            }
        } else if (tex) {
            const i = await this.addImage(`bc:${sig}`, `T_${base}_${src.ref.toString(16)}_BC`, () =>
                encodePng(tex.rgba, tex.width, tex.height),
            );
            m.pbrMetallicRoughness.baseColorTexture = { index: i.texture };
            bcName = i.name;
            matName = `M_${i.name.replace(/^T_/, '').replace(/_BC(_\d+)?$/, '')}`;
        } else {
            m.pbrMetallicRoughness.baseColorFactor = [0.6, 0.6, 0.6, 1];
            matName = 'M_Grey';
        }
        if (mask) {
            m.alphaMode = 'MASK';
            m.alphaCutoff = resolved?.opacityMaskClip ?? 0.5;
        } else if (resolved && (resolved.blendMode === 'Translucent' || resolved.blendMode === 'Additive')) {
            m.alphaMode = 'BLEND';
        }
        if (resolved?.emissive && m.pbrMetallicRoughness.baseColorTexture) {
            m.emissiveTexture = { index: m.pbrMetallicRoughness.baseColorTexture.index };
            m.emissiveFactor = [1, 1, 1];
        }
        m.name = this.names.unique(matName);
        m.extras = {
            [EXTRAS_KEY]: { material: m.name, baseColor: bcName || null, source: src.key, resolved: !!resolved },
        };
        this.b.doc.materials.push(m);
        const idx = this.b.doc.materials.length - 1;
        this.materials.set(ck, idx);
        this.report.materials++;
        return idx;
    }

    /** Write primitives as a glTF mesh. */
    addMesh(name: string, prims: (Prim & { material: number })[]): number {
        const b = this.b;
        const primitives = prims
            .filter((p) => p.idx.length)
            .map((p) => {
                const attributes: Json = {
                    POSITION: b.addAccessor(new Float32Array(p.pos), 'VEC3', {
                        target: TARGET.ARRAY_BUFFER,
                        bounds: true,
                    }),
                    NORMAL: b.addAccessor(new Float32Array(p.nrm), 'VEC3', { target: TARGET.ARRAY_BUFFER }),
                    TEXCOORD_0: b.addAccessor(new Float32Array(p.uv), 'VEC2', { target: TARGET.ARRAY_BUFFER }),
                };
                if (p.jnt && p.wgt) {
                    attributes.JOINTS_0 = b.addAccessor(new Uint16Array(p.jnt), 'VEC4', {
                        target: TARGET.ARRAY_BUFFER,
                    });
                    attributes.WEIGHTS_0 = b.addAccessor(new Float32Array(p.wgt), 'VEC4', {
                        target: TARGET.ARRAY_BUFFER,
                    });
                }
                const idx = p.pos.length / 3 > 65535 ? new Uint32Array(p.idx) : new Uint16Array(p.idx);
                return {
                    attributes,
                    indices: b.addAccessor(idx, 'SCALAR', { target: TARGET.ELEMENT_ARRAY_BUFFER }),
                    material: p.material,
                    mode: 4,
                };
            });
        b.doc.meshes.push({ name, primitives });
        return b.doc.meshes.length - 1;
    }
}

const sameSign = (a: readonly number[], b: readonly number[]): boolean =>
    a[0]! * b[0]! + a[1]! * b[1]! + a[2]! * b[2]! + a[3]! * b[3]! >= 0;

/** Column-major 4x4 of a row-vector `Mat12` (v' = v R + t), with the translation scaled to metres. */
export function mat12ToGltfMat4(m: ArrayLike<number>, tScale: number): number[] {
    // Row-vector rows are the columns of the transposed (column-vector) matrix glTF wants.
    return [
        m[0]!,
        m[1]!,
        m[2]!,
        0,
        m[3]!,
        m[4]!,
        m[5]!,
        0,
        m[6]!,
        m[7]!,
        m[8]!,
        0,
        m[9]! * tScale,
        m[10]! * tScale,
        m[11]! * tScale,
        1,
    ];
}

interface SkeletonNodes {
    joints: number[];
    rootNode: number;
    /** Optional node holding the separated root motion track. */
    motionNode: number;
}

function addAnimation(
    c: Ctx,
    cls: { name: string; rig: Rig },
    clip: ExportClip,
    nodes: SkeletonNodes,
    skeletalName: string,
): boolean {
    const { rig } = cls;
    const a = clip.anim;
    if (a.tracks.length !== rig.jointCount) return false;
    const b = c.b;
    const samplers: Json[] = [];
    const channels: Json[] = [];
    const inputs = new Map<string, number>();
    const sampler = (times: number[], values: number[], comps: 3 | 4, path: string, node: number): void => {
        const sig = times.join(',');
        let input = inputs.get(sig);
        if (input === undefined) {
            input = b.addAccessor(new Float32Array(times), 'SCALAR', { bounds: true });
            inputs.set(sig, input);
        }
        const output = b.addAccessor(new Float32Array(values), comps === 3 ? 'VEC3' : 'VEC4');
        samplers.push({ input, output, interpolation: 'LINEAR' });
        channels.push({ sampler: samplers.length - 1, target: { node, path } });
    };
    const mode = c.opt.rootMotion;
    let motion: { t: number[]; v: number[] } | null = null;
    for (let j = 0; j < rig.jointCount; j++) {
        const tr = a.tracks[j]!;
        const keys: typeof tr.keys = [];
        for (const k of tr.keys) if (!keys.length || k.time > keys[keys.length - 1]!.time + 1e-6) keys.push(k);
        if (!keys.length) continue;
        const times = keys.map((k) => k.time);
        const rot: number[] = [];
        let prev: number[] | null = null;
        for (const k of keys) {
            const l = Math.hypot(...k.q) || 1;
            let q = [k.q[0] / l, k.q[1] / l, k.q[2] / l, k.q[3] / l];
            if (prev && !sameSign(prev, q)) q = q.map((v) => -v);
            rot.push(...q);
            prev = q;
        }
        sampler(times, rot, 4, 'rotation', nodes.joints[j]!);
        if (tr.hasPos && keys.every((k) => k.pos)) {
            const isRoot = rig.parents[j] === -1;
            const first = keys[0]!.pos!;
            const pos: number[] = [];
            const mot: number[] = [];
            for (const k of keys) {
                const p = k.pos!;
                if (isRoot && mode !== 'keep') {
                    pos.push(first[0] * MODEL_UNIT_M, p[1] * MODEL_UNIT_M, first[2] * MODEL_UNIT_M);
                    mot.push((p[0] - first[0]) * MODEL_UNIT_M, 0, (p[2] - first[2]) * MODEL_UNIT_M);
                } else pos.push(p[0] * MODEL_UNIT_M, p[1] * MODEL_UNIT_M, p[2] * MODEL_UNIT_M);
            }
            sampler(times, pos, 3, 'translation', nodes.joints[j]!);
            if (isRoot && mode === 'separate' && nodes.motionNode >= 0 && !motion) motion = { t: times, v: mot };
        }
    }
    if (motion && nodes.motionNode >= 0) sampler(motion.t, motion.v, 3, 'translation', nodes.motionNode);
    if (!channels.length) return false;
    b.doc.animations.push({
        name: `${cls.name}@${ident(clip.name || `clip${clip.id}`)}`,
        samplers,
        channels,
        extras: {
            [EXTRAS_KEY]: {
                type: 'clip',
                class: cls.name,
                skeletalMesh: skeletalName,
                clipId: clip.id,
                clipName: clip.name,
                stem: clip.stem,
                duration: a.duration,
                loops: (a.flags & 1) === 0,
                rootMotion: mode,
                rootMotionNode:
                    mode === 'separate' && nodes.motionNode >= 0 ? b.doc.nodes[nodes.motionNode].name : null,
            },
        },
    });
    c.report.animations++;
    return true;
}

/** Build the glb of a level. */
export async function buildLevelGltf(
    input: LevelExportInput,
    options: Partial<ExportOptions> = {},
): Promise<LevelExport> {
    const opt: ExportOptions = { ...DEFAULT_EXPORT_OPTIONS, ...options };
    const c = new Ctx(opt);
    const b = c.b;
    const rootChildren: number[] = [];
    const warn = (m: string): void => {
        if (c.report.warnings.length < 200) c.report.warnings.push(m);
    };

    // ---- terrain
    if (opt.includeTerrain && input.terrain?.layers.length) {
        const texByRef = new Map(input.terrain.textures.map((t) => [t.ref, t]));
        const buckets = terrainBuckets(input.terrain.layers, { chunkCells: opt.chunkCells, perCell: true });
        const chunks = new Map<string, typeof buckets>();
        for (const k of buckets) {
            let l = chunks.get(k.chunk);
            if (!l) chunks.set(k.chunk, (l = []));
            l.push(k);
        }
        const kids: number[] = [];
        for (const [ck, list] of [...chunks].sort((x, y) => x[0].localeCompare(y[0], undefined, { numeric: true }))) {
            const prims = terrainChunkPrims(list);
            const withMat: (Prim & { material: number })[] = [];
            for (const p of prims) {
                const tex = texByRef.get(p.ref) ?? null;
                if (!tex) warn(`terrain texture ${p.ref.toString(16)} did not decode (grey material)`);
                const material = await c.material(
                    { key: `terrain:${p.ref}`, kind: 'terrain', scope: 'Terrain', ref: p.ref, txd: tex?.txd },
                    tex,
                    false,
                );
                withMat.push({ ...p, material });
                c.report.terrainTriangles += p.idx.length / 3;
            }
            const [cx, cz] = ck.split(',').map(Number) as [number, number];
            const name = `SM_Terrain_${cx}_${cz}`;
            const mesh = c.addMesh(name, withMat);
            kids.push(
                b.addNode({
                    name,
                    mesh,
                    extras: {
                        [EXTRAS_KEY]: {
                            type: 'terrain',
                            chunk: [cx, cz],
                            chunkCells: opt.chunkCells,
                            staticMesh: name,
                        },
                    },
                }),
            );
            c.report.terrainChunks++;
        }
        rootChildren.push(b.addNode({ name: 'Terrain', children: kids }));
    }

    // ---- class assets
    const classByRes = new Map<string, ExportClass>();
    for (const k of input.classes) classByRes.set(k.resource, k);
    interface Asset {
        staticMesh?: string;
        skeletalMesh?: string;
        meshIndex: number;
    }
    const assets = new Map<string, Asset>();
    const assetNodes: number[] = [];
    let skinnedSlot = 0;
    const used = new Set(input.objects.map((o) => o.resource));
    const helperOf = new Map<string, HelperGroup>();
    for (const o of input.objects) {
        const g = helperGroupOf(o.class);
        if (g && !helperOf.has(o.resource)) helperOf.set(o.resource, g);
    }
    const classNamer = new Namer();

    const classMaterials = async (k: ExportClass, name: string): Promise<number[]> => {
        const byRef = new Map(k.textures.map((t) => [t.ref, t]));
        const out: number[] = [];
        for (let i = 0; i < k.model.textures.length; i++) {
            const t = k.model.textures[i]!;
            const ref = t.id | (t.db << 16);
            const tex = byRef.get(ref) ?? null;
            if (!tex) warn(`${name}: texture ${ref.toString(16)} did not decode (grey material)`);
            out.push(
                await c.material(
                    { key: `class:${k.resource}:${i}`, kind: 'model', scope: name, ref, txd: tex?.txd },
                    tex,
                    true,
                ),
            );
        }
        return out;
    };

    for (const k of input.classes) {
        if (!opt.includeModels && !k.rig) continue;
        if (!used.has(k.resource)) continue;
        if (!opt.helperModels && helperOf.has(k.resource)) continue;
        if (modelPrims(k.model, 1, null).every((p) => !p.idx.length)) {
            warn(`${k.name}: model has no LOD 0 triangles, left out`);
            continue;
        }
        const name = classNamer.unique(ident(k.name || `Class${k.resource}`));
        const mats = await classMaterials(k, name);
        const grey = await c.material(
            { key: `class:${k.resource}:grey`, kind: 'model', scope: name, ref: 0 },
            null,
            true,
        );
        const matOf = (i: number): number => mats[i] ?? grey;
        const skinned = !!k.rig && opt.includeSkinned;
        const entry: Asset = { meshIndex: -1 };
        assets.set(k.resource, entry);

        if (!skinned || opt.skinnedBindPose) {
            const smName = skinned ? `SM_${name}_Bind` : `SM_${name}`;
            const prims = modelPrims(k.model, MODEL_UNIT_M, null).map((p) => ({ ...p, material: matOf(p.tex) }));
            entry.meshIndex = c.addMesh(smName, prims);
            entry.staticMesh = smName;
            if (!skinned) c.report.staticClasses++;
        }
        if (skinned) {
            const rig = k.rig!;
            const skName = `SK_${name}`;
            entry.skeletalMesh = skName;
            const prims = modelPrims(k.model, MODEL_UNIT_M, rig).map((p) => ({ ...p, material: matOf(p.tex) }));
            const skMesh = c.addMesh(skName, prims);
            const jn = new Namer();
            const joints: number[] = [];
            const roots: number[] = [];
            for (let j = 0; j < rig.jointCount; j++) {
                const m = rig.bindLocal.subarray(j * 12, j * 12 + 12);
                const node = b.addNode({
                    name: jn.unique(ident(rig.names[j] || `joint_${j}`)),
                    translation: [m[9]! * MODEL_UNIT_M, m[10]! * MODEL_UNIT_M, m[11]! * MODEL_UNIT_M],
                    rotation: normQuat(mat12ToQuat(m)),
                });
                joints.push(node);
            }
            for (let j = 0; j < rig.jointCount; j++) {
                const p = rig.parents[j]!;
                if (p < 0) roots.push(joints[j]!);
                else (b.doc.nodes[joints[p]!].children ??= []).push(joints[j]!);
            }
            const ibm = new Float32Array(rig.jointCount * 16);
            for (let j = 0; j < rig.jointCount; j++)
                ibm.set(mat12ToGltfMat4(rig.invBind.subarray(j * 12, j * 12 + 12), MODEL_UNIT_M), j * 16);
            const skin: Json = { name: skName, joints, inverseBindMatrices: b.addAccessor(ibm, 'MAT4') };
            if (roots.length === 1) skin.skeleton = roots[0];
            b.doc.skins.push(skin);
            const skinIdx = b.doc.skins.length - 1;
            const meshNode = b.addNode({ name: `${skName}_Mesh`, mesh: skMesh, skin: skinIdx });
            const motionNode = opt.rootMotion === 'separate' ? b.addNode({ name: `${skName}_RootMotion` }) : -1;
            const clips = (k.clips ?? []).slice(0, opt.maxClipsPerClass);
            let animated = 0;
            for (const clip of clips) {
                if (addAnimation(c, { name, rig }, clip, { joints, rootNode: roots[0]!, motionNode }, skName))
                    animated++;
                else
                    warn(
                        `${name}: clip ${clip.id} (${clip.name}) does not fit the rig (${clip.anim.tracks.length} tracks, ${rig.jointCount} joints)`,
                    );
            }
            const holder = b.addNode({
                name: skName,
                translation: [skinnedSlot++ * 6, 0, -6],
                children: [...roots, meshNode, ...(motionNode >= 0 ? [motionNode] : [])],
                extras: {
                    [EXTRAS_KEY]: {
                        type: 'skeletal',
                        class: k.name,
                        resource: k.resource,
                        skeletalMesh: skName,
                        joints: rig.jointCount,
                        clips: animated,
                        rootMotion: opt.rootMotion,
                    },
                },
            });
            assetNodes.push(holder);
            c.report.skinnedClasses++;
        }
    }

    // ---- instances, markers, cameras, lights
    const objectNodes: number[] = [];
    const lights: Json[] = [];
    let cameraIdx = -1;
    const instanceGroups = new Map<
        string,
        { o: ExportObject; t: { translation: number[]; rotation: number[]; scale: number[] } }[]
    >();
    for (const o of input.objects) {
        const asset = assets.get(o.resource) ?? null;
        const pos = gamePosToGltf(o.transform.pos as [number, number, number]);
        const rotation = normQuat(docRotToQuat(o.transform.rot as [number, number, number]));
        const scale = o.scale ? [o.scale[0]!, o.scale[1]!, o.scale[2]!] : [1, 1, 1];
        const t = { translation: pos, rotation, scale };
        const ue = gltfTransformToUe({ translation: pos, rotation, scale: scale as [number, number, number] });
        const name = `${ident(o.class || 'Object')}_${o.id}`;
        const node: Json = { name, translation: pos };
        if (rotation.some((v, i) => Math.abs(v - (i === 3 ? 1 : 0)) > 1e-7)) node.rotation = rotation;
        if (scale.some((v) => v !== 1)) node.scale = scale;
        node.extras = instanceExtras(o, asset, ue);
        if (helperOf.has(o.resource)) node.extras[EXTRAS_KEY].helper = helperOf.get(o.resource);
        if (asset && asset.meshIndex >= 0 && !opt.instancing) {
            node.mesh = asset.meshIndex;
            objectNodes.push(b.addNode(node));
            c.report.instances++;
        } else if (asset && asset.meshIndex >= 0 && opt.instancing) {
            let l = instanceGroups.get(o.resource);
            if (!l) instanceGroups.set(o.resource, (l = []));
            l.push({ o, t });
            c.report.instances++;
        } else if (asset && asset.skeletalMesh) {
            objectNodes.push(b.addNode(node));
            c.report.instances++;
        } else if (o.kind === 'camera' || helperOf.get(o.resource) === 'camera') {
            if (cameraIdx < 0) {
                b.doc.cameras.push({
                    type: 'perspective',
                    name: 'Drakan_Camera',
                    perspective: { yfov: 1.0, znear: 0.05, zfar: 2000 },
                });
                cameraIdx = b.doc.cameras.length - 1;
            }
            node.camera = cameraIdx;
            node.extras[EXTRAS_KEY].cameraGuess = true;
            objectNodes.push(b.addNode(node));
            c.report.cameras++;
        } else if (helperOf.get(o.resource) === 'light') {
            lights.push({ type: 'point', name: name, color: [1, 0.9, 0.7], intensity: 20, range: 12 });
            node.extensions = { KHR_lights_punctual: { light: lights.length - 1 } };
            node.extras[EXTRAS_KEY].lightGuess = true;
            objectNodes.push(b.addNode(node));
            c.report.lights++;
        } else if (opt.includeMarkers) {
            objectNodes.push(b.addNode(node));
            c.report.markers++;
        }
    }
    if (lights.length) {
        b.useExtension('KHR_lights_punctual');
        b.doc.extensions = { KHR_lights_punctual: { lights } };
    }
    if (instanceGroups.size) {
        b.useExtension('EXT_mesh_gpu_instancing');
        for (const [res, list] of instanceGroups) {
            const a = assets.get(res)!;
            const tr = new Float32Array(list.length * 3);
            const ro = new Float32Array(list.length * 4);
            const sc = new Float32Array(list.length * 3);
            list.forEach(({ t }, i) => {
                tr.set(t.translation, i * 3);
                ro.set(t.rotation, i * 4);
                sc.set(t.scale, i * 3);
            });
            const cls = classByRes.get(res)!;
            const first = list[0]!.o;
            objectNodes.push(
                b.addNode({
                    name: `Instances_${a.staticMesh}`,
                    mesh: a.meshIndex,
                    extensions: {
                        EXT_mesh_gpu_instancing: {
                            attributes: {
                                TRANSLATION: b.addAccessor(tr, 'VEC3'),
                                ROTATION: b.addAccessor(ro, 'VEC4'),
                                SCALE: b.addAccessor(sc, 'VEC3'),
                            },
                        },
                    },
                    extras: {
                        [EXTRAS_KEY]: {
                            type: 'instances',
                            class: cls.name,
                            resource: res,
                            staticMesh: a.staticMesh,
                            instances: list.map(
                                ({ o, t }) =>
                                    instanceExtras(
                                        o,
                                        a,
                                        gltfTransformToUe({
                                            translation: t.translation as [number, number, number],
                                            rotation: t.rotation as [number, number, number, number],
                                            scale: t.scale as [number, number, number],
                                        }),
                                    )[EXTRAS_KEY],
                            ),
                            firstId: first.id,
                        },
                    },
                }),
            );
        }
    }
    if (objectNodes.length) rootChildren.push(b.addNode({ name: 'Objects', children: objectNodes }));
    if (assetNodes.length) rootChildren.push(b.addNode({ name: 'SkeletalAssets', children: assetNodes }));

    b.doc.scenes[0]!.nodes = rootChildren;
    b.doc.scenes[0]!.name = ident(input.level || 'Level');
    const sceneInfo = {
        version: EXTRAS_VERSION,
        level: input.level,
        units: {
            gltf: 'metres, y up, right handed (the editor scene space)',
            gameUnitMetres: GAME_UNIT_M,
            modelUnitMetres: MODEL_UNIT_M,
            ueCmPerGameUnit: UE_CM_PER_GAME_UNIT,
            ueFromGltf: UE_FROM_GLTF,
            ueMapping: 'UE (X, Y, Z) = glTF (x, z, y) * 100; quaternion (x, y, z, w) -> (-x, -z, -y, w)',
        },
        rootMotion: opt.rootMotion,
        instancing: opt.instancing,
    };
    b.doc.asset.extras = { [EXTRAS_KEY]: sceneInfo };
    b.doc.extras = { [EXTRAS_KEY]: sceneInfo };

    return { glb: b.toGlb(), files: c.files, json: b.json(), report: c.report };
}
