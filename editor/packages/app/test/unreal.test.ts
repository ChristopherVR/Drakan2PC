import { describe, expect, it } from 'vitest';
import type { Model, Ps2Anim, Rig, TerrainLayer } from '@drakan/formats';
import { CELL_SIZE } from '@drakan/formats';
import { MODEL_SCALE, docInstanceMatrix } from '../src/scene/models.ts';
import {
    GAME_UNIT_M,
    MODEL_UNIT_M,
    UE_CM_PER_GAME_UNIT,
    UE_FROM_GLTF,
    docRotToQuat,
    gamePosToGltf,
    gamePosToUe,
    gltfQuatToUe,
    gltfScaleToUe,
    lvlRotToQuat,
    rotateByQuat,
    quatFromEulerYXZ,
} from '../src/unreal/coords.ts';
import { crc32, encodePng } from '../src/unreal/glb.ts';
import {
    buildLevelGltf,
    ident,
    instanceExtras,
    mat12ToGltfMat4,
    modelPrims,
    type ExportObject,
} from '../src/unreal/levelglb.ts';

const apply = (m: readonly number[], v: readonly number[]): number[] =>
    [0, 1, 2].map((r) => m[r * 3]! * v[0]! + m[r * 3 + 1]! * v[1]! + m[r * 3 + 2]! * v[2]!);

/** Parse a .glb into its JSON and binary chunk (no validation beyond the header). */
function parseGlb(b: Uint8Array): { json: any; bin: Uint8Array } {
    const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
    expect(dv.getUint32(0, true)).toBe(0x46546c67);
    expect(dv.getUint32(8, true)).toBe(b.byteLength);
    const jl = dv.getUint32(12, true);
    const json = JSON.parse(new TextDecoder().decode(b.subarray(20, 20 + jl)));
    const o = 20 + jl;
    const bin = o < b.byteLength ? b.subarray(o + 8, o + 8 + dv.getUint32(o, true)) : new Uint8Array(0);
    return { json, bin };
}

function readAccessor(json: any, bin: Uint8Array, i: number): number[] {
    const a = json.accessors[i];
    const v = json.bufferViews[a.bufferView];
    const n = a.count * { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 }[a.type as 'SCALAR'];
    const dv = new DataView(bin.buffer, bin.byteOffset + v.byteOffset + (a.byteOffset ?? 0), v.byteLength);
    const out: number[] = [];
    for (let k = 0; k < n; k++) {
        if (a.componentType === 5126) out.push(dv.getFloat32(k * 4, true));
        else if (a.componentType === 5123) out.push(dv.getUint16(k * 2, true));
        else if (a.componentType === 5125) out.push(dv.getUint32(k * 4, true));
        else out.push(dv.getUint8(k));
    }
    return out;
}

function quadModel(): Model {
    return {
        id: 1,
        name: 'quad',
        shading: 2,
        textures: [{ id: 5, db: 0 }],
        positions: new Float32Array([0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0]),
        normals: new Float32Array([0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1]),
        indices: new Uint16Array([0, 1, 2, 0, 2, 3]),
        triTexture: new Uint16Array([0, 0]),
        uvs: new Float32Array([0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1]),
        lods: [],
        skinned: false,
        bounds: { min: [0, 0, 0], max: [1, 1, 0] },
        lod0: { firstTri: 0, triCount: 2 },
    } as unknown as Model;
}

const tex = (ref: number, w = 2, h = 2, alpha = 255) => ({
    ref,
    width: w,
    height: h,
    rgba: Uint8Array.from({ length: w * h * 4 }, (_, i) => (i % 4 === 3 ? alpha : (i * 37 + ref) & 255)),
});

describe('coordinate conversion', () => {
    it('converts game units to metres and Unreal centimetres', () => {
        expect(GAME_UNIT_M).toBe(0.001);
        expect(MODEL_UNIT_M).toBeCloseTo(2.048);
        expect(gamePosToGltf([1000, 2000, 3000])).toEqual([1, 2, 3]);
        // glTF (x, y, z) -> UE (X, Y, Z) = (x, z, y) * 100
        expect(gamePosToUe([1000, 2000, 3000])).toEqual([100, 300, 200]);
        expect(UE_CM_PER_GAME_UNIT).toBeCloseTo(0.1);
        expect(2048 * UE_CM_PER_GAME_UNIT).toBeCloseTo(204.8);
        expect(gltfScaleToUe([1, 2, 3])).toEqual([1, 3, 2]);
    });

    it('the axis swap flips handedness and is its own inverse', () => {
        const P = UE_FROM_GLTF;
        const det =
            P[0]! * (P[4]! * P[8]! - P[5]! * P[7]!) -
            P[1]! * (P[3]! * P[8]! - P[5]! * P[6]!) +
            P[2]! * (P[3]! * P[7]! - P[4]! * P[6]!);
        expect(det).toBe(-1);
        expect(apply(P, apply(P, [1, 2, 3]))).toEqual([1, 2, 3]);
    });

    it('rotations convert to the same physical rotation in Unreal axes', () => {
        const P = UE_FROM_GLTF;
        for (const e of [
            [0.3, 1.1, -0.4],
            [0, 2.5, 0],
            [1.2, 0, 0.7],
        ] as const) {
            const q = quatFromEulerYXZ(e[0], e[1], e[2]);
            const qUe = gltfQuatToUe(q);
            for (const v of [
                [1, 0, 0],
                [0, 1, 0],
                [0.3, -2, 5],
            ] as const) {
                // rotate in glTF space, then convert, versus convert, then rotate with the Unreal quaternion
                const a = apply(P, rotateByQuat(q, v));
                const b = rotateByQuat(qUe, apply(P, v) as [number, number, number]);
                a.forEach((x, i) => expect(x).toBeCloseTo(b[i]!, 6));
            }
        }
    });

    it('matches the editor instance matrix (yaw = stored yaw minus 90 degrees, YXZ)', () => {
        const rotDeg = [10, 135, -20] as const;
        const q = lvlRotToQuat(rotDeg);
        const d = Math.PI / 180;
        const doc = {
            transform: { pos: [0, 0, 0], rot: [rotDeg[0] * d, (rotDeg[1] - 90) * d, rotDeg[2] * d] },
            scale: [1, 1, 1],
        };
        const m = docInstanceMatrix(doc);
        const v = [1, 2, 3] as const;
        const want = [
            v[0] * m.elements[0]! + v[1] * m.elements[4]! + v[2] * m.elements[8]!,
            v[0] * m.elements[1]! + v[1] * m.elements[5]! + v[2] * m.elements[9]!,
            v[0] * m.elements[2]! + v[1] * m.elements[6]! + v[2] * m.elements[10]!,
        ];
        const got = rotateByQuat(q, v);
        got.forEach((x, i) => expect(x).toBeCloseTo(want[i]! / MODEL_SCALE, 6));
        expect(docRotToQuat(doc.transform.rot as [number, number, number])).toEqual(q);
    });
});

describe('instance extras', () => {
    it('carry class, id, properties and the Unreal transform', () => {
        const o: ExportObject = {
            id: 42,
            class: 'Barrel',
            type: 8,
            kind: 'static',
            resource: '65538',
            transform: { pos: [1000, 500, 2000], rot: [0, Math.PI / 2, 0] },
            scale: [2, 2, 2],
            props: { hp: 5, tag: 'x' },
            links: [7],
            origin: 3,
            db: 'NPCS',
            fields: [{ slot: 0, fieldIndex: 4, type: 1, name: 'Hp', value: 5 }],
        };
        const ue = {
            location: [100, 200, 50] as [number, number, number],
            quat: [0, 0, 0, 1] as [number, number, number, number],
            scale: [2, 2, 2] as [number, number, number],
        };
        const e = instanceExtras(o, { staticMesh: 'SM_Barrel' }, ue).drakan;
        expect(e).toMatchObject({
            id: 42,
            class: 'Barrel',
            resource: '65538',
            kind: 'static',
            typeId: 8,
            origin: 3,
            links: [7],
            db: 'NPCS',
            staticMesh: 'SM_Barrel',
        });
        expect(e.props).toEqual({ hp: 5, tag: 'x' });
        expect(e.fields[0]).toEqual({ slot: 0, index: 4, type: 1, name: 'Hp', value: 5 });
        expect(e.ue.location).toEqual([100, 200, 50]);
        expect(JSON.parse(JSON.stringify(e))).toEqual(e);
    });
});

describe('png', () => {
    it('writes a valid signature, IHDR and a correct chunk crc', async () => {
        const png = await encodePng(tex(1, 3, 2).rgba, 3, 2);
        expect([...png.subarray(0, 8)]).toEqual([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
        const dv = new DataView(png.buffer, png.byteOffset);
        expect(dv.getUint32(16, false)).toBe(3);
        expect(dv.getUint32(20, false)).toBe(2);
        expect(dv.getUint32(29, false)).toBe(crc32(png, 12, 29));
    });
    it('writes bottom-up data reversed so the first row is the top', async () => {
        const rgba = Uint8Array.from([1, 1, 1, 255, 2, 2, 2, 255]); // 1x2: row 0 (bottom) = 1, row 1 (top) = 2
        const png = await encodePng(rgba, 1, 2, true);
        const inflated = await new Response(
            new Blob([png.subarray(41, png.length - 16)]).stream().pipeThrough(new DecompressionStream('deflate')),
        ).arrayBuffer();
        const raw = new Uint8Array(inflated);
        expect(raw[1]).toBe(2);
        expect(raw[1 + 5]).toBe(1);
    });
});

describe('model geometry', () => {
    it('indexes by (vertex, uv), flips V and scales to metres', () => {
        const prims = modelPrims(quadModel(), MODEL_UNIT_M, null);
        expect(prims).toHaveLength(1);
        const p = prims[0]!;
        expect(p.idx).toHaveLength(6);
        expect(p.pos.length / 3).toBe(4);
        expect(p.pos[3]).toBeCloseTo(MODEL_UNIT_M);
        expect(p.uv[1]).toBe(1); // v = 0 -> 1
    });
});

describe('level glb', () => {
    const layer = (): TerrainLayer =>
        ({
            originX: 0,
            originZ: 0,
            width: 2,
            height: 2,
            heights: new Int16Array(9).map((_, i) => i * 10),
            cellTexLeft: new Uint32Array([1, 1, 2, 2]),
            cellTexRight: new Uint32Array([1, 1, 2, 2]),
            cellFlags: new Uint16Array(4),
            cellUv: new Uint16Array(32).map((_, i) => (i % 2 ? 65535 : 0)),
        }) as unknown as TerrainLayer;

    it('writes terrain chunks, static instances, markers and extras', async () => {
        const objects: ExportObject[] = [
            {
                id: 1,
                class: 'Crate',
                type: 8,
                kind: 'static',
                resource: '10',
                transform: { pos: [2048, 0, 4096], rot: [0, 0, 0] },
                props: { a: 1 },
            },
            {
                id: 2,
                class: 'Crate',
                type: 8,
                kind: 'static',
                resource: '10',
                transform: { pos: [0, 0, 0], rot: [0, 1, 0] },
                scale: [2, 2, 2],
            },
            {
                id: 3,
                class: 'Trigger',
                type: 1,
                kind: 'other',
                resource: '99',
                transform: { pos: [1, 2, 3], rot: [0, 0, 0] },
            },
            {
                id: 4,
                class: 'Camera',
                type: 4,
                kind: 'camera',
                resource: '98',
                transform: { pos: [1, 2, 3], rot: [0, 0, 0] },
            },
            {
                id: 5,
                class: 'Static (Vertex) Light',
                type: 8,
                kind: 'static',
                resource: '97',
                transform: { pos: [1, 2, 3], rot: [0, 0, 0] },
            },
        ];
        const out = await buildLevelGltf(
            {
                level: 'Test Level',
                terrain: { layers: [layer()], textures: [tex(1), tex(2)] },
                classes: [{ resource: '10', name: 'Crate', model: quadModel(), textures: [tex(5, 4, 4, 100)] }],
                objects,
            },
            { chunkCells: 1 },
        );
        const { json, bin } = parseGlb(out.glb);
        expect(json.asset.version).toBe('2.0');
        expect(out.report.terrainChunks).toBe(4);
        expect(out.report.instances).toBe(2);
        expect(out.report.markers).toBe(1);
        expect(out.report.cameras).toBe(1);
        expect(out.report.lights).toBe(1);
        expect(json.extensionsUsed).toContain('KHR_lights_punctual');
        expect([...out.files.keys()].sort()).toEqual([
            'textures/T_Crate_5_BC.png',
            'textures/T_Terrain_1_BC.png',
            'textures/T_Terrain_2_BC.png',
        ]);
        const crate = json.nodes.find((n: any) => n.name === 'Crate_2');
        expect(crate.scale).toEqual([2, 2, 2]);
        expect(crate.extras.drakan).toMatchObject({
            id: 2,
            class: 'Crate',
            staticMesh: 'SM_Crate',
            gameScale: [2, 2, 2],
        });
        expect(json.nodes.find((n: any) => n.name === 'Crate_1').translation).toEqual([2.048, 0, 4.096]);
        // translucent texture -> alpha mask
        expect(json.materials.find((m: any) => m.name.startsWith('M_Crate')).alphaMode).toBe('MASK');
        // accessor bounds on positions
        const pos = json.accessors[json.meshes[0].primitives[0].attributes.POSITION];
        expect(pos.min).toBeDefined();
        expect(readAccessor(json, bin, json.meshes[0].primitives[0].attributes.POSITION).length).toBe(pos.count * 3);
        // terrain is wound counter-clockwise seen from +y
        const tp = json.meshes[0].primitives[0];
        const P = readAccessor(json, bin, tp.attributes.POSITION);
        const I = readAccessor(json, bin, tp.indices);
        const a = [0, 1, 2].map((k) => P.slice(I[k]! * 3, I[k]! * 3 + 3));
        const nrmY =
            (a[1]![2]! - a[0]![2]!) * (a[2]![0]! - a[0]![0]!) - (a[1]![0]! - a[0]![0]!) * (a[2]![2]! - a[0]![2]!);
        expect(nrmY).toBeGreaterThan(0);
        expect(P[0]).toBeGreaterThanOrEqual(0);
        expect(CELL_SIZE * GAME_UNIT_M).toBeCloseTo(2.048);
    });

    it('instancing emits one EXT_mesh_gpu_instancing node per class', async () => {
        const out = await buildLevelGltf(
            {
                level: 'L',
                classes: [{ resource: '10', name: 'Crate', model: quadModel(), textures: [tex(5)] }],
                objects: [1, 2, 3].map((id) => ({
                    id,
                    class: 'Crate',
                    type: 8,
                    kind: 'static',
                    resource: '10',
                    transform: { pos: [id, 0, 0], rot: [0, 0, 0] },
                })),
            },
            { instancing: true, textures: 'embedded' },
        );
        const { json } = parseGlb(out.glb);
        expect(json.extensionsUsed).toContain('EXT_mesh_gpu_instancing');
        const n = json.nodes.find((x: any) => x.extensions?.EXT_mesh_gpu_instancing);
        expect(json.accessors[n.extensions.EXT_mesh_gpu_instancing.attributes.TRANSLATION].count).toBe(3);
        expect(n.extras.drakan.instances.map((i: any) => i.id)).toEqual([1, 2, 3]);
        expect(out.files.size).toBe(0);
        expect(json.images[0].bufferView).toBeDefined();
    });

    it('skinned classes get joints, inverse binds that cancel the bind pose, and animations', async () => {
        // two joints: root at the origin, child one model unit up; both identity rotation
        const invBind = new Float32Array([1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, -1, 0]);
        const bindLocal = new Float32Array([1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0]);
        const model = quadModel();
        const rig: Rig = {
            jointCount: 2,
            parents: [-1, 0],
            order: [0, 1],
            names: ['root', 'child'],
            invBind,
            bindLocal,
            jointIndex: new Uint16Array([0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0]),
            jointWeight: new Float32Array([1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0.5, 0.5, 0, 0]),
            vertexCount: 4,
            droppedInfluences: 0,
            unweighted: 0,
            animIds: [1],
        };
        const anim: Ps2Anim = {
            name: 'walk',
            duration: 1,
            flags: 0,
            frames: 2,
            channels: 2,
            thresholds: { rotation: 0, scale: 0, translation: 0 },
            tracks: [
                {
                    base: [0, 0, 0],
                    hasPos: true,
                    keys: [
                        { time: 0, q: [0, 0, 0, 1], pos: [0, 0, 0] },
                        { time: 1, q: [0, 0.7071, 0, 0.7071], pos: [0, 0, 3] },
                    ],
                },
                { base: [0, 1, 0], hasPos: false, keys: [{ time: 0, q: [0, 0, 0, 1], pos: null }] },
            ],
        };
        const objs: ExportObject[] = [
            {
                id: 9,
                class: 'Hero',
                type: 6,
                kind: 'creature',
                resource: '20',
                transform: { pos: [0, 0, 0], rot: [0, 0, 0] },
            },
        ];
        for (const rootMotion of ['keep', 'strip', 'separate'] as const) {
            const out = await buildLevelGltf(
                {
                    level: 'L',
                    classes: [
                        {
                            resource: '20',
                            name: 'Hero',
                            model,
                            textures: [tex(5)],
                            rig,
                            clips: [{ id: 1, name: 'walk', stem: 'X', anim }],
                        },
                    ],
                    objects: objs,
                },
                { rootMotion },
            );
            const { json, bin } = parseGlb(out.glb);
            expect(json.skins).toHaveLength(1);
            const skin = json.skins[0];
            // joint world * inverse bind = identity at rest
            const ibm = readAccessor(json, bin, skin.inverseBindMatrices);
            const j1 = json.nodes[skin.joints[1]];
            const j0 = json.nodes[skin.joints[0]];
            expect(j0.children).toEqual([skin.joints[1]]);
            expect(j1.translation[1]).toBeCloseTo(MODEL_UNIT_M);
            expect(ibm[16 + 13]).toBeCloseTo(-MODEL_UNIT_M);
            expect(ibm[16 + 13] + j1.translation[1] + j0.translation[1]).toBeCloseTo(0);
            const mesh = json.meshes.find((m: any) => m.name === 'SK_Hero');
            const attrs = mesh.primitives[0].attributes;
            expect(attrs.JOINTS_0).toBeDefined();
            const w = readAccessor(json, bin, attrs.WEIGHTS_0);
            for (let v = 0; v < w.length / 4; v++)
                expect(w[v * 4]! + w[v * 4 + 1]! + w[v * 4 + 2]! + w[v * 4 + 3]!).toBeCloseTo(1);
            expect(json.animations).toHaveLength(1);
            const an = json.animations[0];
            expect(an.name).toBe('Hero@walk');
            const paths = an.channels.map((ch: any) => `${json.nodes[ch.target.node].name}:${ch.target.path}`);
            expect(paths).toContain('root:rotation');
            expect(paths).toContain('child:rotation');
            const rootT = an.channels.find(
                (ch: any) => ch.target.path === 'translation' && ch.target.node === skin.joints[0],
            );
            const vals = readAccessor(json, bin, an.samplers[rootT.sampler].output);
            if (rootMotion === 'keep') expect(vals[5]).toBeCloseTo(3 * MODEL_UNIT_M);
            else expect(vals[5]).toBeCloseTo(0);
            const hasMotion = an.channels.some((ch: any) => json.nodes[ch.target.node].name === 'SK_Hero_RootMotion');
            expect(hasMotion).toBe(rootMotion === 'separate');
            if (hasMotion) {
                const m = an.channels.find((ch: any) => json.nodes[ch.target.node].name === 'SK_Hero_RootMotion');
                expect(readAccessor(json, bin, an.samplers[m.sampler].output)[5]).toBeCloseTo(3 * MODEL_UNIT_M);
                expect(an.extras.drakan.rootMotionNode).toBe('SK_Hero_RootMotion');
            }
            expect(json.nodes.find((n: any) => n.name === 'Hero_9').extras.drakan.skeletalMesh).toBe('SK_Hero');
            expect(out.report.animations).toBe(1);
        }
    });

    it('mat12 to glTF matrices transpose the rotation and keep the translation', () => {
        // 90 degrees about y in row-vector layout: x -> -z
        const m = [0, 0, -1, 0, 1, 0, 1, 0, 0, 5, 6, 7];
        const g = mat12ToGltfMat4(m, 2);
        expect(g.slice(12, 15)).toEqual([10, 12, 14]);
        // column-major: first column of the transposed matrix is the first stored row
        expect(g.slice(0, 3)).toEqual([0, 0, -1]);
    });

    it('ident sanitizes names for UE assets', () => {
        expect(ident('Wall Light #3')).toBe('Wall_Light_3');
        expect(ident('')).toBe('Unnamed');
    });
});

describe('pbr pack resolver', () => {
    const enc = (o: unknown): Uint8Array => new TextEncoder().encode(JSON.stringify(o));
    it('maps TXD sources to materials, flips normals and applies blend and two-sided data', async () => {
        const { PbrPack, pbrSourceKey } = await import('../src/unreal/pbr.ts');
        expect(pbrSourceKey('ENGLISH\\LEVELS\\LEVEL1NA\\LEVEL1NA.TXD', 5 | (2 << 16))).toBe(
            'LEVELS/LEVEL1NA/LEVEL1NA.TXD#5',
        );
        const files = new Map<string, Uint8Array>([
            [
                'Materials/M_Level1na_00005.json',
                enc({
                    schema: 1,
                    name: 'M_Level1na_00005',
                    sources: ['LEVELS/LEVEL1NA/LEVEL1NA.TXD#5'],
                    blendMode: 'Masked',
                    opacityMaskClip: 0.4,
                    twoSided: true,
                    textures: {
                        baseColor: 'Textures/T_Level1na_00005_BC.png',
                        normal: 'Textures/T_Level1na_00005_N.png',
                        orm: 'Textures/T_Level1na_00005_ORM.png',
                    },
                }),
            ],
            ['Textures/T_Level1na_00005_BC.png', new Uint8Array([1])],
            ['Textures/T_Level1na_00005_N.png', new Uint8Array([2])],
            ['Textures/T_Level1na_00005_ORM.png', new Uint8Array([3])],
        ]);
        const pack = new PbrPack(files, { flipGreen: async (p) => Uint8Array.from([...p, 9]) });
        const t = {
            ref: 5,
            width: 2,
            height: 2,
            rgba: new Uint8Array(16),
            txd: 'ENGLISH\\LEVELS\\LEVEL1NA\\LEVEL1NA.TXD',
        };
        const src = { key: 'terrain:5', kind: 'terrain' as const, scope: 'Terrain', ref: 5, txd: t.txd };
        await pack.prepare([src, { ...src, ref: 6 }]);
        expect(pack.missing.size).toBe(1);
        const out = await buildLevelGltf(
            { level: 'L', terrain: { layers: [layerFor(5)], textures: [t] }, classes: [], objects: [] },
            { resolver: pack.resolver(), textures: 'embedded' },
        );
        const m = out.json.materials[0];
        expect(m.name).toBe('M_Level1na_00005');
        expect(m.alphaMode).toBe('MASK');
        expect(m.alphaCutoff).toBe(0.4);
        expect(m.normalTexture).toBeDefined();
        expect(m.occlusionTexture.index).toBe(m.pbrMetallicRoughness.metallicRoughnessTexture.index);
        expect(m.doubleSided).toBe(true);
        expect(out.json.images.map((i: any) => i.name)).toEqual([
            'T_Level1na_00005_BC',
            'T_Level1na_00005_N',
            'T_Level1na_00005_ORM',
        ]);
    });
});

function layerFor(ref: number): TerrainLayer {
    return {
        originX: 0,
        originZ: 0,
        width: 1,
        height: 1,
        heights: new Int16Array(4),
        cellTexLeft: new Uint32Array([ref]),
        cellTexRight: new Uint32Array([ref]),
        cellFlags: new Uint16Array(1),
        cellUv: new Uint16Array(8),
    } as unknown as TerrainLayer;
}

describe('validator regressions', () => {
    it('normalizes quaternions and replaces zero normals with the face normal', async () => {
        const { normQuat } = await import('../src/unreal/levelglb.ts');
        const q = normQuat([0, 0, 0, 1.0000001192092824]);
        expect(q[3]).toBeLessThanOrEqual(1);
        const m = quadModel();
        m.normals = new Float32Array(12);
        const p = modelPrims(m, 1, null)[0]!;
        for (let i = 0; i < p.nrm.length / 3; i++)
            expect(Math.hypot(p.nrm[i * 3]!, p.nrm[i * 3 + 1]!, p.nrm[i * 3 + 2]!)).toBeCloseTo(1);
    });
    it('drops animation keys that go back in time', async () => {
        const anim = {
            name: 'a',
            duration: 1,
            flags: 0,
            frames: 1,
            channels: 1,
            thresholds: { rotation: 0, scale: 0, translation: 0 },
            tracks: [
                {
                    base: [0, 0, 0],
                    hasPos: false,
                    keys: [0, 0.5, 1, 0.13, 0.9].map((time) => ({ time, q: [0, 0, 0, 1], pos: null })),
                },
            ],
        } as unknown as Ps2Anim;
        const rig = {
            jointCount: 1,
            parents: [-1],
            order: [0],
            names: ['r'],
            invBind: new Float32Array([1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0]),
            bindLocal: new Float32Array([1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0]),
            jointIndex: new Uint16Array(16),
            jointWeight: new Float32Array(16).fill(0).map((_, i) => (i % 4 ? 0 : 1)),
            vertexCount: 4,
            droppedInfluences: 0,
            unweighted: 0,
            animIds: [],
        } as unknown as Rig;
        const out = await buildLevelGltf({
            level: 'L',
            classes: [
                {
                    resource: '1',
                    name: 'A',
                    model: quadModel(),
                    textures: [],
                    rig,
                    clips: [{ id: 1, name: 'x', stem: 's', anim }],
                },
            ],
            objects: [
                {
                    id: 1,
                    class: 'A',
                    type: 6,
                    kind: 'creature',
                    resource: '1',
                    transform: { pos: [0, 0, 0], rot: [0, 0, 0] },
                },
            ],
        });
        const t = out.json.accessors[out.json.animations[0].samplers[0].input];
        expect(t.count).toBe(3);
    });
});

describe('rotated skeletons', () => {
    it('joint world times inverse bind is the identity for rotated joints', async () => {
        const { invertMat12, mulMat12, quatToMat12 } = await import('@drakan/formats');
        const q1 = [0, 0.7071068, 0, 0.7071068];
        const q2 = [0.3826834, 0, 0, 0.9238795];
        const bindWorld = [
            quatToMat12([0, 0, 0, 1], [0, 0, 0]),
            quatToMat12(q1, [0, 1, 0]),
            quatToMat12(q2, [1, 1, 0]),
        ];
        const parents = [-1, 0, 1];
        const invBind = new Float32Array(36);
        const bindLocal = new Float32Array(36);
        bindWorld.forEach((w, j) => {
            invBind.set(invertMat12(w), j * 12);
            bindLocal.set(parents[j]! < 0 ? w : mulMat12(w, invertMat12(bindWorld[parents[j]!]!)), j * 12);
        });
        const rig = {
            jointCount: 3,
            parents,
            order: [0, 1, 2],
            names: ['a', 'b', 'c'],
            invBind,
            bindLocal,
            jointIndex: new Uint16Array(16),
            jointWeight: new Float32Array(16).map((_, i) => (i % 4 ? 0 : 1)),
            vertexCount: 4,
            droppedInfluences: 0,
            unweighted: 0,
            animIds: [],
        } as unknown as Rig;
        const out = await buildLevelGltf(
            {
                level: 'L',
                classes: [{ resource: '1', name: 'A', model: quadModel(), textures: [], rig }],
                objects: [
                    {
                        id: 1,
                        class: 'A',
                        type: 6,
                        kind: 'creature',
                        resource: '1',
                        transform: { pos: [0, 0, 0], rot: [0, 0, 0] },
                    },
                ],
            },
            { textures: 'embedded' },
        );
        const { json, bin } = parseGlb(out.glb);
        const skin = json.skins[0];
        const ibm = readAccessor(json, bin, skin.inverseBindMatrices);
        const mul = (A: number[], B: number[]): number[] => {
            const R = new Array(16).fill(0);
            for (let c = 0; c < 4; c++)
                for (let r = 0; r < 4; r++) for (let k = 0; k < 4; k++) R[c * 4 + r] += A[k * 4 + r]! * B[c * 4 + k]!;
            return R;
        };
        const local = (n: any): number[] => {
            const [x, y, z, w] = n.rotation ?? [0, 0, 0, 1];
            const t = n.translation ?? [0, 0, 0];
            return [
                1 - 2 * (y * y + z * z),
                2 * (x * y + z * w),
                2 * (x * z - y * w),
                0,
                2 * (x * y - z * w),
                1 - 2 * (x * x + z * z),
                2 * (y * z + x * w),
                0,
                2 * (x * z + y * w),
                2 * (y * z - x * w),
                1 - 2 * (x * x + y * y),
                0,
                t[0],
                t[1],
                t[2],
                1,
            ];
        };
        let W = local(json.nodes[skin.joints[0]]);
        const worlds = [W];
        for (let j = 1; j < 3; j++) worlds.push((W = mul(W, local(json.nodes[skin.joints[j]]))));
        worlds.forEach((w, j) => {
            const I = mul(w, ibm.slice(j * 16, j * 16 + 16));
            I.forEach((v, i) => expect(v).toBeCloseTo(i % 5 === 0 ? 1 : 0, 4));
        });
    });
});
