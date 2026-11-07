import * as THREE from 'three';
import { resourceString } from '@drakan/editor-core';
import type { Model } from '@drakan/formats';
import type { ModelData } from './decode.ts';
import {
    clearModelCache,
    docInstanceMatrix,
    modelGeometry,
    modelMaterials,
    MODEL_SCALE,
    type DocPlacement,
} from './models.ts';
import { AnimationLayer } from './animlayer.ts';
import { helperGroupOfRfl, type HelperGroup } from './instances.ts';
import { drawResource, standInResource, type StandInField } from './standin.ts';
import { nearestHit } from './pickmath.ts';
import type { TerrainScene } from './terrain.ts';
import type { TextureCache, TextureLookup } from './textures.ts';

export interface LayerObject extends DocPlacement {
    id: number;
    resource: string;
    /** Per-object parameters: an object naming its own stand-in models is drawn under its own key (`drawResource`). */
    fields?: readonly StandInField[];
}

interface Entry {
    resource: string;
    model: Model;
    lookup: TextureLookup;
    geometry: THREE.BufferGeometry;
    materials: THREE.Material[];
    /** Model-space bounding radius (for the outline thickness). */
    radius: number;
}

interface Batch {
    mesh: THREE.InstancedMesh;
    ids: number[];
    capacity: number;
}

export interface LayerHit {
    id: number;
    distance: number;
    point: THREE.Vector3;
}

const tmpM = new THREE.Matrix4();
const ZERO = new THREE.Matrix4().makeScale(0, 0, 0);

/** Selection boxes are capped: a huge box select would otherwise create thousands of line objects. */
const MAX_BOXES = 300;

/**
 * One highlight slot (selection or hover): the same instanced model drawn again with an emissive tint (keeps the
 * alpha-tested silhouette), plus an oriented bounding box outline per object.
 */
class Outline {
    readonly group = new THREE.Group();
    private readonly batches = new Map<string, Batch>();
    private readonly mats = new Map<string, THREE.Material[]>();
    private readonly boxes = new THREE.Group();
    private readonly boxMat: THREE.LineBasicMaterial;
    private readonly boxGeo = new THREE.EdgesGeometry(new THREE.BoxGeometry(1, 1, 1));
    private readonly tint: THREE.Color;
    private animatedBoxes: LayerObject[] = [];

    constructor(
        color: number,
        private readonly layer: ModelLayer,
    ) {
        this.tint = new THREE.Color(color).multiplyScalar(0.28);
        this.boxMat = new THREE.LineBasicMaterial({ color, depthTest: false, transparent: true });
        this.boxes.renderOrder = 20;
        this.group.add(this.boxes);
    }

    private materials(e: Entry): THREE.Material[] {
        let m = this.mats.get(e.resource);
        if (!m) {
            m = e.materials.map((src) => {
                const c = src.clone() as THREE.MeshLambertMaterial;
                c.emissive = this.tint.clone();
                c.polygonOffset = true;
                c.polygonOffsetFactor = -2;
                c.polygonOffsetUnits = -2;
                c.depthFunc = THREE.LessEqualDepth;
                return c;
            });
            this.mats.set(e.resource, m);
        }
        return m;
    }

    set(objects: readonly LayerObject[]): void {
        const by = new Map<string, LayerObject[]>();
        this.animatedBoxes = [];
        for (const o of objects) {
            const key = drawResource(o);
            if (!this.layer.has(key)) continue;
            // Animated instances get their tint from the animation layer (the batch only knows the bind pose).
            const animated = this.layer.anim.isAnimated(o.id);
            if (animated) {
                this.animatedBoxes.push(o);
                continue;
            }
            let l = by.get(key);
            if (!l) by.set(key, (l = []));
            l.push(o);
        }
        for (const [res, b] of this.batches) {
            if (!by.has(res)) {
                this.group.remove(b.mesh);
                b.mesh.dispose();
                this.batches.delete(res);
            }
        }
        for (const l of this.boxes.children) (l as THREE.LineSegments).removeFromParent();
        this.boxes.clear();
        let nBox = 0;
        for (const [res, list] of by) {
            const e = this.layer.entry(res)!;
            let b = this.batches.get(res);
            if (!b || b.capacity < list.length) {
                if (b) {
                    this.group.remove(b.mesh);
                    b.mesh.dispose();
                }
                const capacity = Math.max(list.length, 8);
                const mesh = new THREE.InstancedMesh(e.geometry, this.materials(e), capacity);
                mesh.frustumCulled = false;
                mesh.renderOrder = 5;
                b = { mesh, ids: [], capacity };
                this.batches.set(res, b);
                this.group.add(mesh);
            }
            b.ids = list.map((o) => o.id);
            b.mesh.count = list.length;
            const { min, max } = e.model.bounds;
            const local = new THREE.Matrix4().compose(
                new THREE.Vector3((min[0] + max[0]) / 2, (min[1] + max[1]) / 2, (min[2] + max[2]) / 2),
                new THREE.Quaternion(),
                new THREE.Vector3(
                    Math.max(max[0] - min[0], 1e-3),
                    Math.max(max[1] - min[1], 1e-3),
                    Math.max(max[2] - min[2], 1e-3),
                ),
            );
            list.forEach((o, i) => {
                docInstanceMatrix(o, tmpM);
                b!.mesh.setMatrixAt(i, tmpM);
                if (nBox < MAX_BOXES) {
                    const box = new THREE.LineSegments(this.boxGeo, this.boxMat);
                    box.matrixAutoUpdate = false;
                    box.matrix.multiplyMatrices(tmpM, local);
                    box.frustumCulled = false;
                    box.renderOrder = 20;
                    this.boxes.add(box);
                    nBox++;
                }
            });
            b.mesh.instanceMatrix.needsUpdate = true;
        }
        for (const o of this.animatedBoxes) {
            const e = this.layer.entry(drawResource(o));
            if (!e || nBox >= MAX_BOXES) continue;
            const { min, max } = e.model.bounds;
            const local = new THREE.Matrix4().compose(
                new THREE.Vector3((min[0] + max[0]) / 2, (min[1] + max[1]) / 2, (min[2] + max[2]) / 2),
                new THREE.Quaternion(),
                new THREE.Vector3(
                    Math.max(max[0] - min[0], 1e-3),
                    Math.max(max[1] - min[1], 1e-3),
                    Math.max(max[2] - min[2], 1e-3),
                ),
            );
            docInstanceMatrix(o, tmpM);
            const box = new THREE.LineSegments(this.boxGeo, this.boxMat);
            box.matrixAutoUpdate = false;
            box.matrix.multiplyMatrices(tmpM, local);
            box.frustumCulled = false;
            box.renderOrder = 20;
            this.boxes.add(box);
            nBox++;
        }
    }

    /** Drop everything cached per class (a new level reuses resource strings for other classes). */
    forget(): void {
        this.set([]);
        for (const list of this.mats.values()) for (const m of list) m.dispose();
        this.mats.clear();
    }

    dispose(): void {
        for (const b of this.batches.values()) b.mesh.dispose();
        for (const list of this.mats.values()) for (const m of list) m.dispose();
        this.boxGeo.dispose();
        this.boxMat.dispose();
        this.batches.clear();
        this.mats.clear();
    }
}

/**
 * Real visuals for the level editor: terrain plus one InstancedMesh per class with a decoded model. It keeps the
 * bookkeeping (which instance index is which object) so picking, outlines and the drag ghost work on models.
 */
export class ModelLayer {
    readonly group = new THREE.Group();
    readonly terrainRoot = new THREE.Group();
    readonly root = new THREE.Group();
    terrain: TerrainScene | null = null;
    private readonly entries = new Map<string, Entry>();
    private readonly failed = new Set<string>();
    /** Classes without a visible model of their own (normal for logic and helpers), with their engine class id. */
    private readonly modelless = new Map<string, number | null>();
    private readonly batches = new Map<string, Batch>();
    private readonly ghosts = new Map<string, THREE.Object3D>();
    private readonly sel: Outline;
    private readonly hov: Outline;
    private raycaster = new THREE.Raycaster();
    /** Animated characters: nearest on-screen skinned instances replace their bind-pose batch entries. */
    readonly anim: AnimationLayer;
    /** Instances of the batches that are hidden because the animation layer draws them. */
    private readonly hiddenInst = new Set<number>();
    private readonly placed = new Map<number, LayerObject>();
    private readonly slot = new Map<number, { b: Batch; i: number }>();
    private selObjs: readonly LayerObject[] = [];
    private hovObjs: readonly LayerObject[] = [];
    /** Draw calls the last `sync` produced (one per class with instances). */
    batchCount = 0;

    constructor() {
        this.group.name = 'models';
        this.terrainRoot.name = 'terrain-root';
        this.sel = new Outline(0xffc83d, this);
        this.hov = new Outline(0x8fd3ff, this);
        this.anim = new AnimationLayer(
            (id, hidden) => this.hideInstance(id, hidden),
            () => {
                this.sel.set(this.selObjs);
                this.hov.set(this.hovObjs);
            },
        );
        this.root.add(this.terrainRoot, this.group, this.anim.group, this.sel.group, this.hov.group);
    }

    private hideInstance(id: number, hidden: boolean): void {
        if (hidden) this.hiddenInst.add(id);
        else this.hiddenInst.delete(id);
        const at = this.slot.get(id);
        const o = this.placed.get(id);
        if (!at || !o) return;
        if (hidden) at.b.mesh.setMatrixAt(at.i, ZERO);
        else at.b.mesh.setMatrixAt(at.i, docInstanceMatrix(o, tmpM));
        at.b.mesh.instanceMatrix.needsUpdate = true;
    }

    // ---- terrain ------------------------------------------------------------------------------------------------

    setTerrain(t: TerrainScene | null): void {
        if (this.terrain) {
            this.terrainRoot.remove(this.terrain.group);
            this.terrain.dispose();
        }
        this.terrain = t;
        if (t) this.terrainRoot.add(t.group);
    }

    /** Scene-space bounds of the terrain, or null without one. */
    terrainBounds(): THREE.Box3 | null {
        if (!this.terrain) return null;
        const b = new THREE.Box3().setFromObject(this.terrain.group);
        return b.isEmpty() ? null : b;
    }

    setTerrainWireframe(on: boolean): void {
        const seen = new Set<THREE.Material>();
        this.terrain?.group.traverse((o) => {
            const m = (o as THREE.Mesh).material as THREE.Material | undefined;
            if (m && !seen.has(m)) {
                seen.add(m);
                (m as THREE.MeshLambertMaterial).wireframe = on;
            }
        });
    }

    // ---- models -------------------------------------------------------------------------------------------------

    has(resource: string): boolean {
        return this.entries.has(resource);
    }

    /** True once the class was tried: it has a model, has none of its own, or decoding failed (so a marker stays). */
    known(resource: string): boolean {
        return this.entries.has(resource) || this.failed.has(resource) || this.modelless.has(resource);
    }

    entry(resource: string): Entry | undefined {
        return this.entries.get(resource);
    }

    /** Classes with a decoded model. */
    get entryCount(): number {
        return this.entries.size;
    }

    /** Model and texture lookup of a loaded class (thumbnails). */
    modelOf(resource: string): { model: Model; lookup: TextureLookup } | null {
        const e = this.entries.get(resource);
        return e ? { model: e.model, lookup: e.lookup } : null;
    }

    /** Classes that name a model which did not resolve or decode (a marker stays for their objects). */
    get failedResources(): ReadonlySet<string> {
        return this.failed;
    }

    /** Classes with no visible model of their own: detectors, timers, generators, level logic. Not an error. */
    get modellessResources(): ReadonlySet<string> {
        return new Set(this.modelless.keys());
    }

    get modellessCount(): number {
        return this.modelless.size;
    }

    /**
     * Editor icon group of a class without a model of its own, from its engine class (a converted Order of the Flame
     * "Orc Plate waypoint" or "Soul Flare Generator" is named nothing like the disc's helpers). Null when it has no icon,
     * undefined when the class is not known to be model-less.
     */
    modellessGroup(resource: string): HelperGroup | null | undefined {
        if (!this.modelless.has(resource)) return undefined;
        const rfl = this.modelless.get(resource);
        return rfl === null || rfl === undefined ? null : helperGroupOfRfl(rfl);
    }

    /** Register worker results. Returns the resources that now have a model. */
    addModelData(data: readonly ModelData[], cache: TextureCache): string[] {
        const ok: string[] = [];
        for (const d of data) {
            const base = resourceString(d.dbIndex, d.classId);
            const resource = d.standIn ? standInResource(base, d.standIn) : base;
            if (this.entries.has(resource)) continue;
            // No model named, or one with no polygons ("Bounding Vertices": an invisible mover or pickup point).
            if (d.model ? !d.model.triTexture.length : d.noModel) {
                this.modelless.set(resource, d.rfl ?? null);
                continue;
            }
            if (!d.model) {
                this.failed.add(resource);
                continue;
            }
            this.modelless.delete(resource);
            const lookup = cache.addShared(d.textures);
            const geometry = modelGeometry(d.model, resource);
            const radius = geometry.boundingSphere?.radius ?? 0.1;
            const mats = modelMaterials(d.model, lookup);
            this.entries.set(resource, {
                resource,
                model: d.model,
                lookup,
                geometry,
                materials: mats.length
                    ? mats
                    : [new THREE.MeshLambertMaterial({ color: 0x9aa0a6, side: THREE.DoubleSide })],
                radius,
            });
            this.failed.delete(resource);
            ok.push(resource);
        }
        return ok;
    }

    // ---- instances ----------------------------------------------------------------------------------------------

    /** Rebuild the instance batches for `byResource` (classes without an entry are ignored). */
    sync(byResource: ReadonlyMap<string, readonly LayerObject[]>): void {
        for (const [res, b] of this.batches) {
            if (!byResource.has(res) || !this.entries.has(res)) {
                b.mesh.visible = false;
                b.mesh.count = 0;
                b.ids = [];
            }
        }
        let n = 0;
        this.placed.clear();
        this.slot.clear();
        for (const [res, list] of byResource) {
            const e = this.entries.get(res);
            if (!e || !list.length) continue;
            let b = this.batches.get(res);
            if (!b || b.capacity < list.length) {
                if (b) {
                    this.group.remove(b.mesh);
                    b.mesh.dispose();
                }
                const capacity = Math.ceil(list.length * 1.25) + 4;
                const mesh = new THREE.InstancedMesh(e.geometry, e.materials, capacity);
                mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
                mesh.name = e.model.name;
                b = { mesh, ids: [], capacity };
                this.batches.set(res, b);
                this.group.add(mesh);
            }
            b.ids = list.map((o) => o.id);
            b.mesh.count = list.length;
            b.mesh.visible = true;
            list.forEach((o, i) => {
                this.placed.set(o.id, o);
                this.slot.set(o.id, { b: b!, i });
                b!.mesh.setMatrixAt(i, this.hiddenInst.has(o.id) ? ZERO : docInstanceMatrix(o, tmpM));
            });
            b.mesh.instanceMatrix.needsUpdate = true;
            b.mesh.computeBoundingSphere();
            n++;
        }
        this.batchCount = n;
    }

    /** Ids drawn with a model right now. */
    get instanceCount(): number {
        let n = 0;
        for (const b of this.batches.values()) n += b.ids.length;
        return n;
    }

    /** Instance index to object id for a class (for tests and tools). */
    idsOf(resource: string): readonly number[] {
        return this.batches.get(resource)?.ids ?? [];
    }

    /** Nearest model instance under the ray in `ray`; `skip` drops objects (locked ones). */
    pick(ray: THREE.Ray, skip?: (id: number) => boolean): LayerHit | null {
        this.raycaster.ray.copy(ray);
        const hits: (LayerHit | null)[] = [];
        for (const b of this.batches.values()) {
            if (!b.ids.length || !b.mesh.visible) continue;
            const list = this.raycaster.intersectObject(b.mesh, false);
            for (const h of list) {
                if (h.instanceId === undefined) continue;
                const id = b.ids[h.instanceId];
                if (id === undefined || skip?.(id)) continue;
                hits.push({ id, distance: h.distance, point: h.point });
                break;
            }
        }
        hits.push(this.anim.pick(ray, skip));
        return nearestHit(hits);
    }

    // ---- outlines and ghost ---------------------------------------------------------------------------------------

    setSelection(objects: readonly LayerObject[]): void {
        this.selObjs = objects;
        this.sel.set(objects);
        this.anim.setSelected(objects.map((o) => o.id));
    }

    setHover(objects: readonly LayerObject[]): void {
        this.hovObjs = objects;
        this.hov.set(objects);
    }

    /** A translucent copy of a class's model for the drag preview (origin at the object position), or null. */
    ghost(resource: string): THREE.Object3D | null {
        const hit = this.ghosts.get(resource);
        if (hit) return hit;
        const e = this.entries.get(resource);
        if (!e) return null;
        const mats = e.materials.map((m) => {
            const c = m.clone();
            c.transparent = true;
            c.opacity = 0.65;
            c.depthWrite = false;
            return c;
        });
        const mesh = new THREE.Mesh(e.geometry, mats);
        mesh.scale.setScalar(MODEL_SCALE);
        mesh.renderOrder = 12;
        this.ghosts.set(resource, mesh);
        return mesh;
    }

    /** Forget everything level specific (terrain, models, instances): resources mean other classes in another level. */
    reset(): void {
        this.setTerrain(null);
        this.anim.reset();
        this.hiddenInst.clear();
        this.placed.clear();
        this.slot.clear();
        this.selObjs = [];
        this.hovObjs = [];
        for (const b of this.batches.values()) {
            this.group.remove(b.mesh);
            b.mesh.dispose();
        }
        this.batches.clear();
        this.sel.forget();
        this.hov.forget();
        for (const g of this.ghosts.values()) {
            for (const m of ((g as THREE.Mesh).material as THREE.Material[]) ?? []) m.dispose();
        }
        this.ghosts.clear();
        this.entries.clear();
        this.failed.clear();
        this.modelless.clear();
        clearModelCache();
    }

    dispose(): void {
        this.reset();
        this.anim.dispose();
        this.sel.dispose();
        this.hov.dispose();
        this.group.clear();
    }
}
