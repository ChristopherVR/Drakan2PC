import * as THREE from 'three';
import { clipFitsRig, clipLoops, type Model, type Ps2Anim, type Rig } from '@drakan/formats';
import { MODEL_SCALE, docInstanceMatrix, type DocPlacement } from './models.ts';
import { ClipClock, SkinnedModel } from './skinned.ts';
import { chooseActive, phaseOffset, poseInterval, MAX_ANIMATED, type AnimCandidate } from './animplan.ts';
import type { TextureLookup } from './textures.ts';

export interface ClipRef {
    stem: string;
    id: number;
}

export interface ClassClip extends ClipRef {
    name: string;
    /** Class field(s) that name it (Base Idle), empty for plain model clips. */
    label: string;
}

/** Everything needed to skin a class: model, rig, its clips (class fields first) and the default idle. */
export interface ClassRig {
    model: Model;
    lookup: TextureLookup;
    rig: Rig;
    clips: ClassClip[];
    idle: ClipRef | null;
}

/** Where rigs and clips come from (the worker through `RefService` in the app, fakes in tests). */
export interface AnimSource {
    /** Cheap check: can objects of this class animate (it has animation fields)? */
    canAnimate(resource: string): boolean;
    load(resource: string): Promise<ClassRig | null>;
    clip(ref: ClipRef): Promise<Ps2Anim | null>;
}

export interface AnimObject extends DocPlacement {
    id: number;
    resource: string;
}

interface Inst {
    obj: AnimObject;
    matrix: THREE.Matrix4;
    center: THREE.Vector3;
    sk: SkinnedModel | null;
    clock: ClipClock;
    clip: Ps2Anim | null;
    /** Key of the clip being (or already) loaded for this instance. */
    clipKey: string;
    fits: boolean;
    active: boolean;
    acc: number;
    due: number;
    lastUse: number;
}

const keyOf = (c: ClipRef): string => `${c.stem}:${c.id}`;
const tmpSphere = new THREE.Sphere();
const frustum = new THREE.Frustum();
const projView = new THREE.Matrix4();

/** Skinned instances alive (active or cached) at most; the least recently shown are freed past this. */
const MAX_ALLOCATED = 96;
/** CPU budget for posing per frame in milliseconds. */
const POSE_BUDGET_MS = 5;

/**
 * Animated characters in the level viewport. Placed instances of skinned classes are drawn bind-pose by the instanced
 * model batches; this layer swaps the nearest on-screen ones for CPU-skinned meshes playing a clip (the class's idle
 * unless overridden per instance), pauses everything off-screen or far away, re-poses at a distance-dependent rate and
 * shares the decoded clips and class materials between instances.
 */
export class AnimationLayer {
    readonly group = new THREE.Group();
    enabled = true;
    /** Scene units: instances farther than this stay in bind pose. */
    range = 600;
    private source: AnimSource | null = null;
    private readonly insts = new Map<number, Inst>();
    private readonly rigs = new Map<string, ClassRig | null | 'loading'>();
    private readonly mats = new Map<string, THREE.Material[]>();
    private readonly tints = new Map<string, THREE.Material[]>();
    private readonly clips = new Map<string, Promise<Ps2Anim | null>>();
    private readonly overrides = new Map<number, ClipRef>();
    private selected = new Set<number>();
    private overlays = new Map<number, THREE.Mesh>();
    private raycaster = new THREE.Raycaster();
    private frame = 0;
    private loading = 0;
    private dirtySelect = true;
    private tick = 0;

    constructor(
        /** Hide or show the instanced bind-pose copy of an object. */
        private readonly hide: (id: number, hidden: boolean) => void,
        /** Called when the animated set changed (outlines refresh). */
        private readonly onActiveChange: () => void = () => undefined,
    ) {
        this.group.name = 'animated';
    }

    setSource(s: AnimSource | null): void {
        this.source = s;
        this.rigs.clear();
        this.dirtySelect = true;
    }

    setEnabled(on: boolean): void {
        if (this.enabled === on) return;
        this.enabled = on;
        this.dirtySelect = true;
        if (!on) this.applyActive(new Set());
    }

    /** Placed objects drawn with their model: state of known ids is kept, gone ones are dropped. */
    setObjects(objs: readonly AnimObject[]): void {
        const src = this.source;
        const keep = new Set<number>();
        for (const o of objs) {
            if (!src?.canAnimate(o.resource)) continue;
            keep.add(o.id);
            let i = this.insts.get(o.id);
            if (!i || i.obj.resource !== o.resource) {
                if (i) this.drop(o.id);
                i = {
                    obj: o,
                    matrix: new THREE.Matrix4(),
                    center: new THREE.Vector3(),
                    sk: null,
                    clock: new ClipClock(0),
                    clip: null,
                    clipKey: '',
                    fits: true,
                    active: false,
                    acc: 0,
                    due: 0,
                    lastUse: 0,
                };
                this.insts.set(o.id, i);
            }
            i.obj = o;
            docInstanceMatrix(o, i.matrix);
            i.center.setFromMatrixPosition(i.matrix);
            if (i.sk) {
                i.sk.mesh.matrix.copy(i.matrix);
                i.sk.mesh.matrixWorldNeedsUpdate = true;
            }
        }
        for (const id of [...this.insts.keys()]) if (!keep.has(id)) this.drop(id);
        this.dirtySelect = true;
    }

    private drop(id: number): void {
        const i = this.insts.get(id);
        if (!i) return;
        if (i.active) {
            this.deactivate(i);
            this.onActiveChange();
        }
        this.free(i);
        this.insts.delete(id);
    }

    private free(i: Inst): void {
        if (!i.sk) return;
        this.group.remove(i.sk.mesh);
        this.overlays.get(i.obj.id)?.removeFromParent();
        this.overlays.delete(i.obj.id);
        i.sk.dispose();
        i.sk = null;
    }

    // ---- clips ----------------------------------------------------------------------------------------------------

    /** Per-instance clip override (the Details picker); null goes back to the class idle. */
    setClip(id: number, ref: ClipRef | null): void {
        if (ref) this.overrides.set(id, ref);
        else this.overrides.delete(id);
        const i = this.insts.get(id);
        if (i) {
            i.clipKey = '';
            i.clip = null;
            const r = this.rigOf(i.obj.resource);
            if (r && i.active) this.wantClip(i, r);
        }
    }

    /** True when objects of this class can animate (a source exists and says so). */
    canAnimate(resource: string): boolean {
        return this.source?.canAnimate(resource) ?? false;
    }

    clipOf(id: number): ClipRef | null {
        return this.overrides.get(id) ?? null;
    }

    /** Clip currently playing on an instance (tests and the status line). */
    playing(id: number): { key: string; name: string; time: number; duration: number } | null {
        const i = this.insts.get(id);
        if (!i?.active || !i.clip) return null;
        return { key: i.clipKey, name: i.clip.name, time: i.clock.time, duration: i.clip.duration };
    }

    private clipData(ref: ClipRef): Promise<Ps2Anim | null> {
        const k = keyOf(ref);
        let p = this.clips.get(k);
        if (!p) {
            p = (this.source ? this.source.clip(ref) : Promise.resolve(null)).catch(() => null);
            this.clips.set(k, p);
        }
        return p;
    }

    private wantClip(i: Inst, rig: ClassRig): void {
        const ref = this.overrides.get(i.obj.id) ?? rig.idle;
        const key = ref ? keyOf(ref) : '-';
        if (i.clipKey === key) return;
        i.clipKey = key;
        i.clip = null;
        i.fits = true;
        if (!ref) return;
        void this.clipData(ref).then((a) => {
            if (i.clipKey !== key) return;
            if (!a || !clipFitsRig(rig.rig, a)) {
                i.fits = false;
                return;
            }
            i.clip = a;
            i.clock.duration = a.duration;
            i.clock.time = phaseOffset(i.obj.id, a.duration);
            i.clock.playing = true;
            i.due = 0;
        });
    }

    // ---- rigs -----------------------------------------------------------------------------------------------------

    /** Ask for a class's rig now (selection, the Details picker); resolves with it or null. */
    async want(resource: string): Promise<ClassRig | null> {
        const have = this.rigs.get(resource);
        if (have && have !== 'loading') return have;
        if (have === null || !this.source) return null;
        if (have === 'loading') {
            await new Promise((r) => setTimeout(r, 50));
            return this.want(resource);
        }
        return this.loadRig(resource);
    }

    private async loadRig(resource: string): Promise<ClassRig | null> {
        this.rigs.set(resource, 'loading');
        this.loading++;
        const src = this.source;
        try {
            const r = src ? await src.load(resource) : null;
            if (this.source !== src) return null;
            this.rigs.set(resource, r);
            this.dirtySelect = true;
            return r;
        } catch {
            this.rigs.set(resource, null);
            return null;
        } finally {
            this.loading--;
        }
    }

    rigOf(resource: string): ClassRig | null {
        const r = this.rigs.get(resource);
        return r && r !== 'loading' ? r : null;
    }

    private materialsFor(resource: string, r: ClassRig): THREE.Material[] {
        let m = this.mats.get(resource);
        if (!m) {
            m = r.model.textures.map((t) => {
                const tex = r.lookup(t.id | (t.db << 16));
                return tex
                    ? new THREE.MeshLambertMaterial({ map: tex, side: THREE.DoubleSide, alphaTest: 0.5 })
                    : new THREE.MeshLambertMaterial({ color: 0x9aa0a6, side: THREE.DoubleSide });
            });
            this.mats.set(resource, m);
        }
        return m;
    }

    private tintFor(resource: string, r: ClassRig): THREE.Material[] {
        let m = this.tints.get(resource);
        if (!m) {
            m = this.materialsFor(resource, r).map((src) => {
                const c = src.clone() as THREE.MeshLambertMaterial;
                c.emissive = new THREE.Color(0xffc83d).multiplyScalar(0.28);
                c.polygonOffset = true;
                c.polygonOffsetFactor = -2;
                c.polygonOffsetUnits = -2;
                c.depthFunc = THREE.LessEqualDepth;
                return c;
            });
            this.tints.set(resource, m);
        }
        return m;
    }

    // ---- per frame ------------------------------------------------------------------------------------------------

    isAnimated(id: number): boolean {
        return this.insts.get(id)?.active ?? false;
    }

    get stats(): { candidates: number; active: number; allocated: number; clips: number } {
        let active = 0;
        let allocated = 0;
        for (const i of this.insts.values()) {
            if (i.active) active++;
            if (i.sk) allocated++;
        }
        return { candidates: this.insts.size, active, allocated, clips: this.clips.size };
    }

    /** Advance animation by `dt` seconds for what the camera sees. Cheap when nothing can animate. */
    update(dt: number, camera: THREE.Camera): void {
        if (!this.enabled || !this.source || !this.insts.size) return;
        this.tick++;
        for (const i of this.insts.values()) if (i.active) i.acc += dt;
        if (this.dirtySelect || ++this.frame % 5 === 0) {
            this.dirtySelect = false;
            this.select(camera);
        }
        const t0 = performance.now();
        const act = [...this.insts.values()].filter((i) => i.active && i.sk && i.clip && i.fits);
        act.sort((a, b) => a.due - b.due);
        for (const i of act) {
            if (performance.now() - t0 > POSE_BUDGET_MS) break;
            if (this.tick < i.due) continue;
            i.clock.tick(i.acc, clipLoops(i.clip!));
            i.acc = 0;
            i.sk!.setClip(i.clip, i.clock.time, { inPlace: true });
            // Interval in frames at about 60 fps.
            const dist = camera.position.distanceTo(i.center);
            i.due = this.tick + Math.max(1, Math.round(poseInterval(dist) * 60));
        }
    }

    private select(camera: THREE.Camera): void {
        camera.updateMatrixWorld();
        projView.multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse);
        frustum.setFromProjectionMatrix(projView);
        const cands: AnimCandidate[] = [];
        for (const i of this.insts.values()) {
            const dist = camera.position.distanceTo(i.center);
            tmpSphere.set(i.center, MODEL_SCALE * 0.7);
            const visible = frustum.intersectsSphere(tmpSphere);
            const r = this.rigs.get(i.obj.resource);
            if (r === undefined) {
                // Load rigs only for classes that are near and on screen, a few at a time.
                if (dist <= this.range && visible && this.loading < 2) void this.loadRig(i.obj.resource);
                continue;
            }
            if (!r || r === 'loading') continue;
            cands.push({ id: i.obj.id, dist, visible });
        }
        this.applyActive(new Set(chooseActive(cands, this.range, MAX_ANIMATED)));
    }

    private applyActive(want: Set<number>): void {
        let changed = false;
        for (const i of this.insts.values()) {
            const on = want.has(i.obj.id);
            if (on === i.active) continue;
            changed = true;
            if (on) this.activate(i);
            else this.deactivate(i);
        }
        if (changed) {
            this.evict();
            this.refreshOverlays();
            this.onActiveChange();
        }
    }

    private activate(i: Inst): void {
        const r = this.rigOf(i.obj.resource);
        if (!r) return;
        if (!i.sk) {
            i.sk = new SkinnedModel(r.model, r.rig, r.lookup, this.materialsFor(i.obj.resource, r));
            i.sk.mesh.matrixAutoUpdate = false;
            i.sk.mesh.matrix.copy(i.matrix);
            i.sk.mesh.matrixWorldNeedsUpdate = true;
            i.sk.geo.computeBoundingSphere();
            if (i.sk.geo.boundingSphere) i.sk.geo.boundingSphere.radius *= 1.5;
        }
        i.active = true;
        i.lastUse = this.tick;
        i.due = 0;
        i.sk.mesh.visible = true;
        this.group.add(i.sk.mesh);
        this.wantClip(i, r);
        if (i.clip) i.sk.setClip(i.clip, i.clock.time, { inPlace: true });
        this.hide(i.obj.id, true);
    }

    private deactivate(i: Inst): void {
        i.active = false;
        i.lastUse = this.tick;
        if (i.sk) this.group.remove(i.sk.mesh);
        this.overlays.get(i.obj.id)?.removeFromParent();
        this.overlays.delete(i.obj.id);
        this.hide(i.obj.id, false);
    }

    private evict(): void {
        const held = [...this.insts.values()].filter((i) => i.sk);
        if (held.length <= MAX_ALLOCATED) return;
        const idle = held.filter((i) => !i.active).sort((a, b) => a.lastUse - b.lastUse);
        for (const i of idle.slice(0, held.length - MAX_ALLOCATED)) this.free(i);
    }

    // ---- selection highlight and picking ----------------------------------------------------------------------------

    /** Tinted overlay on animated selected instances (the instanced outline only knows the bind pose). */
    setSelected(ids: Iterable<number>): void {
        this.selected = new Set(ids);
        this.refreshOverlays();
    }

    private refreshOverlays(): void {
        for (const [id, m] of this.overlays) {
            const i = this.insts.get(id);
            if (!i?.active || !this.selected.has(id)) {
                m.removeFromParent();
                this.overlays.delete(id);
            }
        }
        for (const id of this.selected) {
            const i = this.insts.get(id);
            if (!i?.active || !i.sk || this.overlays.has(id)) continue;
            const r = this.rigOf(i.obj.resource);
            if (!r) continue;
            const m = new THREE.Mesh(i.sk.geo, this.tintFor(i.obj.resource, r));
            m.renderOrder = 5;
            m.frustumCulled = false;
            i.sk.mesh.add(m);
            this.overlays.set(id, m);
        }
    }

    /** Nearest animated instance under the ray. */
    pick(
        ray: THREE.Ray,
        skip?: (id: number) => boolean,
    ): { id: number; distance: number; point: THREE.Vector3 } | null {
        this.raycaster.ray.copy(ray);
        let best: { id: number; distance: number; point: THREE.Vector3 } | null = null;
        for (const i of this.insts.values()) {
            if (!i.active || !i.sk || skip?.(i.obj.id)) continue;
            i.sk.mesh.updateWorldMatrix(true, false);
            const hit = this.raycaster.intersectObject(i.sk.mesh, false)[0];
            if (hit && (!best || hit.distance < best.distance)) {
                best = { id: i.obj.id, distance: hit.distance, point: hit.point };
            }
        }
        return best;
    }

    /** Forget everything level specific; the source and the enabled flag stay. */
    reset(): void {
        for (const id of [...this.insts.keys()]) this.drop(id);
        for (const list of this.mats.values()) for (const m of list) m.dispose();
        for (const list of this.tints.values()) for (const m of list) m.dispose();
        this.mats.clear();
        this.tints.clear();
        this.rigs.clear();
        this.clips.clear();
        this.overrides.clear();
        this.overlays.clear();
        this.selected.clear();
    }

    dispose(): void {
        this.reset();
        this.group.clear();
    }
}
