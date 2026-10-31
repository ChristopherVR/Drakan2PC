import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { TransformControls } from 'three/examples/jsm/controls/TransformControls.js';
import {
    CommandStack,
    DeleteCommand,
    LevelDocument,
    SetPropCommand,
    TransformCommand,
    type EntityKind,
    type LevelObject,
    type Transform,
} from '@drakan/editor-core';

/** Game units to scene units. World coordinates run to about 7.4e5; y is up. */
const S = 0.001;
export const KINDS: { kind: EntityKind; color: number; label: string }[] = [
    { kind: 'player', color: 0x4ade80, label: 'Player' },
    { kind: 'creature', color: 0xef4444, label: 'Creature' },
    { kind: 'weapon', color: 0xfacc15, label: 'Weapon' },
    { kind: 'usable', color: 0xa78bfa, label: 'Usable' },
    { kind: 'static', color: 0x7a8699, label: 'Static' },
    { kind: 'camera', color: 0x38bdf8, label: 'Camera' },
    { kind: 'other', color: 0xf472b6, label: 'Other' },
];
const DRAG_PX = 4;

const tmpM = new THREE.Matrix4();
const tmpQ = new THREE.Quaternion();
const tmpE = new THREE.Euler();
const tmpV = new THREE.Vector3();
const tmpS = new THREE.Vector3();

export interface LevelViewEvents {
    onSelection(ids: number[]): void;
    onChange(): void;
}

/** three.js level view: instanced markers per entity kind, picking, gizmo, filters and the undoable document. */
export class LevelView {
    readonly renderer = new THREE.WebGLRenderer({ antialias: true });
    readonly scene = new THREE.Scene();
    readonly camera = new THREE.PerspectiveCamera(50, 1, 0.5, 20000);
    readonly orbit: OrbitControls;
    readonly gizmo: TransformControls;
    private readonly pivot = new THREE.Object3D();
    private readonly raycaster = new THREE.Raycaster();
    private meshes = new Map<EntityKind, { mesh: THREE.InstancedMesh; ids: number[] }>();
    private selMesh: THREE.InstancedMesh | null = null;
    private readonly geo: THREE.BufferGeometry;
    private raf = 0;
    private keys = new Set<string>();
    private downAt: { x: number; y: number } | null = null;
    private dragBefore: Map<number, Transform> | null = null;
    private dragStart: { c: THREE.Vector3; q: THREE.Quaternion; pos: Map<number, [number, number, number]> } | null =
        null;
    private readonly mats = new Map<EntityKind, THREE.MeshLambertMaterial>();
    private lastTick = 0;
    private dirtyInstances = true;
    private dirtySel = true;

    doc = new LevelDocument();
    stack = new CommandStack(this.doc);
    selection = new Set<number>();
    hiddenKinds = new Set<EntityKind>();
    classFilter = '';
    search = '';
    /** Count of objects that pass the filters (for the status line). */
    visibleCount = 0;
    fps = 0;
    private frames = 0;
    private fpsT = performance.now();

    constructor(
        private readonly host: HTMLElement,
        private readonly ev: LevelViewEvents,
    ) {
        this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
        host.appendChild(this.renderer.domElement);
        this.scene.background = new THREE.Color(0x14161a);
        this.scene.add(new THREE.HemisphereLight(0xffffff, 0x445066, 1.6));
        const dl = new THREE.DirectionalLight(0xffffff, 1.2);
        dl.position.set(1, 2, 1);
        this.scene.add(dl);
        const grid = new THREE.GridHelper(2000, 100, 0x3a4150, 0x24282f);
        this.scene.add(grid);

        // Marker: a pyramid whose tip points along local +x (yaw 0 faces +x, positive yaw turns toward -z, matching
        // three's rotation.y). The convention is unverified against the game; see the journal note.
        const g = new THREE.ConeGeometry(0.6, 2.4, 4);
        g.rotateZ(-Math.PI / 2);
        this.geo = g;

        this.camera.position.set(450, 600, 900);
        this.orbit = new OrbitControls(this.camera, this.renderer.domElement);
        this.orbit.enableDamping = false;
        this.orbit.target.set(450, 170, 350);
        this.orbit.mouseButtons = { LEFT: THREE.MOUSE.ROTATE, MIDDLE: THREE.MOUSE.DOLLY, RIGHT: THREE.MOUSE.PAN };

        this.gizmo = new TransformControls(this.camera, this.renderer.domElement);
        this.gizmo.size = 0.8;
        this.scene.add(this.pivot);
        this.scene.add(this.gizmo.getHelper());
        this.gizmo.addEventListener('dragging-changed', (e) => {
            const dragging = Boolean((e as unknown as { value: boolean }).value);
            this.orbit.enabled = !dragging;
            if (dragging) this.beginDrag();
            else this.endDrag();
        });
        this.gizmo.addEventListener('objectChange', () => this.liveDrag());

        const el = this.renderer.domElement;
        el.addEventListener('pointerdown', (e) => (this.downAt = { x: e.clientX, y: e.clientY }));
        el.addEventListener('pointerup', (e) => this.onPointerUp(e));
        window.addEventListener('keydown', this.onKeyDown);
        window.addEventListener('keyup', (e) => this.keys.delete(e.code));
        window.addEventListener('blur', () => this.keys.clear());
        new ResizeObserver(() => this.resize()).observe(host);
        this.resize();
        this.rebuildScheduled();
    }

    dispose(): void {
        cancelAnimationFrame(this.raf);
        window.removeEventListener('keydown', this.onKeyDown);
    }

    setActive(on: boolean): void {
        cancelAnimationFrame(this.raf);
        if (on) {
            this.resize();
            this.lastTick = performance.now();
            const loop = (): void => {
                this.raf = requestAnimationFrame(loop);
                this.frame();
            };
            loop();
        }
    }

    // ---- document ---------------------------------------------------------------------------------------------

    setDocument(doc: LevelDocument): void {
        this.doc = doc;
        this.stack = new CommandStack(doc);
        this.stack.onChange = () => this.afterCommand();
        this.selection.clear();
        this.dirtyInstances = true;
        this.updateGizmo();
        this.ev.onSelection([]);
        this.frameAll();
        this.ev.onChange();
    }

    run(cmd: Parameters<CommandStack['run']>[0]): void {
        this.stack.run(cmd);
    }

    private afterCommand(): void {
        for (const id of [...this.selection]) if (!this.doc.has(id)) this.selection.delete(id);
        this.dirtyInstances = true;
        this.updateGizmo();
        this.ev.onSelection([...this.selection]);
        this.ev.onChange();
    }

    undo(): void {
        this.stack.undo();
    }
    redo(): void {
        this.stack.redo();
    }
    deleteSelected(): void {
        if (this.selection.size) this.stack.run(new DeleteCommand([...this.selection]));
    }
    duplicateSelected(): void {
        if (!this.selection.size) return;
        const cmd = this.stack.duplicate([...this.selection]);
        this.select(cmd.ids);
    }
    setTransformField(id: number, field: 'pos' | 'rot', axis: number, value: number): void {
        const o = this.doc.get(id);
        if (!o || !Number.isFinite(value)) return;
        const before = structuredClone(o.transform);
        const after = structuredClone(o.transform);
        after[field][axis] = value;
        this.stack.run(new TransformCommand(new Map([[id, before]]), new Map([[id, after]])));
    }
    setProp(id: number, key: string, value: string | number | boolean | undefined): void {
        this.stack.run(new SetPropCommand(id, key, value));
    }

    // ---- filters / selection ----------------------------------------------------------------------------------

    passes(o: LevelObject): boolean {
        if (this.hiddenKinds.has(o.kind)) return false;
        if (this.classFilter && !o.class.toLowerCase().includes(this.classFilter)) return false;
        if (this.search) {
            const q = this.search;
            const hay = `${o.id} ${o.class} ${o.resource} ${o.model} ${o.kind} ${o.props.handle ?? ''}`.toLowerCase();
            if (!hay.includes(q)) return false;
        }
        return true;
    }

    setFilters(f: { hidden?: Set<EntityKind>; cls?: string; search?: string }): void {
        if (f.hidden) this.hiddenKinds = f.hidden;
        if (f.cls !== undefined) this.classFilter = f.cls.trim().toLowerCase();
        if (f.search !== undefined) this.search = f.search.trim().toLowerCase();
        for (const id of [...this.selection]) {
            const o = this.doc.get(id);
            if (!o || !this.passes(o)) this.selection.delete(id);
        }
        this.dirtyInstances = true;
        this.updateGizmo();
        this.ev.onSelection([...this.selection]);
    }

    select(ids: number[], additive = false): void {
        if (!additive) this.selection.clear();
        for (const id of ids) {
            if (additive && this.selection.has(id)) this.selection.delete(id);
            else if (this.doc.has(id)) this.selection.add(id);
        }
        this.dirtySel = true;
        this.updateGizmo();
        this.ev.onSelection([...this.selection]);
    }

    selectVisible(): void {
        const ids: number[] = [];
        for (const o of this.doc.objects()) if (this.passes(o)) ids.push(o.id);
        this.select(ids);
    }

    private pick(e: PointerEvent): number | null {
        const r = this.renderer.domElement.getBoundingClientRect();
        const ndc = new THREE.Vector2(
            ((e.clientX - r.left) / r.width) * 2 - 1,
            -((e.clientY - r.top) / r.height) * 2 + 1,
        );
        this.raycaster.setFromCamera(ndc, this.camera);
        let best: { d: number; id: number } | null = null;
        for (const { mesh, ids } of this.meshes.values()) {
            const hit = this.raycaster.intersectObject(mesh, false)[0];
            if (hit && hit.instanceId !== undefined && (!best || hit.distance < best.d)) {
                best = { d: hit.distance, id: ids[hit.instanceId]! };
            }
        }
        return best ? best.id : null;
    }

    private onPointerUp(e: PointerEvent): void {
        const d = this.downAt;
        this.downAt = null;
        if (!d || e.button !== 0 || this.gizmo.dragging) return;
        if (Math.hypot(e.clientX - d.x, e.clientY - d.y) > DRAG_PX) return;
        if (this.gizmo.axis) return;
        const id = this.pick(e);
        if (id === null) {
            if (!e.shiftKey) this.select([]);
        } else this.select([id], e.shiftKey || e.ctrlKey);
    }

    // ---- gizmo ------------------------------------------------------------------------------------------------

    private centroid(): THREE.Vector3 | null {
        if (!this.selection.size) return null;
        const c = new THREE.Vector3();
        for (const id of this.selection) {
            const p = this.doc.get(id)!.transform.pos;
            c.add(tmpV.set(p[0] * S, p[1] * S, p[2] * S));
        }
        return c.multiplyScalar(1 / this.selection.size);
    }

    private updateGizmo(): void {
        const c = this.centroid();
        if (!c) {
            this.gizmo.detach();
            return;
        }
        this.pivot.position.copy(c);
        this.pivot.quaternion.identity();
        this.gizmo.attach(this.pivot);
    }

    setMode(m: 'translate' | 'rotate'): void {
        this.gizmo.setMode(m);
        if (m === 'rotate') this.gizmo.showX = this.gizmo.showZ = false;
        else this.gizmo.showX = this.gizmo.showZ = true;
        this.gizmo.showY = true;
    }

    private beginDrag(): void {
        this.dragBefore = new Map();
        const pos = new Map<number, [number, number, number]>();
        for (const id of this.selection) {
            const o = this.doc.get(id)!;
            this.dragBefore.set(id, structuredClone(o.transform));
            pos.set(id, [...o.transform.pos]);
        }
        this.dragStart = { c: this.pivot.position.clone(), q: this.pivot.quaternion.clone(), pos };
    }

    private liveDrag(): void {
        const s = this.dragStart;
        const before = this.dragBefore;
        if (!s || !before) return;
        const dq = this.pivot.quaternion.clone().multiply(s.q.clone().invert());
        const dp = this.pivot.position.clone().sub(s.c);
        for (const id of this.selection) {
            const o = this.doc.get(id)!;
            const b = before.get(id)!;
            const p0 = s.pos.get(id)!;
            tmpV.set(p0[0] * S, p0[1] * S, p0[2] * S)
                .sub(s.c)
                .applyQuaternion(dq)
                .add(s.c)
                .add(dp);
            o.transform.pos = [tmpV.x / S, tmpV.y / S, tmpV.z / S];
            if (this.gizmo.mode === 'rotate') {
                tmpQ.setFromEuler(tmpE.set(b.rot[0], b.rot[1], b.rot[2]));
                tmpQ.premultiply(dq);
                tmpE.setFromQuaternion(tmpQ);
                o.transform.rot = [tmpE.x, tmpE.y, tmpE.z];
            }
        }
        this.dirtyInstances = true;
        this.dirtySel = true;
    }

    private endDrag(): void {
        const before = this.dragBefore;
        this.dragBefore = null;
        this.dragStart = null;
        if (!before) return;
        const after = new Map<number, Transform>();
        let moved = false;
        for (const id of before.keys()) {
            const o = this.doc.get(id);
            if (!o) continue;
            after.set(id, structuredClone(o.transform));
            const b = before.get(id)!;
            if (b.pos.some((v, i) => v !== o.transform.pos[i]) || b.rot.some((v, i) => v !== o.transform.rot[i])) {
                moved = true;
            }
        }
        if (moved) this.stack.run(new TransformCommand(before, after));
        this.updateGizmo();
    }

    // ---- camera -----------------------------------------------------------------------------------------------

    frameSelection(): void {
        const ids = this.selection.size ? [...this.selection] : null;
        const box = new THREE.Box3();
        for (const o of this.doc.objects()) {
            if (ids ? !ids.includes(o.id) : !this.passes(o)) continue;
            box.expandByPoint(tmpV.set(o.transform.pos[0] * S, o.transform.pos[1] * S, o.transform.pos[2] * S));
        }
        if (box.isEmpty()) return;
        const c = box.getCenter(new THREE.Vector3());
        const r = Math.max(box.getSize(new THREE.Vector3()).length() / 2, 8);
        const dir = this.camera.position.clone().sub(this.orbit.target).normalize();
        this.orbit.target.copy(c);
        this.camera.position.copy(c).addScaledVector(dir, r * 2.2);
        this.orbit.update();
    }

    frameAll(): void {
        const keep = this.selection;
        this.selection = new Set();
        this.frameSelection();
        this.selection = keep;
    }

    private readonly onKeyDown = (e: KeyboardEvent): void => {
        const t = e.target as HTMLElement | null;
        if (t && /^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName)) return;
        if (!this.host.offsetParent) return;
        const ctrl = e.ctrlKey || e.metaKey;
        if (ctrl && e.code === 'KeyZ') {
            e.preventDefault();
            if (e.shiftKey) this.redo();
            else this.undo();
        } else if (ctrl && e.code === 'KeyY') {
            e.preventDefault();
            this.redo();
        } else if (ctrl && e.code === 'KeyD') {
            e.preventDefault();
            this.duplicateSelected();
        } else if (ctrl && e.code === 'KeyA') {
            e.preventDefault();
            this.selectVisible();
        } else if (!ctrl && (e.code === 'Delete' || e.code === 'Backspace')) {
            this.deleteSelected();
        } else if (!ctrl && e.code === 'KeyF') {
            this.frameSelection();
        } else if (!ctrl && e.code === 'Digit1') {
            this.setMode('translate');
        } else if (!ctrl && e.code === 'Digit2') {
            this.setMode('rotate');
        } else if (!ctrl && e.code === 'Escape') {
            this.select([]);
        } else if (!ctrl && 'KeyW KeyA KeyS KeyD KeyQ KeyE'.includes(e.code)) {
            this.keys.add(e.code);
        }
    };

    private fly(dt: number): void {
        if (!this.keys.size) return;
        const dist = this.camera.position.distanceTo(this.orbit.target);
        const speed = Math.max(dist, 20) * 0.9 * dt;
        const f = new THREE.Vector3();
        this.camera.getWorldDirection(f);
        const r = new THREE.Vector3().crossVectors(f, this.camera.up).normalize();
        const d = new THREE.Vector3();
        if (this.keys.has('KeyW')) d.add(f);
        if (this.keys.has('KeyS')) d.sub(f);
        if (this.keys.has('KeyD')) d.add(r);
        if (this.keys.has('KeyA')) d.sub(r);
        if (this.keys.has('KeyE')) d.y += 1;
        if (this.keys.has('KeyQ')) d.y -= 1;
        d.multiplyScalar(speed);
        this.camera.position.add(d);
        this.orbit.target.add(d);
    }

    // ---- instancing -------------------------------------------------------------------------------------------

    private rebuildScheduled(): void {
        this.dirtyInstances = true;
    }

    private objMatrix(o: LevelObject, scale: number, out: THREE.Matrix4): void {
        tmpQ.setFromEuler(tmpE.set(o.transform.rot[0], o.transform.rot[1], o.transform.rot[2]));
        tmpV.set(o.transform.pos[0] * S, o.transform.pos[1] * S, o.transform.pos[2] * S);
        out.compose(tmpV, tmpQ, tmpS.set(scale, scale, scale));
    }

    private rebuild(): void {
        for (const { mesh } of this.meshes.values()) {
            this.scene.remove(mesh);
            mesh.dispose();
        }
        this.meshes.clear();
        const groups = new Map<EntityKind, LevelObject[]>();
        this.visibleCount = 0;
        for (const o of this.doc.objects()) {
            if (!this.passes(o)) continue;
            this.visibleCount++;
            let g = groups.get(o.kind);
            if (!g) groups.set(o.kind, (g = []));
            g.push(o);
        }
        for (const { kind, color } of KINDS) {
            const list = groups.get(kind);
            if (!list?.length) continue;
            let mat = this.mats.get(kind);
            if (!mat) this.mats.set(kind, (mat = new THREE.MeshLambertMaterial({ color })));
            const mesh = new THREE.InstancedMesh(this.geo, mat, list.length);
            mesh.frustumCulled = false;
            const sz = kind === 'static' ? 0.2 : 0.35;
            list.forEach((o, i) => {
                this.objMatrix(o, sz, tmpM);
                mesh.setMatrixAt(i, tmpM);
            });
            mesh.instanceMatrix.needsUpdate = true;
            mesh.computeBoundingSphere();
            this.scene.add(mesh);
            this.meshes.set(kind, { mesh, ids: list.map((o) => o.id) });
        }
        this.dirtySel = true;
    }

    private rebuildSel(): void {
        if (this.selMesh) {
            this.scene.remove(this.selMesh);
            this.selMesh.dispose();
            this.selMesh = null;
        }
        const objs = [...this.selection].map((id) => this.doc.get(id)).filter((o): o is LevelObject => !!o);
        if (!objs.length) return;
        const m = new THREE.InstancedMesh(
            this.geo,
            new THREE.MeshBasicMaterial({ color: 0xffffff, wireframe: true, depthTest: false }),
            objs.length,
        );
        m.frustumCulled = false;
        m.renderOrder = 10;
        objs.forEach((o, i) => {
            this.objMatrix(o, 0.6, tmpM);
            m.setMatrixAt(i, tmpM);
        });
        m.instanceMatrix.needsUpdate = true;
        this.scene.add(m);
        this.selMesh = m;
    }

    // ---- loop -------------------------------------------------------------------------------------------------

    private resize(): void {
        const w = Math.max(this.host.clientWidth, 1);
        const h = Math.max(this.host.clientHeight, 1);
        this.renderer.setSize(w, h, false);
        this.camera.aspect = w / h;
        this.camera.updateProjectionMatrix();
    }

    private frame(): void {
        const now = performance.now();
        const dt = Math.min((now - this.lastTick) / 1000, 0.1);
        this.lastTick = now;
        this.fly(dt);
        if (this.dirtyInstances) {
            this.dirtyInstances = false;
            this.rebuild();
        }
        if (this.dirtySel) {
            this.dirtySel = false;
            this.rebuildSel();
        }
        this.orbit.update();
        this.renderer.render(this.scene, this.camera);
        this.frames++;
        if (now - this.fpsT >= 1000) {
            this.fps = Math.round((this.frames * 1000) / (now - this.fpsT));
            this.frames = 0;
            this.fpsT = now;
        }
    }
}
