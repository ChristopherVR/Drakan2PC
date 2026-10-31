import * as THREE from 'three';
import {
    localToWorld,
    restPose,
    sampleClip,
    skinMatrices,
    skinVertices,
    type Model,
    type Ps2Anim,
    type Rig,
    type PoseOptions,
} from '@drakan/formats';
import { MODEL_SCALE, modelMaterials } from './models.ts';
import type { TextureLookup } from './textures.ts';

/**
 * A skinned character: LOD 0 of a model deformed on the CPU by a rig and an animation clip (linear blend skinning, up to
 * four bones per vertex; skeleton and clip semantics in `@drakan/formats` skeleton.ts / anim.ts). CPU skinning rather
 * than a three.js `SkinnedMesh` because the data is row-vector with per-joint inverse bind matrices and the models are
 * small (1600 vertices for Rynn, a few thousand for the dragons), so one pass per frame is cheap and the preview mesh
 * stays an ordinary non-indexed mesh grouped by texture like `modelGeometry`.
 */
export class SkinnedModel {
    readonly mesh: THREE.Mesh;
    private readonly geometry: THREE.BufferGeometry;
    private readonly materials: THREE.Material[];
    private readonly corner: Uint32Array;
    private readonly first: number;
    private readonly count: number;
    private readonly outPos: Float32Array;
    private readonly outNrm: Float32Array;
    private readonly local: Float32Array;
    private readonly world: Float32Array;
    private readonly skin: Float32Array;

    constructor(
        readonly model: Model,
        readonly rig: Rig,
        lookup: TextureLookup,
    ) {
        const { firstTri, triCount } = model.lod0;
        const order: number[] = [];
        for (let t = firstTri; t < firstTri + triCount; t++) order.push(t);
        order.sort((a, b) => model.triTexture[a]! - model.triTexture[b]! || a - b);
        const n = order.length * 3;
        this.corner = new Uint32Array(n);
        const pos = new Float32Array(n * 3);
        const nrm = new Float32Array(n * 3);
        const uv = new Float32Array(n * 2);
        const g = new THREE.BufferGeometry();
        let i = 0;
        let groupStart = 0;
        let groupTex = order.length ? model.triTexture[order[0]!]! : 0;
        let lo = Infinity;
        let hi = -1;
        for (const t of order) {
            const tex = model.triTexture[t]!;
            if (tex !== groupTex) {
                g.addGroup(groupStart * 3, (i - groupStart) * 3, groupTex);
                groupStart = i;
                groupTex = tex;
            }
            for (let k = 0; k < 3; k++) {
                const v = model.indices[t * 3 + k]!;
                this.corner[i * 3 + k] = v;
                if (v < lo) lo = v;
                if (v > hi) hi = v;
                uv[i * 6 + k * 2] = model.uvs[t * 6 + k * 2]!;
                uv[i * 6 + k * 2 + 1] = model.uvs[t * 6 + k * 2 + 1]!;
            }
            i++;
        }
        if (order.length) g.addGroup(groupStart * 3, (i - groupStart) * 3, groupTex);
        this.first = Number.isFinite(lo) ? lo : 0;
        this.count = hi >= lo ? hi - lo + 1 : 0;
        this.outPos = new Float32Array(model.positions.length);
        this.outNrm = new Float32Array(model.normals.length);
        g.setAttribute('position', new THREE.BufferAttribute(pos, 3).setUsage(THREE.DynamicDrawUsage));
        g.setAttribute('normal', new THREE.BufferAttribute(nrm, 3).setUsage(THREE.DynamicDrawUsage));
        g.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
        this.geometry = g;
        this.materials = modelMaterials(model, lookup);
        this.mesh = new THREE.Mesh(g, this.materials.length ? this.materials : undefined);
        this.mesh.scale.setScalar(MODEL_SCALE);
        this.mesh.frustumCulled = false;
        this.mesh.name = model.name;
        this.local = new Float32Array(rig.jointCount * 12);
        this.world = new Float32Array(rig.jointCount * 12);
        this.skin = new Float32Array(rig.jointCount * 12);
        this.setLocalPose(restPose(rig));
    }

    /** Deform the mesh by local joint matrices (12 floats per joint, parent relative). */
    setLocalPose(local: Float32Array): void {
        localToWorld(this.rig, local, this.world);
        skinMatrices(this.rig, this.world, this.skin);
        skinVertices(
            this.rig,
            this.skin,
            this.model.positions,
            this.model.normals,
            this.first,
            this.count,
            this.outPos,
            this.outNrm,
        );
        const pa = this.geometry.getAttribute('position') as THREE.BufferAttribute;
        const na = this.geometry.getAttribute('normal') as THREE.BufferAttribute;
        const p = pa.array as Float32Array;
        const q = na.array as Float32Array;
        const c = this.corner;
        for (let i = 0; i < c.length; i++) {
            const v = c[i]! * 3;
            p[i * 3] = this.outPos[v]!;
            p[i * 3 + 1] = this.outPos[v + 1]!;
            p[i * 3 + 2] = this.outPos[v + 2]!;
            q[i * 3] = this.outNrm[v]!;
            q[i * 3 + 1] = this.outNrm[v + 1]!;
            q[i * 3 + 2] = this.outNrm[v + 2]!;
        }
        pa.needsUpdate = true;
        na.needsUpdate = true;
    }

    /** Pose the model at `t` seconds of `clip` (wrapped or clamped like the engine); null = rest pose. */
    setClip(clip: Ps2Anim | null, t: number, opt: PoseOptions = {}): void {
        this.setLocalPose(clip ? sampleClip(this.rig, clip, t, opt, this.local) : restPose(this.rig));
    }

    /** Bounding box of the current pose in scene units. */
    bounds(): THREE.Box3 {
        this.geometry.computeBoundingBox();
        const b = this.geometry.boundingBox!.clone();
        b.min.multiplyScalar(MODEL_SCALE);
        b.max.multiplyScalar(MODEL_SCALE);
        return b;
    }

    dispose(): void {
        this.geometry.dispose();
    }
}

/** Playback clock for a clip: time in seconds, play/pause, speed, looping per clip (one-shots hold their last pose). */
export class ClipClock {
    time = 0;
    playing = true;
    speed = 1;

    constructor(public duration = 0) {}

    tick(dtSeconds: number, loops: boolean): void {
        if (!this.playing || this.duration <= 0) return;
        this.time += dtSeconds * this.speed;
        if (loops) this.time = ((this.time % this.duration) + this.duration) % this.duration;
        else this.time = Math.min(this.time, this.duration);
    }

    /** True when a one-shot clip reached its end. */
    finished(loops: boolean): boolean {
        return !loops && this.duration > 0 && this.time >= this.duration;
    }
}
