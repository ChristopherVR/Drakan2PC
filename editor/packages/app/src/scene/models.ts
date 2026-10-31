import * as THREE from 'three';
import type { Model } from '@drakan/formats';
import { SCENE_SCALE } from './terrain.ts';
import type { TextureLookup } from './textures.ts';

/** One model unit is one terrain cell (2048 game units) [inf: bounds of known models are cell-sized]. */
export const MODEL_UNIT = 2048;
/** Scene units per model unit. */
export const MODEL_SCALE = MODEL_UNIT * SCENE_SCALE;

const geometryCache = new Map<string, THREE.BufferGeometry>();

/**
 * Geometry for LOD 0 of a model: non-indexed (the PS2 format keeps one UV per polygon corner), grouped by texture
 * so a material array indexed like `Model.textures` works. Cached per `key` (e.g. `${container}:${model.id}`).
 */
export function modelGeometry(model: Model, key = String(model.id)): THREE.BufferGeometry {
    const hit = geometryCache.get(key);
    if (hit) return hit;
    const { firstTri, triCount } = model.lod0;
    const order: number[] = [];
    for (let t = firstTri; t < firstTri + triCount; t++) order.push(t);
    order.sort((a, b) => model.triTexture[a]! - model.triTexture[b]! || a - b);
    const pos = new Float32Array(order.length * 9);
    const nrm = new Float32Array(order.length * 9);
    const uv = new Float32Array(order.length * 6);
    const g = new THREE.BufferGeometry();
    let i = 0;
    let groupStart = 0;
    let groupTex = order.length ? model.triTexture[order[0]!]! : 0;
    for (const t of order) {
        const tex = model.triTexture[t]!;
        if (tex !== groupTex) {
            g.addGroup(groupStart * 3, (i - groupStart) * 3, groupTex);
            groupStart = i;
            groupTex = tex;
        }
        for (let k = 0; k < 3; k++) {
            const v = model.indices[t * 3 + k]!;
            for (let a = 0; a < 3; a++) {
                pos[i * 9 + k * 3 + a] = model.positions[v * 3 + a]!;
                nrm[i * 9 + k * 3 + a] = model.normals[v * 3 + a]!;
            }
            uv[i * 6 + k * 2] = model.uvs[t * 6 + k * 2]!;
            uv[i * 6 + k * 2 + 1] = model.uvs[t * 6 + k * 2 + 1]!;
        }
        i++;
    }
    if (order.length) g.addGroup(groupStart * 3, (i - groupStart) * 3, groupTex);
    g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    g.setAttribute('normal', new THREE.BufferAttribute(nrm, 3));
    g.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
    g.computeBoundingSphere();
    g.computeBoundingBox();
    geometryCache.set(key, g);
    return g;
}

export function clearModelCache(): void {
    for (const g of geometryCache.values()) g.dispose();
    geometryCache.clear();
}

const grey = new THREE.MeshLambertMaterial({ color: 0x9aa0a6, side: THREE.DoubleSide });

/** Materials indexed like `Model.textures` (flat grey where a texture is missing). Double sided: polygon flags are not applied. */
export function modelMaterials(model: Model, lookup: TextureLookup): THREE.Material[] {
    return model.textures.map((t) => {
        const tex = lookup(t.id | (t.db << 16));
        return tex ? new THREE.MeshLambertMaterial({ map: tex, side: THREE.DoubleSide, alphaTest: 0.5 }) : grey;
    });
}

/** A single textured mesh (scaled to scene units). */
export function buildModel(model: Model, lookup: TextureLookup, key?: string): THREE.Mesh {
    const mats = modelMaterials(model, lookup);
    const mesh = new THREE.Mesh(modelGeometry(model, key), mats.length ? mats : grey);
    mesh.scale.setScalar(MODEL_SCALE);
    mesh.name = model.name;
    return mesh;
}

export interface ModelInstance {
    /** Game units, y up. */
    pos: readonly [number, number, number];
    /** Degrees as stored in the LVL; yaw = rot[1] - 90 (level-format.md section 3), pitch/roll applied in YXZ order [?]. */
    rot?: readonly [number, number, number];
    scale?: readonly [number, number, number] | null;
}

const eul = new THREE.Euler();
const quat = new THREE.Quaternion();
const vp = new THREE.Vector3();
const vs = new THREE.Vector3();
const tmpM = new THREE.Matrix4();

/** Instance matrix for an object (model space -> scene), includes the model unit scale. */
export function instanceMatrix(i: ModelInstance, out = new THREE.Matrix4()): THREE.Matrix4 {
    const r = i.rot ?? [0, 90, 0];
    eul.set((r[0] * Math.PI) / 180, ((r[1] - 90) * Math.PI) / 180, (r[2] * Math.PI) / 180, 'YXZ');
    quat.setFromEuler(eul);
    vp.set(i.pos[0] * SCENE_SCALE, i.pos[1] * SCENE_SCALE, i.pos[2] * SCENE_SCALE);
    const sc = i.scale ?? [1, 1, 1];
    vs.set(sc[0] * MODEL_SCALE, sc[1] * MODEL_SCALE, sc[2] * MODEL_SCALE);
    return out.compose(vp, quat, vs);
}

/**
 * One InstancedMesh per distinct model (a 4675-object level is a few hundred draw calls at most). Frustum culling is
 * per InstancedMesh (bounding sphere over all its instances).
 */
export function buildInstancedModels(
    items: { key: string; model: Model; lookup: TextureLookup; instances: readonly ModelInstance[] }[],
): THREE.Group {
    const group = new THREE.Group();
    group.name = 'models';
    for (const it of items) {
        if (!it.instances.length) continue;
        const mats = modelMaterials(it.model, it.lookup);
        const mesh = new THREE.InstancedMesh(
            modelGeometry(it.model, it.key),
            mats.length ? mats : grey,
            it.instances.length,
        );
        it.instances.forEach((inst, n) => mesh.setMatrixAt(n, instanceMatrix(inst, tmpM)));
        mesh.instanceMatrix.needsUpdate = true;
        mesh.computeBoundingSphere();
        mesh.frustumCulled = true;
        mesh.name = it.model.name;
        mesh.userData.modelKey = it.key;
        group.add(mesh);
    }
    return group;
}
