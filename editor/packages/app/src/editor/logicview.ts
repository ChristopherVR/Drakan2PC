import * as THREE from 'three';
import { resizeBoxFace, setVolumeRadius, volumesOf, type LogicEnv, type Volume } from '@drakan/editor-core';
import type { LevelView } from '../level.ts';
import type { Ctx } from './ctx.ts';
import { nameOf } from './linking.ts';

/**
 * Trigger volumes in the viewport: wire boxes for volume classes (their scale in cells), circles for the radius fields
 * (door opening distance, talk distance), and face / ring handles on the selected ones. Dragging a handle resizes the
 * volume through `ctx.edit`, so it is one undo step and exports with the level like any other edit.
 */

const S = 0.001;
const BOX = 0x4cc2ff;
const SPHERE = 0xffc857;
const HOT = 0xffffff;

export type VolumeMode = 'selected' | 'all' | 'off';

interface Handle {
    mesh: THREE.Mesh;
    id: number;
    kind: 'face' | 'ring';
    axis: 0 | 1 | 2;
    sign: 1 | -1;
    /** World direction of the drag axis and the volume centre (game units). */
    dir: THREE.Vector3;
    centre: THREE.Vector3;
    half: number;
    volume: Volume;
}

const handleGeo = new THREE.BoxGeometry(1, 1, 1);

class Overlay {
    readonly root = new THREE.Group();
    private handles: Handle[] = [];
    private drag: { h: Handle; local: number; live: THREE.Object3D } | null = null;
    private hover: Handle | null = null;
    mode: VolumeMode = 'selected';

    constructor(
        private readonly view: LevelView,
        private readonly ctx: Ctx,
        private readonly env: () => LogicEnv | null,
    ) {
        this.root.renderOrder = 20;
        view.scene.add(this.root);
        const host = view.renderer.domElement.parentElement ?? view.renderer.domElement;
        host.addEventListener('pointerdown', this.onDown, true);
        host.addEventListener('pointermove', this.onMove, true);
        window.addEventListener('pointerup', this.onUp, true);
    }

    private ray(e: PointerEvent): THREE.Raycaster {
        const r = this.view.renderer.domElement.getBoundingClientRect();
        const rc = new THREE.Raycaster();
        rc.setFromCamera(
            new THREE.Vector2(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1),
            this.view.camera,
        );
        return rc;
    }

    private pick(e: PointerEvent): Handle | null {
        if (!this.handles.length) return null;
        const hit = this.ray(e).intersectObjects(
            this.handles.map((h) => h.mesh),
            false,
        )[0];
        return hit ? (this.handles.find((h) => h.mesh === hit.object) ?? null) : null;
    }

    private onMove = (e: PointerEvent): void => {
        if (this.drag) {
            this.dragTo(e);
            e.stopPropagation();
            return;
        }
        const h = this.pick(e);
        if (h !== this.hover) {
            if (this.hover)
                (this.hover.mesh.material as THREE.MeshBasicMaterial).color.set(
                    this.hover.kind === 'ring' ? SPHERE : BOX,
                );
            this.hover = h;
            (h?.mesh.material as THREE.MeshBasicMaterial | undefined)?.color.set(HOT);
            this.view.renderer.domElement.style.cursor = h ? 'ew-resize' : '';
        }
    };

    private onDown = (e: PointerEvent): void => {
        if (e.button !== 0 || e.altKey) return;
        const h = this.pick(e);
        if (!h) return;
        e.stopPropagation();
        e.preventDefault();
        this.view.orbit.enabled = false;
        const local = this.axisCoord(e, h);
        this.drag = { h, local: local ?? h.half, live: new THREE.Object3D() };
    };

    /** Coordinate (game units, from the centre) along the handle's axis closest to the pointer ray. */
    private axisCoord(e: PointerEvent, h: Handle): number | null {
        const rc = this.ray(e);
        const o = rc.ray.origin.clone().divideScalar(S);
        const d = rc.ray.direction.clone();
        const a = h.dir;
        const w = o.clone().sub(h.centre);
        const b = d.dot(a);
        const denom = 1 - b * b;
        if (denom < 1e-4) return null;
        const dw = d.dot(w);
        const aw = a.dot(w);
        return (aw - b * dw) / denom;
    }

    private dragTo(e: PointerEvent): void {
        const d = this.drag!;
        const t = this.axisCoord(e, d.h);
        if (t === null) return;
        d.local = d.h.kind === 'ring' ? Math.max(100, t) : t * d.h.sign;
        // live preview of the drag: redraw from a clone of the object
        const env = this.env();
        const obj = this.ctx.doc().get(d.h.id);
        if (!obj || !env) return;
        const clone = structuredClone(obj);
        if (d.h.kind === 'face') resizeBoxFace(clone, d.h.axis, d.h.sign, d.h.sign * d.local);
        else if (d.h.volume.kind === 'sphere') setVolumeRadius(clone, env, d.h.volume, d.local);
        this.draw([clone], true);
    }

    private onUp = (): void => {
        const d = this.drag;
        if (!d) return;
        this.drag = null;
        this.view.orbit.enabled = true;
        const env = this.env();
        if (!env) return;
        const { h } = d;
        this.ctx.edit(
            `${h.kind === 'face' ? 'Resize trigger volume' : 'Resize radius'} of ${nameOf(this.ctx, h.id)}`,
            [h.id],
            (o) => {
                if (h.kind === 'face') resizeBoxFace(o, h.axis, h.sign, h.sign * d.local);
                else if (h.volume.kind === 'sphere') setVolumeRadius(o, env, h.volume, d.local);
            },
        );
        this.refresh();
    };

    refresh(): void {
        const doc = this.ctx.doc();
        const sel = [...this.view.selection].map((id) => doc.get(id)).filter((o): o is NonNullable<typeof o> => !!o);
        const objs =
            this.mode === 'all'
                ? [...doc.objects()].filter((o) => this.view.passes(o))
                : this.mode === 'selected'
                  ? sel
                  : [];
        this.draw(objs, false);
    }

    private clear(): void {
        for (const c of [...this.root.children]) {
            this.root.remove(c);
            (c as THREE.Mesh).geometry?.dispose?.();
        }
        this.handles = [];
    }

    private draw(objs: readonly import('@drakan/editor-core').LevelObject[], live: boolean): void {
        this.clear();
        const env = this.env();
        if (!env) return;
        const selected = new Set(this.view.selection);
        for (const o of objs) {
            for (const v of volumesOf(o, env)) this.add(v, o.id, live || selected.has(o.id));
        }
    }

    private add(v: Volume, id: number, withHandles: boolean): void {
        const c = new THREE.Vector3(...v.center);
        if (v.kind === 'box') {
            const geo = new THREE.EdgesGeometry(
                new THREE.BoxGeometry(v.half[0] * 2 * S, v.half[1] * 2 * S, v.half[2] * 2 * S),
            );
            const m = new THREE.LineSegments(
                geo,
                new THREE.LineBasicMaterial({ color: BOX, depthTest: false, transparent: true }),
            );
            m.position.copy(c).multiplyScalar(S);
            m.rotation.set(v.rot[0], v.rot[1], v.rot[2]);
            m.renderOrder = 20;
            this.root.add(m);
            if (withHandles) {
                const q = new THREE.Quaternion().setFromEuler(new THREE.Euler(v.rot[0], v.rot[1], v.rot[2]));
                for (const axis of [0, 1, 2] as const) {
                    for (const sign of [1, -1] as const) {
                        const dir = new THREE.Vector3(
                            axis === 0 ? 1 : 0,
                            axis === 1 ? 1 : 0,
                            axis === 2 ? 1 : 0,
                        ).applyQuaternion(q);
                        const pos = c.clone().addScaledVector(dir, sign * v.half[axis]);
                        this.handle(pos, dir.clone().multiplyScalar(1), {
                            id,
                            kind: 'face',
                            axis,
                            sign,
                            centre: c,
                            half: v.half[axis],
                            volume: v,
                            color: BOX,
                        });
                    }
                }
            }
        } else {
            const pts: THREE.Vector3[] = [];
            for (let i = 0; i <= 48; i++) {
                const a = (i / 48) * Math.PI * 2;
                pts.push(new THREE.Vector3(Math.cos(a) * v.radius * S, 0, Math.sin(a) * v.radius * S));
            }
            const ring = new THREE.Line(
                new THREE.BufferGeometry().setFromPoints(pts),
                new THREE.LineBasicMaterial({ color: SPHERE, depthTest: false, transparent: true }),
            );
            ring.position.copy(c).multiplyScalar(S);
            ring.renderOrder = 20;
            this.root.add(ring);
            if (withHandles) {
                const dir = new THREE.Vector3(1, 0, 0);
                this.handle(c.clone().addScaledVector(dir, v.radius), dir, {
                    id,
                    kind: 'ring',
                    axis: 0,
                    sign: 1,
                    centre: c,
                    half: v.radius,
                    volume: v,
                    color: SPHERE,
                });
            }
        }
    }

    private handle(pos: THREE.Vector3, dir: THREE.Vector3, h: Omit<Handle, 'mesh' | 'dir'> & { color: number }): void {
        const mesh = new THREE.Mesh(handleGeo, new THREE.MeshBasicMaterial({ color: h.color, depthTest: false }));
        mesh.position.copy(pos).multiplyScalar(S);
        mesh.scale.setScalar(0.28);
        mesh.renderOrder = 21;
        this.root.add(mesh);
        const { color: _c, ...rest } = h;
        this.handles.push({ ...rest, mesh, dir });
    }

    dispose(): void {
        this.clear();
        this.view.scene.remove(this.root);
        const host = this.view.renderer.domElement.parentElement ?? this.view.renderer.domElement;
        host.removeEventListener('pointerdown', this.onDown, true);
        host.removeEventListener('pointermove', this.onMove, true);
        window.removeEventListener('pointerup', this.onUp, true);
    }
}

export interface VolumeLayer {
    mode(): VolumeMode;
    setMode(m: VolumeMode): void;
    refresh(): void;
}

/** One overlay per level view (the view is rebuilt when a level opens). */
export function createVolumeLayer(ctx: Ctx, env: () => LogicEnv | null): VolumeLayer {
    let overlay: Overlay | null = null;
    let forView: LevelView | null = null;
    let mode: VolumeMode = 'selected';
    const get = (): Overlay | null => {
        let v: LevelView;
        try {
            v = ctx.view();
        } catch {
            return null;
        }
        if (forView !== v) {
            overlay?.dispose();
            overlay = new Overlay(v, ctx, env);
            forView = v;
        }
        overlay!.mode = mode;
        return overlay;
    };
    return {
        mode: () => mode,
        setMode(m) {
            mode = m;
            get()?.refresh();
        },
        refresh: () => get()?.refresh(),
    };
}
